#include <pebble.h>
#include "windows/checklist_window.h"
#include "messaging.h"

static void init(void) {
  messaging_init(checklist_window_refresh);
  checklist_window_push();
}

static void deinit(void) {
  messaging_deinit();
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
