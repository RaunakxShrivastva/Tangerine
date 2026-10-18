#include "drag-monitor.h"
#include <gdk/gdk.h>
#include <gtk/gtk.h>

struct _TangerineDragMonitor {
  GObject parent_instance;
  GdkDisplay *display;
  GtkEventController *key_controller;
  TangerineDragCallback callback;
  gpointer user_data;
  guint current_modifiers;
};

G_DEFINE_TYPE(TangerineDragMonitor, tangerine_drag_monitor, G_TYPE_OBJECT)

static gboolean on_key_pressed(GtkEventControllerKey *controller G_GNUC_UNUSED, guint keyval G_GNUC_UNUSED, guint keycode G_GNUC_UNUSED, GdkModifierType state, gpointer data) {
  TangerineDragMonitor *self = data;
  self->current_modifiers = state;
  return FALSE;
}

static gboolean on_key_released(GtkEventControllerKey *controller G_GNUC_UNUSED, guint keyval G_GNUC_UNUSED, guint keycode G_GNUC_UNUSED, GdkModifierType state, gpointer data) {
  TangerineDragMonitor *self = data;
  self->current_modifiers = state;
  return FALSE;
}

static void tangerine_drag_monitor_class_init(TangerineDragMonitorClass *klass G_GNUC_UNUSED) {
}

static void tangerine_drag_monitor_init(TangerineDragMonitor *self) {
  self->current_modifiers = 0;

  self->key_controller = gtk_event_controller_key_new();
  g_signal_connect(self->key_controller, "key-pressed", G_CALLBACK(on_key_pressed), self);
  g_signal_connect(self->key_controller, "key-released", G_CALLBACK(on_key_released), self);
}

TangerineDragMonitor *tangerine_drag_monitor_new(GdkDisplay *display) {
  TangerineDragMonitor *self = g_object_new(TANGERINE_TYPE_DRAG_MONITOR, NULL);
  self->display = display;
  return self;
}

void tangerine_drag_monitor_set_callback(TangerineDragMonitor *self, TangerineDragCallback cb, gpointer user_data) {
  self->callback = cb;
  self->user_data = user_data;
}