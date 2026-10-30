#include "sync_state.h"
#include "pending_store.h"
bool sync_state_load(SyncState *state) {
  SyncState saved;
  if (!pending_store_read(&saved, sizeof(saved)) || saved.version != 1 ||
      saved.num_toggles < 0 || saved.num_toggles > MAX_CHECKLIST_ITEMS ||
      (unsigned)saved.command > COMMAND_CLEAR ||
      saved.clear_count < 0 || saved.clear_count > MAX_CHECKLIST_ITEMS) return false;
  saved.command_name[sizeof(saved.command_name) - 1] = '\0';
  for (int i = 0; i < saved.num_toggles; i++) saved.toggles[i].sent = false;
  *state = saved;
  return true;
}
bool sync_state_save(SyncState *state) {
  state->version = 1;
  return pending_store_write(state, sizeof(*state));
}
