#ifndef TUSB_CONFIG_H_
#define TUSB_CONFIG_H_

// Roothub port 0 = native USB (device side, to the PathPilot PC).
// Roothub port 1 = Pico-PIO-USB on GPIO pins (host side, to the gamepad).

#define CFG_TUSB_OS              OPT_OS_PICO
#define CFG_TUD_ENABLED          1
#define CFG_TUH_ENABLED          1
#define CFG_TUH_RPI_PIO_USB      1

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif
#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN       __attribute__((aligned(4)))
#endif

//------------- Device -------------//
#define CFG_TUD_ENDPOINT0_SIZE   64
#define CFG_TUD_HID              1
#define CFG_TUD_HID_EP_BUFSIZE   32
#define CFG_TUD_CDC              1
#define CFG_TUD_CDC_RX_BUFSIZE   256
#define CFG_TUD_CDC_TX_BUFSIZE   1024
#define CFG_TUD_CDC_EP_BUFSIZE   64
#define CFG_TUD_MSC              1
#define CFG_TUD_MSC_EP_BUFSIZE   512

//------------- Host -------------//
#define CFG_TUH_ENUMERATION_BUFSIZE 512
#define CFG_TUH_HUB              1
#define CFG_TUH_DEVICE_MAX       (CFG_TUH_HUB ? 4 : 1)
#define CFG_TUH_HID              4
#define CFG_TUH_HID_EPIN_BUFSIZE 64
#define CFG_TUH_HID_EPOUT_BUFSIZE 64
#define CFG_TUH_XINPUT           2

#endif
