// Shared definitions for the pendulum firmware.
#ifndef PENDULUM_H_
#define PENDULUM_H_

#include <stdbool.h>
#include <stdint.h>

#define PENDULUM_FW_VERSION   "0.1.0"

// USB identity. 0x1209 is pid.codes; 0x0001/0x0002 are its test PIDs.
// Fine for your own shop. Get a real PID before distributing hardware.
#define PENDULUM_USB_VID      0x1209
#define PENDULUM_USB_PID_RUN  0x0001
#define PENDULUM_USB_PID_CFG  0x0002

// Gamepad port: Pico-PIO-USB uses D+ on this pin and D- on pin + 1.
#define PENDULUM_HOST_DP_PIN  0

// ---------------------------------------------------------------------------
// Output report contract, version 1. 20 bytes, little-endian, no report ID.
// The PathPilot component refuses any report whose byte 0 does not match.
// ---------------------------------------------------------------------------
#define PENDULUM_PROTOCOL_VERSION 1

#define PENDULUM_FLAG_CONNECTED  0x01  // a pendant is plugged in and reporting
#define PENDULUM_FLAG_ARMED      0x02  // neutral-at-connect check passed
#define PENDULUM_FLAG_DEADMAN    0x04  // deadman satisfied (or none configured)
#define PENDULUM_FLAG_MOTION     0x08  // some axis is commanded to move
#define PENDULUM_FLAG_SUPPRESSED 0x10  // a mode combo is being held

typedef struct __attribute__((packed)) {
  uint8_t  version;     // PENDULUM_PROTOCOL_VERSION
  uint8_t  seq;         // increments every report (heartbeat)
  uint8_t  flags;       // PENDULUM_FLAG_*
  uint8_t  owner;       // owning axis group, 0xFF = none
  int16_t  vel[4];      // X Y Z A, +/-32767 = +/-100% of max jog speed
  uint16_t buttons;     // XInput bit layout, Back always masked out
  uint8_t  counts[4];   // X Y Z A single-step counters, wrap at 256
  uint8_t  reserved[2];
} pendulum_report_t;

_Static_assert(sizeof(pendulum_report_t) == 20, "report must be 20 bytes");

// ---------------------------------------------------------------------------
// Normalized pendant state, written by the host core, read by the main loop.
// Buttons use the XInput bit layout whatever the source device.
// ---------------------------------------------------------------------------
#define BTN_DPAD_UP    0x0001
#define BTN_DPAD_DOWN  0x0002
#define BTN_DPAD_LEFT  0x0004
#define BTN_DPAD_RIGHT 0x0008
#define BTN_START      0x0010
#define BTN_BACK       0x0020
#define BTN_L3         0x0040
#define BTN_R3         0x0080
#define BTN_LB         0x0100
#define BTN_RB         0x0200
#define BTN_GUIDE      0x0400
#define BTN_A          0x1000
#define BTN_B          0x2000
#define BTN_X          0x4000
#define BTN_Y          0x8000

enum { PAD_NONE = 0, PAD_XINPUT, PAD_HID };

typedef struct {
  bool     connected;
  uint8_t  source;          // PAD_*
  uint16_t vid, pid;
  uint16_t buttons;
  int16_t  lx, ly, rx, ry;  // -32767..32767, +Y = stick pushed up
  uint8_t  lt, rt;          // 0..255
  uint32_t updated_ms;      // time of last report from the pad
} pad_state_t;

// pad.c
void pad_get(pad_state_t *out);
void pad_set_log(bool on);
void host_core_main(void);
void pad_init(void);

// usb mode, chosen at boot
enum { USB_MODE_RUN = 0, USB_MODE_CONFIG = 1 };
extern uint8_t g_usb_mode;

// main.c
void request_reboot(uint8_t next_mode);
void request_bootsel(void);

#endif
