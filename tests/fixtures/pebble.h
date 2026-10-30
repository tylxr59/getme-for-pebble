#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define PERSIST_DATA_MAX_LENGTH 256
#define S_SUCCESS 0
bool persist_exists(int key);
int persist_delete(int key);
int persist_read_data(int key, void *data, size_t size);
int persist_write_data(int key, const void *data, size_t size);
