#pragma once

#include <gio/gio.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef void (*TangerineFileOpCompleteCallback)(gboolean success, const char *message, gpointer user_data);

void tangerine_files_copy(GList *files, GtkWindow *parent, TangerineFileOpCompleteCallback cb, gpointer user_data);
void tangerine_files_move(GList *files, GtkWindow *parent, TangerineFileOpCompleteCallback cb, gpointer user_data);
void tangerine_files_trash(GList *files, TangerineFileOpCompleteCallback cb, gpointer user_data);
void tangerine_files_copy_path(GList *files, TangerineFileOpCompleteCallback cb, gpointer user_data);
void tangerine_files_open(GList *files, TangerineFileOpCompleteCallback cb, gpointer user_data);
void tangerine_files_open_terminal(GList *files, TangerineFileOpCompleteCallback cb, gpointer user_data);
void tangerine_files_open_with(GList *files, const char *app_id, TangerineFileOpCompleteCallback cb, gpointer user_data);

G_END_DECLS