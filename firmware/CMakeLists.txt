cmake_minimum_required(VERSION 3.13)

# pendulum firmware: gamepad -> fixed jog report translator for PathPilot.
# Board: Raspberry Pi Pico W (RP2040). The Wi-Fi chip is not used.

set(PICO_BOARD pico_w CACHE STRING "Board type")

include(pico_sdk_import.cmake)

project(pendulum C CXX ASM)
set(CMAKE_C_STANDARD 11)

pico_sdk_init()

include(FetchContent)

# Pico-PIO-USB: software USB host on two GPIO pins (the gamepad port).
FetchContent_Declare(pico_pio_usb_src
  GIT_REPOSITORY https://github.com/sekigon-gonnoc/Pico-PIO-USB.git
  GIT_TAG        5a37a66dc5d3fbe0ef3cdbeda923a757440f984f)
# tusb_xinput: TinyUSB host class driver for XInput (Xbox-style) gamepads.
# Pinned to the last commit for TinyUSB 0.18 (what Pico SDK 2.1.x ships).
FetchContent_Declare(tusb_xinput_src
  GIT_REPOSITORY https://github.com/Ryzee119/tusb_xinput.git
  GIT_TAG        cfd83ba9b0809cf69f7b63d351f44ff73ebd0e30)
FetchContent_GetProperties(pico_pio_usb_src)
if(NOT pico_pio_usb_src_POPULATED)
  FetchContent_Populate(pico_pio_usb_src)
endif()
FetchContent_GetProperties(tusb_xinput_src)
if(NOT tusb_xinput_src_POPULATED)
  FetchContent_Populate(tusb_xinput_src)
endif()

add_subdirectory(${pico_pio_usb_src_SOURCE_DIR} pico_pio_usb)

# Build the read-only USB drive image (installer files) from ../pathpilot.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(PATHPILOT_DIR ${CMAKE_CURRENT_LIST_DIR}/../pathpilot)
file(GLOB PATHPILOT_FILES ${PATHPILOT_DIR}/*)
set(DISK_C ${CMAKE_CURRENT_BINARY_DIR}/disk_image.c)
add_custom_command(
  OUTPUT ${DISK_C}
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/tools/mkdisk.py
          ${PATHPILOT_DIR} ${DISK_C}
  DEPENDS ${CMAKE_CURRENT_LIST_DIR}/tools/mkdisk.py ${PATHPILOT_FILES}
  COMMENT "Building installer drive image")

add_executable(pendulum
  src/main.c
  src/pad.c
  src/jog.c
  src/config.c
  src/console.c
  src/logbuf.c
  src/usb_descriptors.c
  src/msc_disk.c
  ${DISK_C}
  ${tusb_xinput_src_SOURCE_DIR}/xinput_host.c
  ${PICO_TINYUSB_PATH}/src/portable/raspberrypi/pio_usb/hcd_pio_usb.c
)

target_include_directories(pendulum PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/src
  ${tusb_xinput_src_SOURCE_DIR})

target_compile_definitions(pendulum PRIVATE PIO_USB_USE_TINYUSB)
target_compile_options(pendulum PRIVATE -Wall)

target_link_libraries(pendulum PRIVATE
  pico_stdlib
  pico_multicore
  pico_flash
  pico_unique_id
  hardware_flash
  hardware_watchdog
  pico_pio_usb
  tinyusb_device
  tinyusb_host)

# Output is pendulum.uf2: drag it onto the RPI-RP2 drive to flash.
pico_add_extra_outputs(pendulum)
