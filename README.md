# Tangerine Linux

Zero-click offline file converter for Linux — inspired by [Tangerine for Mac](https://tangerineformac.com/).

## Features
- **Drop files, no modifier** → convert (images, video, audio, PDF, archives)
- **Drop files + Shift** → file tools (compress, crop, trim, split, redact, strip metadata)
- **Drop files + Ctrl** → file actions (copy, move, copy path, open, terminal, trash)
- **File-manager integration** — Nautilus script + Dolphin service menu, or use
  Tangerine as an "Open With" handler
- **CLI** for scripting (`--convert-to`, `--action`)
- Preferences window (output location, quality preset, notifications)
- All local, no uploads

## Usage

### GUI
```bash
GSETTINGS_SCHEMA_DIR=builddir ./builddir/tangerine
```
Drag files onto the window. Hold a modifier to switch the radial menu:

| Modifier | Menu |
|----------|------|
| (none)   | Auto-convert to a smart default format |
| Shift    | Tools |
| Ctrl     | File operations |

`Super+Shift+V` also opens the convert menu.

- Dropping files with no modifier auto-converts them to a sensible format based on type:
  heic/avif→jpg, webp→png, images keep their type, video→mp4, audio→mp3, pdf→png,
  zip→tar, tar→zip. Outputs are written `<name>_converted.<ext>` next to the source;
  if that exists, `_1`, `_2`… is appended.
- Conversion progress, per-file queue, error toasts, and desktop notifications.
- **Recent conversions** — click the recent icon in the header bar to see recent
  conversions and open their output folder. Persisted in GSettings.

### Command line
```bash
# Convert one or more files (writes <name>_converted.<ext> next to each source)
tangerine --convert-to webp photo.png
tangerine --convert-to jpg *.heic

# Run a file operation / tool action on the given files
tangerine --action copy-path document.pdf
tangerine --action strip-meta image.jpg
tangerine --action trash old.png

# Open preferences
tangerine --preferences

# Show the radial menu for files (default)
tangerine photo.png video.mp4
```

### GUI file queue
Drop files onto the window and they appear in a per-file queue (basename + status + remove button). With no modifier held, Tangerine auto-converts them to the default format; failures show an error toast.

### File managers
- **Nautilus**: `data/nautilus/tangerine-convert` (installs to
  `~/.local/share/nautilus/scripts/`, appears under right-click → Scripts)
- **Dolphin/KDE**: `data/kio/tangerine-servicemenu.desktop` (installs to
  `~/.local/share/kio/servicemenus/`)
- **Any DE**: `data/com.github.yourname.Tangerine.desktop` registers Tangerine
  as an "Open With" application for the listed MIME types

## Build Dependencies
```bash
# Ubuntu/Debian
sudo apt install meson ninja-build libgtk-4-dev libadwaita-1-dev \
  libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
  libavformat-dev libvips-dev libpoppler-glib-dev \
  libarchive-dev libyaml-dev libportal-dev

# Fedora
sudo dnf install meson ninja-build gtk4-devel libadwaita-devel \
  gstreamer1-devel gstreamer1-plugins-base-devel \
  ffmpeg-devel vips-devel poppler-glib-devel \
  libarchive-devel libyaml-devel xdg-desktop-portal-devel

# Arch
sudo pacman -S meson ninja gtk4 libadwaita gstreamer \
  gst-plugins-base ffmpeg vips poppler-glib libarchive yaml-cpp xdg-desktop-portal
```

## Build
```bash
meson setup builddir
meson compile -C builddir
meson test -C builddir        # runs the conversion test suite
```

## Run (Development)
```bash
# The compiled GSettings schema lives in builddir, so point GLib at it:
GSETTINGS_SCHEMA_DIR=builddir ./builddir/tangerine
```
Or install it system-wide: `meson install -C builddir`.

## CI
GitHub Actions builds and runs `meson test` on every push/PR to `master`
(`.github/workflows/ci.yml`).

## Flatpak Build
```bash
flatpak-builder --user --install --force-clean build-dir flatpak/com.github.yourname.Tangerine.yml
flatpak run com.github.yourname.Tangerine
```

## Project Structure
```
tangerine-linux/
├── meson.build
├── flatpak/
│   └── com.github.yourname.Tangerine.yml
├── src/
│   ├── main.c                 # app, CLI parsing, drop zone, menu wiring
│   ├── radial-menu.c/h        # Cairo radial menu
│   ├── drag-monitor.c/h       # modifier tracking
│   ├── conversion-engine.c/h  # format mapping + conversion pipelines
│   ├── file-operations.c/h    # copy, move, copy-path, open, terminal, trash
│   ├── preferences.c/h        # AdwPreferencesDialog
│   ├── formats.yaml
│   └── tests/
│       ├── test-conversion.c
│       └── test-radial-menu.c
└── data/
    ├── com.github.yourname.Tangerine.gschema.xml
    ├── com.github.yourname.Tangerine.desktop
    ├── nautilus/tangerine-convert
    ├── kio/tangerine-servicemenu.desktop
    └── icons/
```

## Architecture
- **GTK4 + libadwaita** — UI toolkit
- **GStreamer + VA-API** — Video processing
- **libvips** — Fast image processing
- **FFmpeg** — Audio/video transcoding
- **Poppler** — PDF rendering
- **LibreOfficeKit** — Document conversion
- **libarchive** — Archive handling
- **Flatpak + Portals** — Sandbox & file access

## License
MIT