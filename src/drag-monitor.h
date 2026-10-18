#pragma once

#include <gtk/gtk.h>
#include "radial-menu.h"

G_BEGIN_DECLS

#define TANGERINE_TYPE_DRAG_MONITOR (tangerine_drag_monitor_get_type())
G_DECLARE_FINAL_TYPE(TangerineDragMonitor, tangerine_drag_monitor, TANGERINE, DRAG_MONITOR, GObject)

typedef void (*TangerineDragCallback)(TangerineActionMode mode, GdkDrag *drag, gpointer user_data);

TangerineDragMonitor *tangerine_drag_monitor_new(GdkDisplay *display);
void tangerine_drag_monitor_set_callback(TangerineDragMonitor *self, TangerineDragCallback cb, gpointer user_data);

G_END_DECLS