#include "messaging.h"
#include "checklist.h"
#include "util.h"
#include <message_keys.auto.h>
#include <pebble.h>

#define REQUEST_TIMEOUT_MS 45000
#define PHONE_READY_TIMEOUT_MS 10000
#define OUTBOX_RETRY_MS 250
#define OUTBOX_MAX_ATTEMPTS 5

typedef enum {
  COMMAND_NONE,
  COMMAND_FETCH,
  COMMAND_ADD,
  COMMAND_CLEAR,
} CommandType;

// A checkbox change shown on the watch before getme has confirmed it.
typedef struct {
  int32_t server_id;
  uint32_t seq;
  // state the user asked for
  bool is_checked;
  // last state known to be on the server, restored if saving fails
  bool confirmed_checked;
  bool sent;
} PendingToggle;

static char s_last_error[64];
static bool s_has_error;
static void (*s_message_processed_callback)(void);

static uint32_t s_first_request_seq;
static uint32_t s_next_request_seq;

// Add, clear, and refresh requests run one at a time.
static CommandType s_command;
static uint32_t s_command_seq;
static bool s_command_sent;
static char s_command_name[MAX_NAME_LENGTH];

// Toggles never block the UI; at most one is tracked per item.
static PendingToggle s_toggles[MAX_CHECKLIST_ITEMS];
static int s_num_toggles;

static bool s_outbox_busy;
static uint32_t s_inflight_seq;
static int s_outbox_attempts;
static AppTimer *s_outbox_retry_timer;

static bool s_sync_in_progress;
static uint32_t s_sync_seq;
static int s_expected_sync_items;
static int s_received_sync_items;

static AppTimer *s_request_timer;
static AppTimer *s_phone_ready_timer;
static bool s_phone_ready;

static void pump_outbox(void);

static void notify_message_processed(void) {
  if (s_message_processed_callback) {
    s_message_processed_callback();
  }
}

static void set_last_error(const char *message) {
  snprintf(s_last_error, sizeof(s_last_error), "%s", message);
  s_has_error = true;
  notify_message_processed();
}

static uint32_t next_request_seq(void) {
  s_next_request_seq++;
  if (s_next_request_seq == 0) {
    s_next_request_seq++;
  }
  return s_next_request_seq;
}

// Messages from a previous app launch carry older sequence numbers.
static bool seq_is_current(uint32_t seq) {
  return seq >= s_first_request_seq && seq <= s_next_request_seq;
}

static PendingToggle *find_toggle(int32_t server_id) {
  for (int i = 0; i < s_num_toggles; i++) {
    if (s_toggles[i].server_id == server_id) {
      return &s_toggles[i];
    }
  }
  return NULL;
}

static void remove_toggle(int index) {
  s_num_toggles--;
  s_toggles[index] = s_toggles[s_num_toggles];
}

static bool awaiting_phone(void) {
  if (s_sync_in_progress || (s_command != COMMAND_NONE && s_command_sent)) {
    return true;
  }
  for (int i = 0; i < s_num_toggles; i++) {
    if (s_toggles[i].sent) {
      return true;
    }
  }
  return false;
}

static void cancel_sync(void) {
  checklist_cancel_replace();
  s_sync_in_progress = false;
}

static void fail_all_requests(const char *message) {
  cancel_sync();
  for (int i = 0; i < s_num_toggles; i++) {
    checklist_set_checked_by_server_id(s_toggles[i].server_id,
                                       s_toggles[i].confirmed_checked);
  }
  s_num_toggles = 0;
  s_command = COMMAND_NONE;
  if (s_request_timer != NULL) {
    app_timer_cancel(s_request_timer);
    s_request_timer = NULL;
  }
  set_last_error(message);
}

static void request_timeout_callback(void *context) {
  s_request_timer = NULL;
  if (awaiting_phone()) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Phone request timed out");
    fail_all_requests("Phone timeout");
  }
}

// Restarts the timeout whenever the phone makes progress.
static void update_request_timer(void) {
  if (!awaiting_phone()) {
    if (s_request_timer != NULL) {
      app_timer_cancel(s_request_timer);
      s_request_timer = NULL;
    }
    return;
  }

  if (s_request_timer == NULL ||
      !app_timer_reschedule(s_request_timer, REQUEST_TIMEOUT_MS)) {
    s_request_timer = app_timer_register(REQUEST_TIMEOUT_MS,
                                         request_timeout_callback, NULL);
  }
}

static void phone_ready_timeout_callback(void *context) {
  s_phone_ready_timer = NULL;
  if (!s_phone_ready) {
    set_last_error("Phone not ready");
  }
}

static void outbox_retry_callback(void *context) {
  s_outbox_retry_timer = NULL;
  pump_outbox();
}

static void schedule_outbox_retry(void) {
  s_outbox_attempts++;
  if (s_outbox_attempts >= OUTBOX_MAX_ATTEMPTS) {
    s_outbox_attempts = 0;
    fail_all_requests("Phone send failed");
    return;
  }

  s_outbox_retry_timer = app_timer_register(OUTBOX_RETRY_MS,
                                            outbox_retry_callback, NULL);
}

// Sends the oldest unsent request so the phone applies them in order.
static void pump_outbox(void) {
  if (s_outbox_busy || s_outbox_retry_timer != NULL) {
    return;
  }

  PendingToggle *toggle = NULL;
  for (int i = 0; i < s_num_toggles; i++) {
    if (!s_toggles[i].sent &&
        (toggle == NULL || s_toggles[i].seq < toggle->seq)) {
      toggle = &s_toggles[i];
    }
  }

  bool send_command = s_command != COMMAND_NONE && !s_command_sent &&
                      (toggle == NULL || s_command_seq < toggle->seq);
  if (!send_command && toggle == NULL) {
    return;
  }

  DictionaryIterator *iterator = NULL;
  AppMessageResult result = app_message_outbox_begin(&iterator);
  if (result != APP_MSG_OK || iterator == NULL) {
    schedule_outbox_retry();
    return;
  }

  uint32_t seq = send_command ? s_command_seq : toggle->seq;
  dict_write_uint32(iterator, MESSAGE_KEY_KEY_REQUEST_SEQ, seq);

  if (!send_command) {
    dict_write_uint8(iterator, MESSAGE_KEY_KEY_TOGGLE_REQUEST, 1);
    dict_write_int32(iterator, MESSAGE_KEY_KEY_ITEM_ID, toggle->server_id);
    dict_write_uint8(iterator, MESSAGE_KEY_KEY_ITEM_CHECKED,
                     toggle->is_checked ? 1 : 0);
  } else if (s_command == COMMAND_ADD) {
    dict_write_cstring(iterator, MESSAGE_KEY_KEY_ADD_REQUEST, s_command_name);
  } else if (s_command == COMMAND_CLEAR) {
    dict_write_uint8(iterator, MESSAGE_KEY_KEY_CLEAR_CHECKED_REQUEST, 1);
  } else {
    dict_write_uint8(iterator, MESSAGE_KEY_KEY_FETCH_REQUEST, 1);
  }

  result = app_message_outbox_send();
  if (result != APP_MSG_OK) {
    schedule_outbox_retry();
    return;
  }

  s_outbox_busy = true;
  s_inflight_seq = seq;
  if (send_command) {
    s_command_sent = true;
  } else {
    toggle->sent = true;
  }
  update_request_timer();
}

static void queue_command(CommandType type, const char *name) {
  if (s_command != COMMAND_NONE) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "Ignoring action while request %lu is active",
            (unsigned long)s_command_seq);
    return;
  }

  s_command = type;
  s_command_seq = next_request_seq();
  s_command_sent = false;
  snprintf(s_command_name, sizeof(s_command_name), "%s", name ? name : "");
  notify_message_processed();
  pump_outbox();
}

static void handle_phone_ready(void) {
  s_phone_ready = true;
  if (s_phone_ready_timer != NULL) {
    app_timer_cancel(s_phone_ready_timer);
    s_phone_ready_timer = NULL;
  }

  // The phone may have restarted and lost anything already sent. Toggles are
  // idempotent, so resend them; a lost add or clear is replaced by a refresh.
  cancel_sync();
  for (int i = 0; i < s_num_toggles; i++) {
    s_toggles[i].sent = false;
  }
  if (s_command == COMMAND_NONE || s_command_sent) {
    s_command = COMMAND_NONE;
    queue_command(COMMAND_FETCH, NULL);
  } else {
    pump_outbox();
  }
  update_request_timer();
}

// Every request up to and including seq has been applied on the server.
static void commit_sync(uint32_t seq) {
  bool covers_command = s_command != COMMAND_NONE && s_command_sent &&
                        s_command_seq <= seq;

  // Only a refresh reorders the list; otherwise rows would shift under the
  // selection while the user is still checking items off.
  checklist_commit_replace(!(covers_command && s_command == COMMAND_FETCH));
  s_sync_in_progress = false;

  if (covers_command) {
    s_command = COMMAND_NONE;
  }

  // Keep showing toggles the server has not seen yet.
  for (int i = s_num_toggles - 1; i >= 0; i--) {
    PendingToggle *toggle = &s_toggles[i];
    if ((toggle->sent && toggle->seq <= seq) ||
        !checklist_set_checked_by_server_id(toggle->server_id,
                                            toggle->is_checked)) {
      remove_toggle(i);
    }
  }

  notify_message_processed();
}

static void handle_request_error(uint32_t seq, const char *message) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Request %lu failed: %s", (unsigned long)seq,
          message);
  cancel_sync();

  if (s_command != COMMAND_NONE && s_command_sent && s_command_seq <= seq) {
    s_command = COMMAND_NONE;
  }

  for (int i = s_num_toggles - 1; i >= 0; i--) {
    PendingToggle *toggle = &s_toggles[i];
    if (!toggle->sent || toggle->seq > seq) {
      continue;
    }
    // Earlier toggles were processed; the next sync reports their result.
    if (toggle->seq == seq) {
      checklist_set_checked_by_server_id(toggle->server_id,
                                         toggle->confirmed_checked);
    }
    remove_toggle(i);
  }

  set_last_error(message);
}

void messaging_init(void (*processed_callback)(void)) {
  s_message_processed_callback = processed_callback;
  s_next_request_seq = (uint32_t)time(NULL);
  s_first_request_seq = s_next_request_seq + 1;

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
  if (s_request_timer != NULL) {
    app_timer_cancel(s_request_timer);
    s_request_timer = NULL;
  }
  if (s_outbox_retry_timer != NULL) {
    app_timer_cancel(s_outbox_retry_timer);
    s_outbox_retry_timer = NULL;
  }
  if (s_phone_ready_timer != NULL) {
    app_timer_cancel(s_phone_ready_timer);
    s_phone_ready_timer = NULL;
  }
  app_message_deregister_callbacks();
  s_message_processed_callback = NULL;
}

void messaging_send_fetch_request(void) {
  queue_command(COMMAND_FETCH, NULL);
}

void messaging_send_add_request(const char *name) {
  if (name == NULL || strlen(name) == 0) {
    return;
  }

  char truncated_name[MAX_NAME_LENGTH];
  strncpy(truncated_name, name, sizeof(truncated_name) - 1);
  truncated_name[sizeof(truncated_name) - 1] = '\0';
  utf8_trim_partial(truncated_name);
  if (truncated_name[0] == '\0') {
    return;
  }

  queue_command(COMMAND_ADD, truncated_name);
}

void messaging_send_toggle_request(int32_t server_id, bool is_checked) {
  if (server_id <= 0) {
    set_last_error("Sync before editing");
    return;
  }

  PendingToggle *toggle = find_toggle(server_id);
  if (toggle == NULL) {
    if (s_num_toggles >= MAX_CHECKLIST_ITEMS) {
      return;
    }
    toggle = &s_toggles[s_num_toggles++];
    toggle->server_id = server_id;
    toggle->confirmed_checked = !is_checked;
  }

  // A new sequence number moves the toggle behind anything queued earlier.
  toggle->is_checked = is_checked;
  toggle->seq = next_request_seq();
  toggle->sent = false;

  checklist_set_checked_by_server_id(server_id, is_checked);
  notify_message_processed();
  pump_outbox();
}

void messaging_send_clear_checked_request(void) {
  queue_command(COMMAND_CLEAR, NULL);
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
  const char *status = NULL;
  if (s_command == COMMAND_FETCH) {
    status = "Refreshing data";
  } else if (s_command == COMMAND_ADD) {
    status = "Adding";
  } else if (s_command == COMMAND_CLEAR) {
    status = "Clearing";
  } else if (s_num_toggles > 0) {
    status = "Saving";
  }

  if (status == NULL || buffer == NULL || buffer_size == 0) {
    return false;
  }

  snprintf(buffer, buffer_size, "%s", status);
  return true;
}

void inbox_received_callback(DictionaryIterator *iterator, void *context) {
  if (dict_find(iterator, MESSAGE_KEY_KEY_CONFIG_CHANGED) != NULL ||
      dict_find(iterator, MESSAGE_KEY_KEY_PHONE_READY) != NULL) {
    handle_phone_ready();
    return;
  }

  Tuple *seq_tuple = dict_find(iterator, MESSAGE_KEY_KEY_REQUEST_SEQ);
  if (seq_tuple == NULL || !seq_is_current(seq_tuple->value->uint32)) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "Ignoring stale or unsequenced message");
    return;
  }
  uint32_t seq = seq_tuple->value->uint32;

  Tuple *sync_error_tuple = dict_find(iterator, MESSAGE_KEY_KEY_SYNC_ERROR);
  if (sync_error_tuple != NULL) {
    if (sync_error_tuple->type != TUPLE_CSTRING ||
        sync_error_tuple->length <= 1) {
      handle_request_error(seq, "Sync failed");
    } else {
      char error[sizeof(s_last_error)];
      strncpy(error, sync_error_tuple->value->cstring, sizeof(error) - 1);
      error[sizeof(error) - 1] = '\0';
      handle_request_error(seq, error);
    }
    update_request_timer();
    return;
  }

  Tuple *sync_begin_tuple = dict_find(iterator, MESSAGE_KEY_KEY_SYNC_BEGIN);
  if (sync_begin_tuple != NULL) {
    int expected = sync_begin_tuple->value->int32;
    if (expected < 0 || expected > MAX_CHECKLIST_ITEMS) {
      handle_request_error(seq, "List is too long");
      update_request_timer();
      return;
    }

    s_sync_seq = seq;
    s_expected_sync_items = expected;
    s_received_sync_items = 0;
    s_sync_in_progress = true;
    checklist_begin_replace();
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Sync %lu begin: %d", (unsigned long)seq,
            s_expected_sync_items);
  }

  bool sync_message = dict_find(iterator, MESSAGE_KEY_KEY_SYNC_ITEM) != NULL ||
                      dict_find(iterator, MESSAGE_KEY_KEY_SYNC_DONE) != NULL;
  if (sync_message && (!s_sync_in_progress || seq != s_sync_seq)) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "Ignoring message for inactive sync");
    return;
  }

  Tuple *sync_item_tuple = dict_find(iterator, MESSAGE_KEY_KEY_SYNC_ITEM);
  if (sync_item_tuple != NULL) {
    Tuple *index_tuple = dict_find(iterator, MESSAGE_KEY_KEY_ITEM_INDEX);
    Tuple *id_tuple = dict_find(iterator, MESSAGE_KEY_KEY_ITEM_ID);
    Tuple *name_tuple = dict_find(iterator, MESSAGE_KEY_KEY_ITEM_NAME);
    Tuple *checked_tuple = dict_find(iterator, MESSAGE_KEY_KEY_ITEM_CHECKED);

    bool valid_item = index_tuple != NULL && id_tuple != NULL &&
                      name_tuple != NULL && checked_tuple != NULL &&
                      index_tuple->value->int32 == s_received_sync_items &&
                      s_received_sync_items < s_expected_sync_items &&
                      id_tuple->value->int32 > 0 &&
                      name_tuple->type == TUPLE_CSTRING &&
                      name_tuple->length > 1;
    if (!valid_item) {
      handle_request_error(seq, "Invalid sync data");
      update_request_timer();
      return;
    }

    checklist_add_remote_item(id_tuple->value->int32,
                              name_tuple->value->cstring,
                              checked_tuple->value->int32 != 0);
    s_received_sync_items++;
  }

  Tuple *sync_done_tuple = dict_find(iterator, MESSAGE_KEY_KEY_SYNC_DONE);
  if (sync_done_tuple != NULL) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Sync %lu done: %d/%d", (unsigned long)seq,
            s_received_sync_items, s_expected_sync_items);
    if (s_received_sync_items != s_expected_sync_items) {
      handle_request_error(seq, "Sync incomplete");
    } else {
      commit_sync(seq);
    }
  }

  update_request_timer();
}

void inbox_dropped_callback(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Inbox message dropped: %d", reason);
  if (awaiting_phone()) {
    fail_all_requests("Message dropped");
  }
}

void outbox_failed_callback(DictionaryIterator *iterator,
                            AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox send failed: %d", reason);
  s_outbox_busy = false;

  // Put the failed request back so it is retried in its original order.
  if (s_command != COMMAND_NONE && s_command_seq == s_inflight_seq) {
    s_command_sent = false;
  }
  for (int i = 0; i < s_num_toggles; i++) {
    if (s_toggles[i].seq == s_inflight_seq) {
      s_toggles[i].sent = false;
    }
  }

  schedule_outbox_retry();
}

void outbox_sent_callback(DictionaryIterator *iterator, void *context) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Request %lu sent",
          (unsigned long)s_inflight_seq);
  s_outbox_busy = false;
  s_outbox_attempts = 0;
  pump_outbox();
}
