// USB identity toward the PC. Two layouts, picked at boot:
//   run mode:    HID jog report + CDC serial console
//   config mode: read-only USB drive (installer) + CDC serial console
#include <string.h>

#include "pico/unique_id.h"
#include "tusb.h"

#include "ppbox.h"

enum { ITF_CDC_COMM = 0, ITF_CDC_DATA, ITF_THIRD, ITF_TOTAL };

#define EP_CDC_NOTIF 0x81
#define EP_CDC_OUT   0x02
#define EP_CDC_IN    0x82
#define EP_HID_IN    0x83
#define EP_MSC_OUT   0x04
#define EP_MSC_IN    0x84

// Vendor-defined usage page, so the OS leaves it alone (no joystick or mouse
// driver grabs it); PathPilot reads it through /dev/hidraw.
uint8_t const desc_hid_report[] = {
  0x06, 0x00, 0xFF,       // Usage Page (Vendor 0xFF00)
  0x09, 0x01,             // Usage (1)
  0xA1, 0x01,             // Collection (Application)
  0x09, 0x02,             //   Usage (2)
  0x15, 0x00,             //   Logical Minimum (0)
  0x26, 0xFF, 0x00,       //   Logical Maximum (255)
  0x75, 0x08,             //   Report Size (8)
  0x95, sizeof(ppbox_report_t),  // Report Count (20)
  0x81, 0x02,             //   Input (Data, Var, Abs)
  0xC0                    // End Collection
};

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
  (void)instance;
  return desc_hid_report;
}

static tusb_desc_device_t desc_device = {
  .bLength = sizeof(tusb_desc_device_t),
  .bDescriptorType = TUSB_DESC_DEVICE,
  .bcdUSB = 0x0200,
  .bDeviceClass = TUSB_CLASS_MISC,
  .bDeviceSubClass = MISC_SUBCLASS_COMMON,
  .bDeviceProtocol = MISC_PROTOCOL_IAD,
  .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
  .idVendor = PPBOX_USB_VID,
  .idProduct = PPBOX_USB_PID_RUN,
  .bcdDevice = 0x0100,
  .iManufacturer = 1,
  .iProduct = 2,
  .iSerialNumber = 3,
  .bNumConfigurations = 1,
};

uint8_t const *tud_descriptor_device_cb(void) {
  desc_device.idProduct = g_usb_mode == USB_MODE_CONFIG ? PPBOX_USB_PID_CFG : PPBOX_USB_PID_RUN;
  return (uint8_t const *)&desc_device;
}

#define RUN_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_HID_DESC_LEN)
#define CFG_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_MSC_DESC_LEN)

static uint8_t const desc_run[] = {
  TUD_CONFIG_DESCRIPTOR(1, ITF_TOTAL, 0, RUN_LEN, 0x00, 100),
  TUD_CDC_DESCRIPTOR(ITF_CDC_COMM, 4, EP_CDC_NOTIF, 8, EP_CDC_OUT, EP_CDC_IN, 64),
  TUD_HID_DESCRIPTOR(ITF_THIRD, 5, HID_ITF_PROTOCOL_NONE, sizeof(desc_hid_report),
                     EP_HID_IN, CFG_TUD_HID_EP_BUFSIZE, 4),
};

static uint8_t const desc_cfg[] = {
  TUD_CONFIG_DESCRIPTOR(1, ITF_TOTAL, 0, CFG_LEN, 0x00, 100),
  TUD_CDC_DESCRIPTOR(ITF_CDC_COMM, 4, EP_CDC_NOTIF, 8, EP_CDC_OUT, EP_CDC_IN, 64),
  TUD_MSC_DESCRIPTOR(ITF_THIRD, 6, EP_MSC_OUT, EP_MSC_IN, 64),
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
  (void)index;
  return g_usb_mode == USB_MODE_CONFIG ? desc_cfg : desc_run;
}

static char serial_str[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];

static char const *string_desc[] = {
  (const char[]){0x09, 0x04},  // English
  "IRW Manufacturing",
  "ppbox jog pendant adapter",
  serial_str,
  "ppbox console",
  "ppbox jog",
  "ppbox installer drive",
};

static uint16_t desc_str[33];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void)langid;
  uint8_t n;
  if (index == 0) {
    memcpy(&desc_str[1], string_desc[0], 2);
    n = 1;
  } else {
    if (index >= sizeof(string_desc) / sizeof(string_desc[0])) return NULL;
    if (index == 3 && !serial_str[0])
      pico_get_unique_board_id_string(serial_str, sizeof serial_str);
    const char *s = string_desc[index];
    n = (uint8_t)strlen(s);
    if (n > 32) n = 32;
    for (uint8_t i = 0; i < n; i++) desc_str[1 + i] = s[i];
  }
  desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * n + 2));
  return desc_str;
}

// HID GET_REPORT / SET_REPORT: not used.
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t type,
                               uint8_t *buffer, uint16_t reqlen) {
  (void)instance; (void)report_id; (void)type; (void)buffer; (void)reqlen;
  return 0;
}
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t type,
                           uint8_t const *buffer, uint16_t bufsize) {
  (void)instance; (void)report_id; (void)type; (void)buffer; (void)bufsize;
}
