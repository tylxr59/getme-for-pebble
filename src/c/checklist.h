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
 * Replaces the current list with items received from getme. With keep_order,
 * items already on the watch stay in their current rows instead of moving to
 * the server's order (getme moves checked items to the bottom).
 */
extern void checklist_begin_replace(void);
extern void checklist_add_remote_item(int32_t server_id, const char *name,
                                      bool is_checked);
extern void checklist_commit_replace(bool keep_order);
extern void checklist_cancel_replace(void);

extern int32_t checklist_get_server_id(int id);

/*
 * Updates the local checked state of an item without contacting the server.
 * Returns false when no item has the given server id.
 */
extern bool checklist_set_checked_by_server_id(int32_t server_id,
                                               bool is_checked);

/*
 * Returns the checklist item referred to by the given id
 */
extern ChecklistItem *checklist_get_item_by_id(int id);
