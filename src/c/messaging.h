#pragma once
#include <pebble.h>

#define INBOX_SIZE 256
#define OUTBOX_SIZE 256

void messaging_init(void (*message_processed_callback)(void));
void messaging_deinit(void);
void inbox_received_callback(DictionaryIterator *iterator, void *context);
void inbox_dropped_callback(AppMessageResult reason, void *context);
void outbox_failed_callback(DictionaryIterator *iterator,
                            AppMessageResult reason, void *context);
void outbox_sent_callback(DictionaryIterator *iterator, void *context);

void messaging_send_fetch_request(void);
void messaging_send_add_request(const char *name);
void messaging_send_toggle_request(int32_t server_id, bool is_checked);
void messaging_send_clear_checked_request(void);
bool messaging_consume_last_error(char *buffer, size_t buffer_size);
bool messaging_get_status(char *buffer, size_t buffer_size);
