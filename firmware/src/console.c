// Text console on the USB serial port (both modes).
// Open it from VS Code's Serial Monitor, screen, minicom, or PuTTY.
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "pico/stdlib.h"
#include "tusb.h"

#include "config.h"
#include "console.h"
#include "logbuf.h"
#include "pendulum.h"

static char line[96];
static size_t line_len;
static bool dirty;  // settings changed but not saved

static void out(const char *s) {
  // Drop output if nobody has the port open, rather than blocking.
  if (!tud_cdc_connected()) return;
  size_t n = strlen(s);
  // Bounded: if the PC has the port open but isn't reading, drop the rest
  // instead of stalling the jog loop.
  for (int tries = 0; n && tries < 50; tries++) {
    uint32_t w = tud_cdc_write(s, n);
    s += w;
    n -= w;
    tud_cdc_write_flush();
    if (n) tud_task();
  }
}

static void help(void) {
  out("Pendulum " PENDULUM_FW_VERSION " commands:\r\n"
      "  show                 list settings\r\n"
      "  set <key> <value>    change a setting (until reboot)\r\n"
      "  save                 store settings in flash\r\n"
      "  defaults             reset settings (then 'save')\r\n"
      "  pad                  show the connected pendant\r\n"
      "  log on|off           stream raw pendant reports\r\n"
      "  config               reboot into config mode (installer drive)\r\n"
      "  run                  reboot into run mode\r\n"
      "  bootsel              reboot into the UF2 bootloader\r\n"
      "Pendant combos (hold Back + ...):\r\n"
      "  Start, 3 s           toggle run/config mode\r\n"
      "  Start+LB+RB, 3 s     UF2 bootloader\r\n");
}

static void show_pad(void) {
  pad_state_t p;
  pad_get(&p);
  char b[160];
  static const char *src[] = {"none", "XInput", "HID"};
  const char *state = p.connected ? "connected"
                    : p.source != PAD_NONE ? "plugged in, no input received yet"
                                           : "nothing plugged in";
  snprintf(b, sizeof b,
           "pad: %s %s %04x:%04x btn=%04x lx=%d ly=%d rx=%d ry=%d lt=%u rt=%u\r\n",
           state, src[p.source < 3 ? p.source : 0],
           p.vid, p.pid, p.buttons, p.lx, p.ly, p.rx, p.ry, p.lt, p.rt);
  out(b);
  uint32_t nr, ne, nf; bool armed; uint8_t err;
  pad_get_diag(&nr, &ne, &nf, &armed, &err);
  snprintf(b, sizeof b, "     reading=%s reports=%lu errors=%lu (last %u) start_fail=%lu\r\n",
           armed ? "yes" : "no", (unsigned long)nr, (unsigned long)ne, err, (unsigned long)nf);
  out(b);
}

static void run_command(char *cmd) {
  char *argv[4] = {0};
  int argc = 0;
  for (char *t = strtok(cmd, " \t"); t && argc < 4; t = strtok(NULL, " \t")) argv[argc++] = t;
  if (!argc) return;

  if (!strcasecmp(argv[0], "help") || !strcmp(argv[0], "?")) help();
  else if (!strcasecmp(argv[0], "show")) {
    config_show(out);
    if (dirty) out("(unsaved changes; type 'save')\r\n");
  } else if (!strcasecmp(argv[0], "set")) {
    if (argc != 3) { out("usage: set <key> <value>\r\n"); return; }
    const char *err = config_set(argv[1], argv[2]);
    dirty = true;
    out(err ? err : "ok");
    out("\r\n");
  } else if (!strcasecmp(argv[0], "save")) {
    out(config_save() ? "saved\r\n" : "save FAILED\r\n");
    dirty = false;
  } else if (!strcasecmp(argv[0], "defaults")) {
    config_defaults(&g_cfg);
    dirty = true;
    out("defaults loaded; type 'save' to keep them\r\n");
  } else if (!strcasecmp(argv[0], "pad")) show_pad();
  else if (!strcasecmp(argv[0], "log")) {
    bool on = argc > 1 && !strcasecmp(argv[1], "on");
    pad_set_log(on);
    out(on ? "logging on\r\n" : "logging off\r\n");
  } else if (!strcasecmp(argv[0], "config")) {
    out("rebooting into CONFIG mode (installer drive); this session will close\r\n");
    request_reboot(USB_MODE_CONFIG);
  } else if (!strcasecmp(argv[0], "run")) {
    out("rebooting into RUN mode; this session will close\r\n");
    request_reboot(USB_MODE_RUN);
  } else if (!strcasecmp(argv[0], "bootsel")) {
    out("rebooting into the UF2 bootloader (RPI-RP2 drive); this session will close\r\n");
    request_bootsel();
  } else {
    out("unknown command: '");
    out(argv[0]);
    out("' (type help for commands)\r\n");
  }
}

void console_task(void) {
  // Drain the log ring.
  char buf[128];
  size_t n;
  while ((n = logbuf_drain(buf, sizeof buf - 1)) > 0) {
    buf[n] = 0;
    out(buf);
  }
  // Read input.
  while (tud_cdc_available()) {
    char c = (char)tud_cdc_read_char();
    if (c == '\r' || c == '\n') {
      out("\r\n");
      line[line_len] = 0;
      run_command(line);
      line_len = 0;
      out("> ");
    } else if ((c == 0x08 || c == 0x7F) && line_len) {
      line_len--;
      out("\b \b");
    } else if (c >= 0x20 && line_len < sizeof line - 1) {
      line[line_len++] = c;
      char e[2] = {c, 0};
      out(e);
    }
  }
}

void console_banner(void) {
  out(g_usb_mode == USB_MODE_CONFIG ? "\r\nPendulum " PENDULUM_FW_VERSION " - CONFIG mode\r\n"
                                    : "\r\nPendulum " PENDULUM_FW_VERSION " - RUN mode\r\n");
  out("Type help for commands.\r\n> ");
}
