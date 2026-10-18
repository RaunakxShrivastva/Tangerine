#include <gtk/gtk.h>
#include <adwaita.h>
#include "radial-menu.h"
#include "drag-monitor.h"
#include "conversion-engine.h"
#include "file-operations.h"

static TangerineRadialMenu *radial_menu = NULL;
static TangerineDragMonitor *drag_monitor = NULL;
static TangerineConversionEngine *conversion_engine = NULL;
static GtkWindow *main_window = NULL;
static AdwApplication *app_instance = NULL;

static GList *dropped_files = NULL;
static guint conversions_pending = 0;
static guint conversions_ok = 0;
static guint conversions_failed = 0;

static void clear_dropped_files(void) {
  g_list_free_full(dropped_files, g_object_unref);
  dropped_files = NULL;
}

static void notify(const char *title, const char *body) {
  if (!app_instance) return;
  GNotification *n = g_notification_new(title);
  g_notification_set_body(n, body);
  g_application_send_notification(G_APPLICATION(app_instance), "tangerine-result", n);
  g_object_unref(n);
}

static TangerineActionMode mode_from_modifiers(GdkModifierType state) {
  if (state & GDK_CONTROL_MASK) return TANGERINE_ACTION_FILE_OPS;
  if (state & GDK_SHIFT_MASK) return TANGERINE_ACTION_TOOLS;
  return TANGERINE_ACTION_CONVERT;
}

static void on_conversion_progress(double progress, const char *status, gpointer data G_GNUC_UNUSED) {
  g_print("Progress: %.0f%% - %s\n", progress * 100, status);
}

static void conversion_maybe_finish(void) {
  if (conversions_pending > 0) return;

  if (conversions_failed == 0) {
    char *msg = g_strdup_printf("Converted %u file(s) successfully", conversions_ok);
    notify("Tangerine", msg);
    g_print("Done: %s\n", msg);
    g_free(msg);
  } else {
    char *msg = g_strdup_printf("%u succeeded, %u failed", conversions_ok, conversions_failed);
    notify("Tangerine", msg);
    g_print("Done: %s\n", msg);
    g_free(msg);
  }
}

static void on_conversion_complete(TangerineConversionResult result, const char *output_path, const char *error, gpointer data G_GNUC_UNUSED) {
  if (result == TANGERINE_CONVERSION_SUCCESS) {
    conversions_ok++;
    g_print("Conversion complete: %s\n", output_path);
  } else {
    conversions_failed++;
    g_print("Conversion failed: %s\n", error ? error : "Unknown error");
  }

  if (conversions_pending > 0) conversions_pending--;
  conversion_maybe_finish();
}

static void on_file_op_done(gboolean success, const char *message, gpointer user_data G_GNUC_UNUSED) {
  g_print("File op: %s (%s)\n", message, success ? "ok" : "failed");
  notify("Tangerine", message);
}

static const char *output_format_for_action(const char *action_id) {
  if (g_strcmp0(action_id, "convert-png") == 0) return "png";
  if (g_strcmp0(action_id, "convert-jpg") == 0) return "jpg";
  if (g_strcmp0(action_id, "convert-webp") == 0) return "webp";
  if (g_strcmp0(action_id, "extract-audio") == 0) return "mp3";
  if (g_strcmp0(action_id, "redact") == 0) return "pdf";
  if (g_strcmp0(action_id, "merge") == 0) return "pdf";
  return NULL;
}

static void execute_action(const char *action_id) {
  if (!dropped_files) {
    notify("Tangerine", "No files to act on");
    return;
  }

  if (g_strcmp0(action_id, "copy") == 0) {
    tangerine_files_copy(dropped_files, main_window, on_file_op_done, NULL);
    return;
  }
  if (g_strcmp0(action_id, "move") == 0) {
    tangerine_files_move(dropped_files, main_window, on_file_op_done, NULL);
    return;
  }
  if (g_strcmp0(action_id, "copy-path") == 0) {
    tangerine_files_copy_path(dropped_files, on_file_op_done, NULL);
    return;
  }
  if (g_strcmp0(action_id, "terminal") == 0) {
    tangerine_files_open_terminal(dropped_files, on_file_op_done, NULL);
    return;
  }
  if (g_strcmp0(action_id, "trash") == 0) {
    tangerine_files_trash(dropped_files, on_file_op_done, NULL);
    return;
  }
  if (g_strcmp0(action_id, "open") == 0) {
    tangerine_files_open(dropped_files, on_file_op_done, NULL);
    return;
  }

  const char *fixed_format = output_format_for_action(action_id);
  conversions_pending = 0;
  conversions_ok = 0;
  conversions_failed = 0;

  for (GList *l = dropped_files; l; l = l->next) {
    char *path = g_file_get_path(l->data);
    if (!path) continue;

    const char *format = fixed_format;
    char *ext = NULL;
    if (!format) {
      const char *dot = strrchr(path, '.');
      if (dot) {
        ext = g_strdup(dot + 1);
        format = ext;
      }
    }

    if (format) {
      conversions_pending++;
      tangerine_conversion_engine_convert(conversion_engine, path, format,
        on_conversion_progress, on_conversion_complete, NULL);
    }
    g_free(ext);
    g_free(path);
  }

  if (conversions_pending == 0) notify("Tangerine", "Nothing to convert");
}

static void on_radial_item_activated(TangerineRadialMenu *menu G_GNUC_UNUSED, const char *action_id, gpointer data G_GNUC_UNUSED) {
  execute_action(action_id);
}

static void show_menu(TangerineActionMode mode) {
  static TangerineRadialItem convert_items[] = {
    { "Convert to PNG", "image-x-generic", "convert-png", NULL },
    { "Convert to JPG", "image-x-generic", "convert-jpg", NULL },
    { "Convert to WebP", "image-x-generic", "convert-webp", NULL },
    { "Extract Audio", "audio-x-generic", "extract-audio", NULL },
    { "Compress", "package-x-generic", "compress", NULL },
    { "Open", "document-open", "open", NULL },
  };
  static TangerineRadialItem tools_items[] = {
    { "Strip Metadata", "document-properties", "strip-meta", NULL },
    { "Crop", "image-crop", "crop", NULL },
    { "Trim Video", "media-seek-backward", "trim", NULL },
    { "Split PDF", "edit-cut", "split", NULL },
    { "Redact PDF", "document-edit", "redact", NULL },
    { "Open", "document-open", "open", NULL },
  };
  static TangerineRadialItem fileops_items[] = {
    { "Copy", "edit-copy", "copy", NULL },
    { "Move", "edit-cut", "move", NULL },
    { "Copy Path", "edit-copy", "copy-path", NULL },
    { "Open", "document-open", "open", NULL },
    { "Terminal", "utilities-terminal", "terminal", NULL },
    { "Trash", "user-trash", "trash", NULL },
  };

  TangerineRadialItem *items = NULL;
  guint n_items = 0;

  switch (mode) {
    case TANGERINE_ACTION_CONVERT: items = convert_items; n_items = 6; break;
    case TANGERINE_ACTION_TOOLS:   items = tools_items;   n_items = 6; break;
    case TANGERINE_ACTION_FILE_OPS: items = fileops_items; n_items = 6; break;
  }

  if (radial_menu) {
    tangerine_radial_menu_set_items(radial_menu, items, n_items);
    tangerine_radial_menu_set_mode(radial_menu, mode);
    tangerine_radial_menu_show_at_cursor(radial_menu);
  }
}

static gboolean on_drop(GtkDropTarget *target, const GValue *value, double x G_GNUC_UNUSED, double y G_GNUC_UNUSED, gpointer data G_GNUC_UNUSED) {
  GdkFileList *file_list = g_value_get_boxed(value);
  if (!file_list) return FALSE;

  GSList *files = gdk_file_list_get_files(file_list);
  if (!files) return FALSE;

  clear_dropped_files();
  for (GSList *l = files; l; l = l->next)
    dropped_files = g_list_prepend(dropped_files, g_object_ref(l->data));
  dropped_files = g_list_reverse(dropped_files);

  GdkModifierType state = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(target));
  TangerineActionMode mode = mode_from_modifiers(state);

  g_print("Dropped %u file(s), modifiers=0x%x, mode=%d\n",
    g_list_length(dropped_files), state, mode);

  show_menu(mode);
  return TRUE;
}

static void on_drag_action(TangerineActionMode mode, GdkDrag *drag G_GNUC_UNUSED, gpointer data G_GNUC_UNUSED) {
  show_menu(mode);
}

static gboolean on_key_pressed(GtkEventControllerKey *self G_GNUC_UNUSED, guint keyval, guint keycode G_GNUC_UNUSED, GdkModifierType state, gpointer data G_GNUC_UNUSED) {
  if (keyval == GDK_KEY_v && (state & GDK_SUPER_MASK) && (state & GDK_SHIFT_MASK)) {
    show_menu(TANGERINE_ACTION_CONVERT);
    return TRUE;
  }
  return FALSE;
}

static void on_activate(GtkApplication *app) {
  if (main_window) {
    gtk_window_present(main_window);
    return;
  }

  GtkWidget *window = gtk_application_window_new(app);
  main_window = GTK_WINDOW(window);
  gtk_window_set_title(GTK_WINDOW(window), "Tangerine");
  gtk_window_set_default_size(GTK_WINDOW(window), 360, 260);

  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
  gtk_widget_set_halign(box, GTK_ALIGN_CENTER);
  gtk_widget_set_margin_top(box, 24);
  gtk_widget_set_margin_bottom(box, 24);
  gtk_widget_set_margin_start(box, 24);
  gtk_widget_set_margin_end(box, 24);

  GtkWidget *icon = gtk_image_new_from_icon_name("folder-download-symbolic");
  gtk_image_set_pixel_size(GTK_IMAGE(icon), 64);
  gtk_box_append(GTK_BOX(box), icon);

  GtkWidget *label = gtk_label_new(NULL);
  gtk_label_set_markup(GTK_LABEL(label),
    "<span size='large' weight='bold'>Drop files here</span>\n"
    "<small>No modifier: convert  ·  Shift: tools  ·  Ctrl: file ops</small>");
  gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);
  gtk_box_append(GTK_BOX(box), label);

  gtk_window_set_child(GTK_WINDOW(window), box);

  GtkDropTarget *target = gtk_drop_target_new(GDK_TYPE_FILE_LIST, GDK_ACTION_COPY);
  g_signal_connect(target, "drop", G_CALLBACK(on_drop), NULL);
  gtk_widget_add_controller(window, GTK_EVENT_CONTROLLER(target));

  radial_menu = tangerine_radial_menu_new(GTK_WINDOW(window));
  g_signal_connect(radial_menu, "item-activated", G_CALLBACK(on_radial_item_activated), NULL);

  conversion_engine = tangerine_conversion_engine_new();
  const char *fmt_path = "src/formats.yaml";
  if (!g_file_test(fmt_path, G_FILE_TEST_EXISTS)) fmt_path = "/app/share/tangerine/formats.yaml";
  tangerine_conversion_engine_load_formats(conversion_engine, fmt_path);

  GList *missing = tangerine_conversion_engine_missing_tools();
  if (missing) {
    GString *names = g_string_new(NULL);
    for (GList *l = missing; l; l = l->next) {
      if (names->len > 0) g_string_append(names, ", ");
      g_string_append(names, (const char *)l->data);
    }
    g_warning("Missing conversion tools: %s", names->str);
    char *body = g_strdup_printf("Missing tools: %s. Some conversions may fail.", names->str);
    notify("Tangerine", body);
    g_free(body);
    g_string_free(names, TRUE);
    g_list_free(missing);
  }

  GdkDisplay *display = gdk_display_get_default();
  drag_monitor = tangerine_drag_monitor_new(display);
  tangerine_drag_monitor_set_callback(drag_monitor, on_drag_action, NULL);

  GtkEventController *keys = gtk_event_controller_key_new();
  g_signal_connect(keys, "key-pressed", G_CALLBACK(on_key_pressed), NULL);
  gtk_widget_add_controller(window, keys);

  gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char **argv) {
  g_autoptr(AdwApplication) app = adw_application_new("com.github.yourname.Tangerine", G_APPLICATION_DEFAULT_FLAGS);
  app_instance = app;
  g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
  return g_application_run(G_APPLICATION(app), argc, argv);
}