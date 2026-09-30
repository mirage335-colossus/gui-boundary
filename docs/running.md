# Running and comparing the interfaces

Build a profile from [Building](building.md) before running its commands below.
All paths are relative to the repository root and assume a single-configuration
build. Each launch starts fresh in-memory application state; edits and rows are
not saved between runs or shared across interfaces.

## Try the same features in each interface

The interactive interfaces show **Boundary Workshop**. At the initial 640×480
logical size, controls and rows are on the left, the bitmap and details control
are on the right, and page tabs are at the bottom. Use a matching logical client
size when comparing geometry. A browser's status area and a terminal's status
line are host chrome outside that application view.

1. Select **Second** from the choice, then replace **Example text** with a short
   phrase in the editor. The terminal uses Tab to focus controls and Ctrl+O to
   open options; graphical hosts provide clickable controls.
2. Check **Enable action**, then activate **Add row**. A row containing the editor
   value appears. Change the text and add a second row. Turning the toggle off
   disables **Add row** without deleting either row.
3. Open **Actions** and select **Clear rows**. The list returns to **No rows**.
   This demonstrates an option carrying a stable ID to a shared event handler.
4. Open **Show details**. The centered modal contains explanatory text and a
   **Close details** button. Background controls and page changes are blocked.
   Close it with its button or Escape; focus returns to the previous control.
5. Focus the editor and press Enter. The host prompt is titled **Enter text**.
   Enter a short value and accept it; that value becomes the caption below the
   bitmap. Repeat and cancel; the previous caption remains. This prompt tests a
   host service, while **Show details** tests a shared composed view.
6. Switch to **Other page**, then back to **Controls**. The second page is
   intentionally empty apart from page navigation. Controls and rows retain
   their state when you return.
7. Open the bitmap's **Refresh image** action. It increments the source revision
   and requests a repaint; this solid-color producer intentionally looks the
   same afterward. In pointer-capable hosts, pointing/clicking in the bitmap
   updates the caption with a pixel coordinate.

The [conformance scenarios](conformance.md#required-visual-and-interaction-scenarios)
go further into clipping, resize, stale input, lifetime, and transport failures.
The walkthrough above exercises existing features without editing code. To add
one, follow the [shared-code feature exercise](feature-recipes.md).

## Reference demonstration

```sh
./build-core/gui_example
```

This is a display-free scripted demonstration, useful for verifying the build
and tracing semantic input. It prints the selected option, editor text, row
count, and bitmap dimensions, then exits successfully. Expected output is in
the [README](../README.md#start-with-a-build-that-needs-no-gui-toolkit).
Its synthetic text measurements are fixture data, not native font measurements.

## Terminal UI

```sh
./build-core/gui_terminal
```

Run it with terminal input and output attached, not through a pipe or an output
log panel. `./build-core/gui_terminal --help` prints keys without entering the
interactive screen. The POSIX host uses raw input and an alternate screen; it
restores terminal modes on ordinary exit and handled termination signals.
The Windows host uses console events and virtual-terminal output.

| Key | Action |
| --- | --- |
| Tab / Shift+Tab | Move focus forward / backward |
| Enter / Space | Activate a button or toggle; Enter submits the editor |
| Arrow keys, Home, End | Move the caret or navigate the focused control |
| Ctrl+A / Ctrl+U | Select all editor text / clear editor text |
| Ctrl+O | Open focused choice/menu, editor suggestions, or bitmap actions |
| Up/Down, then Enter | Choose an item in an open popup |
| Escape | Cancel a popup or prompt; dismiss the shared details modal |
| Ctrl+N / Ctrl+P | Next / previous page |
| Page Up / Page Down | Scroll the focused list |
| Ctrl+Q | Close the application and return to the shell |

For multiline controls added in shared code, the POSIX byte decoder also maps
Ctrl+J to Shift+Enter and Ctrl+S to Ctrl+Enter. The control's text policy determines
whether these submit or insert a newline. Prefer the documented Ctrl+O and
Ctrl+N/P aliases over terminal-specific modifier escape sequences.

Use **80 columns × 31 rows** or larger for the initial view: 80×30 application
cells at 8×16 logical units, plus one status row. Smaller terminals retain the
shared layout and pan to keep focus visible. A larger terminal expands the
logical client area. To reach an offscreen control, continue tabbing rather than
expecting the UI to rearrange into a single column.

This host supports keyboard input and bracketed paste, but does not enable or
decode mouse reporting. It outputs printable ASCII cells and synthesized color
escapes. Non-ASCII values appear as literal `\u{…}` sequences, and control
characters are escaped, so application text cannot become terminal commands.
The underlying application values remain UTF-8. Bitmap detail is reduced to the
cell grid. Terminal fonts, color support, and cell rounding affect appearance.

## Framebuffer image

```sh
./build-core/gui_framebuffer --output build-core/example.ppm
```

This writes the initial UI to a binary RGB PPM image, prints
`Rendered: 640x480 to build-core/example.ppm`, and exits. Open the file in an image
viewer that supports PPM. It does not open a window or wait for input. Without
`--output`, it writes `gui-boundary.ppm` in the current directory. An existing
output file is replaced.

To exercise the scripted framebuffer host path instead of capturing the initial
view, run `./build-core/gui_framebuffer --self-test`. You may combine it with
`--output PATH.ppm` to capture the resulting test frame. The full-frame host and
application bitmap producers are separate concepts; the
[bitmap contract](bitmap-contract.md) describes their ownership.

## Framebuffer window (SDL2)

```sh
./build-sdl/gui_framebuffer_sdl
```

This opens a resizable window displaying the software framebuffer. Use mouse
clicks, wheel input, Tab, and the usual editing/navigation keys. Ctrl+N/P changes
pages; Ctrl+Q or the window close button exits. Alt+Down or the context-menu key
opens options for the focused control. Right-click the bitmap to open its
actions. Ctrl+C/X/V handles editor clipboard text through SDL.

For a display-free host check, run:

```sh
SDL_VIDEODRIVER=dummy ./build-sdl/gui_framebuffer_sdl --self-test
```

That command injects SDL input, completes a prompt, adds a row, resizes, uploads
the texture, and closes. The environment assignment shown uses POSIX shell
syntax. CTest sets the same environment automatically for `framebuffer_sdl`.
`--self-test --output PATH.ppm` also saves the resized test frame; `--output` on
its own does not capture an ordinary interactive session. Do not leave the dummy
driver set when you want a visible window.

The default software font is a small ASCII demonstration font. Unsupported
Unicode glyphs display a fallback while the application retains their UTF-8
values. A paired measurement/raster provider can replace it through the generic
framebuffer interface; see the [adapter guide](adapter-guide.md).

## Native widgets (FLTK)

```sh
./build-fltk/gui_fltk_demo
```

This opens **Boundary Workshop** using native FLTK controls. A working desktop
display is required. Use the normal toolkit editing and selection controls,
Tab/Shift+Tab for focus, and page buttons for navigation. Close the window to
exit. The application still supplies widget bounds, rows, page geometry, and
modal scope; the toolkit supplies native control mechanics and font metrics.

FLTK prompts are asynchronous host controls, so the application's event pump
continues while a service is pending. The example implements prompt and clipboard
write services in this profile. File/location services report an explicit error;
their presence in the public vocabulary is not an implementation in every host.

## Browser interfaces

Both browser modes render the same shared C++ application using the same DOM
renderer. They differ in where C++ executes:

| Mode | Application execution | Files needed |
| --- | --- | --- |
| Hosted C++ | Independent native subprocess for each tab | `build-core/gui_web_demo` and repository web assets |
| Wasm | Compiled C++ module inside each tab | `build-wasm/gui_web_wasm.js`, companion `.wasm`, and repository web assets |

### Hosted C++

On a POSIX host, start:

```sh
python3 backends/web/host.py --executable build-core/gui_web_demo --port 8765
```

Keep that terminal open, then visit [the local interface](http://127.0.0.1:8765/).
The bottom status begins with **Hosted C++**. `gui_web_demo` is a protocol worker,
so running it directly in a terminal does not open a browser. The Python host
uses standard-library HTTP serving and polls subprocess pipes; that subprocess
path is POSIX-specific.

### Browser-only Wasm

After the [Wasm build](building.md#browser-only-webassembly), start:

```sh
python3 backends/web/host.py --wasm-dir build-wasm --port 8765
```

Open [the local Wasm interface](http://127.0.0.1:8765/?mode=wasm). The status begins
with **Wasm**. Python only serves assets in this mode; it does not execute the
application or spawn a C++ process. Keep both module files in the supplied
directory. Opening `index.html` directly from disk does not supply the HTTP
asset paths and session endpoints used by the example.

### Serve both modes and manage sessions

To compare both in separate tabs, start one host with both build outputs:

```sh
python3 backends/web/host.py --executable build-core/gui_web_demo \
  --wasm-dir build-wasm --port 8765
```

Use `/` for hosted C++ and `/?mode=wasm` for Wasm. Open the exact printed
`127.0.0.1` URL. The server checks the Host and Origin, so substituting `localhost`
or another hostname is rejected. Use `--port 8766` if 8765 is occupied, and open
the corresponding printed URL. Stop the host with Ctrl+C; this also closes its
owned subprocesses. `python3 backends/web/host.py --help` lists the arguments.

Each tab has independent state and a fresh transport epoch. Reloading starts a
new application; it does not recover previous rows. Hosted sessions are released
when the page sends its close notification and also expire after 15 idle minutes.
There are at most 16 simultaneous hosted sessions. Wasm has no server-owned
application session and is not subject to that idle expiry.

If the status offers **Retry connection**, retry after resolving the connection
problem. The client resends the same pending operation with its sequence number,
allowing the same session to suppress duplicate commands. Reload only when you
intend to start over or the host/session has been replaced: an old session's
operations cannot be replayed into a new one.

## Troubleshooting

| Symptom | Resolution |
| --- | --- |
| `gui_example` prints text but no GUI appears | It is the reference runner. Launch the terminal, a window host, or the browser host for interaction. |
| Terminal asks for an interactive terminal | Attach both standard input and output to a real terminal or PTY; do not redirect them. |
| Terminal controls are offscreen or clicks do nothing | Use Tab to reveal controls and resize to at least 80×31. This terminal host is keyboard-driven. |
| A terminal shortcut is intercepted | Check the emulator's bindings and use the documented aliases. The host must receive the keystroke to decode it. |
| SDL creates no visible window | Unset `SDL_VIDEODRIVER=dummy` for ordinary use and check that a display/video driver is available. |
| FLTK cannot open a display | Run in a graphical session; use Xvfb for automated Linux checks. |
| Browser host reports address already in use | Choose another `--port` and open its printed URL. |
| `Unexpected Host` or `Unexpected Origin` | Use the exact `http://127.0.0.1:PORT/` origin printed by the host. |
| `Only Wasm hosting is configured` | Open `/?mode=wasm`, or restart the host with `--executable` to enable hosted C++. |
| Browser cannot load the module or start its process | Check that the build finished and paths point to the executable or both Wasm files. Include the configuration subdirectory for multi-configuration builds. |
| `Unknown session token` after leaving a hosted tab idle or restarting the server | The session is gone. Reload to start a fresh application; previous in-memory state is lost. |
| `Session limit reached` | Close unneeded tabs. If release notifications were lost, restart the host to clear its sessions; all hosted tabs will then need reloading. |
| A page has no controls | **Other page** is intentionally empty. Return to **Controls**. |
| Unicode looks escaped or has fallback glyphs | This is a documented terminal/software-font profile limit. UTF-8 values remain intact. Native/DOM glyph coverage depends on host fonts. |

For compilation and missing-test problems, see [Building](building.md#build-troubleshooting).
For unsupported services and platform qualification, see
[backend profiles](conformance.md#backend-profiles).
