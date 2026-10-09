// Gamepad side: USB host on core 1 via Pico-PIO-USB.
// Supports XInput pads (Xbox-style, incl. 8BitDo in X-input mode) natively,
// and generic HID pads through byte offsets set in the config.
#include <stdio.h>
#include <string.h>

#include "pico/critical_section.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

#include "pio_usb.h"
#include "tusb.h"
#include "xinput_host.h"

#include "config.h"
#include "logbuf.h"
#include "pendulum.h"

static critical_section_t pad_lock;
static pad_state_t pad;
static volatile bool log_on;

// Diagnostics, shown by the console 'pad' command.
static volatile uint32_t n_reports, n_errors, n_rearm_fail;
static volatile bool rx_armed;
static volatile uint8_t last_err;

void pad_get_diag(uint32_t *reports, uint32_t *errors, uint32_t *rearm_fail,
                  bool *armed, uint8_t *err) {
  *reports = n_reports; *errors = n_errors; *rearm_fail = n_rearm_fail;
  *armed = rx_armed; *err = last_err;
}

static void arm_rx(uint8_t dev_addr, uint8_t instance) {
  rx_armed = tuh_xinput_receive_report(dev_addr, instance);
  if (!rx_armed) {
    n_rearm_fail++;
    logbuf_printf("pad: could not start reading reports\r\n");
  }
}

void pad_get(pad_state_t *out) {
  critical_section_enter_blocking(&pad_lock);
  *out = pad;
  critical_section_exit(&pad_lock);
}

void pad_set_log(bool on) { log_on = on; }

static void pad_update(const pad_state_t *p) {
  critical_section_enter_blocking(&pad_lock);
  pad = *p;
  critical_section_exit(&pad_lock);
}

static void pad_disconnect(void) {
  critical_section_enter_blocking(&pad_lock);
  memset(&pad, 0, sizeof pad);
  critical_section_exit(&pad_lock);
}

// ---------------------------------------------------------------------------
// XInput
// ---------------------------------------------------------------------------
usbh_class_driver_t const *usbh_app_driver_get_cb(uint8_t *driver_count) {
  *driver_count = 1;
  return &usbh_xinput_driver;
}

static int16_t clamp16(int32_t v) {
  if (v > 32767) return 32767;
  if (v < -32767) return -32767;
  return (int16_t)v;
}

void tuh_xinput_report_received_cb(uint8_t dev_addr, uint8_t instance,
                                   xinputh_interface_t const *xid_itf, uint16_t len) {
  (void)len;
  if (xid_itf->last_xfer_result != XFER_RESULT_SUCCESS) {
    n_errors++;
    last_err = (uint8_t)xid_itf->last_xfer_result;
    // Rate-limit: first few, then every 100th.
    if (n_errors <= 5 || n_errors % 100 == 0)
      logbuf_printf("pad: read error %u (count %lu)\r\n", last_err, (unsigned long)n_errors);
  } else {
    n_reports++;
  }
  if (xid_itf->last_xfer_result == XFER_RESULT_SUCCESS && xid_itf->connected &&
      xid_itf->new_pad_data) {
    const xinput_gamepad_t *x = &xid_itf->pad;
    pad_state_t p;
    pad_get(&p);
    p.connected = true;
    p.source = PAD_XINPUT;
    p.buttons = x->wButtons;
    p.lx = clamp16(x->sThumbLX);
    p.ly = clamp16(x->sThumbLY);
    p.rx = clamp16(x->sThumbRX);
    p.ry = clamp16(x->sThumbRY);
    p.lt = x->bLeftTrigger;
    p.rt = x->bRightTrigger;
    p.updated_ms = to_ms_since_boot(get_absolute_time());
    pad_update(&p);
    if (log_on) {
      logbuf_printf("xin btn=%04x lx=%6d ly=%6d rx=%6d ry=%6d lt=%3u rt=%3u\r\n",
                    p.buttons, p.lx, p.ly, p.rx, p.ry, p.lt, p.rt);
    }
  }
  arm_rx(dev_addr, instance);
}

void tuh_xinput_mount_cb(uint8_t dev_addr, uint8_t instance,
                         const xinputh_interface_t *itf) {
  uint16_t vid = 0, pid = 0;
  tuh_vid_pid_get(dev_addr, &vid, &pid);
  logbuf_printf("pad: XInput mounted %04x:%04x type %d\r\n", vid, pid, itf->type);

  pad_state_t p;
  memset(&p, 0, sizeof p);
  p.source = PAD_XINPUT;
  p.vid = vid;
  p.pid = pid;
  pad_update(&p);  // connected stays false until the first real report

  n_reports = n_errors = n_rearm_fail = 0;
  last_err = 0;
  // Start reading first. This runs inside enumeration, so no blocking calls
  // here (the earlier blocking LED/rumble writes could stall the stack).
  arm_rx(dev_addr, instance);
  if (itf->type == XBOX360_WIRELESS && !itf->connected) return;
  // Player-1 LED, non-blocking. Some third-party pads wait for this before
  // they start sending input.
  bool led = tuh_xinput_set_led(dev_addr, instance, 1, false);
  logbuf_printf("pad: reading started=%d, LED cmd sent=%d\r\n", rx_armed, led);
}

void tuh_xinput_umount_cb(uint8_t dev_addr, uint8_t instance) {
  (void)dev_addr; (void)instance;
  logbuf_printf("pad: XInput unplugged\r\n");
  rx_armed = false;
  pad_disconnect();
}

// ---------------------------------------------------------------------------
// Generic HID. Sticks come from byte offsets in the config (8-bit, 0x80 =
// centre). Buttons are not decoded yet: use "log on" to see raw reports.
// ---------------------------------------------------------------------------
void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *desc, uint16_t desc_len) {
  (void)desc;
  uint16_t vid = 0, pid = 0;
  tuh_vid_pid_get(dev_addr, &vid, &pid);
  uint8_t proto = tuh_hid_interface_protocol(dev_addr, instance);
  logbuf_printf("pad: HID mounted %04x:%04x protocol %u, descriptor %u bytes\r\n",
                vid, pid, proto, desc_len);
  // Keyboards (1) and mice (2) are not pendants.
  if (proto == HID_ITF_PROTOCOL_NONE) {
    pad_state_t p;
    memset(&p, 0, sizeof p);
    p.source = PAD_HID;
    p.vid = vid;
    p.pid = pid;
    pad_update(&p);
    tuh_hid_receive_report(dev_addr, instance);
  }
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
  (void)dev_addr; (void)instance;
  pad_state_t p;
  pad_get(&p);
  if (p.source == PAD_HID) {
    logbuf_printf("pad: HID unplugged\r\n");
    pad_disconnect();
  }
}

static int16_t hid_axis(const uint8_t *r, uint16_t len, uint8_t off, bool inv) {
  if (off == 0xFF || off >= len) return 0;
  int32_t v = ((int32_t)r[off] - 128) * 258;  // 0..255 -> about +/-32767
  if (inv) v = -v;
  return clamp16(v);
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *r, uint16_t len) {
  if (log_on) {
    char line[200];
    int n = snprintf(line, sizeof line, "hid %u:", len);
    for (uint16_t i = 0; i < len && n < (int)sizeof line - 4; i++)
      n += snprintf(line + n, sizeof line - n, " %02x", r[i]);
    logbuf_printf("%s\r\n", line);
  }
  pad_state_t p;
  pad_get(&p);
  p.connected = true;
  p.source = PAD_HID;
  p.buttons = 0;
  // HID sticks: +Y should mean "pushed up". Most HID pads report up as 0x00,
  // so the Y channels are inverted by default; hid_invert flips that.
  p.lx = hid_axis(r, len, g_cfg.hid_axis_off[CH_LX], (g_cfg.hid_invert >> CH_LX) & 1);
  p.ly = hid_axis(r, len, g_cfg.hid_axis_off[CH_LY], !((g_cfg.hid_invert >> CH_LY) & 1));
  p.rx = hid_axis(r, len, g_cfg.hid_axis_off[CH_RX], (g_cfg.hid_invert >> CH_RX) & 1);
  p.ry = hid_axis(r, len, g_cfg.hid_axis_off[CH_RY], !((g_cfg.hid_invert >> CH_RY) & 1));
  p.updated_ms = to_ms_since_boot(get_absolute_time());
  pad_update(&p);
  tuh_hid_receive_report(dev_addr, instance);
}

// ---------------------------------------------------------------------------
// Core 1 entry
// ---------------------------------------------------------------------------
void host_core_main(void) {
  // Lets flash_safe_execute() on core 0 pause this core while saving config.
  multicore_lockout_victim_init();
  sleep_ms(10);
  pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
  pio_cfg.pin_dp = PENDULUM_HOST_DP_PIN;
  tuh_configure(1, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg);
  tuh_init(1);
  while (true) tuh_task();
}

void pad_init(void) { critical_section_init(&pad_lock); }
