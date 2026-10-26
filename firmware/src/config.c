// Persistent settings, stored in the last 4 KB flash sector.
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "hardware/flash.h"
#include "pico/flash.h"
#include "pico/stdlib.h"

#include "config.h"

#define CFG_MAGIC  0x504E444Cu  // "PNDL"
#define CFG_LAYOUT 4  // bump on struct change: old saved settings reset to defaults
#define CFG_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)

pendulum_config_t g_cfg;

static uint32_t crc32(const uint8_t *p, size_t n) {
  uint32_t c = 0xFFFFFFFFu;
  while (n--) {
    c ^= *p++;
    for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  return ~c;
}

static uint32_t cfg_crc(const pendulum_config_t *c) {
  return crc32((const uint8_t *)c, offsetof(pendulum_config_t, crc));
}

void config_defaults(pendulum_config_t *c) {
  memset(c, 0, sizeof(*c));
  c->magic = CFG_MAGIC;
  c->layout = CFG_LAYOUT;
  c->claim_pct = 15;
  c->release_pct = 8;
  c->expo_pct = 40;
  c->max_pct = 100;
  c->mode = MODE_FREE;        // every axis independent (planar: X+Y together, Z alone)
  c->deadman = DM_NONE;
  c->map[CH_LX] = AXIS_X;
  c->map[CH_LY] = AXIS_Y;
  c->map[CH_RX] = AXIS_NONE;  // would be A on a 4th-axis machine
  c->map[CH_RY] = AXIS_Z;
  // Sticks follow the table on the PCNC 1100: stick right = table right = X-,
  // stick up = table away = Y-. Z: stick up = spindle up = Z+.
  c->invert = (1u << CH_LX) | (1u << CH_LY);
  c->dpad_enable = 1;
  c->repeat_delay_ms = 400;
  c->repeat_hz = 8;
  c->combo_ms = 3000;
  c->reverse_ms = 100;
  c->start_ms = 80;
  c->step_ms = 40;
  memset(c->hid_axis_off, 0xFF, sizeof(c->hid_axis_off));
  c->hid_invert = 0;
  c->crc = cfg_crc(c);
}

void config_load(void) {
  const pendulum_config_t *f = (const pendulum_config_t *)(XIP_BASE + CFG_OFFSET);
  if (f->magic == CFG_MAGIC && f->layout == CFG_LAYOUT && f->crc == cfg_crc(f)) {
    g_cfg = *f;
  } else {
    config_defaults(&g_cfg);
  }
}

static void do_flash_write(void *param) {
  static uint8_t page[FLASH_PAGE_SIZE * 2];
  memset(page, 0xFF, sizeof(page));
  memcpy(page, param, sizeof(pendulum_config_t));
  flash_range_erase(CFG_OFFSET, FLASH_SECTOR_SIZE);
  flash_range_program(CFG_OFFSET, page, sizeof(page));
}

_Static_assert(sizeof(pendulum_config_t) <= FLASH_PAGE_SIZE * 2, "config too big");

bool config_save(void) {
  g_cfg.magic = CFG_MAGIC;
  g_cfg.layout = CFG_LAYOUT;
  g_cfg.crc = cfg_crc(&g_cfg);
  // flash_safe_execute pauses the other core while flash is unavailable.
  // The gamepad port will drop out briefly; outputs go to zero meanwhile.
  return flash_safe_execute(do_flash_write, &g_cfg, 1000) == PICO_OK;
}

// ---------------------------------------------------------------------------
// Text interface used by the console: show / set.
// ---------------------------------------------------------------------------
static const char *axis_name(uint8_t a) {
  switch (a) {
    case AXIS_X: return "x";
    case AXIS_Y: return "y";
    case AXIS_Z: return "z";
    case AXIS_A: return "a";
    default: return "none";
  }
}
static const char *mode_name(uint8_t m) {
  return m == MODE_SINGLE ? "single" : m == MODE_FREE ? "free" : "planar";
}
static const char *dm_name(uint8_t d) {
  static const char *n[] = {"none", "lt", "rt", "lb", "rb"};
  return d <= DM_RB ? n[d] : "none";
}
static const char *ch_key[CH_COUNT] = {"lx", "ly", "rx", "ry"};

void config_show(void (*out)(const char *)) {
  char b[96];
#define P(...) do { snprintf(b, sizeof b, __VA_ARGS__); out(b); } while (0)
  P("claim %u        # %% deflection that starts a jog\r\n", g_cfg.claim_pct);
  P("release %u      # %% below which a stick is neutral\r\n", g_cfg.release_pct);
  P("expo %u         # 0 linear .. 100 cubic\r\n", g_cfg.expo_pct);
  P("max %u          # %% cap on jog speed\r\n", g_cfg.max_pct);
  P("mode %s     # free | planar | single\r\n", mode_name(g_cfg.mode));
  P("deadman %s     # none | lt | rt | lb | rb\r\n", dm_name(g_cfg.deadman));
  for (int i = 0; i < CH_COUNT; i++)
    P("map.%s %s%s\r\n", ch_key[i], axis_name(g_cfg.map[i]),
      (g_cfg.invert >> i) & 1 ? "  (inverted)" : "");
  P("dpad %u         # D-pad/bumpers single-step on/off\r\n", g_cfg.dpad_enable);
  P("repeat_delay %u\r\n", g_cfg.repeat_delay_ms);
  P("repeat_hz %u\r\n", g_cfg.repeat_hz);
  P("combo_ms %u\r\n", g_cfg.combo_ms);
  P("reverse_ms %u    # centre dwell before a stick may reverse\r\n", g_cfg.reverse_ms);
  P("start_ms %u       # hold time before a stick starts a jog\r\n", g_cfg.start_ms);
  P("step_ms %u        # hold time before a D-pad/bumper press steps\r\n", g_cfg.step_ms);
  for (int i = 0; i < CH_COUNT; i++) {
    if (g_cfg.hid_axis_off[i] == 0xFF) P("hid.%s off\r\n", ch_key[i]);
    else P("hid.%s %u%s\r\n", ch_key[i], g_cfg.hid_axis_off[i],
           (g_cfg.hid_invert >> i) & 1 ? "  (inverted)" : "");
  }
#undef P
}

static bool parse_uint(const char *s, unsigned lo, unsigned hi, unsigned *out) {
  char *end;
  unsigned long v = strtoul(s, &end, 10);
  if (end == s || *end || v < lo || v > hi) return false;
  *out = (unsigned)v;
  return true;
}

static int find_ch(const char *s) {
  for (int i = 0; i < CH_COUNT; i++) if (!strcasecmp(s, ch_key[i])) return i;
  return -1;
}

const char *config_set(const char *key, const char *val) {
  unsigned v;
  if (!strcasecmp(key, "claim")) {
    if (!parse_uint(val, 1, 90, &v)) return "claim: 1..90";
    g_cfg.claim_pct = v;
  } else if (!strcasecmp(key, "release")) {
    if (!parse_uint(val, 1, 89, &v)) return "release: 1..89";
    g_cfg.release_pct = v;
  } else if (!strcasecmp(key, "expo")) {
    if (!parse_uint(val, 0, 100, &v)) return "expo: 0..100";
    g_cfg.expo_pct = v;
  } else if (!strcasecmp(key, "max")) {
    if (!parse_uint(val, 1, 100, &v)) return "max: 1..100";
    g_cfg.max_pct = v;
  } else if (!strcasecmp(key, "mode")) {
    if (!strcasecmp(val, "single")) g_cfg.mode = MODE_SINGLE;
    else if (!strcasecmp(val, "planar")) g_cfg.mode = MODE_PLANAR;
    else if (!strcasecmp(val, "free")) g_cfg.mode = MODE_FREE;
    else return "mode: free | planar | single";
  } else if (!strcasecmp(key, "deadman")) {
    static const char *n[] = {"none", "lt", "rt", "lb", "rb"};
    int found = -1;
    for (int i = 0; i < 5; i++) if (!strcasecmp(val, n[i])) found = i;
    if (found < 0) return "deadman: none | lt | rt | lb | rb";
    g_cfg.deadman = found;
  } else if (!strncasecmp(key, "map.", 4)) {
    int ch = find_ch(key + 4);
    if (ch < 0) return "map.<lx|ly|rx|ry>";
    // value: x | y | z | a | none, optionally prefixed with '-' to invert
    bool inv = val[0] == '-';
    const char *a = inv ? val + 1 : val;
    uint8_t ax;
    if (!strcasecmp(a, "x")) ax = AXIS_X;
    else if (!strcasecmp(a, "y")) ax = AXIS_Y;
    else if (!strcasecmp(a, "z")) ax = AXIS_Z;
    else if (!strcasecmp(a, "a")) ax = AXIS_A;
    else if (!strcasecmp(a, "none")) ax = AXIS_NONE;
    else return "map value: x | y | z | a | none (prefix - to invert)";
    g_cfg.map[ch] = ax;
    if (inv) g_cfg.invert |= 1u << ch; else g_cfg.invert &= ~(1u << ch);
  } else if (!strcasecmp(key, "dpad")) {
    if (!parse_uint(val, 0, 1, &v)) return "dpad: 0 | 1";
    g_cfg.dpad_enable = v;
  } else if (!strcasecmp(key, "repeat_delay")) {
    if (!parse_uint(val, 100, 5000, &v)) return "repeat_delay: 100..5000";
    g_cfg.repeat_delay_ms = v;
  } else if (!strcasecmp(key, "repeat_hz")) {
    if (!parse_uint(val, 1, 50, &v)) return "repeat_hz: 1..50";
    g_cfg.repeat_hz = v;
  } else if (!strcasecmp(key, "start_ms")) {
    if (!parse_uint(val, 0, 500, &v)) return "start_ms: 0..500";
    g_cfg.start_ms = v;
  } else if (!strcasecmp(key, "step_ms")) {
    if (!parse_uint(val, 0, 500, &v)) return "step_ms: 0..500";
    g_cfg.step_ms = v;
  } else if (!strcasecmp(key, "reverse_ms")) {
    if (!parse_uint(val, 0, 1000, &v)) return "reverse_ms: 0..1000";
    g_cfg.reverse_ms = v;
  } else if (!strcasecmp(key, "combo_ms")) {
    if (!parse_uint(val, 1000, 10000, &v)) return "combo_ms: 1000..10000";
    g_cfg.combo_ms = v;
  } else if (!strncasecmp(key, "hid.", 4)) {
    int ch = find_ch(key + 4);
    if (ch < 0) return "hid.<lx|ly|rx|ry>";
    if (!strcasecmp(val, "off")) { g_cfg.hid_axis_off[ch] = 0xFF; return NULL; }
    bool inv = val[0] == '-';
    if (!parse_uint(inv ? val + 1 : val, 0, 63, &v)) return "hid value: byte offset 0..63 or off (prefix - to invert)";
    g_cfg.hid_axis_off[ch] = v;
    if (inv) g_cfg.hid_invert |= 1u << ch; else g_cfg.hid_invert &= ~(1u << ch);
  } else {
    return "unknown key (type 'show')";
  }
  if (g_cfg.release_pct >= g_cfg.claim_pct) return "warning: release should be below claim";
  return NULL;
}
