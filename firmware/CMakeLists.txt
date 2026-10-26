cmake_minimum_required(VERSION 3.13)

# pendulum firmware: gamepad -> fixed jog report translator for PathPilot.
# Board: Raspberry Pi Pico W (RP2040). The Wi-Fi chip is not used.

set(PICO_BOARD pico_w CACHE STRING "Board type")

include(pico_sdk_import.cmake)

project(pendulum C CXX ASM)
set(CMAKE_C_STANDARD 11)

include(FetchContent)

# TinyUSB 0.21.0 instead of the copy inside the Pico SDK (0.18). 0.19+ reads
# the language/manufacturer/product/serial strings during enumeration, before
# SET_CONFIGURATION, like Windows does. The 8BitDo SN30 Pro sends no input
# without that. Must be set before pico_sdk_init().
FetchContent_Declare(tinyusb_src
  GIT_REPOSITORY https://github.com/hathach/tinyusb.git
  GIT_TAG        dae3f9a366bfcddbf9dcf1b48d7500286a849539  # 0.21.0
  GIT_SHALLOW    FALSE)
FetchContent_GetProperties(tinyusb_src)
if(NOT tinyusb_src_POPULATED)
  FetchContent_Populate(tinyusb_src)
endif()
set(PICO_TINYUSB_PATH ${tinyusb_src_SOURCE_DIR})

pico_sdk_init()

# Pico-PIO-USB: software USB host on two GPIO pins (the gamepad port).
FetchContent_Declare(pico_pio_usb_src
  GIT_REPOSITORY https://github.com/sekigon-gonnoc/Pico-PIO-USB.git
  # the version TinyUSB 0.21.0 pins
  GIT_TAG        675543bcc9baa8170f868ab7ba316d418dbcf41f)
FetchContent_GetProperties(pico_pio_usb_src)
if(NOT pico_pio_usb_src_POPULATED)
  FetchContent_Populate(pico_pio_usb_src)
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
  lib/tusb_xinput/xinput_host.c
  ${PICO_TINYUSB_PATH}/src/portable/raspberrypi/pio_usb/hcd_pio_usb.c
)

target_include_directories(pendulum PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/src
  ${CMAKE_CURRENT_LIST_DIR}/lib/tusb_xinput)

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
