# Rebuilding Rev from local sources

The Rev backend is optional. `GUI_BUILD_REV=OFF` leaves the ordinary C++20
example independent of Rev, its C++23 modules, and its graphics dependencies.
With Rev enabled, the build uses the complete local source package plus the
operating system's development tools and graphics stack. It does not clone an
upstream repository or bootstrap a package manager.

## What is preserved

The [Rev validation record](conformance.md#rev-implementation-validation) records
the Linux environment, offline builds and native checks actually exercised.

| Local input | Contents and purpose |
| --- | --- |
| [`third_party/rev/`](../third_party/rev/) | Selected Rev toolkit modules, compatibility fixes, GLM/NanoSVG headers, fonts, shaders, icons and notices |
| [`third_party/rev-deps/`](../third_party/rev-deps/) | Complete GLEW 2.3.1 and FreeType 2.14.3 source archives, version/checksum metadata and license notices |
| [`cmake/`](../cmake/) | Offline dependency builds, platform module preparation, resource embedding and integrity checks |
| [`backends/rev/`](../backends/rev/) | Generic adapter and host for the shared application |

`UPSTREAM.json` files record provenance; their URLs are not download inputs.
`PATCHES.md` records the toolkit fixes. `SHA256SUMS` manifests cover the preserved
source package. Verify it from the repository root:

```sh
cmake -P tools/verify-rev.cmake
```

Every Rev configuration also runs verification. A missing archive, altered
source, or unexpected file is an error. Restore the matching source package
when verification fails; the build has no online fallback. Keep the notices
with redistributed files and consult the preserved Rev permission record and
individual dependency licenses.

`GUI_REV_BUNDLED_DEPS=ON` builds static GLEW and FreeType libraries from the
included archives. CMake extracts them into the build directory. Generated GLEW
sources are already present, and FreeType's CMake inputs are complete. The
profile needs no Git, vcpkg, Perl, autotools, Python package, or source generator
download. External zlib, bzip2, PNG, HarfBuzz and Brotli dependencies are disabled;
FreeType's own internal inflater remains available. Fonts and artwork are
embedded by CMake, so the resulting program needs no run-time asset directory.

## Debian Bookworm prerequisites

The selected Linux recipe uses Clang 19, its matching dependency scanner,
Ninja, CMake 3.28 or newer, and the system X11/OpenGL development libraries.
Debian publishes [Clang 19](https://packages.debian.org/bookworm/clang-19) and
[clang-tools-19](https://packages.debian.org/bookworm/clang-tools-19) for Bookworm.
Bookworm's original CMake is too old; the Debian
[Bookworm-backports CMake package](https://packages.debian.org/bookworm-backports/cmake)
provides a suitable version.

Using configured Debian package repositories, install the toolchain and system
graphics prerequisites:

```sh
sudo apt-get update
sudo apt-get install build-essential clang-19 clang-tools-19 ninja-build \
  libgl-dev libglx-dev libx11-dev libxrandr-dev libxext-dev
sudo apt-get install -t bookworm-backports cmake
```

The last command assumes that the Debian `bookworm-backports` suite is configured
and available. Debian states that its updates ended on August 9, 2026 in the
[backports instructions](https://backports.debian.org/Instructions/). Preserve
the downloaded Debian packages, their transitive dependencies and package
metadata for a durable rebuild; use the appropriate Debian archive or snapshot
when the live suite is retired. Do not substitute a third-party compiler script
or a checkout of a build-tool repository. A package list alone is not an offline
copy of the packages.

For Linux display tests, also install `xvfb`, `xauth`, and a suitable Mesa OpenGL
software driver such as `libgl1-mesa-dri`. A real desktop uses its installed GPU
driver. Optional Python and Node checks follow the ordinary
[test registration rules](conformance.md#understand-the-inventory).

The commands below build GLEW and FreeType locally. To use distribution versions
instead, install `libglew-dev` and `libfreetype-dev`, then explicitly configure
`GUI_REV_BUNDLED_DEPS=OFF`. Either route obtains external prerequisites only from
distribution packages; the application build itself performs no download.

## Configure, build and run on Linux

Start in the repository root. Choose a fresh directory when changing compiler,
generator or dependency profile:

```sh
cmake -P tools/verify-rev.cmake
cmake -S . -B build-rev -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang-19 -DCMAKE_CXX_COMPILER=clang++-19 \
  -DGUI_BUILD_REV=ON -DGUI_REV_BUNDLED_DEPS=ON -DBUILD_TESTING=ON \
  -DGUI_BUILD_FLTK=OFF -DGUI_BUILD_SDL=OFF -DGUI_TEST_HOSTS=ON
cmake --build build-rev --parallel 2
xvfb-run -a env LIBGL_ALWAYS_SOFTWARE=1 ctest --test-dir build-rev --output-on-failure -R '^rev(_clipboard)?$'
./build-rev/gui_rev_demo
```

The final command opens the interactive application in the current desktop.
Without a desktop, the preceding Xvfb command supplies an offscreen display for
the test. To run the test in an existing graphical session, omit the Xvfb
wrapper. `./build-rev/rev_test build-rev/rev.ppm` also saves the initial native
window through GL readback for visual inspection.

Both the native toolkit and adapter require C++23 modules internally. The
configuration accepts Clang 19+, GCC 15+, or MSVC 19.36+; these are permitted
toolchains, not a claim that every version has been tested. GCC 14 is rejected
because of a module compiler defect for this snapshot. Clang requires its
matching `clang-scan-deps`, supplied by `clang-tools-19` in the recipe above.

## Windows with Microsoft tools

Install Visual Studio 2022 Build Tools 17.6 or newer with the C++ desktop build
tools, an appropriate Windows SDK, and C++ CMake tools (CMake 3.28+ and Ninja).
Preserve a Microsoft offline installer layout when the build machine must be
reconstructed without contacting the installer service. Microsoft documents
[creating an offline installation layout](https://learn.microsoft.com/en-us/visualstudio/install/create-an-offline-installation-of-visual-studio?view=vs-2022).
Include the selected C++ workload and SDK components in that layout.

Open an **x64 Native Tools Command Prompt** for the installed tools and change
to the example root. These are `cmd.exe` commands; the caret continues a line:

```bat
cmake -P tools\verify-rev.cmake
cmake -S . -B build-rev-win -G Ninja -DCMAKE_BUILD_TYPE=Release ^
  -DGUI_BUILD_REV=ON -DGUI_REV_BUNDLED_DEPS=ON -DBUILD_TESTING=ON ^
  -DGUI_BUILD_FLTK=OFF -DGUI_BUILD_SDL=OFF -DGUI_TEST_HOSTS=ON
cmake --build build-rev-win --parallel 2
ctest --test-dir build-rev-win --output-on-failure -R "^rev(_clipboard)?$"
build-rev-win\gui_rev_demo.exe
```

The bundled C libraries compile with the same Microsoft toolchain. Windows SDK
libraries supply the windowing and OpenGL entry points; no vcpkg registry or
prebuilt library from a third-party repository is used. A vendor graphics driver
providing OpenGL 4.4, or 4.3 plus `GL_ARB_buffer_storage`, is still required.
Windows' basic OpenGL implementation does not meet that requirement.

Alternatively, configure with `-G "Visual Studio 17 2022" -A x64` in a separate
directory, then build with `--config Release`, test with `-C Release`, and run
the executable from that directory's `Release` subdirectory. Ninja, Ninja
Multi-Config and the supported Visual Studio generators are accepted; ordinary
Makefile generators do not provide this integration's module dependency scan.
The tests replace clipboard contents; run them on a disposable test desktop.

This is the implemented reconstruction procedure, **not a completed Windows
qualification claim**. A Windows machine must still compile the preserved
dependencies and adapter, pass the native display test, and exercise actual
desktop input before that platform is qualified. The Linux evidence does not
establish Windows driver, clipboard, focus, scaling or input-method behavior.

## Prove the build does not need a network

Install the permitted platform prerequisites first. Copy this entire source
package, including both `third_party` directories, into a clean working
directory. Use a new build directory so no existing toolkit objects, extracted
dependency trees or CMake cache can supply missing inputs.

On Linux where unprivileged user/network namespaces are enabled, the following
runs configuration and compilation in a network namespace without an external
network interface:

```sh
unshare -Urn cmake -S . -B build-rev-offline -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-19 \
  -DCMAKE_CXX_COMPILER=clang++-19 -DGUI_BUILD_REV=ON \
  -DGUI_REV_BUNDLED_DEPS=ON -DBUILD_TESTING=ON -DGUI_TEST_HOSTS=ON
unshare -Urn cmake --build build-rev-offline --parallel 2
```

Where namespace creation is disabled, use a disconnected VM or a host firewall
that denies build-process networking. For Windows, disconnect the reconstructed
VM after installing the retained Microsoft tools and before configuring a fresh
build. Run the native display checks afterward in the target graphical session.
Record the operating system, compiler, generator, graphics implementation and
the exact checks that passed. A successful build on Debian 13 is useful evidence
but does not replace a fresh Bookworm installation test.

## Maintaining the preserved inputs

Application features belong in `examples/application.hpp`; they do not require
changes to the toolkit or native adapter. When a toolkit defect does require a
generic fix, review the source change, update its patch/provenance record, and
regenerate the manifests deliberately:

```sh
cmake -DGUI_REV_ACCEPT_SOURCE_CHANGES=ON -P tools/update-rev-manifests.cmake
cmake -P tools/verify-rev.cmake
```

Do not refresh manifests merely to silence a failed integrity check. Keep the
dependency archives, notices, build recipes and updated checksums together in
the source release. `cmake --install build-rev --prefix DESTINATION` installs
the demo and accompanying dependency notices; an installed binary directory is
not a replacement for the complete rebuildable source package.
