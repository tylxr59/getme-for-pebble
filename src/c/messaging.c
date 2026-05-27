#include "messaging.h"
#include "checklist.h"
#include <pebble.h>

static char s_last_error[64];
static bool s_has_error;
static void (*message_processed_callback)(void);

static void send_simple_request(uint32_t key) {
  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);
  if (!iter) {
    return;
  }

  dict_write_uint8(iter, key, 1);
  app_message_outbox_send();
}

void messaging_init(void (*processed_callback)(void)) {
  message_processed_callback = processed_callback;

  app_message_register_inbox_received(inbox_received_callback);
  app_message_register_inbox_dropped(inbox_dropped_callback);
  app_message_register_outbox_failed(outbox_failed_callback);
  app_message_register_outbox_sent(outbox_sent_callback);

  app_message_open(INBOX_SIZE, OUTBOX_SIZE);

  APP_LOG(APP_LOG_LEVEL_DEBUG, "Watch messaging is started!");
}

void messaging_send_fetch_request() {
  send_simple_request(KEY_FETCH_REQUEST);
}

void messaging_send_add_request(const char *name) {
  if (name == NULL || strlen(name) == 0) {
    return;
  }

  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);
  if (!iter) {
    return;
  }

  dict_write_cstring(iter, KEY_ADD_REQUEST, name);
  app_message_outbox_send();
}

void messaging_send_toggle_request(int32_t server_id, bool is_checked) {
  if (server_id <= 0) {
    snprintf(s_last_error, sizeof(s_last_error), "Sync before editing");
    s_has_error = true;
    if (message_processed_callback) {
      message_processed_callback();
    }
    return;
  }

  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);
  if (!iter) {
    return;
  }

  dict_write_uint8(iter, KEY_TOGGLE_REQUEST, 1);
  dict_write_int32(iter, KEY_ITEM_ID, server_id);
  dict_write_uint8(iter, KEY_ITEM_CHECKED, is_checked ? 1 : 0);
  app_message_outbox_send();
}

void messaging_send_clear_checked_request() {
  send_simple_request(KEY_CLEAR_CHECKED_REQUEST);
}

bool messaging_consume_last_error(char *buffer, size_t buffer_size) {
  if (!s_has_error || buffer == NULL || buffer_size == 0) {
    return false;
  }

  strncpy(buffer, s_last_error, buffer_size - 1);
  buffer[buffer_size - 1] = '\0';
  s_has_error = false;
  return true;
}

void inbox_received_callback(DictionaryIterator *iterator, void *context) {
  Tuple *sync_begin_tuple = dict_find(iterator, KEY_SYNC_BEGIN);
  if (sync_begin_tuple != NULL) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Sync begin: %d",
            (int)sync_begin_tuple->value->int32);
    checklist_begin_replace();
  }

  Tuple *sync_item_tuple = dict_find(iterator, KEY_SYNC_ITEM);
  if (sync_item_tuple != NULL) {
    Tuple *id_tuple = dict_find(iterator, KEY_ITEM_ID);
    Tuple *name_tuple = dict_find(iterator, KEY_ITEM_NAME);
    Tuple *checked_tuple = dict_find(iterator, KEY_ITEM_CHECKED);

    if (id_tuple != NULL && name_tuple != NULL && checked_tuple != NULL) {
      checklist_add_remote_item(id_tuple->value->int32,
                                name_tuple->value->cstring,
                                checked_tuple->value->int32 != 0);
    }
  }

  Tuple *sync_done_tuple = dict_find(iterator, KEY_SYNC_DONE);
  if (sync_done_tuple != NULL) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Sync done");
    checklist_commit_replace();
  }

  Tuple *sync_error_tuple = dict_find(iterator, KEY_SYNC_ERROR);
  if (sync_error_tuple != NULL) {
    strncpy(s_last_error, sync_error_tuple->value->cstring,
            sizeof(s_last_error) - 1);
    s_last_error[sizeof(s_last_error) - 1] = '\0';
    s_has_error = true;
    APP_LOG(APP_LOG_LEVEL_ERROR, "Sync error: %s", s_last_error);
  }

  if (message_processed_callback) {
    message_processed_callback();
  }
}

void inbox_dropped_callback(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Message dropped!");
}

void outbox_failed_callback(DictionaryIterator *iterator,
                            AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox send failed! %d %d %d", reason,
          APP_MSG_SEND_TIMEOUT, APP_MSG_SEND_REJECTED);
}

void outbox_sent_callback(DictionaryIterator *iterator, void *context) {
  APP_LOG(APP_LOG_LEVEL_INFO, "Outbox send success!");
}
