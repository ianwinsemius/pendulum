// Read-only USB drive served in config mode. The FAT12 image is generated at
// build time by tools/mkdisk.py from the ../pathpilot folder.
#include <string.h>

#include "tusb.h"

extern const uint8_t disk_image[];
extern const uint32_t disk_image_size;

#define BLOCK_SIZE 512

void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16],
                        uint8_t product_rev[4]) {
  (void)lun;
  memcpy(vendor_id, "IRW     ", 8);
  memcpy(product_id, "Pendulum install", 16);
  memcpy(product_rev, "0.1 ", 4);
}

bool tud_msc_test_unit_ready_cb(uint8_t lun) { (void)lun; return true; }

void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size) {
  (void)lun;
  *block_count = disk_image_size / BLOCK_SIZE;
  *block_size = BLOCK_SIZE;
}

bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject) {
  (void)lun; (void)power_condition; (void)start; (void)load_eject;
  return true;
}

bool tud_msc_is_writable_cb(uint8_t lun) { (void)lun; return false; }

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer,
                          uint32_t bufsize) {
  (void)lun;
  uint32_t addr = lba * BLOCK_SIZE + offset;
  if (addr >= disk_image_size) return -1;
  if (addr + bufsize > disk_image_size) bufsize = disk_image_size - addr;
  memcpy(buffer, disk_image + addr, bufsize);
  return (int32_t)bufsize;
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer,
                           uint32_t bufsize) {
  (void)lun; (void)lba; (void)offset; (void)buffer; (void)bufsize;
  return -1;  // read-only
}

int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void *buffer, uint16_t bufsize) {
  (void)buffer; (void)bufsize;
  switch (scsi_cmd[0]) {
    default:
      tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
      return -1;
  }
}
