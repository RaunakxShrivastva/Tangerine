#include <gtk/gtk.h>
#include <adwaita.h>
#include "radial-menu.h"
#include "drag-monitor.h"
#include "conversion-engine.h"

static void test_radial_menu_creation(void) {
  GtkWidget *window = gtk_window_new();
  TangerineRadialMenu *menu = tangerine_radial_menu_new(GTK_WINDOW(window));

  g_assert_nonnull(menu);
  g_assert_true(GTK_IS_WINDOW(menu));

  g_object_unref(menu);
  g_object_unref(window);
}

static void test_radial_menu_items(void) {
  GtkWidget *window = gtk_window_new();
  TangerineRadialMenu *menu = tangerine_radial_menu_new(GTK_WINDOW(window));

  TangerineRadialItem items[] = {
    { "Test Item 1", "image-x-generic", "test-1", NULL },
    { "Test Item 2", "audio-x-generic", "test-2", NULL },
  };

  tangerine_radial_menu_set_items(menu, items, 2);

  g_object_unref(menu);
  g_object_unref(window);
}

static void test_drag_monitor_creation(void) {
  GdkDisplay *display = gdk_display_get_default();
  TangerineDragMonitor *monitor = tangerine_drag_monitor_new(display);

  g_assert_nonnull(monitor);
  g_assert_true(G_IS_OBJECT(monitor));

  g_object_unref(monitor);
}

static void test_conversion_engine(void) {
  TangerineConversionEngine *engine = tangerine_conversion_engine_new();

  g_assert_nonnull(engine);
  g_assert_true(G_IS_OBJECT(engine));

  g_object_unref(engine);
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);

  g_test_add_func("/tangerine/radial-menu/creation", test_radial_menu_creation);
  g_test_add_func("/tangerine/radial-menu/items", test_radial_menu_items);
  g_test_add_func("/tangerine/drag-monitor/creation", test_drag_monitor_creation);
  g_test_add_func("/tangerine/conversion-engine/creation", test_conversion_engine);

  return g_test_run();
}