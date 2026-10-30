#include "messaging.h"
#include "checklist.h"
#include "util.h"
#include "sync_state.h"
#include "watch_protocol.h"
#include <message_keys.auto.h>
#include <pebble.h>

#define REQUEST_TIMEOUT_MS 45000
#define PHONE_READY_TIMEOUT_MS 10000
#define OUTBOX_RETRY_MS 250
#define OUTBOX_MAX_ATTEMPTS 5

static char s_last_error[64];
static bool s_has_error;
static void (*s_message_processed_callback)(void);

static uint32_t s_first_request_seq;

// Add, clear, and refresh requests run one at a time.
static bool s_command_sent;

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
static bool s_paused;
static int s_total_items;
static int s_pending_total_items;
static uint32_t s_pending_revision;

static SyncState s_state;

static bool save_pending(void) {
  if (!sync_state_save(&s_state)) {
    s_paused = true;
    s_has_error = true;
    snprintf(s_last_error, sizeof(s_last_error), "Cannot save pending changes");
    return false;
  }
  return true;
}

static void restore_pending(void) {
  if (!sync_state_load(&s_state)) return;
  s_first_request_seq = s_state.next_seq + 1;
  if (s_state.command != COMMAND_NONE && s_state.command_seq < s_first_request_seq) s_first_request_seq = s_state.command_seq;
  for (int i = 0; i < s_state.num_toggles; i++) {
    s_state.toggles[i].sent = false;
    if (s_state.toggles[i].seq < s_first_request_seq) s_first_request_seq = s_state.toggles[i].seq;
  }
}


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
  s_state.next_seq++;
  if (s_state.next_seq == 0) {
    s_state.next_seq++;
  }
  return s_state.next_seq;
}

// Only accept replies for this journal's current operation range.
static bool seq_is_current(uint32_t seq) {
  return seq >= s_first_request_seq && seq <= s_state.next_seq;
}

static PendingToggle *find_toggle(int32_t server_id) {
  for (int i = 0; i < s_state.num_toggles; i++) {
    if (s_state.toggles[i].server_id == server_id) {
      return &s_state.toggles[i];
    }
  }
  return NULL;
}

static void remove_toggle(int index) {
  s_state.num_toggles--;
  s_state.toggles[index] = s_state.toggles[s_state.num_toggles];
}

static bool awaiting_phone(void) {
  if (s_sync_in_progress || (s_state.command != COMMAND_NONE && s_command_sent)) {
    return true;
  }
  for (int i = 0; i < s_state.num_toggles; i++) {
    if (s_state.toggles[i].sent) {
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
  // Connectivity failure is not a server rejection. Retain durable work.
  s_paused = true;
  s_command_sent = false;
  for (int i = 0; i < s_state.num_toggles; i++) s_state.toggles[i].sent = false;
  save_pending();
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
  if (s_paused || s_outbox_busy || s_outbox_retry_timer != NULL) {
    return;
  }

  PendingToggle *toggle = NULL;
  for (int i = 0; i < s_state.num_toggles; i++) {
    if (!s_state.toggles[i].sent &&
        (toggle == NULL || s_state.toggles[i].seq < toggle->seq)) {
      toggle = &s_state.toggles[i];
    }
  }

  bool send_command = s_state.command != COMMAND_NONE && !s_command_sent &&
                      (toggle == NULL || s_state.command_seq < toggle->seq);
  if (!send_command && toggle == NULL) {
    return;
  }

  if (!save_pending()) return;

  uint32_t seq = send_command ? s_state.command_seq : toggle->seq;
  AppMessageResult result = watch_protocol_send(&s_state, seq, send_command ? NULL : toggle);
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
  if (s_state.command != COMMAND_NONE) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "Action blocked by pending request %lu",
            (unsigned long)s_state.command_seq);
    set_last_error("Sync pending; refresh then retry");
    return;
  }

  s_state.command = type;
  s_state.command_seq = next_request_seq();
  s_command_sent = false;
  snprintf(s_state.command_name, sizeof(s_state.command_name), "%s", name ? name : "");
  save_pending();
  notify_message_processed();
  pump_outbox();
}

static void handle_phone_ready(void) {
  s_phone_ready = true;
  if (s_phone_ready_timer != NULL) {
    app_timer_cancel(s_phone_ready_timer);
    s_phone_ready_timer = NULL;
  }

  for (int i = 0; i < s_state.num_toggles; i++) {
    checklist_set_checked_by_server_id(s_state.toggles[i].server_id, s_state.toggles[i].is_checked);
  }
  // Stable operation IDs make every pending mutation safe to resend.
  s_paused = false;
  cancel_sync();
  for (int i = 0; i < s_state.num_toggles; i++) s_state.toggles[i].sent = false;
  s_command_sent = false;
  if (s_state.command == COMMAND_NONE && s_state.num_toggles == 0) queue_command(COMMAND_FETCH, NULL);
  else pump_outbox();
  update_request_timer();
}

// Every request up to and including seq has been applied on the server.
static void commit_sync(uint32_t seq) {
  bool covers_command = s_state.command != COMMAND_NONE && s_command_sent &&
                        s_state.command_seq <= seq;

  // Only a refresh reorders the list; otherwise rows would shift under the
  // selection while the user is still checking items off.
  checklist_commit_replace(!(covers_command && s_state.command == COMMAND_FETCH));
  s_sync_in_progress = false;
  s_total_items = s_pending_total_items;
  s_state.revision = s_pending_revision;
  s_state.ack_seq = seq;

  if (covers_command) {
    s_state.command = COMMAND_NONE;
  }

  // Keep showing toggles the server has not seen yet.
  for (int i = s_state.num_toggles - 1; i >= 0; i--) {
    PendingToggle *toggle = &s_state.toggles[i];
    if ((toggle->sent && toggle->seq <= seq) ||
        !checklist_set_checked_by_server_id(toggle->server_id,
                                            toggle->is_checked)) {
      remove_toggle(i);
    }
  }

  save_pending();
  notify_message_processed();
}

static void handle_request_error(uint32_t seq, const char *message) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Request %lu failed: %s", (unsigned long)seq,
          message);
  cancel_sync();

  if (s_state.command != COMMAND_NONE && s_command_sent && s_state.command_seq <= seq) {
    s_state.command = COMMAND_NONE;
  }

  for (int i = s_state.num_toggles - 1; i >= 0; i--) {
    PendingToggle *toggle = &s_state.toggles[i];
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

  s_state.ack_seq = seq;
  save_pending();
  set_last_error(message);
}

void messaging_init(void (*processed_callback)(void)) {
  s_message_processed_callback = processed_callback;
  s_state.next_seq = (uint32_t)time(NULL);
  s_first_request_seq = s_state.next_seq + 1;
  restore_pending();

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
  save_pending();
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
  if (s_paused) {
    s_paused = false;
    if (s_state.command != COMMAND_NONE || s_state.num_toggles > 0) { pump_outbox(); return; }
  }
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
    if (s_state.num_toggles >= MAX_CHECKLIST_ITEMS) {
      return;
    }
    toggle = &s_state.toggles[s_state.num_toggles++];
    toggle->server_id = server_id;
    toggle->confirmed_checked = !is_checked;
  }

  // A new sequence number moves the toggle behind anything queued earlier.
  toggle->is_checked = is_checked;
  toggle->seq = next_request_seq();
  toggle->sent = false;

  checklist_set_checked_by_server_id(server_id, is_checked);
  save_pending();
  notify_message_processed();
  pump_outbox();
}

void messaging_send_clear_checked_request(void) {
  if (s_state.command != COMMAND_NONE) return;
  s_state.clear_count = 0;
  for (int i = 0; i < checklist_get_num_items(); i++) {
    ChecklistItem *item = checklist_get_item_by_id(i);
    if (item && item->is_checked && item->server_id > 0) s_state.clear_ids[s_state.clear_count++] = item->server_id;
  }
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
  if (s_paused && (s_state.command != COMMAND_NONE || s_state.num_toggles > 0)) status = "Not saved; refresh to retry";
  else if (s_state.command == COMMAND_FETCH) {
    status = "Refreshing data";
  } else if (s_state.command == COMMAND_ADD) {
    status = "Adding";
  } else if (s_state.command == COMMAND_CLEAR) {
    status = "Clearing";
  } else if (s_state.num_toggles > 0) {
    status = "Saving";
  }

  if (status == NULL && s_total_items > MAX_CHECKLIST_ITEMS && buffer && buffer_size) {
    snprintf(buffer, buffer_size, "Showing %d of %d", MAX_CHECKLIST_ITEMS, s_total_items);
    return true;
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
    Tuple *ready = dict_find(iterator, MESSAGE_KEY_KEY_PHONE_READY);
    if (!ready) ready = dict_find(iterator, MESSAGE_KEY_KEY_CONFIG_CHANGED);
    if (ready->value->int32 != 2) { fail_all_requests("Update the phone bridge"); return; }
    if (dict_find(iterator, MESSAGE_KEY_KEY_CONFIG_CHANGED)) {
      bool had_pending = s_state.num_toggles > 0 || (s_state.command != COMMAND_NONE && s_state.command != COMMAND_FETCH);
      s_state.num_toggles = 0;
      s_state.command = COMMAND_NONE;
      s_state.revision = 0;
      s_first_request_seq = s_state.next_seq + 1;
      save_pending();
      if (had_pending) set_last_error("Server changed; edits discarded");
    }
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
    Tuple *retryable = dict_find(iterator, MESSAGE_KEY_KEY_RETRYABLE);
    if (retryable && retryable->value->int32) {
      fail_all_requests(sync_error_tuple->value->cstring);
      return;
    }
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
    Tuple *version = dict_find(iterator, MESSAGE_KEY_KEY_PROTOCOL_VERSION);
    if (!version || version->value->int32 != 2) { fail_all_requests("Update the phone bridge"); return; }
    Tuple *revision = dict_find(iterator, MESSAGE_KEY_KEY_REVISION);
    if (!revision) { fail_all_requests("Missing list revision"); return; }
    s_pending_revision = revision->value->uint32;
    Tuple *total = dict_find(iterator, MESSAGE_KEY_KEY_TOTAL_ITEMS);
    s_pending_total_items = total ? total->value->int32 : 0;
    int expected = sync_begin_tuple->value->int32;
    if (expected < 0 || expected > MAX_CHECKLIST_ITEMS) {
      fail_all_requests("Invalid snapshot size; retry");
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
      fail_all_requests("Invalid sync data; retry");
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
      fail_all_requests("Sync incomplete; retry");
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
  if (s_state.command != COMMAND_NONE && s_state.command_seq == s_inflight_seq) {
    s_command_sent = false;
  }
  for (int i = 0; i < s_state.num_toggles; i++) {
    if (s_state.toggles[i].seq == s_inflight_seq) {
      s_state.toggles[i].sent = false;
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
