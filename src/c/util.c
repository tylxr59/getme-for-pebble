#include "util.h"
#include <stdint.h>
#include <string.h>

// Adapted from Checklist's UTF-8 truncation fix (608762a).
void utf8_trim_partial(char *str) {
  size_t len = strlen(str);
  if (len == 0) {
    return;
  }

  size_t start = len - 1;
  while (start > 0 && ((uint8_t)str[start] & 0xC0) == 0x80) {
    start--;
  }

  uint8_t lead = (uint8_t)str[start];
  size_t expected = (lead < 0x80) ? 1 : (lead >= 0xF0) ? 4 :
                    (lead >= 0xE0) ? 3 : 2;
  if (len - start < expected) {
    str[start] = '\0';
  }
}
