#include "dialog_message_window.h"

#define MARGIN 10

static Window *s_main_window;
static TextLayer *s_label_layer;
static GBitmap *s_icon_bitmap;
static BitmapLayer *s_icon_layer;
static char *s_message_text;

static void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  window_set_background_color(window, PBL_IF_COLOR_ELSE(GColorOrange,
                                                         GColorWhite));

  s_icon_bitmap = gbitmap_create_with_resource(RESOURCE_ID_SETTINGS);
  GRect bitmap_bounds = gbitmap_get_bounds(s_icon_bitmap);
  s_icon_layer = bitmap_layer_create(
      GRect((bounds.size.w - bitmap_bounds.size.w) / 2, MARGIN,
            bitmap_bounds.size.w, bitmap_bounds.size.h));
  bitmap_layer_set_bitmap(s_icon_layer, s_icon_bitmap);
  bitmap_layer_set_compositing_mode(s_icon_layer, GCompOpSet);
  layer_add_child(window_layer, bitmap_layer_get_layer(s_icon_layer));

  s_label_layer = text_layer_create(
      GRect(MARGIN, bounds.size.h / 2 + 10, bounds.size.w - (2 * MARGIN),
            bounds.size.h / 2 - 10));
  text_layer_set_text(s_label_layer, s_message_text);
  text_layer_set_background_color(s_label_layer, GColorClear);
  text_layer_set_text_alignment(s_label_layer, GTextAlignmentCenter);
  text_layer_set_font(s_label_layer,
                      fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  layer_add_child(window_layer, text_layer_get_layer(s_label_layer));
}

static void window_unload(Window *window) {
  text_layer_destroy(s_label_layer);
  s_label_layer = NULL;

  bitmap_layer_destroy(s_icon_layer);
  s_icon_layer = NULL;

  gbitmap_destroy(s_icon_bitmap);
  s_icon_bitmap = NULL;

  window_destroy(window);
  s_main_window = NULL;
  s_message_text = NULL;
}

void dialog_settings_window_push(char *message) {
  s_message_text = message;

  if (!s_main_window) {
    s_main_window = window_create();
    window_set_window_handlers(s_main_window, (WindowHandlers) {
        .load = window_load,
        .unload = window_unload
    });
  }

  window_stack_push(s_main_window, true);
}
