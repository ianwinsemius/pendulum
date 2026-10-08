#include <stdarg.h>
#include <stdio.h>

#include "pico/critical_section.h"

#include "logbuf.h"

#define LOG_SIZE 4096  // power of two

static char ring[LOG_SIZE];
static size_t head, tail;  // head = write, tail = read
static critical_section_t lock;

void logbuf_init(void) { critical_section_init(&lock); }

void logbuf_printf(const char *fmt, ...) {
  char line[160];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(line, sizeof line, fmt, ap);
  va_end(ap);
  if (n <= 0) return;
  if (n > (int)sizeof line - 1) n = sizeof line - 1;

  critical_section_enter_blocking(&lock);
  for (int i = 0; i < n; i++) {
    ring[head & (LOG_SIZE - 1)] = line[i];
    head++;
    if (head - tail > LOG_SIZE) tail = head - LOG_SIZE;  // drop oldest
  }
  critical_section_exit(&lock);
}

size_t logbuf_drain(char *dst, size_t max) {
  size_t n = 0;
  critical_section_enter_blocking(&lock);
  while (tail != head && n < max) {
    dst[n++] = ring[tail & (LOG_SIZE - 1)];
    tail++;
  }
  critical_section_exit(&lock);
  return n;
}
