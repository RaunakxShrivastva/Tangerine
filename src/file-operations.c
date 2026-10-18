#include "file-operations.h"
#include <string.h>

static gpointer ref_object(gconstpointer obj, gpointer data G_GNUC_UNUSED) {
  return g_object_ref((gpointer)obj);
}

typedef struct {
  GList *files;
  GFile *dest_dir;
  TangerineFileOpCompleteCallback cb;
  gpointer user_data;
  gboolean is_move;
  guint remaining;
  guint succeeded;
  guint failed;
} TransferContext;

static void transfer_context_free(TransferContext *ctx) {
  g_list_free_full(ctx->files, g_object_unref);
  if (ctx->dest_dir) g_object_unref(ctx->dest_dir);
  g_free(ctx);
}

static void transfer_maybe_finish(TransferContext *ctx) {
  if (ctx->remaining > 0) return;

  if (ctx->cb) {
    char *msg;
    if (ctx->failed == 0)
      msg = g_strdup_printf("%s %u file(s)", ctx->is_move ? "Moved" : "Copied", ctx->succeeded);
    else
      msg = g_strdup_printf("%u succeeded, %u failed", ctx->succeeded, ctx->failed);
    ctx->cb(ctx->failed == 0, msg, ctx->user_data);
    g_free(msg);
  }
  transfer_context_free(ctx);
}

static void on_file_copied(GObject *source, GAsyncResult *result, gpointer data) {
  TransferContext *ctx = data;
  GError *error = NULL;
  GFile *dest = G_FILE(source);

  if (!g_file_copy_finish(dest, result, &error)) {
    g_warning("Transfer failed: %s", error->message);
    g_error_free(error);
    ctx->failed++;
  } else {
    ctx->succeeded++;
    if (ctx->is_move) {
      GFile *src = g_object_get_data(G_OBJECT(dest), "tangerine-src");
      if (src) {
        GError *del_error = NULL;
        if (!g_file_delete(src, NULL, &del_error)) {
          if (del_error) g_error_free(del_error);
        }
      }
    }
  }

  ctx->remaining--;
  transfer_maybe_finish(ctx);
}

static void start_transfer(TransferContext *ctx) {
  if (!ctx->files) {
    if (ctx->cb) ctx->cb(TRUE, "Nothing to transfer", ctx->user_data);
    transfer_context_free(ctx);
    return;
  }

  ctx->remaining = g_list_length(ctx->files);

  for (GList *l = ctx->files; l; l = l->next) {
    GFile *src = l->data;
    GFile *dest = g_file_get_child(ctx->dest_dir, g_file_get_basename(src));
    g_object_set_data_full(G_OBJECT(dest), "tangerine-src", g_object_ref(src), g_object_unref);

    g_file_copy_async(src, dest, G_FILE_COPY_OVERWRITE, G_PRIORITY_DEFAULT,
      NULL, NULL, NULL, on_file_copied, ctx);
  }
}

static void on_dest_dir_chosen(GObject *source, GAsyncResult *result, gpointer data) {
  TransferContext *ctx = data;
  GError *error = NULL;
  GFile *dir = gtk_file_dialog_select_folder_finish(GTK_FILE_DIALOG(source), result, &error);

  if (!dir) {
    if (ctx->cb) ctx->cb(FALSE, "Cancelled", ctx->user_data);
    if (error) g_error_free(error);
    transfer_context_free(ctx);
    return;
  }

  ctx->dest_dir = dir;
  start_transfer(ctx);
}

static void begin_transfer(GList *files, GtkWindow *parent, TangerineFileOpCompleteCallback cb, gpointer user_data, gboolean is_move) {
  TransferContext *ctx = g_new0(TransferContext, 1);
  ctx->files = g_list_copy_deep(files, ref_object, NULL);
  ctx->cb = cb;
  ctx->user_data = user_data;
  ctx->is_move = is_move;

  GtkFileDialog *dialog = gtk_file_dialog_new();
  gtk_file_dialog_set_title(dialog, is_move ? "Move to folder" : "Copy to folder");
  gtk_file_dialog_select_folder(dialog, parent, NULL, on_dest_dir_chosen, ctx);
}

void tangerine_files_copy(GList *files, GtkWindow *parent, TangerineFileOpCompleteCallback cb, gpointer user_data) {
  begin_transfer(files, parent, cb, user_data, FALSE);
}

void tangerine_files_move(GList *files, GtkWindow *parent, TangerineFileOpCompleteCallback cb, gpointer user_data) {
  begin_transfer(files, parent, cb, user_data, TRUE);
}

void tangerine_files_copy_path(GList *files, TangerineFileOpCompleteCallback cb, gpointer user_data) {
  GString *paths = g_string_new(NULL);
  for (GList *l = files; l; l = l->next) {
    char *path = g_file_get_path(l->data);
    if (path) {
      if (paths->len > 0) g_string_append_c(paths, '\n');
      g_string_append(paths, path);
      g_free(path);
    }
  }

  GdkClipboard *clipboard = gdk_display_get_clipboard(gdk_display_get_default());
  gdk_clipboard_set_text(clipboard, paths->str);
  if (cb) cb(TRUE, "Path copied to clipboard", user_data);
  g_string_free(paths, TRUE);
}

void tangerine_files_open(GList *files, TangerineFileOpCompleteCallback cb, gpointer user_data) {
  guint opened = 0;
  for (GList *l = files; l; l = l->next) {
    char *uri = g_file_get_uri(l->data);
    GError *error = NULL;
    if (g_app_info_launch_default_for_uri(uri, NULL, &error)) {
      opened++;
    } else if (error) {
      g_warning("Failed to open %s: %s", uri, error->message);
      g_error_free(error);
    }
    g_free(uri);
  }
  if (cb) cb(opened > 0, "Opened file(s)", user_data);
}

void tangerine_files_open_terminal(GList *files, TangerineFileOpCompleteCallback cb, gpointer user_data) {
  if (!files) {
    if (cb) cb(FALSE, "No file selected", user_data);
    return;
  }

  GFile *file = files->data;
  GFile *parent = g_file_get_parent(file);
  char *dir = parent ? g_file_get_path(parent) : g_strdup(g_get_home_dir());
  if (parent) g_object_unref(parent);

  const char *terminals[] = {
    "xdg-terminal-exec --working-directory=%s",
    "ptyxis --working-directory=%s",
    "gnome-terminal --working-directory=%s",
    "konsole --workdir %s",
    "alacritty --working-directory %s",
    "kitty --directory %s",
    "xfce4-terminal --working-directory=%s",
    "foot -D %s",
    NULL
  };

  gboolean launched = FALSE;
  for (int i = 0; terminals[i] && !launched; i++) {
    char *cmd = g_strdup_printf(terminals[i], dir);
    int argc = 0;
    char **parsed = NULL;
    GError *error = NULL;
    if (g_shell_parse_argv(cmd, &argc, &parsed, NULL)) {
      if (g_spawn_async(NULL, parsed, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &error))
        launched = TRUE;
      g_strfreev(parsed);
    }
    if (error) g_error_free(error);
    g_free(cmd);
  }

  if (cb) cb(launched, launched ? "Terminal opened" : "No terminal found", user_data);
  g_free(dir);
}

void tangerine_files_open_with(GList *files, const char *app_id, TangerineFileOpCompleteCallback cb, gpointer user_data) {
  GString *cmd = g_string_new("gio launch ");
  g_string_append(cmd, app_id);
  for (GList *l = files; l; l = l->next) {
    char *path = g_file_get_path(l->data);
    if (path) {
      g_string_append_c(cmd, ' ');
      char *quoted = g_shell_quote(path);
      g_string_append(cmd, quoted);
      g_free(quoted);
      g_free(path);
    }
  }

  GError *error = NULL;
  gboolean ok = g_spawn_command_line_async(cmd->str, &error);
  if (error) {
    g_warning("open_with failed: %s", error->message);
    g_error_free(error);
  }
  g_string_free(cmd, TRUE);
  if (cb) cb(ok, ok ? "Opened" : "Failed to open", user_data);
}

void tangerine_files_trash(GList *files, TangerineFileOpCompleteCallback cb, gpointer user_data) {
  guint trashed = 0;
  for (GList *l = files; l; l = l->next) {
    GError *error = NULL;
    if (g_file_trash(l->data, NULL, &error)) {
      trashed++;
    } else if (error) {
      g_warning("Failed to trash: %s", error->message);
      g_error_free(error);
    }
  }
  if (cb) {
    char *msg = g_strdup_printf("Moved %u file(s) to trash", trashed);
    cb(trashed > 0, msg, user_data);
    g_free(msg);
  }
}