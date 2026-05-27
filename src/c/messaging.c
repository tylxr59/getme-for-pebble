#include "messaging.h"
#include "checklist.h"
#include <pebble.h>

static char s_last_error[64];
static char s_status[64];
static bool s_has_error;
static bool s_has_status;
static void (*message_processed_callback)(void);
static int s_expected_sync_items;
static int s_received_sync_items;

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

bool messaging_get_status(char *buffer, size_t buffer_size) {
  if (!s_has_status || buffer == NULL || buffer_size == 0) {
    return false;
  }

  strncpy(buffer, s_status, buffer_size - 1);
  buffer[buffer_size - 1] = '\0';
  return true;
}

void inbox_received_callback(DictionaryIterator *iterator, void *context) {
  bool should_refresh = false;

  Tuple *status_tuple = dict_find(iterator, KEY_STATUS);
  if (status_tuple != NULL) {
    strncpy(s_status, status_tuple->value->cstring, sizeof(s_status) - 1);
    s_status[sizeof(s_status) - 1] = '\0';
    s_has_status = true;
    should_refresh = true;
  }

  Tuple *sync_begin_tuple = dict_find(iterator, KEY_SYNC_BEGIN);
  if (sync_begin_tuple != NULL) {
    s_expected_sync_items = sync_begin_tuple->value->int32;
    s_received_sync_items = 0;
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Sync begin: %d",
            s_expected_sync_items);
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
      s_received_sync_items++;
    }
  }

  Tuple *sync_done_tuple = dict_find(iterator, KEY_SYNC_DONE);
  if (sync_done_tuple != NULL) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Sync done: %d/%d", s_received_sync_items,
            s_expected_sync_items);
    if (s_expected_sync_items <= MAX_CHECKLIST_ITEMS &&
        s_received_sync_items == s_expected_sync_items) {
      checklist_commit_replace();
    } else {
      snprintf(s_last_error, sizeof(s_last_error), "Sync incomplete");
      s_has_error = true;
      checklist_cancel_replace();
      APP_LOG(APP_LOG_LEVEL_ERROR, "Sync incomplete: %d/%d",
              s_received_sync_items, s_expected_sync_items);
    }
    s_has_status = false;
    should_refresh = true;
  }

  Tuple *sync_error_tuple = dict_find(iterator, KEY_SYNC_ERROR);
  if (sync_error_tuple != NULL) {
    strncpy(s_last_error, sync_error_tuple->value->cstring,
            sizeof(s_last_error) - 1);
    s_last_error[sizeof(s_last_error) - 1] = '\0';
    s_has_error = true;
    s_has_status = false;
    checklist_cancel_replace();
    APP_LOG(APP_LOG_LEVEL_ERROR, "Sync error: %s", s_last_error);
    should_refresh = true;
  }

  if (should_refresh && message_processed_callback) {
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
