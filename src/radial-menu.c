#include "radial-menu.h"
#include <cairo.h>
#include <math.h>

#define RADIAL_RADIUS 120
#define INNER_RADIUS 40
#define ANIMATION_DURATION 200

struct _TangerineRadialMenu {
  GtkWindow parent_instance;
  GtkWidget *drawing_area;
  TangerineRadialItem *items;
  guint n_items;
  TangerineActionMode mode;
  guint animation_id;
  double progress;
  gint hovered_index;
  gint pressed_index;
};

G_DEFINE_TYPE(TangerineRadialMenu, tangerine_radial_menu, GTK_TYPE_WINDOW)

static double ease_out_cubic(double t) {
  double f = t - 1.0;
  return f * f * f + 1.0;
}

static void animate_tick(TangerineRadialMenu *self) {
  self->progress += 1.0 / (ANIMATION_DURATION / 16.0);
  if (self->progress >= 1.0) {
    self->progress = 1.0;
    if (self->animation_id) {
      g_source_remove(self->animation_id);
      self->animation_id = 0;
    }
  }
  gtk_widget_queue_draw(self->drawing_area);
}

static gboolean on_tick(GtkWidget *widget G_GNUC_UNUSED, GdkFrameClock *clock G_GNUC_UNUSED, gpointer data) {
  animate_tick(data);
  return G_SOURCE_CONTINUE;
}

static void start_animation(TangerineRadialMenu *self) {
  self->progress = 0.0;
  if (self->animation_id) g_source_remove(self->animation_id);
  self->animation_id = gtk_widget_add_tick_callback(self->drawing_area, on_tick, self, NULL);
}

static gint get_item_at_angle(TangerineRadialMenu *self, double cx, double cy, double x, double y) {
  double dx = x - cx;
  double dy = y - cy;
  double dist = sqrt(dx * dx + dy * dy);

  if (dist < INNER_RADIUS || dist > RADIAL_RADIUS) return -1;

  double angle = atan2(dy, dx) + G_PI / 2;
  if (angle < 0) angle += 2 * G_PI;

  double sector = 2 * G_PI / self->n_items;
  return (gint)(angle / sector);
}

static gboolean on_motion(GtkEventControllerMotion *controller G_GNUC_UNUSED, double x, double y, gpointer data) {
  TangerineRadialMenu *self = data;
  gint idx = get_item_at_angle(self, RADIAL_RADIUS, RADIAL_RADIUS, x, y);
  if (idx != self->hovered_index) {
    self->hovered_index = idx;
    gtk_widget_queue_draw(self->drawing_area);
  }
  return FALSE;
}

static gboolean on_leave(GtkEventControllerMotion *controller G_GNUC_UNUSED, gpointer data) {
  TangerineRadialMenu *self = data;
  self->hovered_index = -1;
  gtk_widget_queue_draw(self->drawing_area);
  return FALSE;
}

static gboolean on_button_pressed(GtkGestureClick *gesture G_GNUC_UNUSED, gint n_press G_GNUC_UNUSED, double x, double y, gpointer data) {
  TangerineRadialMenu *self = data;
  gint idx = get_item_at_angle(self, RADIAL_RADIUS, RADIAL_RADIUS, x, y);
  if (idx >= 0) {
    self->pressed_index = idx;
    gtk_widget_queue_draw(self->drawing_area);
  }
  return TRUE;
}

static gboolean on_button_released(GtkGestureClick *gesture G_GNUC_UNUSED, gint n_press G_GNUC_UNUSED, double x G_GNUC_UNUSED, double y G_GNUC_UNUSED, gpointer data) {
  TangerineRadialMenu *self = data;
  if (self->pressed_index >= 0 && self->pressed_index == self->hovered_index) {
    TangerineRadialItem *item = &self->items[self->pressed_index];
    g_signal_emit_by_name(self, "item-activated", item->action_id);
    tangerine_radial_menu_hide(self);
  }
  self->pressed_index = -1;
  gtk_widget_queue_draw(self->drawing_area);
  return TRUE;
}

static void on_escape(GtkEventControllerKey *controller G_GNUC_UNUSED, guint keyval, guint keycode G_GNUC_UNUSED, GdkModifierType state G_GNUC_UNUSED, gpointer data) {
  if (keyval == GDK_KEY_Escape) {
    tangerine_radial_menu_hide(data);
  }
}

static void draw_sector(cairo_t *cr, double cx, double cy, double r1, double r2, double start, double end, gboolean hover, gboolean pressed) {
  cairo_new_sub_path(cr);
  cairo_arc(cr, cx, cy, r2, start, end);
  cairo_arc_negative(cr, cx, cy, r1, end, start);
  cairo_close_path(cr);

  if (pressed) {
    cairo_set_source_rgba(cr, 0.2, 0.5, 0.9, 0.4);
  } else if (hover) {
    cairo_set_source_rgba(cr, 0.3, 0.6, 0.95, 0.25);
  } else {
    cairo_set_source_rgba(cr, 0.2, 0.2, 0.25, 0.15);
  }
  cairo_fill_preserve(cr);

  cairo_set_source_rgba(cr, 0.4, 0.4, 0.5, 0.3);
  cairo_set_line_width(cr, 1);
  cairo_stroke(cr);
}

static void draw_icon_text(cairo_t *cr, double cx, double cy, double angle, const char *icon_name G_GNUC_UNUSED, const char *label, double progress) {
  double mid_angle = angle;
  double radius = (RADIAL_RADIUS + INNER_RADIUS) / 2;
  double icon_r = 24;

  double scale = progress < 0.5 ? progress * 2 : 1.0;
  if (scale < 0.01) return;

  cairo_save(cr);
  cairo_translate(cr, cx + cos(mid_angle) * radius * scale, cy + sin(mid_angle) * radius * scale);
  cairo_rotate(cr, mid_angle + G_PI / 2);

  cairo_set_source_rgba(cr, 0.15, 0.15, 0.2, 0.9 * progress);
  cairo_arc(cr, 0, 0, icon_r + 4, 0, 2 * G_PI);
  cairo_fill(cr);

  cairo_set_font_size(cr, 18);
  cairo_set_source_rgba(cr, 0.9, 0.9, 0.95, progress);
  cairo_text_extents_t extents;
  cairo_text_extents(cr, label, &extents);
  cairo_move_to(cr, -extents.width / 2, extents.height / 2);
  cairo_show_text(cr, label);

  cairo_restore(cr);
}

static void draw_content(GtkDrawingArea *area G_GNUC_UNUSED, cairo_t *cr, int width, int height, gpointer data) {
  TangerineRadialMenu *self = data;
  double cx = width / 2.0;
  double cy = height / 2.0;

  double eased = ease_out_cubic(self->progress);
  double radius = INNER_RADIUS + (RADIAL_RADIUS - INNER_RADIUS) * eased;

  cairo_set_source_rgba(cr, 0.1, 0.1, 0.12, 0.95 * eased);
  cairo_arc(cr, cx, cy, radius, 0, 2 * G_PI);
  cairo_fill(cr);

  cairo_set_source_rgba(cr, 0.3, 0.3, 0.35, eased);
  cairo_set_line_width(cr, 1.5);
  cairo_arc(cr, cx, cy, radius, 0, 2 * G_PI);
  cairo_stroke(cr);

  if (self->n_items == 0 || self->progress < 0.1) return;

  double sector = 2 * G_PI / self->n_items;

  for (guint i = 0; i < self->n_items; i++) {
    double start = i * sector - G_PI / 2;
    double end = start + sector;

    gboolean hover = (i == (guint)self->hovered_index);
    gboolean pressed = (i == (guint)self->pressed_index);

    draw_sector(cr, cx, cy, INNER_RADIUS, radius, start, end, hover, pressed);
    draw_icon_text(cr, cx, cy, start + sector / 2, self->items[i].icon_name, self->items[i].label, eased);
  }

  cairo_set_font_size(cr, 11);
  cairo_set_source_rgba(cr, 0.6, 0.6, 0.65, eased);
  cairo_text_extents_t ext;
  cairo_text_extents(cr, "ESC to cancel", &ext);
  cairo_move_to(cr, cx - ext.width / 2, cy + ext.height / 2);
  cairo_show_text(cr, "ESC to cancel");
}

void tangerine_radial_menu_show_at_cursor(TangerineRadialMenu *self) {
  GdkDisplay *display = gtk_widget_get_display(GTK_WIDGET(self));
  GdkSeat *seat = gdk_display_get_default_seat(display);
  GdkDevice *pointer = gdk_seat_get_pointer(seat);
  double x, y;
  GdkSurface *surface = gtk_native_get_surface(gtk_widget_get_native(GTK_WIDGET(self)));
  gdk_surface_get_device_position(surface, pointer, &x, &y, NULL);

  GtkWindow *window = GTK_WINDOW(self);
  gtk_window_set_transient_for(window, NULL);
  gtk_widget_set_halign(GTK_WIDGET(self), GTK_ALIGN_START);
  gtk_widget_set_valign(GTK_WIDGET(self), GTK_ALIGN_START);
  gtk_widget_set_margin_start(GTK_WIDGET(self), (gint)x - RADIAL_RADIUS);
  gtk_widget_set_margin_top(GTK_WIDGET(self), (gint)y - RADIAL_RADIUS);
  gtk_window_present(window);

  self->hovered_index = -1;
  self->pressed_index = -1;
  start_animation(self);
}

void tangerine_radial_menu_hide(TangerineRadialMenu *self) {
  gtk_window_close(GTK_WINDOW(self));
}

static void tangerine_radial_menu_class_init(TangerineRadialMenuClass *klass G_GNUC_UNUSED) {
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
  gtk_widget_class_set_css_name(widget_class, "tangerine-radial-menu");

  g_signal_new("item-activated",
    G_TYPE_FROM_CLASS(klass),
    G_SIGNAL_RUN_LAST,
    0, NULL, NULL,
    g_cclosure_marshal_generic,
    G_TYPE_NONE, 1, G_TYPE_STRING);
}

static void tangerine_radial_menu_init(TangerineRadialMenu *self) {
  gtk_window_set_decorated(GTK_WINDOW(self), FALSE);
  gtk_window_set_resizable(GTK_WINDOW(self), FALSE);
  gtk_window_set_default_size(GTK_WINDOW(self), 2 * RADIAL_RADIUS, 2 * RADIAL_RADIUS);

  self->drawing_area = gtk_drawing_area_new();
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(self->drawing_area), draw_content, self, NULL);
  gtk_widget_set_size_request(self->drawing_area, 2 * RADIAL_RADIUS, 2 * RADIAL_RADIUS);
  gtk_window_set_child(GTK_WINDOW(self), self->drawing_area);

  GtkEventController *motion = gtk_event_controller_motion_new();
  g_signal_connect(motion, "motion", G_CALLBACK(on_motion), self);
  g_signal_connect(motion, "leave", G_CALLBACK(on_leave), self);
  gtk_widget_add_controller(self->drawing_area, motion);

  GtkGesture *click = gtk_gesture_click_new();
  g_signal_connect(click, "pressed", G_CALLBACK(on_button_pressed), self);
  g_signal_connect(click, "released", G_CALLBACK(on_button_released), self);
  gtk_widget_add_controller(self->drawing_area, GTK_EVENT_CONTROLLER(click));

  GtkEventController *keys = gtk_event_controller_key_new();
  g_signal_connect(keys, "key-pressed", G_CALLBACK(on_escape), self);
  gtk_widget_add_controller(GTK_WIDGET(self), keys);

  GtkCssProvider *provider = gtk_css_provider_new();
  gtk_css_provider_load_from_string(provider,
    ".tangerine-radial-menu { background: transparent; }"
    "drawingarea { background: transparent; }"
  );
  gtk_style_context_add_provider_for_display(gtk_widget_get_display(GTK_WIDGET(self)),
    GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref(provider);
}

TangerineRadialMenu *tangerine_radial_menu_new(GtkWindow *parent) {
  return g_object_new(TANGERINE_TYPE_RADIAL_MENU, "transient-for", parent, NULL);
}

void tangerine_radial_menu_set_items(TangerineRadialMenu *self, TangerineRadialItem *items, guint n_items) {
  self->items = items;
  self->n_items = n_items;
  gtk_widget_queue_draw(self->drawing_area);
}

void tangerine_radial_menu_set_mode(TangerineRadialMenu *self, TangerineActionMode mode) {
  self->mode = mode;
}