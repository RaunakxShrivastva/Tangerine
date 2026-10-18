#pragma once

#include <gtk/gtk.h>
#include <adwaita.h>

G_BEGIN_DECLS

GtkWidget *tangerine_preferences_new(GSettings *settings, GtkWindow *parent);

G_END_DECLS