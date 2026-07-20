#pragma once

#include <pebble.h>

#define CHECKLIST_WINDOW_BOX_SIZE   12
#define CHECKLIST_CELL_MIN_HEIGHT   PBL_IF_ROUND_ELSE(49, 45)
#define CHECKLIST_CELL_MAX_HEIGHT   82
#define CHECKLIST_CELL_MARGIN        5


#define BG_COLOR PBL_IF_COLOR_ELSE(GColorYellow, GColorWhite)


void checklist_window_push(void);
void checklist_window_refresh(void);
