#include <assert.h>
#include <string.h>
#include "pending_store.h"
static unsigned char slots[800][256];
static int lengths[800];
static int writes_left = -1;
bool persist_exists(int key) { return lengths[key] > 0; }
int persist_delete(int key) { lengths[key] = 0; return 1; }
int persist_read_data(int key, void *data, size_t size) {
  if (lengths[key] == 0) return -1;
  if (size > (size_t)lengths[key]) size = lengths[key];
  memcpy(data, slots[key], size); return size;
}
int persist_write_data(int key, const void *data, size_t size) {
  if (writes_left == 0) return -1;
  if (writes_left > 0) writes_left--;
  assert(size <= 256);
  memcpy(slots[key], data, size); lengths[key] = size; return size;
}
int main(void) {
  unsigned char first[1500], second[1500], restored[1500];
  memset(first, 17, sizeof(first)); memset(second, 29, sizeof(second));
  assert(!pending_store_read(restored, sizeof(restored)));
  assert(pending_store_write(first, sizeof(first)));
  // A power loss at every chunk and at the commit header must preserve old data.
  for (int boundary = 0; boundary < 7; boundary++) {
    writes_left = boundary;
    assert(!pending_store_write(second, sizeof(second)));
    assert(pending_store_read(restored, sizeof(restored)));
    assert(memcmp(first, restored, sizeof(first)) == 0);
  }
  writes_left = -1;
  assert(pending_store_write(second, sizeof(second)));
  assert(pending_store_read(restored, sizeof(restored)));
  assert(memcmp(second, restored, sizeof(second)) == 0);
  // A third commit reuses a bank with an existing header. Pebble's successful
  // deletion returns S_TRUE (1), not S_SUCCESS (0).
  assert(pending_store_write(first, sizeof(first)));
  assert(pending_store_read(restored, sizeof(restored)));
  assert(memcmp(first, restored, sizeof(first)) == 0);
  // Corruption of newest bank falls back to the previous complete bank.
  slots[701][0] ^= 1;
  assert(pending_store_read(restored, sizeof(restored)));
  assert(memcmp(second, restored, sizeof(second)) == 0);
  return 0;
}
