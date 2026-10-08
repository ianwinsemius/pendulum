#include <math.h>
#include <string.h>

#include "jog.h"

enum { COMBO_NONE = 0, COMBO_MODE, COMBO_BOOT };

static const uint16_t step_btn[STEP_COUNT] = {
  BTN_DPAD_RIGHT, BTN_DPAD_LEFT, BTN_DPAD_UP, BTN_DPAD_DOWN, BTN_RB, BTN_LB};
static const uint8_t step_axis[STEP_COUNT] = {AXIS_X, AXIS_X, AXIS_Y, AXIS_Y, AXIS_Z, AXIS_Z};
static const int8_t step_dir[STEP_COUNT] = {+1, -1, +1, -1, +1, -1};

void jog_init(jog_state_t *s) {
  memset(s, 0, sizeof *s);
  s->owner = -1;
}

// Axis groups decide what may move together.
// planar: X and Y share a group (diagonals allowed); Z and A are alone.
// single: every axis is its own group.
static int8_t group_of(uint8_t axis, uint8_t mode) {
  if (axis >= AXIS_COUNT) return -1;
  if (mode == MODE_PLANAR && axis == AXIS_Y) return AXIS_X;
  return (int8_t)axis;
}

static float shape(float v, const ppbox_config_t *c) {
  float rel = c->release_pct / 100.0f;
  float a = fabsf(v);
  if (a <= rel) return 0.0f;
  float t = (a - rel) / (1.0f - rel);
  if (t > 1.0f) t = 1.0f;
  float e = c->expo_pct / 100.0f;
  float y = (1.0f - e) * t + e * t * t * t;
  y *= c->max_pct / 100.0f;
  return v < 0 ? -y : y;
}

static bool deadman_ok(const pad_state_t *p, const ppbox_config_t *c) {
  switch (c->deadman) {
    case DM_LT: return p->lt > 128;
    case DM_RT: return p->rt > 128;
    case DM_LB: return p->buttons & BTN_LB;
    case DM_RB: return p->buttons & BTN_RB;
    default: return true;
  }
}

static uint16_t deadman_button(const ppbox_config_t *c) {
  return c->deadman == DM_LB ? BTN_LB : c->deadman == DM_RB ? BTN_RB : 0;
}

static void drop_motion(jog_state_t *s) {
  s->owner = -1;
  for (int i = 0; i < STEP_COUNT; i++) s->step_state[i] = 0;
}

int jog_step(jog_state_t *s, const pad_state_t *p, const ppbox_config_t *c,
             uint32_t now, ppbox_report_t *out) {
  int action = JOG_ACT_NONE;
  memset(out, 0, sizeof *out);
  out->version = PPBOX_PROTOCOL_VERSION;
  out->seq = ++s->seq;
  out->owner = 0xFF;

  if (!p->connected) {
    s->armed = false;
    s->combo_kind = COMBO_NONE;
    drop_motion(s);
    memcpy(out->counts, s->counts, sizeof out->counts);
    return action;
  }
  out->flags |= PPBOX_FLAG_CONNECTED;

  // ---- Mode combos: Back held suppresses everything ----
  if (p->buttons & BTN_BACK) {
    uint8_t kind = COMBO_NONE;
    const uint16_t boot = BTN_START | BTN_LB | BTN_RB;
    if ((p->buttons & boot) == boot) kind = COMBO_BOOT;
    else if (p->buttons & BTN_START) kind = COMBO_MODE;
    if (kind != s->combo_kind) {
      s->combo_kind = kind;
      s->combo_start_ms = now;
      s->combo_fired = false;
    } else if (kind != COMBO_NONE && !s->combo_fired &&
               now - s->combo_start_ms >= c->combo_ms) {
      s->combo_fired = true;
      action = kind == COMBO_BOOT ? JOG_ACT_BOOTSEL : JOG_ACT_TOGGLE_MODE;
    }
    s->armed = false;  // require neutral again afterwards
    drop_motion(s);
    out->flags |= PPBOX_FLAG_SUPPRESSED;
    memcpy(out->counts, s->counts, sizeof out->counts);
    return action;
  }
  s->combo_kind = COMBO_NONE;

  // ---- Normalize sticks ----
  const int16_t raw[CH_COUNT] = {p->lx, p->ly, p->rx, p->ry};
  float v[CH_COUNT];
  float claim = c->claim_pct / 100.0f, rel = c->release_pct / 100.0f;
  bool all_neutral = true;
  for (int i = 0; i < CH_COUNT; i++) {
    v[i] = raw[i] / 32767.0f;
    if ((c->invert >> i) & 1) v[i] = -v[i];
    if (c->map[i] >= AXIS_COUNT) v[i] = 0.0f;
    if (fabsf(v[i]) >= rel) all_neutral = false;
  }

  // ---- Neutral-at-connect lockout ----
  if (!s->armed) {
    bool triggers_low = p->lt < 32 && p->rt < 32;
    if (all_neutral && p->buttons == 0 && triggers_low) {
      s->armed = true;
    } else {
      drop_motion(s);
      memcpy(out->counts, s->counts, sizeof out->counts);
      return action;
    }
  }
  out->flags |= PPBOX_FLAG_ARMED;
  out->buttons = p->buttons & ~BTN_BACK;

  // ---- Deadman ----
  if (!deadman_ok(p, c)) {
    drop_motion(s);
    memcpy(out->counts, s->counts, sizeof out->counts);
    return action;
  }
  out->flags |= PPBOX_FLAG_DEADMAN;

  uint16_t dm_btn = deadman_button(c);
  bool step_held[STEP_COUNT];
  bool any_step_held = false;
  for (int k = 0; k < STEP_COUNT; k++) {
    step_held[k] = c->dpad_enable && (p->buttons & step_btn[k]) && !(step_btn[k] & dm_btn);
    if (step_held[k]) any_step_held = true;
  }

  // ---- Claim ownership ----
  if (s->owner < 0) {
    float best = 0.0f;
    for (int i = 0; i < CH_COUNT; i++) {
      float a = fabsf(v[i]);
      if (a > claim && a > best) {
        best = a;
        s->owner = group_of(c->map[i], c->mode);
      }
    }
    if (s->owner < 0) {
      for (int k = 0; k < STEP_COUNT; k++) {
        if (step_held[k] && s->step_state[k] == 0) {
          s->owner = group_of(step_axis[k], c->mode);
          break;
        }
      }
    }
  }

  // ---- Velocity from sticks in the owning group ----
  if (s->owner >= 0) {
    float vel[AXIS_COUNT] = {0};
    for (int i = 0; i < CH_COUNT; i++) {
      if (group_of(c->map[i], c->mode) == s->owner) vel[c->map[i]] += shape(v[i], c);
    }
    for (int a = 0; a < AXIS_COUNT; a++) {
      if (vel[a] > 1.0f) vel[a] = 1.0f;
      if (vel[a] < -1.0f) vel[a] = -1.0f;
      out->vel[a] = (int16_t)lrintf(vel[a] * 32767.0f);
      if (out->vel[a]) out->flags |= PPBOX_FLAG_MOTION;
    }
  }

  // ---- Single steps from D-pad / bumpers ----
  uint32_t period = 1000u / (c->repeat_hz ? c->repeat_hz : 1);
  for (int k = 0; k < STEP_COUNT; k++) {
    if (!step_held[k]) { s->step_state[k] = 0; continue; }
    bool eligible = s->owner >= 0 && group_of(step_axis[k], c->mode) == s->owner;
    if (s->step_state[k] == 0) {
      if (!eligible) { s->step_state[k] = 2; continue; }  // pressed while another axis owns
      s->step_state[k] = 1;
      s->counts[step_axis[k]] += step_dir[k];
      s->step_next_ms[k] = now + c->repeat_delay_ms;
    } else if (s->step_state[k] == 1 && eligible &&
               (int32_t)(now - s->step_next_ms[k]) >= 0) {
      s->counts[step_axis[k]] += step_dir[k];
      s->step_next_ms[k] += period;
    }
  }

  // ---- Release ownership when everything is back to neutral ----
  if (s->owner >= 0) out->owner = (uint8_t)s->owner;
  if (all_neutral && !any_step_held) s->owner = -1;

  memcpy(out->counts, s->counts, sizeof out->counts);
  return action;
}
