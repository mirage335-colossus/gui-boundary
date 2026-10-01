# Offline GLEW and FreeType sources

This directory preserves the complete upstream GLEW 2.3.1 and FreeType 2.14.3
release archives. `UPSTREAM.json` records their versions, provenance URLs and
SHA-256 digests. `SHA256SUMS` covers the archives and accompanying metadata and
license notices. These URLs are records, not download inputs.

`GUI_REV_BUNDLED_DEPS=ON` verifies and extracts both archives into the build
directory, builds static libraries and uses them for Rev. This defaults to ON on
Windows and OFF on Linux. It requires a C compiler as well as the C++ compiler.
The archives include generated GLEW sources and FreeType's CMake build inputs;
no vcpkg, git, Perl, Python, autotools or source generation download is needed.

FreeType's optional zlib, bzip2, PNG, HarfBuzz and Brotli dependencies are disabled
for this profile. The embedded TrueType font does not require them. The platform
OpenGL library/driver and, on Linux, X11/XRandR/Xext development libraries remain
system prerequisites. Distribution GLEW and FreeType remain available through
`GUI_REV_BUNDLED_DEPS=OFF`.

The source archives contain their complete licenses. Copies of the GLEW license
and FreeType's license selection, FreeType License and GPLv2 are included here
so binary distributions can carry the notices without unpacking source archives.
The example uses FreeType under the FreeType License. Portions of this software
are copyright © 1996–2026 The FreeType Project (www.freetype.org). All rights
reserved.
