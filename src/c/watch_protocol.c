#include "watch_protocol.h"
#include <message_keys.auto.h>
AppMessageResult watch_protocol_send(const SyncState *state, uint32_t seq,
                                    const PendingToggle *toggle) {
  DictionaryIterator *iterator = NULL;
  AppMessageResult result = app_message_outbox_begin(&iterator);
  if (result != APP_MSG_OK || iterator == NULL) return result == APP_MSG_OK ? APP_MSG_BUSY : result;
  dict_write_uint32(iterator, MESSAGE_KEY_KEY_REQUEST_SEQ, seq);
  dict_write_uint8(iterator, MESSAGE_KEY_KEY_PROTOCOL_VERSION, 2);
  dict_write_uint32(iterator, MESSAGE_KEY_KEY_ACK_SEQ, state->ack_seq);
  dict_write_uint32(iterator, MESSAGE_KEY_KEY_REVISION, state->revision);
  if (toggle) {
    dict_write_uint8(iterator, MESSAGE_KEY_KEY_TOGGLE_REQUEST, 1);
    dict_write_int32(iterator, MESSAGE_KEY_KEY_ITEM_ID, toggle->server_id);
    dict_write_uint8(iterator, MESSAGE_KEY_KEY_ITEM_CHECKED, toggle->is_checked ? 1 : 0);
  } else if (state->command == COMMAND_ADD) {
    dict_write_cstring(iterator, MESSAGE_KEY_KEY_ADD_REQUEST, state->command_name);
  } else if (state->command == COMMAND_CLEAR) {
    dict_write_uint8(iterator, MESSAGE_KEY_KEY_CLEAR_CHECKED_REQUEST, 1);
    dict_write_data(iterator, MESSAGE_KEY_KEY_TARGET_IDS, (const uint8_t *)state->clear_ids,
                    state->clear_count * sizeof(int32_t));
  } else {
    dict_write_uint8(iterator, MESSAGE_KEY_KEY_FETCH_REQUEST, 1);
  }
  return app_message_outbox_send();
}
