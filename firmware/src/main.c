#include <string.h>
// pendulum: gamepad in, fixed jog report out.
// Core 0: native USB device (to PathPilot) + jog logic + console.
// Core 1: PIO USB host (from the gamepad).
#include "hardware/clocks.h"
#include "hardware/watchdog.h"
#include "pico/bootrom.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "tusb.h"

#include "config.h"
#include "console.h"
#include "jog.h"
#include "logbuf.h"
#include "pendulum.h"

#define MODE_MAGIC   0x4D4F4400u  // "MOD\0" | mode in low byte
#define REPORT_MS    8

uint8_t g_usb_mode = USB_MODE_RUN;
volatile bool g_jog_log;

// Let pending console output reach the PC, then drop off the bus so the
// host sees a clean unplug before the board comes back as the new device.
static void usb_goodbye(void) {
  absolute_time_t until = make_timeout_time_ms(100);
  while (absolute_time_diff_us(get_absolute_time(), until) > 0) {
    tud_task();
    tud_cdc_write_flush();
  }
  tud_disconnect();
  sleep_ms(250);
}

void request_reboot(uint8_t next_mode) {
  usb_goodbye();
  watchdog_hw->scratch[0] = MODE_MAGIC | next_mode;
  watchdog_reboot(0, 0, 10);
  while (true) tight_loop_contents();
}

void request_bootsel(void) {
  usb_goodbye();
  reset_usb_boot(0, 0);
}

static uint8_t boot_mode(void) {
  uint32_t s = watchdog_hw->scratch[0];
  watchdog_hw->scratch[0] = 0;  // a power cycle always comes back in run mode
  if ((s & 0xFFFFFF00u) == MODE_MAGIC && (s & 0xFF) == USB_MODE_CONFIG) return USB_MODE_CONFIG;
  return USB_MODE_RUN;
}

int main(void) {
  // PIO USB needs a system clock that is a multiple of 12 MHz.
  set_sys_clock_khz(120000, true);

  g_usb_mode = boot_mode();
  logbuf_init();
  pad_init();
  config_load();

  multicore_reset_core1();
  multicore_launch_core1(host_core_main);

  tusb_rhport_init_t dev_init = {.role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_AUTO};
  tusb_init(0, &dev_init);

  jog_state_t jog;
  jog_init(&jog);
  absolute_time_t next_report = make_timeout_time_ms(REPORT_MS);
  bool was_connected = false;

  while (true) {
    tud_task();

    bool cdc = tud_cdc_connected();
    if (cdc && !was_connected) console_banner();
    was_connected = cdc;
    console_task();

    if (absolute_time_diff_us(get_absolute_time(), next_report) <= 0) {
      next_report = delayed_by_ms(next_report, REPORT_MS);
      pad_state_t p;
      pad_get(&p);
      pendulum_report_t r;
      int act = jog_step(&jog, &p, &g_cfg, to_ms_since_boot(get_absolute_time()), &r);

      // 'jog on': print the output report whenever it changes (ignoring seq).
      static pendulum_report_t last_r;
      if (g_jog_log) {
        pendulum_report_t a = r, b = last_r;
        a.seq = b.seq = 0;
        if (memcmp(&a, &b, sizeof a)) {
          static const char *own[] = {"X", "Y", "Z", "A"};
          logbuf_printf("jog %c%c%c%c owner=%s vx=%6d vy=%6d vz=%6d steps=%u/%u/%u btn=%04x\r\n",
                        r.flags & PENDULUM_FLAG_CONNECTED ? 'C' : '-',
                        r.flags & PENDULUM_FLAG_ARMED ? 'A' : '-',
                        r.flags & PENDULUM_FLAG_MOTION ? 'M' : '-',
                        r.flags & PENDULUM_FLAG_SUPPRESSED ? 'S' : '-',
                        r.owner < 4 ? own[r.owner] : "none",
                        r.vel[0], r.vel[1], r.vel[2], r.counts[0], r.counts[1], r.counts[2],
                        r.buttons);
        }
      }
      last_r = r;

      // Always send, even when nothing changed: the seq byte is the heartbeat.
      if (g_usb_mode == USB_MODE_RUN && tud_hid_ready()) tud_hid_report(0, &r, sizeof r);

      if (act == JOG_ACT_TOGGLE_MODE)
        request_reboot(g_usb_mode == USB_MODE_RUN ? USB_MODE_CONFIG : USB_MODE_RUN);
      else if (act == JOG_ACT_BOOTSEL)
        request_bootsel();
    }
  }
}
