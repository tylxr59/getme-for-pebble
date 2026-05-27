#include <pebble.h>
#include "windows/checklist_window.h"
#include "messaging.h"

static void init() {
  messaging_init(checklist_window_refresh);
  checklist_window_push();
}

static void deinit() {
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
