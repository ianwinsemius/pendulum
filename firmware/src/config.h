#ifndef CONFIG_H_
#define CONFIG_H_

#include <stdbool.h>
#include <stdint.h>

// Stick channels, in the order the mapping table uses.
enum { CH_LX = 0, CH_LY, CH_RX, CH_RY, CH_COUNT };
// Machine axes. AXIS_NONE disables a channel.
enum { AXIS_X = 0, AXIS_Y, AXIS_Z, AXIS_A, AXIS_COUNT, AXIS_NONE = 0xFF };
// Arbitration modes.
enum { MODE_SINGLE = 0, MODE_PLANAR = 1 };
// Deadman sources.
enum { DM_NONE = 0, DM_LT, DM_RT, DM_LB, DM_RB };

typedef struct {
  uint32_t magic;
  uint16_t layout;          // bump when this struct changes

  uint8_t  claim_pct;       // stick deflection that starts a jog
  uint8_t  release_pct;     // deflection below which a stick counts as neutral
  uint8_t  expo_pct;        // 0 = linear, 100 = full cubic
  uint8_t  max_pct;         // cap on output speed
  uint8_t  mode;            // MODE_SINGLE or MODE_PLANAR
  uint8_t  deadman;         // DM_*
  uint8_t  map[CH_COUNT];   // channel -> AXIS_*
  uint8_t  invert;          // bit per channel
  uint8_t  dpad_enable;     // D-pad / bumpers emit single-step counts
  uint16_t repeat_delay_ms; // hold time before a step auto-repeats
  uint16_t repeat_hz;       // auto-repeat rate
  uint16_t combo_ms;        // hold time for mode combos

  // Generic HID pads: byte offsets of 8-bit stick axes (0xFF = unused).
  // Find them with "log on" in the console.
  uint8_t  hid_axis_off[CH_COUNT];
  uint8_t  hid_invert;      // bit per channel

  uint32_t crc;
} pendulum_config_t;

extern pendulum_config_t g_cfg;

void config_defaults(pendulum_config_t *c);
void config_load(void);         // from flash, or defaults if missing/corrupt
bool config_save(void);         // to flash
void config_show(void (*out)(const char *));
// Returns NULL on success, or an error message.
const char *config_set(const char *key, const char *value);

#endif
