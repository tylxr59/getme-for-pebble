#pragma once
#include "sync_state.h"
// Packs one request. A NULL toggle selects the pending command.
AppMessageResult watch_protocol_send(const SyncState *state, uint32_t seq,
                                    const PendingToggle *toggle);
