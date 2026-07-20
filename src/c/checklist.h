#pragma once

#include <pebble.h>
#include <string.h>

#define MAX_NAME_LENGTH 90
#define MAX_CHECKLIST_ITEMS 52

typedef struct ChecklistItem {
  // stable id assigned by getme
  int32_t server_id;

  // the name displayed for the item
  char name[MAX_NAME_LENGTH];

  // is the item checked?
  bool is_checked;

  // reserved for future use
  uint8_t sublist_id;
} ChecklistItem;

extern void checklist_init(void);
extern void checklist_deinit(void);

/*
 * Returns the total number of checklist items
 */
extern int checklist_get_num_items(void);

/*
 * Returns the total number of checked items
 */
extern int checklist_get_num_items_checked(void);

/*
 * Replaces the current list with items received from getme.
 */
extern void checklist_begin_replace(void);
extern void checklist_add_remote_item(int32_t server_id, const char *name,
                                      bool is_checked);
extern void checklist_commit_replace(void);
extern void checklist_cancel_replace(void);

extern int32_t checklist_get_server_id(int id);

/*
 * Returns the checklist item referred to by the given id
 */
extern ChecklistItem *checklist_get_item_by_id(int id);
