#pragma once
#include "checklist.h"
typedef enum { COMMAND_NONE, COMMAND_FETCH, COMMAND_ADD, COMMAND_CLEAR } CommandType;
typedef struct {
  int32_t server_id;
  uint32_t seq;
  bool is_checked;
  bool confirmed_checked;
  bool sent;
} PendingToggle;
// Durable client state. Delivery flags are transient and reset on load.
typedef struct {
  uint32_t version;
  uint32_t next_seq;
  uint32_t ack_seq;
  uint32_t revision;
  int clear_count;
  int32_t clear_ids[MAX_CHECKLIST_ITEMS];
  CommandType command;
  uint32_t command_seq;
  char command_name[MAX_NAME_LENGTH];
  int num_toggles;
  PendingToggle toggles[MAX_CHECKLIST_ITEMS];
} SyncState;
bool sync_state_load(SyncState *state);
bool sync_state_save(SyncState *state);
