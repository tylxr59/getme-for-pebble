#include "checklist.h"

// constants
#define CURRENT_CHECKLIST_DATA_VERSION 4

// persistent storage keys
#define PERSIST_KEY_CHECKLIST_DATA_VERSION 50
#define PERSIST_KEY_CHECKLIST_LENGTH       100
#define PERSIST_KEY_CHECKLIST_NUM_CHECKED  101
#define PERSIST_KEY_CHECKLIST_BLOCK_FIRST  300

static ChecklistItem s_checklist_items[MAX_CHECKLIST_ITEMS];
static ChecklistItem s_replace_items[MAX_CHECKLIST_ITEMS];

static int s_checklist_length;
static int s_checklist_num_checked;
static int s_replace_length;
static int s_replace_num_checked;
static bool s_replace_in_progress;

// storage parameters
static int s_items_per_block;
static int s_block_size;

static void read_data_from_storage(void);
static void save_data_to_storage(void);

static bool checklist_length_is_valid(int length) {
  return length >= 0 && length <= MAX_CHECKLIST_ITEMS;
}

static int blocks_for_length(int length, int items_per_block) {
  if (length <= 0 || items_per_block <= 0) {
    return 0;
  }

  return (length + items_per_block - 1) / items_per_block;
}

static void reset_checklist(void) {
  memset(s_checklist_items, 0, sizeof(s_checklist_items));
  s_checklist_length = 0;
  s_checklist_num_checked = 0;
}

static void recount_checked_items(void) {
  s_checklist_num_checked = 0;
  for (int i = 0; i < s_checklist_length; i++) {
    if (s_checklist_items[i].is_checked) {
      s_checklist_num_checked++;
    }
  }
}

void checklist_init(void) {
  // determine storage params
  s_items_per_block =  PERSIST_DATA_MAX_LENGTH / sizeof(ChecklistItem);
  s_block_size = sizeof(ChecklistItem) * s_items_per_block;

  read_data_from_storage();
}

void checklist_deinit(void) {
  save_data_to_storage();
}

#define LEGACY_NAME_LENGTH 50

typedef struct {
  char name[LEGACY_NAME_LENGTH];
  bool is_checked;
  uint8_t sublist_id;
} LegacyV2ChecklistItem;

typedef struct {
  char name[MAX_NAME_LENGTH];
  bool is_checked;
  uint8_t sublist_id;
} LegacyV3ChecklistItem;

static bool migrate_v2_to_v4(void) {
  s_checklist_length = persist_read_int(PERSIST_KEY_CHECKLIST_LENGTH);
  if (!checklist_length_is_valid(s_checklist_length)) {
    reset_checklist();
    return false;
  }

  int old_items_per_block = PERSIST_DATA_MAX_LENGTH / sizeof(LegacyV2ChecklistItem);
  int old_block_size = old_items_per_block * sizeof(LegacyV2ChecklistItem);
  int num_old_blocks =
      blocks_for_length(s_checklist_length, old_items_per_block);

  LegacyV2ChecklistItem block_buf[PERSIST_DATA_MAX_LENGTH / sizeof(LegacyV2ChecklistItem)];

  for (int block = 0; block < num_old_blocks; block++) {
    if (persist_read_data(PERSIST_KEY_CHECKLIST_BLOCK_FIRST + block, block_buf,
                          old_block_size) != old_block_size) {
      reset_checklist();
      return false;
    }
    for (int j = 0; j < old_items_per_block; j++) {
      int i = block * old_items_per_block + j;
      if (i >= s_checklist_length) break;
      s_checklist_items[i].server_id = 0;
      strncpy(s_checklist_items[i].name, block_buf[j].name, LEGACY_NAME_LENGTH);
      s_checklist_items[i].name[LEGACY_NAME_LENGTH] = '\0';
      s_checklist_items[i].is_checked = block_buf[j].is_checked;
      s_checklist_items[i].sublist_id = block_buf[j].sublist_id;
    }
  }

  recount_checked_items();
  save_data_to_storage();
  return true;
}

static bool migrate_v3_to_v4(void) {
  s_checklist_length = persist_read_int(PERSIST_KEY_CHECKLIST_LENGTH);
  if (!checklist_length_is_valid(s_checklist_length)) {
    reset_checklist();
    return false;
  }

  int old_items_per_block = PERSIST_DATA_MAX_LENGTH / sizeof(LegacyV3ChecklistItem);
  int old_block_size = old_items_per_block * sizeof(LegacyV3ChecklistItem);
  int num_old_blocks =
      blocks_for_length(s_checklist_length, old_items_per_block);

  LegacyV3ChecklistItem block_buf[PERSIST_DATA_MAX_LENGTH / sizeof(LegacyV3ChecklistItem)];

  for (int block = 0; block < num_old_blocks; block++) {
    if (persist_read_data(PERSIST_KEY_CHECKLIST_BLOCK_FIRST + block, block_buf,
                          old_block_size) != old_block_size) {
      reset_checklist();
      return false;
    }
    for (int j = 0; j < old_items_per_block; j++) {
      int i = block * old_items_per_block + j;
      if (i >= s_checklist_length) break;
      s_checklist_items[i].server_id = 0;
      strncpy(s_checklist_items[i].name, block_buf[j].name, MAX_NAME_LENGTH - 1);
      s_checklist_items[i].name[MAX_NAME_LENGTH - 1] = '\0';
      s_checklist_items[i].is_checked = block_buf[j].is_checked;
      s_checklist_items[i].sublist_id = block_buf[j].sublist_id;
    }
  }

  recount_checked_items();
  save_data_to_storage();
  return true;
}

static void read_data_from_storage(void) {
  int saved_version = persist_read_int(PERSIST_KEY_CHECKLIST_DATA_VERSION);

  if (saved_version == 2) {
    migrate_v2_to_v4();
    return;
  } else if (saved_version == 3) {
    migrate_v3_to_v4();
    return;
  } else if (saved_version != CURRENT_CHECKLIST_DATA_VERSION) {
    reset_checklist();
    return;
  }

  // load checklist information from storage
  s_checklist_length = persist_read_int(PERSIST_KEY_CHECKLIST_LENGTH);
  if (!checklist_length_is_valid(s_checklist_length)) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Invalid saved checklist length: %d",
            s_checklist_length);
    reset_checklist();
    return;
  }

  // load the checklist by the block
  int num_blocks_required =
      blocks_for_length(s_checklist_length, s_items_per_block);

  for(int block = 0; block < num_blocks_required; block++) {
    if (persist_read_data(PERSIST_KEY_CHECKLIST_BLOCK_FIRST + block,
                          &s_checklist_items[block * s_items_per_block],
                          s_block_size) != s_block_size) {
      APP_LOG(APP_LOG_LEVEL_ERROR, "Missing saved checklist block: %d", block);
      reset_checklist();
      return;
    }
  }

  recount_checked_items();
}

static void save_data_to_storage(void) {
  // save version info
  persist_write_int(PERSIST_KEY_CHECKLIST_DATA_VERSION, CURRENT_CHECKLIST_DATA_VERSION);

  // save checklist information
  persist_write_int(PERSIST_KEY_CHECKLIST_LENGTH, s_checklist_length);
  persist_write_int(PERSIST_KEY_CHECKLIST_NUM_CHECKED , s_checklist_num_checked);

  // save the rest of the checklist
  // calculate how many persist blocks we'll need
  int num_blocks_required =
      blocks_for_length(s_checklist_length, s_items_per_block);

  for(int block = 0; block < num_blocks_required; block++) {
    persist_write_data(PERSIST_KEY_CHECKLIST_BLOCK_FIRST + block,
                       &s_checklist_items[block * s_items_per_block],
                       s_block_size);
  }
}

int checklist_get_num_items(void) {
  return s_checklist_length;
}

int checklist_get_num_items_checked(void) {
  return s_checklist_num_checked;
}

void checklist_begin_replace(void) {
  s_replace_length = 0;
  s_replace_num_checked = 0;
  s_replace_in_progress = true;
}

void checklist_add_remote_item(int32_t server_id, const char *name,
                               bool is_checked) {
  if (!s_replace_in_progress) {
    checklist_begin_replace();
  }

  if (s_replace_length >= MAX_CHECKLIST_ITEMS || name == NULL ||
      strlen(name) == 0) {
    return;
  }

  s_replace_items[s_replace_length].server_id = server_id;
  strncpy(s_replace_items[s_replace_length].name, name, MAX_NAME_LENGTH - 1);
  s_replace_items[s_replace_length].name[MAX_NAME_LENGTH - 1] = '\0';
  s_replace_items[s_replace_length].is_checked = is_checked;
  s_replace_items[s_replace_length].sublist_id = 0;

  if (is_checked) {
    s_replace_num_checked++;
  }

  s_replace_length++;
}

void checklist_commit_replace(void) {
  if (s_replace_in_progress) {
    memcpy(s_checklist_items, s_replace_items,
           sizeof(ChecklistItem) * s_replace_length);
    s_checklist_length = s_replace_length;
    s_checklist_num_checked = s_replace_num_checked;
    s_replace_in_progress = false;
  }

  save_data_to_storage();
}

void checklist_cancel_replace(void) {
  s_replace_length = 0;
  s_replace_num_checked = 0;
  s_replace_in_progress = false;
}

ChecklistItem *checklist_get_item_by_id(int id) {
  if (id < 0 || id >= s_checklist_length) {
    return NULL;
  }

  return &s_checklist_items[id];
}

int32_t checklist_get_server_id(int id) {
  if (id < 0 || id >= s_checklist_length) {
    return 0;
  }

  return s_checklist_items[id].server_id;
}
