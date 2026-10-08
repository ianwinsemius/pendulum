// Host-side tests for the jog logic: gcc test/test_jog.c src/jog.c -Isrc -lm
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "jog.h"

pendulum_config_t g_cfg;
// minimal defaults (mirror config_defaults without flash deps)
static void defaults(pendulum_config_t *c) {
  memset(c, 0, sizeof *c);
  c->claim_pct = 15; c->release_pct = 8; c->expo_pct = 40; c->max_pct = 100;
  c->mode = MODE_PLANAR; c->deadman = DM_NONE;
  c->map[CH_LX] = AXIS_X; c->map[CH_LY] = AXIS_Y; c->map[CH_RX] = AXIS_NONE; c->map[CH_RY] = AXIS_Z;
  c->dpad_enable = 1; c->repeat_delay_ms = 400; c->repeat_hz = 8; c->combo_ms = 3000;
}

static jog_state_t s; static pendulum_config_t c; static pad_state_t p; static pendulum_report_t r;
static uint32_t t;
static int step(void) { t += 8; return jog_step(&s, &p, &c, t, &r); }
static void reset(void) { defaults(&c); jog_init(&s); memset(&p, 0, sizeof p); t = 0; }

int main(void) {
  // 1. Disconnected: all zero, not connected
  reset(); step();
  assert(r.version == 1 && r.flags == 0 && r.vel[0] == 0);

  // 2. Connect with stick deflected: lockout, no motion until neutral
  reset(); p.connected = 1; p.lx = 20000; step();
  assert((r.flags & PENDULUM_FLAG_CONNECTED) && !(r.flags & PENDULUM_FLAG_ARMED) && r.vel[0] == 0);
  p.lx = 0; step(); assert(r.flags & PENDULUM_FLAG_ARMED);
  p.lx = 20000; step(); assert(r.vel[AXIS_X] > 0 && (r.flags & PENDULUM_FLAG_MOTION));

  // 3. Planar: X+Y together allowed
  p.ly = 20000; step(); assert(r.vel[AXIS_X] > 0 && r.vel[AXIS_Y] > 0);
  // Z ignored while XY owns
  p.ry = 30000; step(); assert(r.vel[AXIS_Z] == 0);
  // release all, then Z can claim
  p.lx = p.ly = 0; step(); assert(r.vel[AXIS_X] == 0);  // Z still held -> not all neutral, owner kept
  assert(r.vel[AXIS_Z] == 0);
  p.ry = 0; step(); p.ry = 30000; step(); assert(r.vel[AXIS_Z] > 0 && r.vel[AXIS_X] == 0);

  // 4. Single mode: first channel wins, diagonal does not leak
  reset(); c.mode = MODE_SINGLE; p.connected = 1; step();
  p.lx = 20000; p.ly = 10000; step();
  assert(r.vel[AXIS_X] > 0 && r.vel[AXIS_Y] == 0);
  p.ly = 32000; step(); assert(r.vel[AXIS_Y] == 0);  // still X-owned

  // 5. Deadzone + full scale
  reset(); p.connected = 1; step();
  p.lx = 32767; step(); assert(r.vel[AXIS_X] == 32767);
  p.lx = -32767; step(); assert(r.vel[AXIS_X] == -32767);
  p.lx = 2000; step(); assert(r.vel[AXIS_X] == 0);  // below release (6%)

  // 6. Deadman RT: no motion without trigger
  reset(); c.deadman = DM_RT; p.connected = 1; step();
  p.lx = 30000; step(); assert(r.vel[AXIS_X] == 0 && !(r.flags & PENDULUM_FLAG_DEADMAN));
  p.rt = 255; step(); assert(r.vel[AXIS_X] > 0);
  p.rt = 0; step(); assert(r.vel[AXIS_X] == 0);

  // 7. D-pad single step + auto-repeat
  reset(); p.connected = 1; step();
  p.buttons = BTN_DPAD_RIGHT; step(); assert(r.counts[AXIS_X] == 1);
  for (int i = 0; i < 10; i++) step();
  assert(r.counts[AXIS_X] == 1);   // within delay
  for (int i = 0; i < 60; i++) step();  // ~480ms more
  assert(r.counts[AXIS_X] > 1);
  p.buttons = 0; step(); uint8_t cx = r.counts[AXIS_X];
  p.buttons = BTN_LB; step(); assert(r.counts[AXIS_Z] == 255);  // -1 wraps
  p.buttons = 0; step(); assert(r.counts[AXIS_X] == cx);

  // 8. D-pad blocked while stick owns another group
  reset(); p.connected = 1; step();
  p.ry = 30000; step();             // Z owns
  p.buttons = BTN_DPAD_RIGHT; step(); assert(r.counts[AXIS_X] == 0);
  p.ry = 0; step();                 // stick released, dpad still held -> stays blocked
  assert(r.counts[AXIS_X] == 0);
  p.buttons = 0; step(); p.buttons = BTN_DPAD_RIGHT; step(); assert(r.counts[AXIS_X] == 1);

  // 9. Combos: Back suppresses, Back+Start 3s toggles mode, then requires neutral
  reset(); p.connected = 1; step();
  p.lx = 30000; p.buttons = BTN_BACK; step();
  assert((r.flags & PENDULUM_FLAG_SUPPRESSED) && r.vel[0] == 0);
  p.lx = 0; p.buttons = BTN_BACK | BTN_START; int act = 0;
  for (int i = 0; i < 400 && !act; i++) act = step();
  assert(act == JOG_ACT_TOGGLE_MODE && t >= 3000);
  p.buttons = BTN_BACK | BTN_START | BTN_LB | BTN_RB; act = 0;
  for (int i = 0; i < 400 && !act; i++) act = step();
  assert(act == JOG_ACT_BOOTSEL);
  p.buttons = 0; p.lx = 30000; step(); assert(!(r.flags & PENDULUM_FLAG_ARMED) && r.vel[0] == 0);
  p.lx = 0; step(); p.lx = 30000; step(); assert(r.vel[0] > 0);

  // 10. Back never appears in buttons; seq increments
  reset(); p.connected = 1; step(); uint8_t s0 = r.seq; step(); assert(r.seq == (uint8_t)(s0 + 1));
  p.buttons = BTN_A; step(); assert(r.buttons == BTN_A);

  // 11. Unplug mid-jog zeroes everything
  reset(); p.connected = 1; step(); p.lx = 30000; step(); assert(r.vel[0] > 0);
  p.connected = 0; step(); assert(r.vel[0] == 0 && r.flags == 0);

  // 12. Inverted mapping
  reset(); c.invert = 1u << CH_RY; p.connected = 1; step();
  p.ry = 30000; step(); assert(r.vel[AXIS_Z] < 0);

  printf("all jog tests passed\n");
  return 0;
}
