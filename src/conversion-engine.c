#include "conversion-engine.h"
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <string.h>

struct _TangerineConversionEngine {
  GObject parent;
  GHashTable *format_map;
  GThreadPool *thread_pool;
  GCancellable *global_cancellable;
};

typedef struct {
  char *input_path;
  char *output_path;
  char *pipeline_name;
  TangerineConversionProgressCallback progress_cb;
  TangerineConversionCompleteCallback complete_cb;
  gpointer user_data;
  GCancellable *cancellable;
} ConversionJob;

G_DEFINE_TYPE(TangerineConversionEngine, tangerine_conversion_engine, G_TYPE_OBJECT)

static void conversion_job_free(ConversionJob *job) {
  g_free(job->input_path);
  g_free(job->output_path);
  g_free(job->pipeline_name);
  g_object_unref(job->cancellable);
  g_free(job);
}

static void update_progress(ConversionJob *job, double progress, const char *status) {
  if (job->progress_cb) job->progress_cb(progress, status, job->user_data);
}

static gboolean run_command_sync(const char *command, char **stdout_out, char **stderr_out, int *exit_status, GCancellable *cancellable G_GNUC_UNUSED) {
  GError *error = NULL;
  gboolean res = g_spawn_command_line_sync(command, stdout_out, stderr_out, exit_status, &error);
  if (!res && error) {
    g_warning("Command failed: %s", error->message);
    g_error_free(error);
  }
  return res;
}

static void run_pipeline_image(ConversionJob *job) {
  const char *input = job->input_path;
  const char *output = job->output_path;
  const char *pipeline = job->pipeline_name;

  update_progress(job, 0.1, "Initializing image conversion...");

  char *cmd = NULL;

  if (g_strcmp0(pipeline, "heic-to-jpeg") == 0 || g_strcmp0(pipeline, "heic-to-png") == 0) {
    update_progress(job, 0.2, "Reading HEIC...");
    cmd = g_strdup_printf("heif-convert -q 90 \"%s\" \"%s\"", input, output);
  }
  else if (g_strcmp0(pipeline, "avif-to-jpeg") == 0 || g_strcmp0(pipeline, "avif-to-png") == 0) {
    update_progress(job, 0.2, "Reading AVIF...");
    cmd = g_strdup_printf("avifdec \"%s\" \"%s\"", input, output);
  }
  else if (g_strcmp0(pipeline, "jpeg-to-webp") == 0) {
    cmd = g_strdup_printf("vips copy \"%s\" \"%s[Q=85]\"", input, output);
  }
  else if (g_strcmp0(pipeline, "png-to-jpeg") == 0) {
    cmd = g_strdup_printf("vips copy \"%s\" \"%s[Q=90]\"", input, output);
  }
  else if (g_strcmp0(pipeline, "png-to-webp") == 0) {
    cmd = g_strdup_printf("vips copy \"%s\" \"%s[Q=85]\"", input, output);
  }
  else if (g_strcmp0(pipeline, "compress-image") == 0) {
    update_progress(job, 0.2, "Compressing image...");
    cmd = g_strdup_printf("vips copy \"%s\" \"%s[Q=75,strip]\"", input, output);
  }
  else if (g_strcmp0(pipeline, "crop-image") == 0) {
    update_progress(job, 0.2, "Cropping image...");
    cmd = g_strdup_printf("vips crop \"%s\" \"%s\" 100 100 800 600", input, output);
  }
  else if (g_strcmp0(pipeline, "strip-metadata") == 0) {
    update_progress(job, 0.2, "Stripping metadata...");
    cmd = g_strdup_printf("vips copy \"%s\" \"%s[strip]\"", input, output);
  }
  else {
    update_progress(job, 0.2, "Converting with vips...");
    cmd = g_strdup_printf("vips copy \"%s\" \"%s\"", input, output);
  }

  update_progress(job, 0.5, "Processing...");
  int exit_status;
  char *stdout_out = NULL, *stderr_out = NULL;
  gboolean success = run_command_sync(cmd, &stdout_out, &stderr_out, &exit_status, job->cancellable);
  g_free(cmd);
  g_free(stdout_out);
  g_free(stderr_out);

  if (g_cancellable_is_cancelled(job->cancellable)) {
    job->complete_cb(TANGERINE_CONVERSION_ERROR_CANCELLED, NULL, "Cancelled", job->user_data);
  } else if (success && exit_status == 0) {
    update_progress(job, 1.0, "Complete");
    job->complete_cb(TANGERINE_CONVERSION_SUCCESS, job->output_path, NULL, job->user_data);
  } else {
    job->complete_cb(TANGERINE_CONVERSION_ERROR_PIPELINE, NULL, "Image conversion failed", job->user_data);
  }
}

static void run_pipeline_video(ConversionJob *job) {
  const char *input = job->input_path;
  const char *output = job->output_path;
  const char *pipeline = job->pipeline_name;

  update_progress(job, 0.1, "Initializing video conversion...");

  char *cmd = NULL;

  if (g_strcmp0(pipeline, "mp4-to-mkv") == 0) {
    cmd = g_strdup_printf("ffmpeg -y -i \"%s\" -c copy \"%s\"", input, output);
  }
  else if (g_strcmp0(pipeline, "mkv-to-mp4") == 0) {
    cmd = g_strdup_printf("ffmpeg -y -i \"%s\" -c copy -movflags +faststart \"%s\"", input, output);
  }
  else if (g_strcmp0(pipeline, "video-extract-audio-mp3") == 0) {
    cmd = g_strdup_printf("ffmpeg -y -i \"%s\" -vn -acodec libmp3lame -q:a 2 \"%s\"", input, output);
  }
  else if (g_strcmp0(pipeline, "video-to-gif") == 0) {
    cmd = g_strdup_printf("ffmpeg -y -i \"%s\" -vf \"fps=10,scale=480:-1:flags=lanczos\" -c:v gif \"%s\"", input, output);
  }
  else if (g_strcmp0(pipeline, "trim-video") == 0) {
    cmd = g_strdup_printf("ffmpeg -y -i \"%s\" -ss 00:00:10 -t 00:00:30 -c copy \"%s\"", input, output);
  }
  else if (g_strcmp0(pipeline, "compress-video") == 0) {
    cmd = g_strdup_printf("ffmpeg -y -i \"%s\" -vcodec libx265 -crf 28 \"%s\"", input, output);
  }
  else {
    cmd = g_strdup_printf("ffmpeg -y -i \"%s\" \"%s\"", input, output);
  }

  update_progress(job, 0.3, "Processing video...");
  int exit_status;
  char *stdout_out = NULL, *stderr_out = NULL;
  gboolean success = run_command_sync(cmd, &stdout_out, &stderr_out, &exit_status, job->cancellable);
  g_free(cmd);
  g_free(stdout_out);
  g_free(stderr_out);

  if (g_cancellable_is_cancelled(job->cancellable)) {
    job->complete_cb(TANGERINE_CONVERSION_ERROR_CANCELLED, NULL, "Cancelled", job->user_data);
  } else if (success && exit_status == 0) {
    update_progress(job, 1.0, "Complete");
    job->complete_cb(TANGERINE_CONVERSION_SUCCESS, job->output_path, NULL, job->user_data);
  } else {
    job->complete_cb(TANGERINE_CONVERSION_ERROR_PIPELINE, NULL, "Video conversion failed", job->user_data);
  }
}

static void run_pipeline_audio(ConversionJob *job) {
  const char *input = job->input_path;
  const char *output = job->output_path;
  const char *pipeline = job->pipeline_name;

  update_progress(job, 0.1, "Initializing audio conversion...");

  char *cmd = NULL;

  if (g_strcmp0(pipeline, "mp3-to-flac") == 0) {
    cmd = g_strdup_printf("ffmpeg -y -i \"%s\" -c:a flac \"%s\"", input, output);
  }
  else if (g_strcmp0(pipeline, "flac-to-mp3") == 0) {
    cmd = g_strdup_printf("ffmpeg -y -i \"%s\" -c:a libmp3lame -q:a 2 \"%s\"", input, output);
  }
  else if (g_strcmp0(pipeline, "wav-to-mp3") == 0) {
    cmd = g_strdup_printf("ffmpeg -y -i \"%s\" -c:a libmp3lame -q:a 2 \"%s\"", input, output);
  }
  else if (g_strcmp0(pipeline, "opus-to-mp3") == 0) {
    cmd = g_strdup_printf("ffmpeg -y -i \"%s\" -c:a libmp3lame -q:a 2 \"%s\"", input, output);
  }
  else if (g_strcmp0(pipeline, "compress-audio") == 0) {
    cmd = g_strdup_printf("ffmpeg -y -i \"%s\" -c:a libopus -b:a 96k \"%s\"", input, output);
  }
  else {
    cmd = g_strdup_printf("ffmpeg -y -i \"%s\" \"%s\"", input, output);
  }

  update_progress(job, 0.4, "Processing audio...");
  int exit_status;
  char *stdout_out = NULL, *stderr_out = NULL;
  gboolean success = run_command_sync(cmd, &stdout_out, &stderr_out, &exit_status, job->cancellable);
  g_free(cmd);
  g_free(stdout_out);
  g_free(stderr_out);

  if (g_cancellable_is_cancelled(job->cancellable)) {
    job->complete_cb(TANGERINE_CONVERSION_ERROR_CANCELLED, NULL, "Cancelled", job->user_data);
  } else if (success && exit_status == 0) {
    update_progress(job, 1.0, "Complete");
    job->complete_cb(TANGERINE_CONVERSION_SUCCESS, job->output_path, NULL, job->user_data);
  } else {
    job->complete_cb(TANGERINE_CONVERSION_ERROR_PIPELINE, NULL, "Audio conversion failed", job->user_data);
  }
}

static void run_pipeline_pdf(ConversionJob *job) {
  const char *input = job->input_path;
  const char *output = job->output_path;
  const char *pipeline = job->pipeline_name;

  update_progress(job, 0.1, "Initializing PDF conversion...");

  char *cmd = NULL;

  if (g_strcmp0(pipeline, "pdf-to-png") == 0) {
    cmd = g_strdup_printf("pdftoppm -png -r 300 \"%s\" \"%s\"", input, output);
  }
  else if (g_strcmp0(pipeline, "pdf-to-jpeg") == 0) {
    cmd = g_strdup_printf("pdftoppm -jpeg -r 300 \"%s\" \"%s\"", input, output);
  }
  else if (g_strcmp0(pipeline, "pdf-to-text") == 0) {
    cmd = g_strdup_printf("pdftotext \"%s\" \"%s\"", input, output);
  }
  else if (g_strcmp0(pipeline, "pdf-to-md") == 0 || g_strcmp0(pipeline, "pdf-to-text") == 0) {
    cmd = g_strdup_printf("pdftotext \"%s\" \"%s\"", input, output);
  }
  else if (g_strcmp0(pipeline, "docx-to-pdf") == 0 || g_strcmp0(pipeline, "odt-to-pdf") == 0) {
    cmd = g_strdup_printf("libreoffice --headless --convert-to pdf \"%s\" --outdir \"%s\"",
      input, g_path_get_dirname(output));
  }
  else if (g_strcmp0(pipeline, "compress-pdf") == 0) {
    cmd = g_strdup_printf("gs -sDEVICE=pdfwrite -dCompatibilityLevel=1.4 -dPDFSETTINGS=/ebook -dNOPAUSE -dQUIET -dBATCH -sOutputFile=\"%s\" \"%s\"", output, input);
  }
  else if (g_strcmp0(pipeline, "split-pdf") == 0) {
    cmd = g_strdup_printf("pdfseparate \"%s\" \"%s\"", input, output);
  }
  else if (g_strcmp0(pipeline, "merge-pdf") == 0) {
    cmd = g_strdup_printf("pdfunite \"%s\" \"%s\"", input, output);
  }
  else {
    cmd = g_strdup_printf("cp \"%s\" \"%s\"", input, output);
  }

  update_progress(job, 0.4, "Processing PDF...");
  int exit_status;
  char *stdout_out = NULL, *stderr_out = NULL;
  gboolean success = run_command_sync(cmd, &stdout_out, &stderr_out, &exit_status, job->cancellable);
  g_free(cmd);
  g_free(stdout_out);
  g_free(stderr_out);

  if (g_cancellable_is_cancelled(job->cancellable)) {
    job->complete_cb(TANGERINE_CONVERSION_ERROR_CANCELLED, NULL, "Cancelled", job->user_data);
  } else if (success && exit_status == 0) {
    update_progress(job, 1.0, "Complete");
    job->complete_cb(TANGERINE_CONVERSION_SUCCESS, job->output_path, NULL, job->user_data);
  } else {
    job->complete_cb(TANGERINE_CONVERSION_ERROR_PIPELINE, NULL, "PDF conversion failed", job->user_data);
  }
}

static void run_pipeline_archive(ConversionJob *job) {
  const char *input = job->input_path;
  const char *output = job->output_path;
  const char *pipeline = job->pipeline_name;

  update_progress(job, 0.1, "Processing archive...");

  char *cmd = NULL;

  if (g_strcmp0(pipeline, "zip-to-tar") == 0) {
    cmd = g_strdup_printf("cd /tmp && unzip -q \"%s\" && tar -cf \"%s\" *", input, output);
  }
  else if (g_strcmp0(pipeline, "tar-to-gzip") == 0) {
    cmd = g_strdup_printf("gzip -c \"%s\" > \"%s\"", input, output);
  }
  else if (g_strcmp0(pipeline, "create-zip") == 0) {
    cmd = g_strdup_printf("zip -r \"%s\" \"%s\"", output, input);
  }
  else if (g_strcmp0(pipeline, "extract-zip") == 0) {
    cmd = g_strdup_printf("unzip -q \"%s\" -d \"%s\"", input, output);
  }
  else {
    cmd = g_strdup_printf("cp \"%s\" \"%s\"", input, output);
  }

  update_progress(job, 0.5, "Processing...");
  int exit_status;
  char *stdout_out = NULL, *stderr_out = NULL;
  gboolean success = run_command_sync(cmd, &stdout_out, &stderr_out, &exit_status, job->cancellable);
  g_free(cmd);
  g_free(stdout_out);
  g_free(stderr_out);

  if (g_cancellable_is_cancelled(job->cancellable)) {
    job->complete_cb(TANGERINE_CONVERSION_ERROR_CANCELLED, NULL, "Cancelled", job->user_data);
  } else if (success && exit_status == 0) {
    update_progress(job, 1.0, "Complete");
    job->complete_cb(TANGERINE_CONVERSION_SUCCESS, job->output_path, NULL, job->user_data);
  } else {
    job->complete_cb(TANGERINE_CONVERSION_ERROR_PIPELINE, NULL, "Archive operation failed", job->user_data);
  }
}

typedef enum {
  CATEGORY_UNKNOWN,
  CATEGORY_IMAGE,
  CATEGORY_VIDEO,
  CATEGORY_AUDIO,
  CATEGORY_PDF,
  CATEGORY_ARCHIVE,
} FileCategory;

static FileCategory category_from_extension(const char *ext) {
  if (!ext) return CATEGORY_UNKNOWN;
  if (g_ascii_strcasecmp(ext, "heic") == 0 || g_ascii_strcasecmp(ext, "heif") == 0 ||
      g_ascii_strcasecmp(ext, "avif") == 0 || g_ascii_strcasecmp(ext, "jpg") == 0 ||
      g_ascii_strcasecmp(ext, "jpeg") == 0 || g_ascii_strcasecmp(ext, "png") == 0 ||
      g_ascii_strcasecmp(ext, "webp") == 0 || g_ascii_strcasecmp(ext, "tiff") == 0 ||
      g_ascii_strcasecmp(ext, "tif") == 0 || g_ascii_strcasecmp(ext, "bmp") == 0 ||
      g_ascii_strcasecmp(ext, "gif") == 0 || g_ascii_strcasecmp(ext, "svg") == 0)
    return CATEGORY_IMAGE;
  if (g_ascii_strcasecmp(ext, "mp4") == 0 || g_ascii_strcasecmp(ext, "mov") == 0 ||
      g_ascii_strcasecmp(ext, "mkv") == 0 || g_ascii_strcasecmp(ext, "webm") == 0 ||
      g_ascii_strcasecmp(ext, "avi") == 0)
    return CATEGORY_VIDEO;
  if (g_ascii_strcasecmp(ext, "mp3") == 0 || g_ascii_strcasecmp(ext, "m4a") == 0 ||
      g_ascii_strcasecmp(ext, "wav") == 0 || g_ascii_strcasecmp(ext, "flac") == 0 ||
      g_ascii_strcasecmp(ext, "ogg") == 0 || g_ascii_strcasecmp(ext, "opus") == 0 ||
      g_ascii_strcasecmp(ext, "aiff") == 0)
    return CATEGORY_AUDIO;
  if (g_ascii_strcasecmp(ext, "pdf") == 0) return CATEGORY_PDF;
  if (g_ascii_strcasecmp(ext, "zip") == 0 || g_ascii_strcasecmp(ext, "tar") == 0 ||
      g_ascii_strcasecmp(ext, "gz") == 0)
    return CATEGORY_ARCHIVE;
  return CATEGORY_UNKNOWN;
}

static const char *file_extension(const char *path) {
  const char *dot = strrchr(path, '.');
  return dot ? dot + 1 : NULL;
}

static FileCategory category_from_path(const char *path) {
  return category_from_extension(file_extension(path));
}

static void process_job(ConversionJob *job) {
  if (g_cancellable_is_cancelled(job->cancellable)) {
    job->complete_cb(TANGERINE_CONVERSION_ERROR_CANCELLED, NULL, "Cancelled", job->user_data);
    return;
  }

  const char *pipeline = job->pipeline_name;

  if (g_str_has_prefix(pipeline, "heic") || g_str_has_prefix(pipeline, "avif") ||
      g_str_has_prefix(pipeline, "jpeg") || g_str_has_prefix(pipeline, "png") ||
      g_str_has_prefix(pipeline, "webp") || g_strcmp0(pipeline, "compress-image") == 0 ||
      g_strcmp0(pipeline, "crop-image") == 0 || g_strcmp0(pipeline, "strip-metadata") == 0) {
    run_pipeline_image(job);
    return;
  }
  if (g_str_has_prefix(pipeline, "video") || g_strcmp0(pipeline, "trim-video") == 0 ||
      g_strcmp0(pipeline, "compress-video") == 0) {
    run_pipeline_video(job);
    return;
  }
  if (g_str_has_prefix(pipeline, "mp3") || g_str_has_prefix(pipeline, "flac") ||
      g_str_has_prefix(pipeline, "wav") || g_str_has_prefix(pipeline, "opus") ||
      g_strcmp0(pipeline, "compress-audio") == 0) {
    run_pipeline_audio(job);
    return;
  }
  if (g_str_has_prefix(pipeline, "pdf") || g_strcmp0(pipeline, "docx-to-pdf") == 0 ||
      g_strcmp0(pipeline, "odt-to-pdf") == 0 || g_strcmp0(pipeline, "compress-pdf") == 0 ||
      g_strcmp0(pipeline, "split-pdf") == 0 || g_strcmp0(pipeline, "merge-pdf") == 0) {
    run_pipeline_pdf(job);
    return;
  }
  if (g_str_has_prefix(pipeline, "zip") || g_str_has_prefix(pipeline, "tar") ||
      g_strcmp0(pipeline, "create-zip") == 0 || g_strcmp0(pipeline, "extract-zip") == 0) {
    run_pipeline_archive(job);
    return;
  }

  switch (category_from_path(job->input_path)) {
    case CATEGORY_IMAGE: run_pipeline_image(job); break;
    case CATEGORY_VIDEO: run_pipeline_video(job); break;
    case CATEGORY_AUDIO: run_pipeline_audio(job); break;
    case CATEGORY_PDF:   run_pipeline_pdf(job);   break;
    case CATEGORY_ARCHIVE: run_pipeline_archive(job); break;
    default:
      job->complete_cb(TANGERINE_CONVERSION_ERROR_PIPELINE, NULL, "Unsupported file type", job->user_data);
      break;
  }
}

static void job_thread_func(gpointer data, gpointer user_data G_GNUC_UNUSED) {
  ConversionJob *job = data;
  process_job(job);
  conversion_job_free(job);
}

static void tangerine_conversion_engine_class_init(TangerineConversionEngineClass *klass G_GNUC_UNUSED) {
}

static void tangerine_conversion_engine_init(TangerineConversionEngine *self) {
  self->format_map = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
  self->thread_pool = g_thread_pool_new(job_thread_func, self, 4, FALSE, NULL);
  self->global_cancellable = g_cancellable_new();
}

TangerineConversionEngine *tangerine_conversion_engine_new(void) {
  return g_object_new(TANGERINE_TYPE_CONVERSION_ENGINE, NULL);
}

static void load_yaml_formats(TangerineConversionEngine *self, const char *yaml_path) {
  FILE *fh = fopen(yaml_path, "r");
  if (!fh) return;

  char line[1024];
  char *input_mime = NULL, *output_mime = NULL, *pipeline = NULL, *description = NULL;

  while (fgets(line, sizeof(line), fh)) {
    if (g_str_has_prefix(g_strstrip(line), "input_mime:")) {
      input_mime = g_strdup(g_strstrip(line + 11));
      g_strstrip(input_mime);
      if (input_mime[0] == '"') input_mime = g_strndup(input_mime + 1, strlen(input_mime) - 2);
    }
    else if (g_str_has_prefix(g_strstrip(line), "output_mime:")) {
      output_mime = g_strdup(g_strstrip(line + 12));
      g_strstrip(output_mime);
      if (output_mime[0] == '"') output_mime = g_strndup(output_mime + 1, strlen(output_mime) - 2);
    }
    else if (g_str_has_prefix(g_strstrip(line), "pipeline:")) {
      pipeline = g_strdup(g_strstrip(line + 9));
      g_strstrip(pipeline);
      if (pipeline[0] == '"') pipeline = g_strndup(pipeline + 1, strlen(pipeline) - 2);
    }
    else if (g_str_has_prefix(g_strstrip(line), "description:")) {
      description = g_strdup(g_strstrip(line + 12));
      g_strstrip(description);
      if (description[0] == '"') description = g_strndup(description + 1, strlen(description) - 2);

      if (input_mime && output_mime && pipeline) {
        char *key = g_strdup_printf("%s->%s", input_mime, output_mime);
        g_hash_table_insert(self->format_map, key, pipeline);
      }
      g_free(input_mime); input_mime = NULL;
      g_free(output_mime); output_mime = NULL;
      g_free(pipeline); pipeline = NULL;
      g_free(description); description = NULL;
    }
  }

  fclose(fh);
}

void tangerine_conversion_engine_load_formats(TangerineConversionEngine *self, const char *yaml_path) {
  load_yaml_formats(self, yaml_path);
}

static const char *mime_for_extension(const char *ext) {
  if (!ext) return "application/octet-stream";
  if (g_ascii_strcasecmp(ext, "heic") == 0 || g_ascii_strcasecmp(ext, "heif") == 0) return "image/heic";
  if (g_ascii_strcasecmp(ext, "avif") == 0) return "image/avif";
  if (g_ascii_strcasecmp(ext, "jpg") == 0 || g_ascii_strcasecmp(ext, "jpeg") == 0) return "image/jpeg";
  if (g_ascii_strcasecmp(ext, "png") == 0) return "image/png";
  if (g_ascii_strcasecmp(ext, "webp") == 0) return "image/webp";
  if (g_ascii_strcasecmp(ext, "tiff") == 0 || g_ascii_strcasecmp(ext, "tif") == 0) return "image/tiff";
  if (g_ascii_strcasecmp(ext, "bmp") == 0) return "image/bmp";
  if (g_ascii_strcasecmp(ext, "gif") == 0) return "image/gif";
  if (g_ascii_strcasecmp(ext, "svg") == 0) return "image/svg+xml";
  if (g_ascii_strcasecmp(ext, "mp4") == 0) return "video/mp4";
  if (g_ascii_strcasecmp(ext, "mov") == 0) return "video/quicktime";
  if (g_ascii_strcasecmp(ext, "mkv") == 0) return "video/x-matroska";
  if (g_ascii_strcasecmp(ext, "webm") == 0) return "video/webm";
  if (g_ascii_strcasecmp(ext, "avi") == 0) return "video/x-msvideo";
  if (g_ascii_strcasecmp(ext, "mp3") == 0) return "audio/mpeg";
  if (g_ascii_strcasecmp(ext, "m4a") == 0) return "audio/mp4";
  if (g_ascii_strcasecmp(ext, "wav") == 0) return "audio/wav";
  if (g_ascii_strcasecmp(ext, "flac") == 0) return "audio/flac";
  if (g_ascii_strcasecmp(ext, "ogg") == 0) return "audio/ogg";
  if (g_ascii_strcasecmp(ext, "opus") == 0) return "audio/opus";
  if (g_ascii_strcasecmp(ext, "aiff") == 0) return "audio/aiff";
  if (g_ascii_strcasecmp(ext, "pdf") == 0) return "application/pdf";
  if (g_ascii_strcasecmp(ext, "docx") == 0) return "application/vnd.openxmlformats-officedocument.wordprocessingml.document";
  if (g_ascii_strcasecmp(ext, "odt") == 0) return "application/vnd.oasis.opendocument.text";
  if (g_ascii_strcasecmp(ext, "txt") == 0) return "text/plain";
  if (g_ascii_strcasecmp(ext, "md") == 0) return "text/markdown";
  if (g_ascii_strcasecmp(ext, "zip") == 0) return "application/zip";
  if (g_ascii_strcasecmp(ext, "tar") == 0) return "application/x-tar";
  if (g_ascii_strcasecmp(ext, "gz") == 0) return "application/gzip";
  return "application/octet-stream";
}

const char *tangerine_conversion_engine_find_pipeline(TangerineConversionEngine *self,
  const char *input_path, const char *output_format) {

  const char *in_ext = strrchr(input_path, '.');
  if (in_ext) in_ext++;

  const char *in_mime = mime_for_extension(in_ext);
  const char *out_mime = mime_for_extension(output_format);

  char *key = g_strdup_printf("%s->%s", in_mime, out_mime);
  const char *pipeline = g_hash_table_lookup(self->format_map, key);
  g_free(key);

  return pipeline;
}

void tangerine_conversion_engine_convert_to(TangerineConversionEngine *self,
  const char *input_path,
  const char *output_path,
  const char *pipeline_name,
  TangerineConversionProgressCallback progress_cb,
  TangerineConversionCompleteCallback complete_cb,
  gpointer user_data) {

  ConversionJob *job = g_new0(ConversionJob, 1);
  job->input_path = g_strdup(input_path);
  job->output_path = g_strdup(output_path);
  job->pipeline_name = g_strdup(pipeline_name ? pipeline_name : "default");
  job->progress_cb = progress_cb;
  job->complete_cb = complete_cb;
  job->user_data = user_data;
  job->cancellable = g_cancellable_new();

  g_thread_pool_push(self->thread_pool, job, NULL);
}

void tangerine_conversion_engine_convert(TangerineConversionEngine *self,
  const char *input_path,
  const char *output_format,
  TangerineConversionProgressCallback progress_cb,
  TangerineConversionCompleteCallback complete_cb,
  gpointer user_data) {

  const char *pipeline = tangerine_conversion_engine_find_pipeline(self, input_path, output_format);
  if (!pipeline) pipeline = "default";

  char *dir = g_path_get_dirname(input_path);
  char *base = g_path_get_basename(input_path);
  char *dot = strrchr(base, '.');
  if (dot) *dot = '\0';
  char *output_path = g_strdup_printf("%s/%s_converted.%s", dir, base, output_format);

  g_free(dir);
  g_free(base);

  tangerine_conversion_engine_convert_to(self, input_path, output_path, pipeline,
    progress_cb, complete_cb, user_data);

  g_free(output_path);
}

void tangerine_conversion_engine_cancel(TangerineConversionEngine *self, const char *input_path G_GNUC_UNUSED) {
  g_cancellable_cancel(self->global_cancellable);
}

GList *tangerine_conversion_engine_missing_tools(void) {
  static const char *tools[] = {
    "vips", "ffmpeg", "heif-convert", "avifdec",
    "pdftoppm", "pdftotext", "pdfseparate", "pdfunite",
    "gs", "libreoffice", "unzip", "tar", "gzip",
    NULL
  };

  GList *missing = NULL;
  for (int i = 0; tools[i]; i++) {
    char *path = g_find_program_in_path(tools[i]);
    if (path) {
      g_free(path);
    } else {
      missing = g_list_prepend(missing, (gpointer)tools[i]);
    }
  }
  return g_list_reverse(missing);
}