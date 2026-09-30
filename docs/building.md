# Building the example

Run commands from the repository root, where `CMakeLists.txt` lives. This guide
uses separate build directories so choosing an optional backend cannot silently
change another profile. After building, use the [running guide](running.md) to
launch an interface and the [coverage guide](conformance.md) to choose checks.

## Prerequisites

| Requirement | Needed for | How the build uses it |
| --- | --- | --- |
| CMake 3.20 or newer | Every build | Configures targets and CTest |
| C++20 compiler, standard library, and thread support | Every build | Public boundary and application; compiler warnings are treated as errors |
| Generator's build tool | Every build | For example, Make or Ninja for a corresponding single-configuration generator |
| Python 3 | Hosted browser and Python checks | Standard library only; no `pip` packages |
| Node | DOM renderer fixture checks and compiled Wasm checks | No `npm install` step |
| FLTK development headers and libraries | Native-widget profile | CMake's `FindFLTK`; FLTK 1.3 was used for the recorded validation |
| SDL2 development headers, libraries, and CMake package | Interactive framebuffer window | `find_package(SDL2 REQUIRED CONFIG)` and `SDL2::SDL2` |
| Emscripten toolchain available in the shell | Browser-only Wasm compilation | `emcmake` configures the C++ compiler and linker |
| Desktop display, or Xvfb on Linux | FLTK execution/tests; interactive SDL window | Required at run time, not for the default build |

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
  -DGUI_BUILD_FLTK=OFF -DGUI_BUILD_SDL=OFF -DGUI_TEST_HOSTS=OFF
cmake --build build-core --parallel 2
ctest --test-dir build-core --output-on-failure
```

The executables are `build-core/gui_example`, `build-core/gui_terminal`,
`build-core/gui_web_demo`, and `build-core/gui_framebuffer`. The browser process
speaks a line-oriented protocol; launch it through the Python host to get a UI.
Compilation also checks every public header and the shared application without
toolkit includes. Increase `--parallel 2` if your machine has sufficient memory;
use `--parallel 1` if compiler processes are being killed for memory pressure.

## Native widgets (FLTK)

Install or otherwise supply FLTK development files, then configure this profile:

```sh
cmake -S . -B build-fltk -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
  -DGUI_BUILD_FLTK=ON -DGUI_BUILD_SDL=OFF -DGUI_TEST_HOSTS=OFF
cmake --build build-fltk --parallel 2
ctest --test-dir build-fltk --output-on-failure
```

This adds `build-fltk/gui_fltk_demo` and compiles `fltk_test`. With
`GUI_TEST_HOSTS=OFF`, CTest does not run the FLTK display test; the other tests can
still run without a display. See [host-inclusive testing](#all-native-backends-and-host-checks)
to enable that test. The example does not require FLTK's OpenGL, forms, image
libraries, or `fluid` tool.

## Framebuffer window (SDL2)

Install or otherwise supply SDL2 development files, then configure this profile:

```sh
cmake -S . -B build-sdl -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
  -DGUI_BUILD_FLTK=OFF -DGUI_BUILD_SDL=ON -DGUI_TEST_HOSTS=OFF
cmake --build build-sdl --parallel 2
ctest --test-dir build-sdl --output-on-failure
```

This adds `build-sdl/gui_framebuffer_sdl`. The shared software renderer creates
the image; SDL supplies the window, event input, and texture upload. CTest runs
its self-test with `SDL_VIDEODRIVER=dummy`, even when `GUI_TEST_HOSTS` is off. A
normal interactive run uses your desktop video driver. SDL2, rather than SDL3,
is the dependency selected by this project's CMake file.

## All native backends and host checks

On a POSIX system with Python, FLTK, SDL2, and a working display, this profile
builds all native targets and enables local HTTP/process, PTY, and FLTK checks.
Install Node as well to include the DOM renderer fixture test.

```sh
cmake -S . -B build-all -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
  -DGUI_BUILD_FLTK=ON -DGUI_BUILD_SDL=ON -DGUI_TEST_HOSTS=ON
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
or turn on the FLTK/SDL build options. Test registration and what each check
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
| `GUI_TEST_HOSTS` | `OFF` | Registers eligible Python HTTP/PTY tests and the enabled FLTK display test |
| `CMAKE_BUILD_TYPE` | Generator-dependent; set explicitly here | Selects Debug/Release for single-configuration generators |

The three `GUI_*` switches above apply to native builds. The Emscripten branch
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
