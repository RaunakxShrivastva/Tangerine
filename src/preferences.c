#include "preferences.h"

typedef struct {
  GSettings *settings;
  GtkWindow *parent;
  AdwActionRow *folder_row;
} PrefsContext;

static void on_about_activated(AdwActionRow *row G_GNUC_UNUSED, gpointer data);

static void prefs_context_free(gpointer data) {
  PrefsContext *ctx = data;
  g_object_unref(ctx->settings);
  g_free(ctx);
}

static void on_folder_chosen(GObject *source, GAsyncResult *result, gpointer data) {
  PrefsContext *ctx = data;
  GError *error = NULL;
  GFile *folder = gtk_file_dialog_select_folder_finish(GTK_FILE_DIALOG(source), result, &error);

  if (folder) {
    char *path = g_file_get_path(folder);
    if (path) {
      g_settings_set_string(ctx->settings, "output-directory", path);
      adw_action_row_set_subtitle(ctx->folder_row, path);
      g_free(path);
    }
    g_object_unref(folder);
  }
  if (error) g_error_free(error);
}

static void on_choose_folder(GtkButton *button G_GNUC_UNUSED, gpointer data) {
  PrefsContext *ctx = data;
  GtkFileDialog *dialog = gtk_file_dialog_new();
  gtk_file_dialog_set_title(dialog, "Choose output folder");
  gtk_file_dialog_select_folder(dialog, ctx->parent, NULL, on_folder_chosen, ctx);
}

static void on_quality_selected(GObject *row, GParamSpec *pspec G_GNUC_UNUSED, gpointer data G_GNUC_UNUSED) {
  GSettings *settings = g_object_get_data(G_OBJECT(row), "tangerine-prefs-settings");
  const char *key = g_object_get_data(G_OBJECT(row), "tangerine-prefs-key");
  static const char *values[] = { "low", "medium", "high", "lossless" };
  guint selected = adw_combo_row_get_selected(ADW_COMBO_ROW(row));
  if (key && selected < G_N_ELEMENTS(values))
    g_settings_set_string(settings, key, values[selected]);
}

static AdwComboRow *create_quality_row(GSettings *settings, const char *title, const char *key) {
  AdwComboRow *row = ADW_COMBO_ROW(adw_combo_row_new());
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
  const char *quality_items[] = { "Low", "Medium", "High", "Lossless", NULL };
  adw_combo_row_set_model(row, G_LIST_MODEL(gtk_string_list_new(quality_items)));

  char *quality = g_settings_get_string(settings, key);
  const char *values[] = { "low", "medium", "high", "lossless" };
  for (guint i = 0; i < G_N_ELEMENTS(values); i++) {
    if (g_strcmp0(quality, values[i]) == 0) {
      adw_combo_row_set_selected(row, i);
      break;
    }
  }
  g_free(quality);

  g_object_set_data_full(G_OBJECT(row), "tangerine-prefs-settings", g_object_ref(settings), g_object_unref);
  g_object_set_data_full(G_OBJECT(row), "tangerine-prefs-key", g_strdup(key), g_free);
  g_signal_connect(row, "notify::selected", G_CALLBACK(on_quality_selected), NULL);

  return row;
}

static void on_beside_changed(GObject *row, GParamSpec *pspec G_GNUC_UNUSED, gpointer data) {
  PrefsContext *ctx = data;
  gboolean beside = adw_switch_row_get_active(ADW_SWITCH_ROW(row));
  g_settings_set_boolean(ctx->settings, "output-beside-source", beside);
  gtk_widget_set_sensitive(GTK_WIDGET(ctx->folder_row), !beside);
}

static void on_about_activated(AdwActionRow *row G_GNUC_UNUSED, gpointer data) {
  GtkWindow *parent = GTK_WINDOW(data);
  AdwAboutDialog *about = ADW_ABOUT_DIALOG(adw_about_dialog_new());
  adw_about_dialog_set_application_name(about, "Tangerine");
  adw_about_dialog_set_application_icon(about, "com.github.yourname.Tangerine");
  adw_about_dialog_set_version(about, "0.10.0");
  adw_about_dialog_set_developer_name(about, "RaunakxShrivastva");
  adw_about_dialog_set_support_url(about, "https://github.com/RaunakxShrivastva/Tangerine/issues");
  adw_about_dialog_set_issue_url(about, "https://github.com/RaunakxShrivastva/Tangerine/issues");
  adw_about_dialog_set_copyright(about, "© 2026 RaunakxShrivastva");
  adw_about_dialog_set_license_type(about, GTK_LICENSE_MIT_X11);
  adw_about_dialog_set_website(about, "https://github.com/RaunakxShrivastva/Tangerine");
  adw_about_dialog_set_comments(about,
    "Zero-click offline file converter for Linux.\n"
    "Drag files onto the window, pick a format — no uploads, all local.");
  adw_dialog_present(ADW_DIALOG(about), GTK_WIDGET(parent));
  g_object_unref(about);
}

GtkWidget *tangerine_preferences_new(GSettings *settings, GtkWindow *parent) {
  PrefsContext *ctx = g_new0(PrefsContext, 1);
  ctx->settings = g_object_ref(settings);
  ctx->parent = parent;

  AdwPreferencesPage *page = ADW_PREFERENCES_PAGE(adw_preferences_page_new());

  AdwPreferencesGroup *output_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
  adw_preferences_group_set_title(output_group, "Output");
  adw_preferences_group_set_description(output_group, "Where converted files are saved");

  gboolean beside = g_settings_get_boolean(settings, "output-beside-source");

  AdwSwitchRow *beside_row = ADW_SWITCH_ROW(adw_switch_row_new());
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(beside_row), "Save next to original file");
  adw_switch_row_set_active(beside_row, beside);
  g_signal_connect(beside_row, "notify::active", G_CALLBACK(on_beside_changed), ctx);
  adw_preferences_group_add(output_group, GTK_WIDGET(beside_row));

  AdwActionRow *folder_row = ADW_ACTION_ROW(adw_action_row_new());
  ctx->folder_row = folder_row;
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(folder_row), "Output folder");
  char *current_dir = g_settings_get_string(settings, "output-directory");
  adw_action_row_set_subtitle(folder_row, (current_dir && *current_dir) ? current_dir : "Not set");
  g_free(current_dir);

  GtkWidget *choose_button = gtk_button_new_with_label("Choose…");
  gtk_widget_set_valign(choose_button, GTK_ALIGN_CENTER);
  adw_action_row_add_suffix(folder_row, choose_button);
  gtk_widget_set_sensitive(GTK_WIDGET(folder_row), !beside);
  g_signal_connect(choose_button, "clicked", G_CALLBACK(on_choose_folder), ctx);
  adw_preferences_group_add(output_group, GTK_WIDGET(folder_row));

  adw_preferences_page_add(page, output_group);

  AdwPreferencesGroup *conv_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
  adw_preferences_group_set_title(conv_group, "Conversion");

  adw_preferences_group_add(conv_group,
    GTK_WIDGET(create_quality_row(settings, "Image Quality", "image-quality")));
  adw_preferences_group_add(conv_group,
    GTK_WIDGET(create_quality_row(settings, "Video Quality", "video-quality")));
  adw_preferences_group_add(conv_group,
    GTK_WIDGET(create_quality_row(settings, "Audio Quality", "audio-quality")));

  AdwSwitchRow *notify_row = ADW_SWITCH_ROW(adw_switch_row_new());
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(notify_row), "Show notifications");
  adw_switch_row_set_active(notify_row, g_settings_get_boolean(settings, "show-notifications"));
  g_settings_bind(settings, "show-notifications", notify_row, "active", G_SETTINGS_BIND_DEFAULT);
  adw_preferences_group_add(conv_group, GTK_WIDGET(notify_row));

  adw_preferences_page_add(page, conv_group);

  AdwPreferencesGroup *about_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
  adw_preferences_group_set_title(about_group, "About");

  AdwActionRow *about_row = ADW_ACTION_ROW(adw_action_row_new());
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(about_row), "About Tangerine");
  adw_action_row_set_subtitle(about_row, "Zero-click offline file converter");
  adw_preferences_group_add(about_group, GTK_WIDGET(about_row));
  g_signal_connect(about_row, "activated", G_CALLBACK(on_about_activated), parent);

  adw_preferences_page_add(page, about_group);

  AdwPreferencesDialog *dialog = ADW_PREFERENCES_DIALOG(adw_preferences_dialog_new());
  adw_preferences_dialog_add(dialog, page);

  GtkWidget *widget = GTK_WIDGET(dialog);
  g_object_set_data_full(G_OBJECT(widget), "tangerine-prefs-ctx", ctx, prefs_context_free);

  return widget;
}