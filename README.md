# Tangerine Linux

Zero-click offline file converter for Linux — inspired by [Tangerine for Mac](https://tangerineformac.com/).

## Features (Planned)
- **Drag + Shift** → Convert formats (images, video, audio, PDF, docs, archives)
- **Drag + Super** → File tools (compress, crop, trim, split, merge, redact, annotate)
- **Drag + Ctrl** → File actions (copy, move, copy path, open terminal)
- 100+ format conversions
- 10+ file editing tools
- All local, no uploads
- Flatpak sandboxed

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
./builddir/tangerine
```

## Run (Development)
```bash
# Press Super+Shift+V to test radial menu
./builddir/tangerine
```

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
│   ├── main.c
│   ├── radial-menu.c/h
│   ├── drag-monitor.c/h
│   ├── conversion-engine.c/h
│   └── formats.yaml
├── data/
│   └── com.github.yourname.Tangerine.gschema.xml
└── tests/
    └── test-radial-menu.c
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