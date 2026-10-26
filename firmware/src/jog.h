// Turns raw pendant state into the fixed output report.
// Pure logic, no hardware calls, so it can be unit-tested on a PC.
#ifndef JOG_H_
#define JOG_H_

#include "config.h"
#include "pendulum.h"

enum { STEP_XP = 0, STEP_XN, STEP_YP, STEP_YN, STEP_ZP, STEP_ZN, STEP_COUNT };
enum { JOG_ACT_NONE = 0, JOG_ACT_TOGGLE_MODE, JOG_ACT_BOOTSEL };

typedef struct {
  bool     armed;          // neutral seen since connect / last combo
  int8_t   owner;          // owning axis group, -1 = none
  uint8_t  seq;
  uint8_t  counts[AXIS_COUNT];
  // mode combos
  uint8_t  combo_kind;
  uint32_t combo_start_ms;
  bool     combo_fired;
  // single-step sources
  uint8_t  step_state[STEP_COUNT];   // 0 idle, 1 active, 2 blocked
  uint32_t step_next_ms[STEP_COUNT];
  // spring-back guard, per stick channel
  int8_t   last_dir[CH_COUNT];        // -1 / +1 direction last driven, 0 = free
  uint32_t neutral_since[CH_COUNT];   // when the channel last went neutral
  bool     was_neutral[CH_COUNT];
  // start debounce, per stick channel
  bool     live[CH_COUNT];            // channel has passed start_ms and may drive
  int8_t   pend_dir[CH_COUNT];        // direction being debounced
  uint32_t deflect_since[CH_COUNT];
  // step debounce
  uint32_t step_down_since[STEP_COUNT];
  bool     step_was_down[STEP_COUNT];
} jog_state_t;

void jog_init(jog_state_t *s);

// Call at the report rate. Fills *out and returns a JOG_ACT_* request.
int jog_step(jog_state_t *s, const pad_state_t *p, const pendulum_config_t *c,
             uint32_t now_ms, pendulum_report_t *out);

#endif
