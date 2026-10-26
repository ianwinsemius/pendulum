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
  c->dpad_enable = 1; c->repeat_delay_ms = 400; c->repeat_hz = 8; c->combo_ms = 3000; c->reverse_ms = 100;
  c->start_ms = 0; c->step_ms = 0;  // debounce tested separately below
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
  p.lx = 0; for (int i = 0; i < 14; i++) step();   // rest at centre (spring-back guard)
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

  // 13. Spring-back overshoot (real 8BitDo data): release from full left
  //     overshoots to +17152 for one sample. Must not jog positive.
  reset(); p.connected = 1; step();
  p.lx = -31744; step(); assert(r.vel[AXIS_X] < 0);
  p.lx = 17152; step(); assert(r.vel[AXIS_X] == 0);
  p.lx = -4096; step(); assert(r.vel[AXIS_X] == 0);   // inside deadzone
  p.lx = 0; step(); assert(r.vel[AXIS_X] == 0);
  // Deliberate reversal: after 100 ms at centre, positive works.
  for (int i = 0; i < 13; i++) step();
  p.lx = 20000; step(); assert(r.vel[AXIS_X] > 0);
  // Too-quick reversal (centre for 16 ms) is held off until it dwells.
  p.lx = 0; step(); step();
  p.lx = -20000; step(); assert(r.vel[AXIS_X] == 0);
  p.lx = 0; for (int i = 0; i < 14; i++) step();
  p.lx = -20000; step(); assert(r.vel[AXIS_X] < 0);
  // Overshoot on Z (right stick Y, from the log): 12544 -> -12800.
  reset(); p.connected = 1; step();
  p.ry = 30720; step(); assert(r.vel[AXIS_Z] > 0);
  p.ry = 12544; step(); assert(r.vel[AXIS_Z] > 0);
  p.ry = -12800; step(); assert(r.vel[AXIS_Z] == 0);
  p.ry = 0; step(); step(); assert(r.vel[AXIS_Z] == 0 && r.owner == 0xFF);
  // reverse_ms = 0 disables the guard
  reset(); c.reverse_ms = 0; p.connected = 1; step();
  p.lx = -30000; step(); p.lx = 17152; step(); assert(r.vel[AXIS_X] > 0);

  // 14. Start debounce (finger slip): 80 ms default.
  reset(); c.start_ms = 80; p.connected = 1; step();
  p.lx = 30000; for (int i = 0; i < 5; i++) { step(); assert(r.vel[AXIS_X] == 0); }  // 40 ms graze
  p.lx = 0; step(); assert(r.vel[AXIS_X] == 0);
  p.lx = 30000; int moved_at = -1;
  for (int i = 0; i < 20; i++) { step(); if (r.vel[AXIS_X] > 0 && moved_at < 0) moved_at = i; }
  assert(moved_at >= 9 && moved_at <= 11);   // ~80 ms after the push
  p.lx = 0; step(); assert(r.vel[AXIS_X] == 0);  // stop is immediate
  // a graze does not set the reverse guard
  reset(); c.start_ms = 80; p.connected = 1; step();
  p.lx = 30000; step(); step(); p.lx = 0; step();
  p.lx = -30000; for (int i = 0; i < 12; i++) step(); assert(r.vel[AXIS_X] < 0);
  // plug-in lockout still sees a deflected stick even while it's debouncing
  reset(); c.start_ms = 80; p.connected = 1; p.lx = 30000; step();
  assert(!(r.flags & PENDULUM_FLAG_ARMED));

  // 15. Step debounce: 40 ms default.
  reset(); c.step_ms = 40; p.connected = 1; step();
  p.buttons = BTN_DPAD_RIGHT; step(); step(); p.buttons = 0; step();   // 16 ms brush
  assert(r.counts[AXIS_X] == 0);
  p.buttons = BTN_DPAD_RIGHT; for (int i = 0; i < 6; i++) step();      // 48 ms press
  assert(r.counts[AXIS_X] == 1);
  p.buttons = 0; step();

  // 16. Free mode: X, Y and Z jog at the same time.
  reset(); c.mode = MODE_FREE; p.connected = 1; step();
  p.lx = 20000; p.ly = 20000; p.ry = 20000; step();
  assert(r.vel[AXIS_X] > 0 && r.vel[AXIS_Y] > 0 && r.vel[AXIS_Z] > 0);
  assert(r.owner == 0xFF);
  p.ly = 0; step(); assert(r.vel[AXIS_Y] == 0 && r.vel[AXIS_X] > 0 && r.vel[AXIS_Z] > 0);
  // D-pad steps work while a stick is moving another axis
  p.buttons = BTN_DPAD_UP; step(); assert(r.counts[AXIS_Y] == 1);
  p.buttons = 0; p.lx = p.ry = 0; step();
  assert(r.vel[AXIS_X] == 0 && r.vel[AXIS_Z] == 0);

  // 17. Inverted stick mapping flips the D-pad step for that axis too.
  reset(); c.invert = (1u << CH_LX) | (1u << CH_LY); p.connected = 1; step();
  p.lx = 20000; step(); assert(r.vel[AXIS_X] < 0);          // stick right -> X-
  p.lx = 0; step(); step();
  p.buttons = BTN_DPAD_RIGHT; step(); assert(r.counts[AXIS_X] == 255);  // right -> X- too
  p.buttons = 0; step();
  p.buttons = BTN_DPAD_UP; step(); assert(r.counts[AXIS_Y] == 255);     // up -> Y-
  p.buttons = 0; step();
  p.buttons = BTN_RB; step(); assert(r.counts[AXIS_Z] == 1);            // Z not inverted
  p.buttons = 0; step();

  printf("all jog tests passed\n");
  return 0;
}
