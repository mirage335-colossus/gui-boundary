# Preserved Rev toolkit

This is the selected desktop element and 2D rendering closure of Rev, pinned to
the `clean` branch at commit `d73faa7759b5cfd30d592057790ab458568b569b`.
The upstream repository URL and selected module names are recorded in
`UPSTREAM.json` for provenance. A build never contacts that repository.

The source includes the compatibility and correctness fixes recorded in
`PATCHES.md`. `SHA256SUMS` identifies every preserved file, including the patched
sources, external headers and resources. Verify the complete local dependency
package from the example root with `cmake -P tools/verify-rev.cmake`; Rev builds
perform this verification automatically. A deliberate dependency update must
update the provenance, patch record and checksums together.

Rev redistribution permission was confirmed through a verbal agreement between
mirage335 and the upstream Rev developer. The upstream snapshot does not contain
a general license grant; this provenance note does not create one or change the
terms of that agreement. Included dependency notices remain intact:

- `external/glm/LICENSE` — GLM (MIT option).
- `external/nanosvg/LICENSE` — NanoSVG.
- `resources/DejaVu-LICENSE` — DejaVu Sans Mono regular and bold faces; the
  regular face is stored as `DejaVuSans.ttf`. Exact source-package provenance,
  font identities and authors are preserved beside that notice.

The upstream default Arial font is replaced with the preserved DejaVu font.
The adapter selects the matching bold font for the shared `Font.bold` property,
including text measurement; no runtime font discovery or download is involved.
`cmake/PrepareRev.cmake` stages platform-specific module interfaces in the build
directory and embeds the font, shaders and icons there. Installed binaries need
no source checkout, working-directory assets, Python interpreter or network.

OpenGL and the operating system window API are supplied by the destination
machine. GLEW and FreeType can come from distribution development packages or
the complete source archives in `../rev-deps`. Rev requires OpenGL 4.4, or 4.3
with `GL_ARB_buffer_storage`, including a driver exposing those capabilities.
The integrated platform paths are Linux/X11 and Windows/OpenGL; Windows still
requires native build and runtime qualification.
