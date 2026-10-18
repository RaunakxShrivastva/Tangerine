#pragma once

#include <glib.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define TANGERINE_TYPE_CONVERSION_ENGINE (tangerine_conversion_engine_get_type())
G_DECLARE_FINAL_TYPE(TangerineConversionEngine, tangerine_conversion_engine, TANGERINE, CONVERSION_ENGINE, GObject)

typedef enum {
  TANGERINE_CONVERSION_SUCCESS,
  TANGERINE_CONVERSION_ERROR_INPUT,
  TANGERINE_CONVERSION_ERROR_OUTPUT,
  TANGERINE_CONVERSION_ERROR_PIPELINE,
  TANGERINE_CONVERSION_ERROR_CANCELLED,
} TangerineConversionResult;

typedef void (*TangerineConversionProgressCallback)(double progress, const char *status, gpointer user_data);
typedef void (*TangerineConversionCompleteCallback)(TangerineConversionResult result, const char *output_path, const char *error, gpointer user_data);

typedef struct {
  const char *input_mime;
  const char *output_mime;
  const char *pipeline_name;
  const char *description;
} TangerineFormatMapping;

TangerineConversionEngine *tangerine_conversion_engine_new(void);
void tangerine_conversion_engine_load_formats(TangerineConversionEngine *self, const char *yaml_path);
void tangerine_conversion_engine_set_output_dir(TangerineConversionEngine *self, const char *dir);
void tangerine_conversion_engine_convert(TangerineConversionEngine *self,
  const char *input_path,
  const char *output_format,
  TangerineConversionProgressCallback progress_cb,
  TangerineConversionCompleteCallback complete_cb,
  gpointer user_data);
void tangerine_conversion_engine_convert_to(TangerineConversionEngine *self,
  const char *input_path,
  const char *output_path,
  const char *pipeline_name,
  TangerineConversionProgressCallback progress_cb,
  TangerineConversionCompleteCallback complete_cb,
  gpointer user_data);
const char *tangerine_conversion_engine_find_pipeline(TangerineConversionEngine *self,
  const char *input_path, const char *output_format);
void tangerine_conversion_engine_cancel(TangerineConversionEngine *self, const char *input_path);

GList *tangerine_conversion_engine_missing_tools(void);

G_END_DECLS