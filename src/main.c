#include <gtk/gtk.h>
#include <adwaita.h>
#include "radial-menu.h"
#include "drag-monitor.h"
#include "conversion-engine.h"

static TangerineRadialMenu *radial_menu = NULL;
static TangerineDragMonitor *drag_monitor = NULL;
static TangerineConversionEngine *conversion_engine = NULL;
static GdkDrag *current_drag = NULL;

static void on_conversion_progress(double progress, const char *status, gpointer data G_GNUC_UNUSED) {
  g_print("Progress: %.0f%% - %s\n", progress * 100, status);
}

static void on_conversion_complete(TangerineConversionResult result, const char *output_path, const char *error, gpointer data G_GNUC_UNUSED) {
  if (result == TANGERINE_CONVERSION_SUCCESS) {
    g_print("Conversion complete: %s\n", output_path);
    AdwDialog *dialog = adw_alert_dialog_new("Conversion complete!", output_path);
    adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(dialog), "close", ADW_RESPONSE_SUGGESTED);
    gtk_window_present(GTK_WINDOW(dialog));
  } else {
    g_print("Conversion failed: %s\n", error ? error : "Unknown error");
    AdwDialog *dialog = adw_alert_dialog_new("Conversion failed", error ? error : "Unknown error");
    gtk_window_present(GTK_WINDOW(dialog));
  }
}

static const char *get_pipeline_for_action(const char *action_id, const char *path G_GNUC_UNUSED) {
  if (g_strcmp0(action_id, "convert-png") == 0) return "default";
  if (g_strcmp0(action_id, "convert-jpg") == 0) return "default";
  if (g_strcmp0(action_id, "convert-webp") == 0) return "default";
  if (g_strcmp0(action_id, "extract-audio") == 0) return "video-extract-audio-mp3";
  if (g_strcmp0(action_id, "compress") == 0) return "compress-image";
  if (g_strcmp0(action_id, "crop") == 0) return "crop-image";
  if (g_strcmp0(action_id, "trim") == 0) return "trim-video";
  if (g_strcmp0(action_id, "redact") == 0) return "default";
  if (g_strcmp0(action_id, "strip-meta") == 0) return "strip-metadata";
  if (g_strcmp0(action_id, "split") == 0) return "split-pdf";
  if (g_strcmp0(action_id, "merge") == 0) return "merge-pdf";
  if (g_strcmp0(action_id, "annotate") == 0) return "default";
  return NULL;
}

static const char *get_output_format(const char *action_id, const char *path) {
  const char *ext = strrchr(path, '.');
  if (!ext) return NULL;
  ext++;

  if (g_strcmp0(action_id, "convert-png") == 0) return "png";
  if (g_strcmp0(action_id, "convert-jpg") == 0) return "jpg";
  if (g_strcmp0(action_id, "convert-webp") == 0) return "webp";
  if (g_strcmp0(action_id, "extract-audio") == 0) return "mp3";
  if (g_strcmp0(action_id, "compress") == 0) return ext;
  if (g_strcmp0(action_id, "crop") == 0) return ext;
  if (g_strcmp0(action_id, "trim") == 0) return ext;
  if (g_strcmp0(action_id, "redact") == 0) return "pdf";
  if (g_strcmp0(action_id, "strip-meta") == 0) return ext;
  if (g_strcmp0(action_id, "split") == 0) return ext;
  if (g_strcmp0(action_id, "merge") == 0) return "pdf";
  if (g_strcmp0(action_id, "annotate") == 0) return ext;
  return NULL;
}

static void execute_action(const char *action_id, GdkDrag *drag) {
  if (!conversion_engine || !drag) return;

  GdkContentProvider *provider = gdk_drag_get_content(drag);
  if (!provider) return;

  const char *output_format = get_output_format(action_id, "test");
  const char *pipeline = get_pipeline_for_action(action_id, "test");

  g_print("Execute action: %s (format: %s, pipeline: %s)\n", action_id, output_format ? output_format : "none", pipeline ? pipeline : "none");

  // For testing, we'll use a dummy path
  if (output_format && pipeline) {
    tangerine_conversion_engine_convert(conversion_engine, "/tmp/test.txt", output_format,
      on_conversion_progress, on_conversion_complete, NULL);
  }
}

static void on_radial_item_activated(TangerineRadialMenu *menu G_GNUC_UNUSED, const char *action_id, gpointer data G_GNUC_UNUSED) {
  execute_action(action_id, current_drag);
}

static void on_drag_action(TangerineActionMode mode, GdkDrag *drag, gpointer data G_GNUC_UNUSED) {
  current_drag = drag;

  static TangerineRadialItem convert_items[] = {
    { "Convert to PNG", "image-x-generic", "convert-png", NULL },
    { "Convert to JPG", "image-x-generic", "convert-jpg", NULL },
    { "Convert to WebP", "image-x-generic", "convert-webp", NULL },
    { "Extract Audio", "audio-x-generic", "extract-audio", NULL },
    { "Compress", "package-x-generic", "compress", NULL },
    { "Crop", "image-crop", "crop", NULL },
  };
  static TangerineRadialItem tools_items[] = {
    { "Trim Video", "media-seek-backward", "trim", NULL },
    { "Redact PDF", "document-edit", "redact", NULL },
    { "Strip Metadata", "document-properties", "strip-meta", NULL },
    { "Split", "edit-cut", "split", NULL },
    { "Merge", "edit-paste", "merge", NULL },
    { "Annotate", "accessories-text-editor", "annotate", NULL },
  };
  static TangerineRadialItem fileops_items[] = {
    { "Copy", "edit-copy", "copy", NULL },
    { "Move", "edit-cut", "move", NULL },
    { "Copy Path", "edit-copy", "copy-path", NULL },
    { "Open Terminal", "utilities-terminal", "terminal", NULL },
  };

  TangerineRadialItem *items = NULL;
  guint n_items = 0;

  switch (mode) {
    case TANGERINE_ACTION_CONVERT:
      items = convert_items; n_items = 6; break;
    case TANGERINE_ACTION_TOOLS:
      items = tools_items; n_items = 6; break;
    case TANGERINE_ACTION_FILE_OPS:
      items = fileops_items; n_items = 4; break;
  }

  if (radial_menu) {
    tangerine_radial_menu_set_items(radial_menu, items, n_items);
    tangerine_radial_menu_set_mode(radial_menu, mode);
    tangerine_radial_menu_show_at_cursor(radial_menu);
  }
}

static gboolean on_key_pressed(GtkEventControllerKey *self G_GNUC_UNUSED, guint keyval, guint keycode G_GNUC_UNUSED, GdkModifierType state, gpointer data G_GNUC_UNUSED) {
  if (keyval == GDK_KEY_v && (state & GDK_SUPER_MASK) && (state & GDK_SHIFT_MASK)) {
    on_drag_action(TANGERINE_ACTION_CONVERT, NULL, data);
    return TRUE;
  }
  return FALSE;
}

static void on_activate(GtkApplication *app) {
  GtkWidget *window = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(window), "Tangerine");
  gtk_window_set_default_size(GTK_WINDOW(window), 400, 300);
  gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
  gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
  gtk_widget_set_visible(window, FALSE);

  radial_menu = tangerine_radial_menu_new(GTK_WINDOW(window));
  g_signal_connect(radial_menu, "item-activated", G_CALLBACK(on_radial_item_activated), NULL);

  conversion_engine = tangerine_conversion_engine_new();
  tangerine_conversion_engine_load_formats(conversion_engine, "src/formats.yaml");

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
  g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
  return g_application_run(G_APPLICATION(app), argc, argv);
}