#pragma once
#include <pebble.h>

#define KEY_FETCH_REQUEST 0
#define KEY_ADD_REQUEST 1
#define KEY_TOGGLE_REQUEST 2
#define KEY_CLEAR_CHECKED_REQUEST 3
#define KEY_ITEM_ID 10
#define KEY_ITEM_NAME 11
#define KEY_ITEM_CHECKED 12
#define KEY_SYNC_BEGIN 20
#define KEY_SYNC_ITEM 21
#define KEY_SYNC_DONE 22
#define KEY_SYNC_ERROR 23
#define KEY_STATUS 24

#define INBOX_SIZE 4096
#define OUTBOX_SIZE 4096

void messaging_init(void (*message_processed_callback)(void));
void inbox_received_callback(DictionaryIterator *iterator, void *context);
void inbox_dropped_callback(AppMessageResult reason, void *context);
void outbox_failed_callback(DictionaryIterator *iterator,
                            AppMessageResult reason, void *context);
void outbox_sent_callback(DictionaryIterator *iterator, void *context);

void messaging_send_fetch_request();
void messaging_send_add_request(const char *name);
void messaging_send_toggle_request(int32_t server_id, bool is_checked);
void messaging_send_clear_checked_request();
bool messaging_consume_last_error(char *buffer, size_t buffer_size);
