#include "checklist.h"
#include "util.h"

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

// "Private" functions
void read_data_from_storage();
void save_data_to_storage();
void add_item(char *name);

void checklist_init() {
  // determine storage params
  s_items_per_block =  PERSIST_DATA_MAX_LENGTH / sizeof(ChecklistItem);
  s_block_size = sizeof(ChecklistItem) * s_items_per_block;

  read_data_from_storage();
}

void checklist_deinit() {
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

static void migrate_v2_to_v3() {
  s_checklist_length = persist_read_int(PERSIST_KEY_CHECKLIST_LENGTH);
  s_checklist_num_checked = persist_read_int(PERSIST_KEY_CHECKLIST_NUM_CHECKED);

  int old_items_per_block = PERSIST_DATA_MAX_LENGTH / sizeof(LegacyV2ChecklistItem);
  int old_block_size = old_items_per_block * sizeof(LegacyV2ChecklistItem);
  int num_old_blocks = s_checklist_length / old_items_per_block + 1;

  LegacyV2ChecklistItem block_buf[PERSIST_DATA_MAX_LENGTH / sizeof(LegacyV2ChecklistItem)];

  for (int block = 0; block < num_old_blocks; block++) {
    persist_read_data(PERSIST_KEY_CHECKLIST_BLOCK_FIRST + block, block_buf, old_block_size);
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

  save_data_to_storage();
}

static void migrate_v3_to_v4() {
  s_checklist_length = persist_read_int(PERSIST_KEY_CHECKLIST_LENGTH);
  s_checklist_num_checked = persist_read_int(PERSIST_KEY_CHECKLIST_NUM_CHECKED);

  int old_items_per_block = PERSIST_DATA_MAX_LENGTH / sizeof(LegacyV3ChecklistItem);
  int old_block_size = old_items_per_block * sizeof(LegacyV3ChecklistItem);
  int num_old_blocks = s_checklist_length / old_items_per_block + 1;

  LegacyV3ChecklistItem block_buf[PERSIST_DATA_MAX_LENGTH / sizeof(LegacyV3ChecklistItem)];

  for (int block = 0; block < num_old_blocks; block++) {
    persist_read_data(PERSIST_KEY_CHECKLIST_BLOCK_FIRST + block, block_buf, old_block_size);
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

  save_data_to_storage();
}

void read_data_from_storage() {
  int saved_version = persist_read_int(PERSIST_KEY_CHECKLIST_DATA_VERSION);

  if (saved_version == 2) {
    migrate_v2_to_v3();
  } else if (saved_version == 3) {
    migrate_v3_to_v4();
  }

  // load checklist information from storage
  s_checklist_length = persist_read_int(PERSIST_KEY_CHECKLIST_LENGTH);
  s_checklist_num_checked = persist_read_int(PERSIST_KEY_CHECKLIST_NUM_CHECKED);

  // load the checklist by the block
  int num_blocks_required = s_checklist_length / s_items_per_block + 1;

  for(int block = 0; block < num_blocks_required; block++) {
    persist_read_data(PERSIST_KEY_CHECKLIST_BLOCK_FIRST + block,
                       &s_checklist_items[block * s_items_per_block],
                       s_block_size);
  }
}

void save_data_to_storage() {
  // save version info
  persist_write_int(PERSIST_KEY_CHECKLIST_DATA_VERSION, CURRENT_CHECKLIST_DATA_VERSION);

  // save checklist information
  persist_write_int(PERSIST_KEY_CHECKLIST_LENGTH, s_checklist_length);
  persist_write_int(PERSIST_KEY_CHECKLIST_NUM_CHECKED , s_checklist_num_checked);

  // save the rest of the checklist
  // calculate how many persist blocks we'll need
  int num_blocks_required = s_checklist_length / s_items_per_block + 1;

  for(int block = 0; block < num_blocks_required; block++) {
    persist_write_data(PERSIST_KEY_CHECKLIST_BLOCK_FIRST + block,
                       &s_checklist_items[block * s_items_per_block],
                       s_block_size);
  }
}

int checklist_get_num_items() {
  return s_checklist_length;
}

int checklist_get_num_items_checked() {
  return s_checklist_num_checked;
}

void checklist_add_items(char *name) {
  add_item(name);
}

void add_item(char *name) {
  name = capitalize(trim_whitespace(name));

  if(s_checklist_length < MAX_CHECKLIST_ITEMS && strlen(name) > 0) {
    s_checklist_items[s_checklist_length].server_id = 0;
    strncpy(s_checklist_items[s_checklist_length].name, name, MAX_NAME_LENGTH - 1);
    s_checklist_items[s_checklist_length].name[MAX_NAME_LENGTH - 1] = '\0';
    s_checklist_items[s_checklist_length].is_checked = false;
    s_checklist_items[s_checklist_length].sublist_id = 0;

    // capitalize the item
    // s_checklist_items[s_checklist_length].name[0] = toupper(int);

    s_checklist_length++;
  } else {
    APP_LOG(APP_LOG_LEVEL_WARNING, "Failed to add checklist item; list exceeded maximum size.");
  }
}

void checklist_begin_replace() {
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

void checklist_commit_replace() {
  if (s_replace_in_progress) {
    memcpy(s_checklist_items, s_replace_items,
           sizeof(ChecklistItem) * s_replace_length);
    s_checklist_length = s_replace_length;
    s_checklist_num_checked = s_replace_num_checked;
    s_replace_in_progress = false;
  }

  save_data_to_storage();
}

void checklist_cancel_replace() {
  s_replace_length = 0;
  s_replace_num_checked = 0;
  s_replace_in_progress = false;
}

void checklist_item_toggle_checked(int id) {
  if (id < 0 || id >= s_checklist_length) {
    return;
  }

  s_checklist_items[id].is_checked = !(s_checklist_items[id].is_checked);

  if(s_checklist_items[id].is_checked) {
    s_checklist_num_checked++;
  } else {
    s_checklist_num_checked--;
  }

  // save the edited item to persist
  // save_data_to_storage();

  // printf("Num items checked: %i, Num items: %i", checklist_get_num_items_checked(), checklist_get_num_items());
}

void checklist_item_set_checked(int id, bool is_checked) {
  if (id < 0 || id >= s_checklist_length ||
      s_checklist_items[id].is_checked == is_checked) {
    return;
  }

  checklist_item_toggle_checked(id);
}

int checklist_delete_completed_items() {
  int num_deleted = 0;
  int i = 0;

  while (i < s_checklist_length) {
    if(s_checklist_items[i].is_checked) {
      memmove(&s_checklist_items[i], &s_checklist_items[i+1], sizeof(s_checklist_items[0])*(s_checklist_length - i));
      num_deleted++;
      s_checklist_length--;
    } else {
      i++;
    }
  }

  s_checklist_num_checked -= num_deleted;
  return num_deleted;
}

void checklist_clear() {
  s_checklist_length = 0;
  s_checklist_num_checked = 0;
}

ChecklistItem *checklist_get_item_by_id(int id) {
  return &s_checklist_items[id];
}

int32_t checklist_get_server_id(int id) {
  if (id < 0 || id >= s_checklist_length) {
    return 0;
  }

  return s_checklist_items[id].server_id;
}
