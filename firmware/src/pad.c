// Gamepad side: USB host on core 1 via Pico-PIO-USB.
// Supports XInput pads (Xbox-style, incl. 8BitDo in X-input mode) natively,
// and generic HID pads through byte offsets set in the config.
#include <stdio.h>
#include <stdlib.h>
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
static volatile uint8_t log_mode;  // 0 off, 1 changes, 2 raw
static volatile uint8_t xin_addr;  // device address of the mounted XInput pad
static volatile bool xin_ready;    // handshake finished, safe to send LED commands
static volatile uint8_t led_want = 1, led_sent = 0xFF;  // 0..4; 0xFF = unknown

void pad_request_led(uint8_t led) { led_want = led; }
uint8_t pad_current_led(void) { return led_want; }

// Core 1: push the requested LED to the pad whenever it differs.
static void led_task(void) {
  if (!xin_ready || !xin_addr || led_sent == led_want) return;
  uint8_t want = led_want;
  if (tuh_xinput_set_led(xin_addr, 0, want, false)) led_sent = want;
}

// Diagnostics, shown by the console 'pad' command.
static volatile uint32_t n_reports, n_errors, n_rearm_fail, n_raw;
static volatile bool rx_armed;
static volatile uint8_t last_err;

uint32_t pad_raw_count(void) { return n_raw; }

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

void pad_set_log(uint8_t mode) { log_mode = mode; }

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

// Every raw packet from the pad, whatever its type. The first few are always
// logged (they show what this pad actually sends); later ones only with log on.
void tuh_xinput_raw_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *d, uint16_t len) {
  (void)dev_addr; (void)instance;
  n_raw++;
  if (n_raw <= 3 || log_mode == 2) {
    char line[160];
    int n = snprintf(line, sizeof line, "raw %u:", len);
    for (uint16_t i = 0; i < len && n < (int)sizeof line - 4; i++)
      n += snprintf(line + n, sizeof line - n, " %02x", d[i]);
    logbuf_printf("%s\r\n", line);
  }
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
    static pad_state_t last;
    bool changed = p.buttons != last.buttons || p.lt != last.lt || p.rt != last.rt ||
                   abs(p.lx - last.lx) > 256 || abs(p.ly - last.ly) > 256 ||
                   abs(p.rx - last.rx) > 256 || abs(p.ry - last.ry) > 256;
    if (log_mode == 2 || (log_mode == 1 && changed)) {
      logbuf_printf("pad btn=%04x lx=%6d ly=%6d rx=%6d ry=%6d lt=%3u rt=%3u\r\n",
                    p.buttons, p.lx, p.ly, p.rx, p.ry, p.lt, p.rt);
    }
    if (changed) last = p;
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

  n_reports = n_errors = n_rearm_fail = n_raw = 0;
  last_err = 0;
  xin_ready = false;
  xin_addr = dev_addr;  // tuh_mount_cb sends the wake-up requests
  // Start reading first. This runs inside enumeration, so no blocking calls
  // here (the earlier blocking LED/rumble writes could stall the stack).
  arm_rx(dev_addr, instance);
  logbuf_printf("pad: reading started=%d\r\n", rx_armed);
  // The rest of the handshake (strings, re-config, wake requests, LED) runs
  // from tuh_mount_cb once enumeration has finished.
}

void tuh_xinput_umount_cb(uint8_t dev_addr, uint8_t instance) {
  (void)dev_addr; (void)instance;
  logbuf_printf("pad: XInput unplugged\r\n");
  rx_armed = false;
  xin_ready = false;
  pad_disconnect();
}

// ---------------------------------------------------------------------------
// Xbox 360-style wake-up. Some third-party 360 pads (this includes 8BitDo in
// X-input mode) send no input until the host makes a control request, which
// Windows and Linux both do. This mirrors Linux xpad: a class GET_REPORT,
// wValue 0x0100, 20 bytes. Done after enumeration finishes (tuh_mount_cb),
// because the control pipe is busy during set_config.
// ---------------------------------------------------------------------------
static CFG_TUSB_MEM_ALIGN uint8_t wake_buf[20];
static CFG_TUSB_MEM_ALIGN tusb_control_request_t wake_req[2];
static uint8_t wake_step;

static void wake_next(uint8_t daddr);

static void hexdump(const char *tag, const uint8_t *d, uint32_t len);

static void wake_done(tuh_xfer_t *xfer) {
  logbuf_printf("pad: wake request %u result %u (%lu bytes)\r\n", wake_step,
                xfer->result, (unsigned long)xfer->actual_len);
  if (xfer->result == XFER_RESULT_SUCCESS && xfer->actual_len)
    hexdump("wake", wake_buf, xfer->actual_len);
  wake_step++;
  wake_next(xfer->daddr);
}

static void wake_next(uint8_t daddr) {
  // Step 0: class GET_REPORT (Linux xpad, "finish initialization").
  // Step 1: vendor request 0xC1/0x01/0x0100 that Windows sends to 360 pads.
  if (wake_step == 0) {
    wake_req[0] = (tusb_control_request_t){
      .bmRequestType = 0xA1, .bRequest = 0x01, .wValue = 0x0100, .wIndex = 0,
      .wLength = sizeof wake_buf};
  } else if (wake_step == 1) {
    wake_req[1] = (tusb_control_request_t){
      .bmRequestType = 0xC1, .bRequest = 0x01, .wValue = 0x0100, .wIndex = 0,
      .wLength = sizeof wake_buf};
  } else {
    // Handshake finished: player-1 LED, non-blocking.
    // Handshake finished: led_task() now sends the requested LED (player 1
    // until the controller says otherwise).
    led_sent = 0xFF;
    xin_ready = true;
    logbuf_printf("pad: handshake done\r\n");
    return;
  }
  tuh_xfer_t x = {
    .daddr = daddr, .ep_addr = 0, .setup = &wake_req[wake_step],
    .buffer = wake_buf, .complete_cb = wake_done, .user_data = 0};
  if (!tuh_control_xfer(&x)) {
    logbuf_printf("pad: wake request %u could not be sent\r\n", wake_step);
    wake_step++;
    wake_next(daddr);
  }
}

// ---------------------------------------------------------------------------
// Descriptor dump: on every plug-in, print the device and configuration
// descriptors in hex so we can see every interface and endpoint the pad has.
// Runs before the wake-up requests (they share the control pipe).
// ---------------------------------------------------------------------------
static CFG_TUSB_MEM_ALIGN uint8_t dump_buf[512];

static void hexdump(const char *tag, const uint8_t *d, uint32_t len) {
  for (uint32_t off = 0; off < len; off += 16) {
    char line[100];
    int n = snprintf(line, sizeof line, "%s %03lx:", tag, (unsigned long)off);
    for (uint32_t i = off; i < len && i < off + 16; i++)
      n += snprintf(line + n, sizeof line - n, " %02x", d[i]);
    logbuf_printf("%s\r\n", line);
  }
}

// Short human-readable summary of interfaces and endpoints.
static void describe_config(const uint8_t *d, uint32_t len) {
  for (uint32_t i = 0; i + 1 < len && d[i] >= 2; i += d[i]) {
    if (d[i + 1] == 0x04 && i + 8 < len) {
      logbuf_printf("desc: interface %u alt %u class %02x/%02x/%02x, %u endpoints\r\n",
                    d[i + 2], d[i + 3], d[i + 5], d[i + 6], d[i + 7], d[i + 4]);
    } else if (d[i + 1] == 0x05 && i + 6 < len) {
      static const char *type[] = {"control", "iso", "bulk", "interrupt"};
      logbuf_printf("desc:   endpoint %02x %s %s, %u bytes, interval %u\r\n",
                    d[i + 2], d[i + 2] & 0x80 ? "IN " : "OUT", type[d[i + 3] & 3],
                    d[i + 4] | (d[i + 5] << 8), d[i + 6]);
    }
  }
}

// ---------------------------------------------------------------------------
// Windows-style string reads. The 8BitDo SN30 Pro sends no input until the
// host has read its language list and manufacturer/product/serial strings.
// TinyUSB 0.19+ already reads them during enumeration (length first, then
// exact size); we read them again here the way Windows does (wLength 255) as
// a belt-and-braces measure. Do NOT send SET_CONFIGURATION again afterwards:
// v0.1.7 did, and that silenced the pad.
// ---------------------------------------------------------------------------
static uint8_t str_idx[3];  // iManufacturer, iProduct, iSerialNumber
static uint8_t str_step;
static CFG_TUSB_MEM_ALIGN uint8_t str_buf[256];

static void str_next(uint8_t daddr);

static void str_done(tuh_xfer_t *xfer) {
  if (xfer->result == XFER_RESULT_SUCCESS && xfer->actual_len >= 2) {
    if (str_step == 0) {
      logbuf_printf("str: languages %02x%02x\r\n", str_buf[3], str_buf[2]);
    } else {
      char a[64];
      uint32_t n = 0;
      for (uint32_t i = 2; i + 1 < xfer->actual_len && n < sizeof a - 1; i += 2)
        a[n++] = (str_buf[i] >= 0x20 && str_buf[i] < 0x7f) ? (char)str_buf[i] : '?';
      a[n] = 0;
      static const char *what[] = {"", "manufacturer", "product", "serial"};
      logbuf_printf("str: %s \"%s\"\r\n", what[str_step], a);
    }
  } else {
    logbuf_printf("str: read %u failed (%u)\r\n", str_step, xfer->result);
  }
  str_step++;
  str_next(xfer->daddr);
}

static void str_next(uint8_t daddr) {
  while (str_step >= 1 && str_step <= 3 && str_idx[str_step - 1] == 0) str_step++;
  if (str_step > 3) {
    wake_step = 0;
    wake_next(daddr);
    return;
  }
  uint8_t index = str_step == 0 ? 0 : str_idx[str_step - 1];
  uint16_t lang = str_step == 0 ? 0 : 0x0409;
  if (!tuh_descriptor_get_string(daddr, index, lang, str_buf, 255, str_done, 0)) {
    logbuf_printf("str: read %u could not be sent\r\n", str_step);
    str_step++;
    str_next(daddr);
  }
}

static void start_handshake(uint8_t daddr) {
  str_step = 0;
  str_next(daddr);
}

static void dump_config_done(tuh_xfer_t *xfer) {
  if (xfer->result == XFER_RESULT_SUCCESS) {
    hexdump("cfg", dump_buf, xfer->actual_len);
    describe_config(dump_buf, xfer->actual_len);
  } else {
    logbuf_printf("desc: config descriptor read failed (%u)\r\n", xfer->result);
  }
  if (xfer->daddr == xin_addr) start_handshake(xfer->daddr);
}

static void dump_device_done(tuh_xfer_t *xfer) {
  if (xfer->result == XFER_RESULT_SUCCESS) {
    hexdump("dev", dump_buf, xfer->actual_len);
    if (xfer->actual_len >= 17) {
      str_idx[0] = dump_buf[14];
      str_idx[1] = dump_buf[15];
      str_idx[2] = dump_buf[16];
    }
  }
  if (!tuh_descriptor_get_configuration(xfer->daddr, 0, dump_buf, sizeof dump_buf,
                                        dump_config_done, 0)) {
    logbuf_printf("desc: could not request config descriptor\r\n");
    if (xfer->daddr == xin_addr) start_handshake(xfer->daddr);
  }
}

void tuh_mount_cb(uint8_t daddr) {
  uint16_t vid = 0, pid = 0;
  tuh_vid_pid_get(daddr, &vid, &pid);
  logbuf_printf("usb: device %u enumerated, %04x:%04x\r\n", daddr, vid, pid);
  if (!tuh_descriptor_get_device(daddr, dump_buf, 18, dump_device_done, 0)) {
    logbuf_printf("desc: could not request device descriptor\r\n");
    if (daddr == xin_addr) start_handshake(daddr);
  }
}

void tuh_umount_cb(uint8_t daddr) {
  if (daddr == xin_addr) xin_addr = 0;
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
  // Keyboards (1) and mice (2) are not pendants, but reading them anyway is a
  // useful test that the gamepad port can receive input at all.
  if (proto == HID_ITF_PROTOCOL_NONE) {
    pad_state_t p;
    memset(&p, 0, sizeof p);
    p.source = PAD_HID;
    p.vid = vid;
    p.pid = pid;
    pad_update(&p);
  }
  bool ok = tuh_hid_receive_report(dev_addr, instance);
  logbuf_printf("pad: HID reading started=%d\r\n", ok);
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
  n_raw++;
  if (tuh_hid_interface_protocol(dev_addr, instance) != HID_ITF_PROTOCOL_NONE) {
    // keyboard / mouse: log only, never drive the jog
    if (n_raw <= 3 || log_mode) hexdump("hid-kbm", r, len);
    tuh_hid_receive_report(dev_addr, instance);
    return;
  }
  if (log_mode) {
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
  tusb_rhport_init_t host_init = {.role = TUSB_ROLE_HOST, .speed = TUSB_SPEED_AUTO};
  tusb_init(1, &host_init);
  while (true) {
    tuh_task();
    led_task();
  }
}

void pad_init(void) { critical_section_init(&pad_lock); }
