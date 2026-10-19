#include <gtk/gtk.h>
#include <adwaita.h>
#include "radial-menu.h"
#include "drag-monitor.h"
#include "conversion-engine.h"
#include "file-operations.h"
#include "preferences.h"

#define TANGERINE_APP_ID "com.github.yourname.Tangerine"
#define TANGERINE_VERSION "0.5.0"

static TangerineRadialMenu *radial_menu = NULL;
static TangerineDragMonitor *drag_monitor = NULL;
static TangerineConversionEngine *conversion_engine = NULL;
static GtkWindow *main_window = NULL;
static AdwApplication *app_instance = NULL;
static GSettings *settings = NULL;
static GdkModifierType modifier_convert_mask = GDK_SHIFT_MASK;
static GdkModifierType modifier_tools_mask = GDK_SUPER_MASK;
static GdkModifierType modifier_fileops_mask = GDK_CONTROL_MASK;
static gboolean ui_ready = FALSE;

static AdwToastOverlay *toast_overlay = NULL;
static GtkWidget *status_label = NULL;
static GtkWidget *spinner = NULL;
static GtkPopover *recent_popover = NULL;
static GtkWidget *cancel_button = NULL;

static GList *dropped_files = NULL;
static GtkListBox *file_queue = NULL;
static guint conversions_pending = 0;
static guint conversions_ok = 0;
static guint conversions_failed = 0;
static gboolean held_application = FALSE;

static char *cli_action = NULL;
static char *cli_convert_to = NULL;
static gboolean cli_preferences = FALSE;
static gboolean cli_show_version = FALSE;
static GList *cli_files = NULL;

static void clear_dropped_files(void) {
  g_list_free_full(dropped_files, g_object_unref);
  dropped_files = NULL;
}

static void on_remove_file(GtkButton *button G_GNUC_UNUSED, gpointer data);

static void rebuild_file_queue(void) {
  gtk_list_box_remove_all(file_queue);
  if (!dropped_files) {
    gtk_widget_set_visible(GTK_WIDGET(file_queue), FALSE);
    return;
  }
  gtk_widget_set_visible(GTK_WIDGET(file_queue), TRUE);

  for (GList *l = dropped_files; l; l = l->next) {
    GFile *f = l->data;
    char *name = g_file_get_basename(f);

    GtkWidget *row = gtk_list_box_row_new();
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_top(hbox, 4);
    gtk_widget_set_margin_bottom(hbox, 4);
    gtk_widget_set_margin_start(hbox, 8);
    gtk_widget_set_margin_end(hbox, 8);

    GtkWidget *lbl = gtk_label_new(name);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0);
    gtk_label_set_ellipsize(GTK_LABEL(lbl), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_set_hexpand(lbl, TRUE);
    gtk_box_append(GTK_BOX(hbox), lbl);

    GtkWidget *status = gtk_label_new("queued");
    gtk_label_set_xalign(GTK_LABEL(status), 1);
    gtk_widget_add_css_class(status, "dim-label");
    gtk_box_append(GTK_BOX(hbox), status);
    g_object_set_data_full(G_OBJECT(row), "tangerine-status-label", g_object_ref(status), g_object_unref);

    GtkWidget *remove = gtk_button_new_from_icon_name("edit-delete-symbolic");
    gtk_button_set_has_frame(GTK_BUTTON(remove), FALSE);
    gtk_widget_set_tooltip_text(remove, "Remove from queue");
    g_object_set_data_full(G_OBJECT(row), "tangerine-file", g_object_ref(f), g_object_unref);
    g_signal_connect(remove, "clicked", G_CALLBACK(on_remove_file), row);
    gtk_box_append(GTK_BOX(hbox), remove);

    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), hbox);
    gtk_list_box_append(file_queue, row);
    g_free(name);
  }
}

static void on_remove_file(GtkButton *button G_GNUC_UNUSED, gpointer data) {
  GtkWidget *row = GTK_WIDGET(data);
  GFile *f = g_object_get_data(G_OBJECT(row), "tangerine-file");
  if (f) {
    dropped_files = g_list_remove(dropped_files, f);
    g_object_unref(f);
  }
  rebuild_file_queue();
}

static void set_queue_status(const char *status) {
  if (!dropped_files || !file_queue) return;
  guint i = 0;
  for (GList *l = dropped_files; l; l = l->next, i++) {
    GtkListBoxRow *row = gtk_list_box_get_row_at_index(file_queue, i);
    if (!row) continue;
    GtkWidget *lbl = g_object_get_data(G_OBJECT(row), "tangerine-status-label");
    if (lbl) gtk_label_set_label(GTK_LABEL(lbl), status);
  }
}

static void notify(const char *title, const char *body) {
  if (!app_instance) {
    g_print("%s: %s\n", title, body);
    return;
  }
  if (settings && !g_settings_get_boolean(settings, "show-notifications")) {
    g_print("%s: %s\n", title, body);
    return;
  }
  GNotification *n = g_notification_new(title);
  g_notification_set_body(n, body);
  g_application_send_notification(G_APPLICATION(app_instance), "tangerine-result", n);
  g_object_unref(n);
}

static void add_recent_conversion(const char *input_path, const char *output_path) {
  if (!settings || !input_path || !output_path) return;

  char *entry = g_strdup_printf("%s|%s", input_path, output_path);

  GStrv current = g_settings_get_strv(settings, "recent-conversions");
  GList *list = NULL;
  for (int i = 0; current[i]; i++)
    list = g_list_append(list, g_strdup(current[i]));
  g_strfreev(current);

  GList *l = list;
  while (l) {
    if (g_strcmp0(l->data, entry) == 0) {
      list = g_list_delete_link(list, l);
      g_free(l->data);
      break;
    }
    l = l->next;
  }

  list = g_list_prepend(list, entry);

  while (g_list_length(list) > 10) {
    GList *last = g_list_last(list);
    list = g_list_delete_link(list, last);
    g_free(last->data);
  }

  guint n = g_list_length(list);
  char **arr = g_new(char *, n + 1);
  guint i = 0;
  for (l = list; l; l = l->next) {
    arr[i++] = g_strdup(l->data);
  }
  arr[n] = NULL;

  g_settings_set_strv(settings, "recent-conversions", (const char * const *)arr);

  g_strfreev(arr);
  g_list_free_full(list, g_free);
}

static void show_toast(const char *message) {
  if (!toast_overlay) return;
  AdwToast *toast = adw_toast_new(message);
  adw_toast_set_timeout(toast, 3000);
  adw_toast_overlay_add_toast(toast_overlay, toast);
  g_object_unref(toast);
}

static GdkModifierType modifier_mask_from_string(const char *s) {
  if (!s) return 0;
  if (g_str_equal(s, "shift")) return GDK_SHIFT_MASK;
  if (g_str_equal(s, "ctrl") || g_str_equal(s, "control")) return GDK_CONTROL_MASK;
  if (g_str_equal(s, "super") || g_str_equal(s, "meta")) return GDK_SUPER_MASK;
  return 0;
}

static void load_modifier_settings(void) {
  if (!settings) return;
  modifier_convert_mask = modifier_mask_from_string(
    g_settings_get_string(settings, "modifier-convert"));
  modifier_tools_mask = modifier_mask_from_string(
    g_settings_get_string(settings, "modifier-tools"));
  modifier_fileops_mask = modifier_mask_from_string(
    g_settings_get_string(settings, "modifier-fileops"));

  if (conversion_engine) {
    tangerine_conversion_engine_set_image_quality(
      conversion_engine, g_settings_get_string(settings, "image-quality"));
    tangerine_conversion_engine_set_video_quality(
      conversion_engine, g_settings_get_string(settings, "video-quality"));
    tangerine_conversion_engine_set_audio_quality(
      conversion_engine, g_settings_get_string(settings, "audio-quality"));
  }
}

static void on_settings_changed(GSettings *settings G_GNUC_UNUSED, const char *key, gpointer data G_GNUC_UNUSED) {
  if (g_str_has_prefix(key, "modifier-") ||
      g_str_has_prefix(key, "image-quality") ||
      g_str_has_prefix(key, "video-quality") ||
      g_str_has_prefix(key, "audio-quality") ||
      g_str_equal(key, "show-notifications") ||
      g_str_equal(key, "output-beside-source") ||
      g_str_equal(key, "output-directory") ||
      g_str_equal(key, "default-output-format")) {
    load_modifier_settings();
  }
}

static TangerineActionMode mode_from_modifiers(GdkModifierType state) {
  if (state & modifier_fileops_mask) return TANGERINE_ACTION_FILE_OPS;
  if (state & modifier_tools_mask) return TANGERINE_ACTION_TOOLS;
  if (state & modifier_convert_mask) return TANGERINE_ACTION_CONVERT;
  return TANGERINE_ACTION_CONVERT;
}

static void apply_output_settings(void) {
  if (!conversion_engine || !settings) return;
  if (g_settings_get_boolean(settings, "output-beside-source")) {
    tangerine_conversion_engine_set_output_dir(conversion_engine, NULL);
  } else {
    char *dir = g_settings_get_string(settings, "output-directory");
    tangerine_conversion_engine_set_output_dir(conversion_engine, dir);
    g_free(dir);
  }
}

static void set_converting(gboolean converting) {
  if (spinner) {
    if (converting) {
      gtk_spinner_start(GTK_SPINNER(spinner));
      gtk_widget_set_visible(spinner, TRUE);
    } else {
      gtk_spinner_stop(GTK_SPINNER(spinner));
      gtk_widget_set_visible(spinner, FALSE);
    }
  }
  if (cancel_button) gtk_widget_set_visible(cancel_button, converting);
  if (dropped_files) set_queue_status(converting ? "converting" : "done");
  if (status_label) {
    if (converting) {
      gtk_label_set_label(GTK_LABEL(status_label), "Converting…");
    } else {
      gtk_label_set_markup(GTK_LABEL(status_label),
        "<span size='large' weight='bold'>Drop files here</span>\n"
        "<small>No modifier: convert  ·  Shift: tools  ·  Ctrl: file ops</small>");
    }
  }
}

static void release_if_held(void) {
  if (held_application) {
    held_application = FALSE;
    g_application_release(G_APPLICATION(app_instance));
  }
}

static void on_conversion_progress(double progress, const char *status, gpointer data G_GNUC_UNUSED) {
  g_print("Progress: %.0f%% - %s\n", progress * 100, status);
}

static void conversion_maybe_finish(void) {
  if (conversions_pending > 0) return;

  set_converting(FALSE);

  char *msg;
  gboolean any_done = (conversions_ok > 0) || (conversions_failed > 0);
  if (!any_done) {
    msg = g_strdup("Conversion cancelled");
  } else if (conversions_failed == 0) {
    msg = g_strdup_printf("Converted %u file(s) successfully", conversions_ok);
  } else {
    msg = g_strdup_printf("%u succeeded, %u failed", conversions_ok, conversions_failed);
  }

  notify("Tangerine", msg);
  show_toast(msg);
  g_print("Done: %s\n", msg);
  g_free(msg);

  release_if_held();
}

static void on_conversion_complete(TangerineConversionResult result, const char *output_path, const char *error, gpointer user_data) {
  const char *input_path = (const char *)user_data;
  if (result == TANGERINE_CONVERSION_SUCCESS) {
    conversions_ok++;
    g_print("Conversion complete: %s\n", output_path);
    add_recent_conversion(input_path, output_path);
  } else if (result == TANGERINE_CONVERSION_ERROR_CANCELLED) {
    g_print("Conversion cancelled: %s\n", input_path ? input_path : "");
  } else {
    conversions_failed++;
    char *msg = g_strdup_printf("Conversion failed: %s", error ? error : "Unknown error");
    g_print("%s\n", msg);
    show_toast(msg);
    g_free(msg);
  }
  g_free((gpointer)input_path);

  if (conversions_pending > 0) conversions_pending--;
  conversion_maybe_finish();
}

static void on_file_op_done(gboolean success, const char *message, gpointer user_data G_GNUC_UNUSED) {
  g_print("File op: %s (%s)\n", message, success ? "ok" : "failed");
  notify("Tangerine", message);
  show_toast(message);
  release_if_held();
}

static void open_file_uri(const char *uri) {
  GError *error = NULL;
  if (!g_app_info_launch_default_for_uri(uri, NULL, &error)) {
    g_warning("open file failed: %s", error->message);
    g_error_free(error);
    notify("Tangerine", "Failed to open file");
    show_toast("Failed to open file");
  }
}

static void on_recent_open_file(GtkButton *button G_GNUC_UNUSED, gpointer data) {
  GtkWidget *row = GTK_WIDGET(data);
  GFile *f = g_object_get_data(G_OBJECT(row), "tangerine-recent-output");
  if (f) {
    char *uri = g_file_get_uri(f);
    if (uri) {
      open_file_uri(uri);
      g_free(uri);
    }
    if (recent_popover) gtk_popover_popdown(recent_popover);
    g_object_unref(f);
  }
}

static void on_recent_open_folder(GtkButton *button G_GNUC_UNUSED, gpointer data) {
  GtkWidget *row = GTK_WIDGET(data);
  GFile *f = g_object_get_data(G_OBJECT(row), "tangerine-recent-output");
  if (f) {
    GFile *parent = g_file_get_parent(f);
    char *uri = g_file_get_uri(parent);
    if (uri) {
      open_file_uri(uri);
      g_free(uri);
    }
    g_object_unref(parent);
    if (recent_popover) gtk_popover_popdown(recent_popover);
    g_object_unref(f);
  }
}

static void rebuild_recent_popover(void) {
  if (!settings) return;
  gtk_list_box_remove_all(GTK_LIST_BOX(gtk_popover_get_child(recent_popover)));

  GStrv recent = g_settings_get_strv(settings, "recent-conversions");
  if (!recent || !recent[0]) {
    g_strfreev(recent);
    GtkWidget *empty = gtk_label_new("No recent conversions");
    gtk_widget_set_hexpand(empty, TRUE);
    gtk_widget_set_vexpand(empty, TRUE);
    gtk_widget_set_halign(empty, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(empty, GTK_ALIGN_CENTER);
    gtk_list_box_append(GTK_LIST_BOX(gtk_popover_get_child(recent_popover)), empty);
    return;
  }

  for (int i = 0; recent[i]; i++) {
    char *pipe = strchr(recent[i], '|');
    if (!pipe) continue;
    *pipe = '\0';
    const char *input = recent[i];
    const char *output = pipe + 1;

    GFile *in_f = g_file_new_for_path(input);
    GFile *out_f = g_file_new_for_path(output);
    char *in_name = g_file_get_basename(in_f);
    char *out_name = g_file_get_basename(out_f);

    GtkWidget *row = gtk_list_box_row_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_top(box, 6);
    gtk_widget_set_margin_bottom(box, 6);
    gtk_widget_set_margin_start(box, 8);
    gtk_widget_set_margin_end(box, 8);

    GtkWidget *lbl = gtk_label_new(NULL);
    char *markup = g_strdup_printf("<b>%s</b> → <i>%s</i>", in_name, out_name);
    gtk_label_set_markup(GTK_LABEL(lbl), markup);
    g_free(markup);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0);
    gtk_label_set_ellipsize(GTK_LABEL(lbl), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_set_hexpand(lbl, TRUE);
    gtk_box_append(GTK_BOX(box), lbl);

    g_object_set_data_full(G_OBJECT(row), "tangerine-recent-input", g_object_ref(in_f), g_object_unref);
    g_object_set_data_full(G_OBJECT(row), "tangerine-recent-output", g_object_ref(out_f), g_object_unref);

    GtkWidget *open_file_btn = gtk_button_new_from_icon_name("document-open-symbolic");
    gtk_button_set_has_frame(GTK_BUTTON(open_file_btn), FALSE);
    gtk_widget_set_tooltip_text(open_file_btn, "Open file");
    g_signal_connect(open_file_btn, "clicked", G_CALLBACK(on_recent_open_file), row);
    gtk_box_append(GTK_BOX(box), open_file_btn);

    GtkWidget *open_folder_btn = gtk_button_new_from_icon_name("folder-open-symbolic");
    gtk_button_set_has_frame(GTK_BUTTON(open_folder_btn), FALSE);
    gtk_widget_set_tooltip_text(open_folder_btn, "Open output folder");
    g_signal_connect(open_folder_btn, "clicked", G_CALLBACK(on_recent_open_folder), row);
    gtk_box_append(GTK_BOX(box), open_folder_btn);

    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
    gtk_list_box_append(GTK_LIST_BOX(gtk_popover_get_child(recent_popover)), row);

    g_object_unref(in_f);
    g_object_unref(out_f);
    g_free(in_name);
    g_free(out_name);
    *pipe = '|';
  }
  g_strfreev(recent);
}

static void on_recent_clicked(GtkButton *button G_GNUC_UNUSED, gpointer data G_GNUC_UNUSED) {
  if (!recent_popover) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_top(box, 6);
    gtk_widget_set_margin_bottom(box, 6);
    gtk_widget_set_margin_start(box, 6);
    gtk_widget_set_margin_end(box, 6);

    GtkWidget *list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
    gtk_widget_set_hexpand(list, TRUE);
    gtk_box_append(GTK_BOX(box), list);

  recent_popover = GTK_POPOVER(gtk_popover_new());
  gtk_popover_set_child(recent_popover, box);
  gtk_popover_set_has_arrow(recent_popover, TRUE);
}

  rebuild_recent_popover();
  gtk_widget_set_parent(GTK_WIDGET(recent_popover), GTK_WIDGET(button));
  gtk_popover_popup(recent_popover);
}

static void on_cancel_clicked(GtkButton *button G_GNUC_UNUSED, gpointer data G_GNUC_UNUSED) {
  if (!conversion_engine) return;
  tangerine_conversion_engine_cancel(conversion_engine, NULL);
  set_converting(FALSE);
  notify("Tangerine", "Conversion cancelled");
  show_toast("Conversion cancelled");
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

static void convert_one(const char *path, const char *format) {
  conversions_pending++;
  set_converting(TRUE);
  tangerine_conversion_engine_convert(conversion_engine, path, format,
    on_conversion_progress, on_conversion_complete, g_strdup(path));
}

static void execute_action(const char *action_id) {
  if (!dropped_files) {
    notify("Tangerine", "No files to act on");
    release_if_held();
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

  apply_output_settings();

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
      convert_one(path, format);
    }
    g_free(ext);
    g_free(path);
  }

  if (conversions_pending == 0) {
    notify("Tangerine", "Nothing to convert");
    show_toast("Nothing to convert");
    release_if_held();
  }
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

static void on_preferences(GtkButton *button G_GNUC_UNUSED, gpointer data) {
  GtkApplication *app = data;
  GtkWindow *window = gtk_application_get_active_window(app);
  GtkWidget *prefs = tangerine_preferences_new(settings, window);
  adw_dialog_present(ADW_DIALOG(prefs), window ? GTK_WIDGET(window) : GTK_WIDGET(main_window));
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
  rebuild_file_queue();

  GdkModifierType state = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(target));

  g_print("Dropped %u file(s), modifiers=0x%x\n", g_list_length(dropped_files), state);

  gboolean direct_convert = !(state & modifier_convert_mask) &&
    !(state & modifier_tools_mask) && !(state & modifier_fileops_mask);

  if (direct_convert && dropped_files) {
    TangerineActionMode mode = mode_from_modifiers(state);
    if (mode == TANGERINE_ACTION_CONVERT) {
      g_application_hold(G_APPLICATION(app_instance));
      held_application = TRUE;
      apply_output_settings();
      conversions_pending = 0;
      conversions_ok = 0;
      conversions_failed = 0;

      gboolean any = FALSE;
      for (GList *l = dropped_files; l; l = l->next) {
        char *path = g_file_get_path(l->data);
        if (!path) continue;
        char *fmt = tangerine_conversion_engine_default_output_format(conversion_engine, path);
        if (fmt && *fmt) {
          convert_one(path, fmt);
          any = TRUE;
        }
        g_free(fmt);
        g_free(path);
      }

      g_print("Auto-converting %u file(s) with smart formats\n", conversions_pending);
      if (!any) {
        notify("Tangerine", "Nothing to convert");
        show_toast("Nothing to convert");
        release_if_held();
      }
      return TRUE;
    }
  }

  show_menu(mode_from_modifiers(state));
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

static void ensure_ui(GtkApplication *app, gboolean present) {
  if (ui_ready) {
    if (present && main_window) gtk_window_present(main_window);
    return;
  }
  ui_ready = TRUE;

  GtkWidget *window = gtk_application_window_new(app);
  main_window = GTK_WINDOW(window);
  gtk_window_set_title(GTK_WINDOW(window), "Tangerine");
  gtk_window_set_default_size(GTK_WINDOW(window), 380, 300);

  AdwToolbarView *view = ADW_TOOLBAR_VIEW(adw_toolbar_view_new());

  AdwHeaderBar *header = ADW_HEADER_BAR(adw_header_bar_new());
  GtkWidget *prefs_button = gtk_button_new_from_icon_name("emblem-system-symbolic");
  gtk_widget_set_tooltip_text(prefs_button, "Preferences");
  g_signal_connect(prefs_button, "clicked", G_CALLBACK(on_preferences), app);

  GtkWidget *recent_button = gtk_button_new_from_icon_name("document-open-recent-symbolic");
  gtk_widget_set_tooltip_text(recent_button, "Recent conversions");
  g_signal_connect(recent_button, "clicked", G_CALLBACK(on_recent_clicked), NULL);

  cancel_button = gtk_button_new_from_icon_name("process-stop-symbolic");
  gtk_widget_set_tooltip_text(cancel_button, "Cancel conversions");
  gtk_widget_set_visible(cancel_button, FALSE);
  g_signal_connect(cancel_button, "clicked", G_CALLBACK(on_cancel_clicked), NULL);
  adw_header_bar_pack_end(header, prefs_button);
  adw_header_bar_pack_end(header, recent_button);
  adw_header_bar_pack_end(header, cancel_button);
  adw_toolbar_view_add_top_bar(view, GTK_WIDGET(header));

  GtkWidget *inner = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_valign(inner, GTK_ALIGN_CENTER);
  gtk_widget_set_halign(inner, GTK_ALIGN_CENTER);
  gtk_widget_set_margin_top(inner, 24);
  gtk_widget_set_margin_bottom(inner, 24);
  gtk_widget_set_margin_start(inner, 24);
  gtk_widget_set_margin_end(inner, 24);

  GtkWidget *icon = gtk_image_new_from_icon_name("folder-download-symbolic");
  gtk_image_set_pixel_size(GTK_IMAGE(icon), 64);
  gtk_box_append(GTK_BOX(inner), icon);

  spinner = gtk_spinner_new();
  gtk_widget_set_visible(spinner, FALSE);
  gtk_box_append(GTK_BOX(inner), spinner);

  status_label = gtk_label_new("Drop files here");
  gtk_label_set_justify(GTK_LABEL(status_label), GTK_JUSTIFY_CENTER);
  gtk_box_append(GTK_BOX(inner), status_label);

  file_queue = GTK_LIST_BOX(gtk_list_box_new());
  gtk_list_box_set_selection_mode(file_queue, GTK_SELECTION_NONE);
  gtk_widget_set_hexpand(GTK_WIDGET(file_queue), TRUE);
  gtk_widget_set_vexpand(GTK_WIDGET(file_queue), FALSE);
  gtk_widget_set_margin_top(GTK_WIDGET(file_queue), 12);
  gtk_widget_set_visible(GTK_WIDGET(file_queue), FALSE);
  gtk_box_append(GTK_BOX(inner), GTK_WIDGET(file_queue));

  adw_toolbar_view_set_content(view, inner);

  toast_overlay = ADW_TOAST_OVERLAY(adw_toast_overlay_new());
  adw_toast_overlay_set_child(toast_overlay, GTK_WIDGET(view));
  gtk_window_set_child(GTK_WINDOW(window), GTK_WIDGET(toast_overlay));

  GtkDropTarget *target = gtk_drop_target_new(GDK_TYPE_FILE_LIST, GDK_ACTION_COPY);
  g_signal_connect(target, "drop", G_CALLBACK(on_drop), NULL);
  gtk_widget_add_controller(window, GTK_EVENT_CONTROLLER(target));

  radial_menu = tangerine_radial_menu_new(GTK_WINDOW(window));
  g_signal_connect(radial_menu, "item-activated", G_CALLBACK(on_radial_item_activated), NULL);

  conversion_engine = tangerine_conversion_engine_new();
  const char *fmt_path = "src/formats.yaml";
  if (!g_file_test(fmt_path, G_FILE_TEST_EXISTS)) fmt_path = "/app/share/tangerine/formats.yaml";
  tangerine_conversion_engine_load_formats(conversion_engine, fmt_path);
  load_modifier_settings();

  GdkDisplay *display = gdk_display_get_default();
  drag_monitor = tangerine_drag_monitor_new(display);
  tangerine_drag_monitor_set_callback(drag_monitor, on_drag_action, NULL);

  GtkEventController *keys = gtk_event_controller_key_new();
  g_signal_connect(keys, "key-pressed", G_CALLBACK(on_key_pressed), NULL);
  gtk_widget_add_controller(window, keys);

  if (present) gtk_window_present(GTK_WINDOW(window));
}

static void on_activate(GtkApplication *app) {
  ensure_ui(app, TRUE);
}

static gboolean on_command_line(GApplication *application, GApplicationCommandLine *cmdline, gpointer data G_GNUC_UNUSED) {
  GVariantDict *opts = g_application_command_line_get_options_dict(cmdline);
  const char *value = NULL;
  gboolean flag = FALSE;

  if (g_variant_dict_lookup(opts, "convert-to", "&s", &value)) {
    g_free(cli_convert_to);
    cli_convert_to = g_strdup(value);
  }
  if (g_variant_dict_lookup(opts, "action", "&s", &value)) {
    g_free(cli_action);
    cli_action = g_strdup(value);
  }
  if (g_variant_dict_lookup(opts, "preferences", "b", &flag)) cli_preferences = flag;
  if (g_variant_dict_lookup(opts, "version", "b", &flag)) cli_show_version = flag;

  int argc = 0;
  char **argv = g_application_command_line_get_arguments(cmdline, &argc);
  for (int i = 1; i < argc; i++)
    cli_files = g_list_append(cli_files, g_application_command_line_create_file_for_arg(cmdline, argv[i]));
  g_strfreev(argv);

  if (cli_show_version) {
    g_application_command_line_print(cmdline, "tangerine %s\n", TANGERINE_VERSION);
    return 0;
  }

  gboolean direct = (cli_convert_to != NULL) || (cli_action != NULL);
  ensure_ui(GTK_APPLICATION(application), !direct);

  if (cli_preferences) {
    GtkWindow *window = gtk_application_get_active_window(GTK_APPLICATION(application));
    GtkWidget *prefs = tangerine_preferences_new(settings, window);
    adw_dialog_present(ADW_DIALOG(prefs), window ? GTK_WIDGET(window) : GTK_WIDGET(main_window));
    return 0;
  }

  if (cli_files) {
    clear_dropped_files();
    for (GList *l = cli_files; l; l = l->next)
      dropped_files = g_list_append(dropped_files, g_object_ref(l->data));
  }

  if (cli_convert_to) {
    char *action = g_strdup_printf("convert-%s", cli_convert_to);
    g_application_hold(application);
    held_application = TRUE;
    g_print("Converting %u file(s) to %s\n", g_list_length(dropped_files), cli_convert_to);
    execute_action(action);
    g_free(action);
    return 0;
  }

  if (cli_action) {
    g_application_hold(application);
    held_application = TRUE;
    execute_action(cli_action);
    return 0;
  }

  if (dropped_files) show_menu(TANGERINE_ACTION_CONVERT);
  return 0;
}

int main(int argc, char **argv) {
  g_autoptr(AdwApplication) app = adw_application_new(TANGERINE_APP_ID, G_APPLICATION_HANDLES_COMMAND_LINE);
  app_instance = app;

  settings = g_settings_new(TANGERINE_APP_ID);
  g_object_set_data_full(G_OBJECT(app), "tangerine-settings", g_object_ref(settings), g_object_unref);
  load_modifier_settings();
  g_signal_connect(settings, "changed", G_CALLBACK(on_settings_changed), NULL);

  g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
  g_signal_connect(app, "command-line", G_CALLBACK(on_command_line), NULL);

  static const GOptionEntry app_options[] = {
    { "convert-to", 'c', 0, G_OPTION_ARG_STRING, NULL, "Convert the given files to FORMAT and exit", "FORMAT" },
    { "action", 'a', 0, G_OPTION_ARG_STRING, NULL, "Run ACTION on the given files and exit", "ACTION" },
    { "preferences", 'p', 0, G_OPTION_ARG_NONE, NULL, "Open the preferences window", NULL },
    { "version", 'v', 0, G_OPTION_ARG_NONE, NULL, "Print version and exit", NULL },
    { NULL }
  };
  g_application_add_main_option_entries(G_APPLICATION(app), app_options);

  int status = g_application_run(G_APPLICATION(app), argc, argv);

  g_clear_object(&settings);
  g_list_free_full(cli_files, g_object_unref);
  g_free(cli_action);
  g_free(cli_convert_to);

  return status;
}