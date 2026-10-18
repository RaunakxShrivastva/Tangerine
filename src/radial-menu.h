#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define TANGERINE_TYPE_RADIAL_MENU (tangerine_radial_menu_get_type())
G_DECLARE_FINAL_TYPE(TangerineRadialMenu, tangerine_radial_menu, TANGERINE, RADIAL_MENU, GtkWindow)

typedef enum {
  TANGERINE_ACTION_CONVERT,
  TANGERINE_ACTION_TOOLS,
  TANGERINE_ACTION_FILE_OPS,
} TangerineActionMode;

typedef struct {
  const char *label;
  const char *icon_name;
  const char *action_id;
  gpointer user_data;
} TangerineRadialItem;

TangerineRadialMenu *tangerine_radial_menu_new(GtkWindow *parent);
void tangerine_radial_menu_set_items(TangerineRadialMenu *self, TangerineRadialItem *items, guint n_items);
void tangerine_radial_menu_set_mode(TangerineRadialMenu *self, TangerineActionMode mode);
void tangerine_radial_menu_show_at_cursor(TangerineRadialMenu *self);
void tangerine_radial_menu_hide(TangerineRadialMenu *self);

#define TANGERINE_RADIAL_MENU_ITEM_ACTIVATED "item-activated"

G_END_DECLS