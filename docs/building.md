# Building the example

Run commands from the repository root, where `CMakeLists.txt` lives. This guide
uses separate build directories so choosing an optional backend cannot silently
change another profile. After building, use the [running guide](running.md) to
launch an interface and the [coverage guide](conformance.md) to choose checks.

## Which build produces which output

Configure **and** build the selected profile before using its run command. The
Python browser host serves existing outputs; it does not compile them. All paths
below assume a single-configuration generator.

| Interface | Configure/build recipe | CMake target | Output used to run it |
| --- | --- | --- | --- |
| Reference demonstration | [Default profile](#default-profile) | `gui_example` | `build-core/gui_example` |
| Terminal UI | [Default profile](#default-profile) | `gui_terminal` | `build-core/gui_terminal` |
| Framebuffer image | [Default profile](#default-profile) | `gui_framebuffer` | `build-core/gui_framebuffer` |
| Hosted web | [Hosted web profile](#hosted-web-native-process) (same core build) | `gui_web_demo` | `build-core/gui_web_demo`, passed to `host.py --executable` |
| FLTK native widgets | [FLTK profile](#native-widgets-fltk), `GUI_BUILD_FLTK=ON` | `gui_fltk_demo` | `build-fltk/gui_fltk_demo` |
| SDL2 framebuffer window | [SDL profile](#framebuffer-window-sdl2), `GUI_BUILD_SDL=ON` | `gui_framebuffer_sdl` | `build-sdl/gui_framebuffer_sdl` |
| Wasm web | [Emscripten profile](#browser-only-webassembly) | `gui_web_wasm` | `build-wasm/gui_web_wasm.js` and `.wasm`, served with `host.py --wasm-dir build-wasm` |
| Rev native widgets | [Rev profile](#native-widgets-rev), `GUI_BUILD_REV=ON` | `gui_rev_demo` | `build-rev/gui_rev_demo` |

The FLTK and SDL profiles also build the four core executables in their own
build directories. The [FLTK/SDL host profile](#fltk-sdl-and-host-checks)
builds six native application executables under `build-all/`; use that prefix
when running them. Rev uses a separate profile with a module-capable compiler;
none of these native profiles produces Wasm. The Emscripten profile builds
only the browser module and requires its own build directory.

## Prerequisites

| Requirement | Needed for | How the build uses it |
| --- | --- | --- |
| CMake 3.20 or newer | Core, FLTK, SDL and Wasm builds | Configures targets and CTest; Rev requires 3.28 or newer |
| C++20 compiler, standard library, and thread support | Core, FLTK, SDL and Wasm builds | Public boundary and application; compiler warnings are treated as errors |
| Generator's build tool | Every build | For example, Make or Ninja for a corresponding single-configuration generator |
| Python 3 | Hosted browser and Python checks | Standard library only; no `pip` packages |
| Node | DOM renderer fixture checks and compiled Wasm checks | No `npm install` step |
| FLTK development headers and libraries | Native-widget profile | CMake's `FindFLTK`; FLTK 1.3 was used for the recorded validation |
| SDL2 development headers, libraries, and CMake package | Interactive framebuffer window | `find_package(SDL2 REQUIRED CONFIG)` and `SDL2::SDL2` |
| C++23 modules, CMake 3.28+, OpenGL and platform development files | Rev native widgets | See the complete [Rev prerequisites](rev-offline.md); these do not affect the default build |
| Emscripten toolchain available in the shell | Browser-only Wasm compilation | `emcmake` configures the C++ compiler and linker |
| Desktop display, or Xvfb on Linux | FLTK/Rev execution/tests; interactive SDL window | Rev additionally needs a suitable OpenGL driver; none is required for the default build |

Use your platform's toolchain and development packages, including headers rather
than only runtime libraries. The project does not download SDKs or dependencies.
Python and Node checks are added only when those interpreters are found during
configuration. See the [validation record](conformance.md#previous-implementation-validation) for
versions actually exercised; it is not a minimum-version or portability matrix.
The hosted browser's native subprocess mode requires POSIX pipe polling. The
terminal has POSIX and Windows console code, but Windows and macOS execution
have not been qualified by the recorded Linux run.

## Default profile

This builds the reference demonstration, terminal, native browser process, and
file-output framebuffer, along with tests. No FLTK or SDL development files are
needed.

```sh
cmake -S . -B build-core -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
  -DGUI_BUILD_FLTK=OFF -DGUI_BUILD_SDL=OFF -DGUI_BUILD_REV=OFF -DGUI_TEST_HOSTS=OFF
cmake --build build-core --parallel 2
ctest --test-dir build-core --output-on-failure
```

The executables are `build-core/gui_example`, `build-core/gui_terminal`,
`build-core/gui_web_demo`, and `build-core/gui_framebuffer`. The browser process
speaks a line-oriented protocol; launch it through the Python host to get a UI.
Compilation also checks every public header and the shared application without
toolkit includes. Increase `--parallel 2` if your machine has sufficient memory;
use `--parallel 1` if compiler processes are being killed for memory pressure.

## Hosted web (native process)

Hosted web is included in the default profile. If `build-core/gui_web_demo`
already exists from that build, skip the following commands and
[start the Python host](running.md#hosted-c):

```sh
cmake -S . -B build-core -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
  -DGUI_BUILD_FLTK=OFF -DGUI_BUILD_SDL=OFF -DGUI_BUILD_REV=OFF -DGUI_TEST_HOSTS=OFF
cmake --build build-core --parallel 2
```

The result is a native executable, `build-core/gui_web_demo`. Python 3 supplies
the HTTP/process host on POSIX; neither Emscripten nor a desktop GUI toolkit is
needed. Browser JavaScript/CSS assets are served from `backends/web/` and need no
separate build or package installation. This profile does not produce the Wasm
module; use its [separate recipe](#browser-only-webassembly) for that mode.

## Native widgets (FLTK)

Install or otherwise supply FLTK development files, then configure this profile:

```sh
cmake -S . -B build-fltk -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
  -DGUI_BUILD_FLTK=ON -DGUI_BUILD_SDL=OFF -DGUI_BUILD_REV=OFF -DGUI_TEST_HOSTS=OFF
cmake --build build-fltk --parallel 2
ctest --test-dir build-fltk --output-on-failure
```

This adds `build-fltk/gui_fltk_demo` and compiles `fltk_test`. With
`GUI_TEST_HOSTS=OFF`, CTest does not run the FLTK display test; the other tests can
still run without a display. See [host-inclusive testing](#fltk-sdl-and-host-checks)
to enable that test. The example does not require FLTK's OpenGL, forms, image
libraries, or `fluid` tool.

## Framebuffer window (SDL2)

Install or otherwise supply SDL2 development files, then configure this profile:

```sh
cmake -S . -B build-sdl -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
  -DGUI_BUILD_FLTK=OFF -DGUI_BUILD_SDL=ON -DGUI_BUILD_REV=OFF -DGUI_TEST_HOSTS=OFF
cmake --build build-sdl --parallel 2
ctest --test-dir build-sdl --output-on-failure
```

This adds `build-sdl/gui_framebuffer_sdl`. The shared software renderer creates
the image; SDL supplies the window, event input, and texture upload. CTest runs
its self-test with `SDL_VIDEODRIVER=dummy`, even when `GUI_TEST_HOSTS` is off. A
normal interactive run uses your desktop video driver. SDL2, rather than SDL3,
is the dependency selected by this project's CMake file.

## Native widgets (Rev)

The optional Rev target has a newer toolchain requirement than the public
boundary: CMake 3.28 or newer, a supported C++23 module compiler, and a generator
that scans module dependencies. The Linux recipe uses Clang 19 and Ninja. The
[offline reconstruction guide](rev-offline.md) lists exact distribution packages,
preserved sources and Microsoft-toolchain commands.

With those prerequisites installed, this fresh Linux profile builds local
GLEW/FreeType sources together with Rev, the demonstration, and its native test:

```sh
cmake -P tools/verify-rev.cmake
cmake -S . -B build-rev -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang-19 -DCMAKE_CXX_COMPILER=clang++-19 \
  -DGUI_BUILD_REV=ON -DGUI_REV_BUNDLED_DEPS=ON -DBUILD_TESTING=ON \
  -DGUI_BUILD_FLTK=OFF -DGUI_BUILD_SDL=OFF -DGUI_TEST_HOSTS=ON
cmake --build build-rev --parallel 2
ctest --test-dir build-rev --output-on-failure -R '^rev(_clipboard)?$'
./build-rev/gui_rev_demo
```

The `rev` test needs a display with usable OpenGL. On Linux, an installed Xvfb
and Mesa software driver can supply a headless display:

```sh
xvfb-run -a env LIBGL_ALWAYS_SOFTWARE=1 ctest --test-dir build-rev --output-on-failure -R '^rev(_clipboard)?$'
```

`GUI_TEST_HOSTS=OFF` still compiles `rev_test` and `rev_clipboard_test` when
`BUILD_TESTING=ON`, but does not register their host checks. Clipboard tests
replace clipboard contents; use an isolated test display. For an initial build containing only the demo,
use `cmake --build build-rev --target gui_rev_demo --parallel 2`. That does not
build test executables, so build the default target before running the complete
CTest inventory. The `build-rev` profile also builds the four core executables.

On Linux, `GUI_REV_BUNDLED_DEPS=OFF` instead discovers distribution GLEW/FreeType
development packages. The switch defaults to `OFF` on Linux and `ON` on Windows;
the recipe sets it explicitly so there is no ambiguity about which sources are
used. Both paths are offline during configuration and compilation. Resources
are embedded by CMake; no Git, vcpkg, package registry, Python, or asset-generation
download is part of a Rev build.

## FLTK, SDL and host checks

On a POSIX system with Python, FLTK, SDL2, and a working display, this profile
builds the core, FLTK and SDL targets and enables local HTTP/process, PTY, and
FLTK checks. The [Rev profile](#native-widgets-rev) has a separate build recipe.
Install Node as well to include the DOM renderer fixture test.

```sh
cmake -S . -B build-all -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
  -DGUI_BUILD_FLTK=ON -DGUI_BUILD_SDL=ON -DGUI_BUILD_REV=OFF -DGUI_TEST_HOSTS=ON
cmake --build build-all --parallel 2
ctest --test-dir build-all --output-on-failure
```

On Linux without a desktop display, run the last command through an installed
Xvfb wrapper instead:

```sh
xvfb-run -a ctest --test-dir build-all --output-on-failure
```

The process must be allowed to open loopback sockets and pseudo-terminals. This
is why these checks are opt-in. `GUI_TEST_HOSTS=ON` does not install dependencies
or turn on any optional backend. Test registration and what each check
proves are explained in [conformance and coverage](conformance.md).

## Browser-only WebAssembly

Start a shell in which your installed Emscripten toolchain is activated and
`emcmake` is available. Use a fresh build directory; a native CMake cache cannot
be converted into a Wasm cache by adding `emcmake` later.

```sh
emcmake cmake -S . -B build-wasm -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-wasm --parallel 2
ctest --test-dir build-wasm --output-on-failure
```

This toolchain selects the Wasm branch of `CMakeLists.txt`. It builds
`gui_web_wasm.js` and `gui_web_wasm.wasm`, not the native executable targets. Node
must be discoverable at configure time for CTest to register the `wasm` check;
confirm it appears with `ctest --test-dir build-wasm -N`. The check executes the
compiled module. You can also run it directly:

```sh
node tests/wasm_test.mjs build-wasm/gui_web_wasm.js
```

Keep the generated JavaScript and Wasm files together. CMake also writes
`build-wasm/package.json` to declare ES-module semantics for Node. There is no
JavaScript package-install step. Serve the module with the
[browser host](running.md#browser-interfaces), then exercise it in an actual
browser; the Node test does not validate browser rendering.

Use a different directory when changing or relocating the Emscripten SDK. Build
directories contain absolute toolchain paths and are not relocatable SDKs.

## CMake options and cached configurations

| Option | Fresh native build default | Effect |
| --- | --- | --- |
| `BUILD_TESTING` | `ON` | Builds/registers tests and header/application isolation checks |
| `GUI_BUILD_FLTK` | `OFF` | Adds the FLTK adapter executable and, with testing, its test executable |
| `GUI_BUILD_SDL` | `OFF` | Adds the SDL framebuffer host and, with testing, its dummy-driver test |
| `GUI_BUILD_REV` | `OFF` | Adds Rev and its adapter executable; requires the optional module toolchain and graphics dependencies |
| `GUI_REV_BUNDLED_DEPS` | `ON` on Windows, `OFF` elsewhere | With Rev enabled, builds preserved GLEW/FreeType sources instead of discovering installed development packages |
| `GUI_TEST_HOSTS` | `OFF` | Registers eligible Python HTTP/PTY tests and enabled FLTK/Rev display tests |
| `CMAKE_BUILD_TYPE` | Generator-dependent; set explicitly here | Selects Debug/Release for single-configuration generators |

The `GUI_*` switches above apply to native builds. The Emscripten branch
selects its own target and test before those options are declared.

CMake saves options, compilers, and dependency paths in each build directory's
`CMakeCache.txt`. Omitting a switch on a later configure command keeps its old
value; it does not restore the default. In particular, `BUILD_TESTING=OFF` stays
off until explicitly changed. Inspect cached values with:

```sh
cmake -N -LA build-core
```

Reconfigure to change ordinary feature switches or to discover a newly installed
Python/Node interpreter. Use a fresh directory when changing compiler,
generator, architecture, or SDK. No source files need to be moved or edited.

## Dependencies outside standard locations

Add the installation prefix to the configure command for the relevant profile,
for example `-DCMAKE_PREFIX_PATH=/absolute/dependency-prefix`. With several
prefixes, quote the semicolon-separated value so the shell passes one argument:
`'-DCMAKE_PREFIX_PATH=/absolute/fltk-prefix;/absolute/sdl-prefix'`.

For SDL2, `-DSDL2_DIR=/absolute/path/to/the/cmake-package-directory` can point
directly to the directory containing `SDL2Config.cmake` or `sdl2-config.cmake`.
It must be an SDL2 package exporting `SDL2::SDL2`, not just an include directory.
For FLTK, check the configured `FLTK_INCLUDE_DIR` and `FLTK_LIBRARIES` and the
configure diagnostics. Libraries must match the chosen compiler and target
architecture. Avoid reusing cached discovery paths after moving an installation.

## Multi-configuration generators

Visual Studio, Xcode, and Ninja Multi-Config select configuration at build/test
time. `CMAKE_BUILD_TYPE` does not choose it. Starting with a fresh
multi-configuration build directory, use:

```sh
cmake -S . -B build-multi -G "Ninja Multi-Config" -DBUILD_TESTING=ON
cmake --build build-multi --config Debug --parallel 2
ctest --test-dir build-multi -C Debug --output-on-failure
./build-multi/Debug/gui_example
```

Substitute your available generator. On Windows the executable has an `.exe`
suffix and shell path syntax differs. Pass the configuration-specific executable
to `host.py --executable` as well. For a multi-configuration Wasm build, pass
the directory actually containing both generated module files to `--wasm-dir`.

## Build troubleshooting

| Symptom | What to check |
| --- | --- |
| C++20 features are rejected | Confirm the compiler selected in the cache supports C++20; use a fresh directory after changing it. The project sets the language requirement. |
| CMake cannot find FLTK or SDL2 | Supply the development files and correct installation prefix. SDL needs its CMake config package. Disable the corresponding optional backend if you only need the core profile. |
| Compilation is killed without a C++ diagnostic | Reduce build parallelism and check available memory. |
| A previous SDK/compiler path is missing | Configure a fresh build directory using the current toolchain; a cached path is not a downloaded dependency. |
| CTest finds no tests | Check `BUILD_TESTING`, rerun configuration, and use `-C Debug` for a multi-configuration build. For Wasm, also check Node discovery. |
| Some tests are absent | Inspect `ctest --test-dir BUILD_DIRECTORY -N` and the [registration table](conformance.md#understand-the-inventory); Python, Node, platform, and backend flags control optional checks. |
| A compiled program cannot open its display or terminal | Compilation succeeded; follow the run-time requirements and [troubleshooting](running.md#troubleshooting). |
