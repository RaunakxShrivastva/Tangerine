#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include "conversion-engine.h"

static GMainLoop *loop = NULL;
static gboolean finished = FALSE;
static gboolean success = FALSE;

typedef struct {
  GMainLoop *loop;
  guint remaining;
  gboolean all_ok;
} ConvCtx;

static void uniq_complete(TangerineConversionResult result, const char *output_path G_GNUC_UNUSED, const char *error G_GNUC_UNUSED, gpointer data) {
  ConvCtx *ctx = data;
  if (result != TANGERINE_CONVERSION_SUCCESS) ctx->all_ok = FALSE;
  ctx->remaining--;
  if (ctx->remaining == 0) g_main_loop_quit(ctx->loop);
}

static void on_complete(TangerineConversionResult result, const char *output_path, const char *error, gpointer data G_GNUC_UNUSED) {
  if (result == TANGERINE_CONVERSION_SUCCESS) {
    g_print("OK: converted -> %s\n", output_path);
    success = TRUE;
  } else {
    g_print("FAIL: %s\n", error ? error : "unknown error");
    success = FALSE;
  }
  finished = TRUE;
  g_main_loop_quit(loop);
}

static char *make_test_png(const char *dir) {
  GdkPixbuf *pb = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 32, 32);
  gdk_pixbuf_fill(pb, 0xff8800ff);
  char *path = g_build_filename(dir, "tangerine-test.png", NULL);
  GError *error = NULL;
  if (!gdk_pixbuf_save(pb, path, "png", &error, NULL)) {
    g_warning("failed to write test image: %s", error ? error->message : "?");
    if (error) g_error_free(error);
    g_free(path);
    path = NULL;
  }
  g_object_unref(pb);
  return path;
}

static gboolean run_conversion(const char *input, const char *format, const char *out_dir) {
  loop = g_main_loop_new(NULL, FALSE);
  finished = FALSE;
  success = FALSE;

  TangerineConversionEngine *engine = tangerine_conversion_engine_new();
  if (out_dir) tangerine_conversion_engine_set_output_dir(engine, out_dir);
  tangerine_conversion_engine_convert(engine, input, format, NULL, on_complete, NULL);
  g_main_loop_run(loop);

  gboolean ok = finished && success;
  if (ok) {
    char *dir = out_dir ? g_strdup(out_dir) : g_path_get_dirname(input);
    char *base = g_path_get_basename(input);
    char *dot = strrchr(base, '.');
    if (dot) *dot = '\0';
    char *out = g_strdup_printf("%s/%s_converted.%s", dir, base, format);
    ok = g_file_test(out, G_FILE_TEST_IS_REGULAR);
    if (ok) {
      GStatBuf st;
      if (g_stat(out, &st) == 0 && st.st_size > 0)
        g_print("  verified %s (%ld bytes)\n", out, (long)st.st_size);
      else
        ok = FALSE;
    } else {
      g_print("  expected output missing: %s\n", out);
    }
    g_free(out);
    g_free(dir);
    g_free(base);
  }

  g_main_loop_unref(loop);
  g_object_unref(engine);
  return ok;
}

int main(void) {
  gchar *dir = g_dir_make_tmp("tangerine-test-XXXXXX", NULL);
  g_assert_nonnull(dir);

  char *png = make_test_png(dir);
  g_assert_nonnull(png);

  gboolean all_ok = TRUE;

  g_print("[test] png -> jpg\n");
  all_ok &= run_conversion(png, "jpg", NULL);

  g_print("[test] png -> webp\n");
  all_ok &= run_conversion(png, "webp", NULL);

  g_print("[test] png -> jpeg\n");
  all_ok &= run_conversion(png, "jpeg", NULL);

  g_print("[test] png -> png into custom output dir\n");
  char *out_dir = g_build_filename(dir, "out", NULL);
  g_assert_cmpint(g_mkdir(out_dir, 0755), ==, 0);
  all_ok &= run_conversion(png, "jpg", out_dir);
  g_free(out_dir);

  g_print("[test] same input twice -> unique output names\n");
  char *base = g_path_get_basename(png);
  char *dot = strrchr(base, '.'); if (dot) *dot = '\0';
  char *expected1 = g_strdup_printf("%s/%s_converted.jpg", dir, base);
  char *expected2 = g_strdup_printf("%s/%s_converted_1.jpg", dir, base);

  ConvCtx ctx = { .loop = g_main_loop_new(NULL, FALSE), .remaining = 2, .all_ok = TRUE };
  TangerineConversionEngine *engine = tangerine_conversion_engine_new();
  tangerine_conversion_engine_convert(engine, png, "jpg", NULL, uniq_complete, &ctx);
  tangerine_conversion_engine_convert(engine, png, "jpg", NULL, uniq_complete, &ctx);
  g_main_loop_run(ctx.loop);
  g_main_loop_unref(ctx.loop);
  g_object_unref(engine);

  gboolean ok = ctx.all_ok &&
    g_file_test(expected1, G_FILE_TEST_IS_REGULAR) &&
    g_file_test(expected2, G_FILE_TEST_IS_REGULAR);
  g_print("  %s\n", ok ? "both unique outputs exist" : "MISSING output(s)");
  all_ok &= ok;
  g_free(expected1);
  g_free(expected2);
  g_free(base);

  GList *missing = tangerine_conversion_engine_missing_tools();
  for (GList *l = missing; l; l = l->next)
    g_print("  note: missing tool %s\n", (const char *)l->data);
  g_list_free(missing);

  g_free(png);
  g_rmdir(dir);
  g_free(dir);

  g_print(all_ok ? "ALL TESTS PASSED\n" : "SOME TESTS FAILED\n");
  return all_ok ? 0 : 1;
}