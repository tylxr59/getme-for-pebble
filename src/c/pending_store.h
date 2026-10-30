#pragma once
#include <pebble.h>
// Two banks: only a fully written bank becomes current. Keys 700..723 are
// reserved for this journal, separate from checklist storage (300..325).
bool pending_store_read(void *data, size_t size);
bool pending_store_write(const void *data, size_t size);
