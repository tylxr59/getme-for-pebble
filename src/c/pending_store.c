#include "pending_store.h"
#include <string.h>
#define BANK_FIRST 700
#define BANK_SIZE 12
#define MAX_BYTES (10 * PERSIST_DATA_MAX_LENGTH)
typedef struct { uint32_t generation; uint32_t size; uint32_t checksum; } Header;
static uint32_t s_generation;
static int s_bank = 1;
static uint32_t checksum(const void *data, size_t size) {
  const uint8_t *bytes = data;
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < size; i++) hash = (hash ^ bytes[i]) * 16777619u;
  return hash;
}
static bool read_bank(int bank, void *data, size_t size, Header *header) {
  int key = BANK_FIRST + bank * BANK_SIZE;
  if (persist_read_data(key, header, sizeof(*header)) != sizeof(*header) ||
      header->size != size) return false;
  for (size_t offset = 0; offset < size; offset += PERSIST_DATA_MAX_LENGTH) {
    size_t length = size - offset;
    if (length > PERSIST_DATA_MAX_LENGTH) length = PERSIST_DATA_MAX_LENGTH;
    if (persist_read_data(key + 1 + offset / PERSIST_DATA_MAX_LENGTH,
        (uint8_t *)data + offset, length) != (int)length) return false;
  }
  return checksum(data, size) == header->checksum;
}
bool pending_store_read(void *data, size_t size) {
  if (size > MAX_BYTES) return false;
  Header headers[2];
  bool valid[2];
  // Read older first, then restore the newest valid bank.
  for (int bank = 0; bank < 2; bank++) valid[bank] = read_bank(bank, data, size, &headers[bank]);
  if (!valid[0] && !valid[1]) return false;
  s_bank = valid[1] && (!valid[0] || headers[1].generation > headers[0].generation) ? 1 : 0;
  s_generation = headers[s_bank].generation;
  return read_bank(s_bank, data, size, &headers[s_bank]);
}
bool pending_store_write(const void *data, size_t size) {
  if (size > MAX_BYTES) return false;
  int bank = 1 - s_bank;
  int key = BANK_FIRST + bank * BANK_SIZE;
  // Invalidate the target before overwriting any of its chunks.
  // persist_delete returns S_TRUE (1) on success, not S_SUCCESS (0).
  if (persist_exists(key) && persist_delete(key) < 0) return false;
  for (size_t offset = 0; offset < size; offset += PERSIST_DATA_MAX_LENGTH) {
    size_t length = size - offset;
    if (length > PERSIST_DATA_MAX_LENGTH) length = PERSIST_DATA_MAX_LENGTH;
    if (persist_write_data(key + 1 + offset / PERSIST_DATA_MAX_LENGTH,
        (const uint8_t *)data + offset, length) != (int)length) return false;
  }
  Header header = { s_generation + 1, size, checksum(data, size) };
  if (persist_write_data(key, &header, sizeof(header)) != sizeof(header)) return false;
  s_generation = header.generation;
  s_bank = bank;
  return true;
}
