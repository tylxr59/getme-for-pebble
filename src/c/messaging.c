#include "messaging.h"
#include "checklist.h"
#include <message_keys.auto.h>
#include <pebble.h>

#define REQUEST_TIMEOUT_MS 45000
#define PHONE_READY_TIMEOUT_MS 10000

static char s_last_error[64];
static char s_status[64];
static bool s_has_error;
static bool s_has_status;
static void (*s_message_processed_callback)(void);
static int s_expected_sync_items;
static int s_received_sync_items;
static bool s_sync_in_progress;
static bool s_request_pending;
static uint32_t s_next_request_seq;
static uint32_t s_active_request_seq;
static AppTimer *s_request_timer;
static AppTimer *s_phone_ready_timer;
static bool s_phone_ready;

static void notify_message_processed(void) {
  if (s_message_processed_callback) {
    s_message_processed_callback();
  }
}

static void set_last_error(const char *message) {
  snprintf(s_last_error, sizeof(s_last_error), "%s", message);
  s_has_error = true;
  s_has_status = false;
  notify_message_processed();
}

static void clear_request_state(void) {
  if (s_request_timer != NULL) {
    app_timer_cancel(s_request_timer);
    s_request_timer = NULL;
  }

  s_request_pending = false;
  s_sync_in_progress = false;
  s_active_request_seq = 0;
}

static void fail_active_request(const char *message) {
  checklist_cancel_replace();
  clear_request_state();
  set_last_error(message);
}

static void request_timeout_callback(void *context) {
  s_request_timer = NULL;
  if (!s_request_pending) {
    return;
  }

  APP_LOG(APP_LOG_LEVEL_ERROR, "Request %lu timed out",
          (unsigned long)s_active_request_seq);
  fail_active_request("Phone timeout");
}

static void phone_ready_timeout_callback(void *context) {
  s_phone_ready_timer = NULL;
  if (!s_phone_ready) {
    set_last_error("Phone not ready");
  }
}

static uint32_t next_request_seq(void) {
  s_next_request_seq++;
  if (s_next_request_seq == 0) {
    s_next_request_seq++;
  }
  return s_next_request_seq;
}

static bool begin_request(DictionaryIterator **iterator) {
  if (s_request_pending) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "Ignoring action while request %lu is active",
            (unsigned long)s_active_request_seq);
    return false;
  }

  AppMessageResult result = app_message_outbox_begin(iterator);
  if (result != APP_MSG_OK || *iterator == NULL) {
    set_last_error("Phone not ready");
    return false;
  }

  s_active_request_seq = next_request_seq();
  dict_write_uint32(*iterator, MESSAGE_KEY_KEY_REQUEST_SEQ,
                    s_active_request_seq);
  return true;
}

static void send_prepared_request(void) {
  s_request_pending = true;
  s_sync_in_progress = false;
  s_expected_sync_items = 0;
  s_received_sync_items = 0;
  s_has_error = false;

  AppMessageResult result = app_message_outbox_send();
  if (result != APP_MSG_OK) {
    fail_active_request("Phone send failed");
    return;
  }

  s_request_timer = app_timer_register(REQUEST_TIMEOUT_MS,
                                       request_timeout_callback, NULL);
  if (s_request_timer == NULL) {
    fail_active_request("Phone timer failed");
  }
}

static void send_simple_request(uint32_t key) {
  DictionaryIterator *iterator = NULL;
  if (!begin_request(&iterator)) {
    return;
  }

  dict_write_uint8(iterator, key, 1);
  send_prepared_request();
}

static bool incoming_matches_active_request(DictionaryIterator *iterator) {
  Tuple *seq_tuple = dict_find(iterator, MESSAGE_KEY_KEY_REQUEST_SEQ);
  if (!s_request_pending || seq_tuple == NULL) {
    return false;
  }

  return seq_tuple->value->uint32 == s_active_request_seq;
}

void messaging_init(void (*processed_callback)(void)) {
  s_message_processed_callback = processed_callback;
  s_next_request_seq = (uint32_t)time(NULL);

  app_message_register_inbox_received(inbox_received_callback);
  app_message_register_inbox_dropped(inbox_dropped_callback);
  app_message_register_outbox_failed(outbox_failed_callback);
  app_message_register_outbox_sent(outbox_sent_callback);

  AppMessageResult result = app_message_open(INBOX_SIZE, OUTBOX_SIZE);
  if (result != APP_MSG_OK) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Failed to open AppMessage: %d", result);
  }

  s_phone_ready_timer = app_timer_register(PHONE_READY_TIMEOUT_MS,
                                           phone_ready_timeout_callback, NULL);
}

void messaging_deinit(void) {
  clear_request_state();
  if (s_phone_ready_timer != NULL) {
    app_timer_cancel(s_phone_ready_timer);
    s_phone_ready_timer = NULL;
  }
  app_message_deregister_callbacks();
  s_message_processed_callback = NULL;
}

void messaging_send_fetch_request(void) {
  send_simple_request(MESSAGE_KEY_KEY_FETCH_REQUEST);
}

void messaging_send_add_request(const char *name) {
  if (name == NULL || strlen(name) == 0) {
    return;
  }

  char truncated_name[MAX_NAME_LENGTH];
  strncpy(truncated_name, name, sizeof(truncated_name) - 1);
  truncated_name[sizeof(truncated_name) - 1] = '\0';

  DictionaryIterator *iterator = NULL;
  if (!begin_request(&iterator)) {
    return;
  }

  dict_write_cstring(iterator, MESSAGE_KEY_KEY_ADD_REQUEST, truncated_name);
  send_prepared_request();
}

void messaging_send_toggle_request(int32_t server_id, bool is_checked) {
  if (server_id <= 0) {
    set_last_error("Sync before editing");
    return;
  }

  DictionaryIterator *iterator = NULL;
  if (!begin_request(&iterator)) {
    return;
  }

  dict_write_uint8(iterator, MESSAGE_KEY_KEY_TOGGLE_REQUEST, 1);
  dict_write_int32(iterator, MESSAGE_KEY_KEY_ITEM_ID, server_id);
  dict_write_uint8(iterator, MESSAGE_KEY_KEY_ITEM_CHECKED,
                   is_checked ? 1 : 0);
  send_prepared_request();
}

void messaging_send_clear_checked_request(void) {
  send_simple_request(MESSAGE_KEY_KEY_CLEAR_CHECKED_REQUEST);
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
  if (dict_find(iterator, MESSAGE_KEY_KEY_CONFIG_CHANGED) != NULL ||
      dict_find(iterator, MESSAGE_KEY_KEY_PHONE_READY) != NULL) {
    s_phone_ready = true;
    if (s_phone_ready_timer != NULL) {
      app_timer_cancel(s_phone_ready_timer);
      s_phone_ready_timer = NULL;
    }
    checklist_cancel_replace();
    clear_request_state();
    messaging_send_fetch_request();
    return;
  }

  if (!incoming_matches_active_request(iterator)) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "Ignoring stale or unsequenced message");
    return;
  }

  bool should_refresh = false;

  Tuple *status_tuple = dict_find(iterator, MESSAGE_KEY_KEY_STATUS);
  if (status_tuple != NULL) {
    if (status_tuple->type != TUPLE_CSTRING || status_tuple->length <= 1) {
      fail_active_request("Invalid sync status");
      return;
    }
    strncpy(s_status, status_tuple->value->cstring, sizeof(s_status) - 1);
    s_status[sizeof(s_status) - 1] = '\0';
    s_has_status = true;
    should_refresh = true;
  }

  Tuple *sync_begin_tuple = dict_find(iterator, MESSAGE_KEY_KEY_SYNC_BEGIN);
  if (sync_begin_tuple != NULL) {
    int expected = sync_begin_tuple->value->int32;
    if (expected < 0 || expected > MAX_CHECKLIST_ITEMS) {
      fail_active_request("List is too long");
      return;
    }

    s_expected_sync_items = expected;
    s_received_sync_items = 0;
    s_sync_in_progress = true;
    checklist_begin_replace();
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Sync %lu begin: %d",
            (unsigned long)s_active_request_seq, s_expected_sync_items);
  }

  Tuple *sync_item_tuple = dict_find(iterator, MESSAGE_KEY_KEY_SYNC_ITEM);
  if (sync_item_tuple != NULL) {
    Tuple *index_tuple = dict_find(iterator, MESSAGE_KEY_KEY_ITEM_INDEX);
    Tuple *id_tuple = dict_find(iterator, MESSAGE_KEY_KEY_ITEM_ID);
    Tuple *name_tuple = dict_find(iterator, MESSAGE_KEY_KEY_ITEM_NAME);
    Tuple *checked_tuple = dict_find(iterator, MESSAGE_KEY_KEY_ITEM_CHECKED);

    bool valid_item = s_sync_in_progress && index_tuple != NULL &&
                      id_tuple != NULL && name_tuple != NULL &&
                      checked_tuple != NULL &&
                      index_tuple->value->int32 == s_received_sync_items &&
                      s_received_sync_items < s_expected_sync_items &&
                      id_tuple->value->int32 > 0 &&
                      name_tuple->type == TUPLE_CSTRING &&
                      name_tuple->length > 1;
    if (!valid_item) {
      fail_active_request("Invalid sync data");
      return;
    }

    checklist_add_remote_item(id_tuple->value->int32,
                              name_tuple->value->cstring,
                              checked_tuple->value->int32 != 0);
    s_received_sync_items++;
  }

  Tuple *sync_done_tuple = dict_find(iterator, MESSAGE_KEY_KEY_SYNC_DONE);
  if (sync_done_tuple != NULL) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Sync %lu done: %d/%d",
            (unsigned long)s_active_request_seq, s_received_sync_items,
            s_expected_sync_items);
    if (!s_sync_in_progress ||
        s_received_sync_items != s_expected_sync_items) {
      fail_active_request("Sync incomplete");
      return;
    }

    checklist_commit_replace();
    clear_request_state();
    s_has_status = false;
    should_refresh = true;
  }

  Tuple *sync_error_tuple = dict_find(iterator, MESSAGE_KEY_KEY_SYNC_ERROR);
  if (sync_error_tuple != NULL) {
    if (sync_error_tuple->type != TUPLE_CSTRING ||
        sync_error_tuple->length <= 1) {
      fail_active_request("Sync failed");
      return;
    }
    char error[sizeof(s_last_error)];
    strncpy(error, sync_error_tuple->value->cstring, sizeof(error) - 1);
    error[sizeof(error) - 1] = '\0';
    APP_LOG(APP_LOG_LEVEL_ERROR, "Sync error: %s", error);
    fail_active_request(error);
    return;
  }

  if (should_refresh) {
    notify_message_processed();
  }
}

void inbox_dropped_callback(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Inbox message dropped: %d", reason);
  if (s_request_pending) {
    fail_active_request("Message dropped");
  }
}

void outbox_failed_callback(DictionaryIterator *iterator,
                            AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox send failed: %d", reason);
  if (s_request_pending) {
    fail_active_request("Phone send failed");
  }
}

void outbox_sent_callback(DictionaryIterator *iterator, void *context) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Request sent");
}
