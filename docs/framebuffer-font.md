# Built-in software font

The framebuffer and SDL backends use a committed grayscale glyph atlas from the
same DejaVu Sans Mono regular and bold font files as Rev. Normal builds need no
font library, installed font, or network access. Both text measurement and
painting use the atlas's advances, bearings, ascent, descent, and line height.

The source fonts are retained in `third_party/rev/resources`. The historical
filename `DejaVuSans.ttf` contains **DejaVu Sans Mono**, not proportional DejaVu
Sans. The [source manifest](../third_party/rev/resources/DejaVu-SOURCES.json)
records the distribution package and hashes. Its
[license](../third_party/rev/resources/DejaVu-LICENSE) and
[authors](../third_party/rev/resources/DejaVu-AUTHORS) accompany the retained
font sources and derived atlas. DejaVu changes are public domain; the underlying
Bitstream font license permits redistribution with its copyright and license
notices. Keep those notices when distributing the derived font data.

The atlas retains U+0020–U+007E, U+00A0–U+00FF, and U+FFFD: printable ASCII,
printable Latin-1, and a replacement glyph. Unsupported codepoints display the
replacement glyph; application strings and edit events retain their original
UTF-8. This atlas does not provide script shaping or comprehensive Unicode
coverage. `FramebufferTextRenderer` supplies the paired measurement and painting
extension for hosts that require those features.

Every integer device pixel size from 8 through 32 is retained, followed by 36,
40, 42, 48, 56, and 64, for both weights. The renderer scales the nearest retained
size for other requested device sizes. Font sizes and display scaling remain
application declarations; no application labels or widget identities appear in
the atlas. Coverage has 16 levels and is blended with the actual background.
Font rendering and native widget styling can still differ between toolkits.

## Optional regeneration

Regeneration is maintenance work, separate from ordinary builds. It uses the
already retained source fonts and distribution development packages. On Debian:

```sh
sudo apt-get install --no-install-recommends g++ pkg-config libfreetype-dev libssl-dev
c++ -std=c++20 -O2 tools/generate-framebuffer-font.cpp \
  $(pkg-config --cflags --libs freetype2 openssl) -o /tmp/gui-boundary-font-generator
/tmp/gui-boundary-font-generator third_party/rev/resources /tmp/framebuffer_font_data.hpp
cmp include/gui/detail/framebuffer_font_data.hpp /tmp/framebuffer_font_data.hpp
```

The generator verifies the SHA-256 of both input fonts before rasterizing them
with `FT_LOAD_RENDER`, the same FreeType mode as Rev. The committed header
records FreeType 2.13.3. Matching source fonts, generator, and FreeType version
produce identical output. A different rasterizer version can change hinting or
coverage; review those changes before replacing the committed header:

```sh
cp /tmp/framebuffer_font_data.hpp include/gui/detail/framebuffer_font_data.hpp
```

`include/gui/detail/framebuffer_font_data.hpp` is generated; edit the generator
to change coverage or retained sizes. It stores tight glyph bitmaps as short
run lengths of 4-bit coverage, encoded in bounded Base64 strings to keep source
size and compiler requirements modest. Glyphs are decoded on demand; decoding
does not open files, call a font library, or allocate a full atlas at startup.

After updating the font, run the framebuffer tests and recapture the screenshot
gallery using the commands in [screenshots.md](screenshots.md). Inspect regular
and bold text at ordinary and increased display scales, including accented
letters, selection, caret placement, and wrapped text.
