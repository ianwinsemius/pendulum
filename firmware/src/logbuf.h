// Small cross-core log ring. Either core writes; the console on core 0
// drains it to the USB serial port.
#ifndef LOGBUF_H_
#define LOGBUF_H_

#include <stddef.h>

void logbuf_init(void);
void logbuf_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
// Copies up to max bytes out of the ring; returns the count.
size_t logbuf_drain(char *dst, size_t max);

#endif
