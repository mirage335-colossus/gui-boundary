#!/usr/bin/env python3
"""Build and capture all seven visual backends on Linux. See docs/screenshots.md."""

import argparse
from contextlib import contextmanager
import hashlib
import importlib.util
import os
from pathlib import Path
import platform
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parents[1]
BACKENDS = ("fltk", "rev", "sdl", "tui", "framebuffer", "web-hosted", "web-wasm")
TARGETS = ("gui_fltk_demo", "gui_rev_demo", "gui_framebuffer_sdl", "gui_terminal",
           "gui_framebuffer", "gui_web_demo")


def command(args, **kwargs):
    try:
        return subprocess.run([str(arg) for arg in args], check=True, timeout=30, **kwargs)
    except subprocess.CalledProcessError as error:
        if error.stderr:
            detail = error.stderr.decode(errors="replace") if isinstance(error.stderr, bytes) else error.stderr
            print(detail.strip(), file=sys.stderr)
        raise


def text(args):
    return command(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True).stdout.strip()


def build(build_dir, jobs):
    native, wasm = build_dir / "native", build_dir / "wasm"
    env = os.environ.copy()
    # emcmake selects its own compiler; a user's native CC/CXX must not override it.
    env.pop("CC", None)
    env.pop("CXX", None)
    steps = [
        # A fresh configuration prevents inherited cache options or a compiler
        # change from silently disabling a backend while old binaries remain.
        ["cmake", "--fresh", "-S", ROOT, "-B", native, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
         "-DCMAKE_C_COMPILER=clang-19", "-DCMAKE_CXX_COMPILER=clang++-19",
         "-DGUI_BUILD_FLTK=ON", "-DGUI_BUILD_SDL=ON", "-DGUI_BUILD_REV=ON",
         "-DGUI_REV_BUNDLED_DEPS=ON", "-DBUILD_TESTING=OFF"],
        ["cmake", "--build", native, "--parallel", jobs, "--target", *TARGETS],
        ["emcmake", "cmake", "--fresh", "-S", ROOT, "-B", wasm, "-G", "Ninja",
         "-DCMAKE_BUILD_TYPE=Release", "-DBUILD_TESTING=OFF"],
        ["cmake", "--build", wasm, "--parallel", jobs, "--target", "gui_web_wasm"],
    ]
    build_dir.mkdir(parents=True, exist_ok=True)
    log_path = build_dir / "screenshots-build.log"
    with log_path.open("w") as log:
        for step in steps:
            print("+", " ".join(map(str, step)), flush=True)
            try:
                subprocess.run(list(map(str, step)), check=True, timeout=1200,
                               stdout=log, stderr=subprocess.STDOUT, env=env)
            except (subprocess.CalledProcessError, subprocess.TimeoutExpired):
                log.flush()
                print(log_path.read_text()[-12000:], file=sys.stderr)
                raise RuntimeError(f"Build failed; complete log: {log_path}") from None


@contextmanager
def process(args, log_path):
    """Own only this process group, including xterm's terminal child."""
    with log_path.open("w") as log:
        child = subprocess.Popen(list(map(str, args)), stdout=log, stderr=log,
                                 start_new_session=True)
        try:
            yield child
        except Exception:
            log.flush()
            print(log_path.read_text()[-4000:], file=sys.stderr)
            raise
        finally:
            # A terminal child may still exist after the immediate parent exited.
            for sig in (signal.SIGTERM, signal.SIGKILL):
                try:
                    os.killpg(child.pid, sig)
                except ProcessLookupError:
                    break
                try:
                    child.wait(timeout=2)
                    if sig == signal.SIGTERM:
                        continue
                except subprocess.TimeoutExpired:
                    pass
            child.wait(timeout=2)


def windows():
    result = subprocess.run(["xdotool", "search", "--onlyvisible", "--maxdepth", "1",
                             "--name", "."], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            text=True, timeout=5)
    if result.returncode not in (0, 1):
        raise RuntimeError(f"Cannot inspect X11 windows: {result.stderr.strip()}")
    return set(result.stdout.split())


def image_info(path):
    width, height, colors = text(["identify", "-format", "%w %h %k", path]).split()
    return int(width), int(height), int(colors)


def capture_window(args, name, directory):
    before = windows()
    output = directory / f"{name}.png"
    with process(args, directory / f"{name}.log") as child:
        deadline = time.monotonic() + 30
        previous, since = None, time.monotonic()
        while time.monotonic() < deadline:
            if child.poll() is not None:
                raise RuntimeError(f"{name} exited before capture (status {child.returncode})")
            candidates = windows() - before
            if len(candidates) == 1:
                window = candidates.pop()
                # Strip creation timestamps so stability compares pixels, not metadata.
                command(["import", "-silent", "-window", window, "-strip", output],
                        stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
                width, height, colors = image_info(output)
                expected_size = (width, height) == (640, 480) if name != "tui" else width >= 640 and height >= 496
                fingerprint = hashlib.sha256(output.read_bytes()).digest()
                if expected_size and colors >= 4:
                    if fingerprint != previous:
                        previous, since = fingerprint, time.monotonic()
                    elif time.monotonic() - since >= 1:
                        return
                else:
                    previous = None
            time.sleep(.2)
        raise RuntimeError(f"{name}: no stable, painted window of the expected size within 30 seconds")


@contextmanager
def web_host(native, wasm):
    # Reuse the actual host and bind port zero; no duplicated server or port race.
    spec = importlib.util.spec_from_file_location("screenshot_web_host", ROOT / "backends/web/host.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    with module.Host(("127.0.0.1", 0), native / "gui_web_demo", wasm) as host:
        thread = threading.Thread(target=host.serve_forever, daemon=True)
        thread.start()
        try:
            yield f"http://{host.authority}/"
        finally:
            host.shutdown()
            thread.join(timeout=5)


def capture_web(native, wasm, directory):
    from selenium import webdriver
    from selenium.webdriver.chrome.service import Service
    from selenium.webdriver.common.by import By

    options = webdriver.ChromeOptions()
    options.binary_location = shutil.which("chromium")
    for argument in ("--headless", "--disable-dev-shm-usage", "--force-device-scale-factor=1",
                     "--no-first-run", "--no-default-browser-check"):
        options.add_argument(argument)
    # The release job runs in an isolated root-owned Debian container.
    if os.geteuid() == 0:
        options.add_argument("--no-sandbox")
    # Explicit distribution paths prevent Selenium Manager downloading a browser/driver.
    service = Service(executable_path=shutil.which("chromedriver"))
    with web_host(native, wasm) as url, webdriver.Chrome(service=service, options=options) as driver:
        driver.set_page_load_timeout(30)
        driver.set_script_timeout(10)
        driver.execute_cdp_cmd("Page.addScriptToEvaluateOnNewDocument", {"source": """
            window.screenshotPendingFetches=0;
            const originalFetch=window.fetch;
            window.fetch=async (...args)=>{
                ++window.screenshotPendingFetches;
                try{return await originalFetch(...args);}
                finally{--window.screenshotPendingFetches;}
            };
        """})
        driver.execute_cdp_cmd("Emulation.setDeviceMetricsOverride",
                               {"width": 640, "height": 512, "deviceScaleFactor": 1, "mobile": False})
        for name, query, status in (("web-hosted", "", "Hosted C++"),
                                    ("web-wasm", "?mode=wasm", "Wasm")):
            print(f"Capturing {name}", flush=True)
            driver.get(url + query)
            deadline = time.monotonic() + 30
            previous, since = None, time.monotonic()
            while time.monotonic() < deadline:
                state = driver.execute_script("""
                    const stage=document.querySelector('#stage');
                    return {status:document.querySelector('#status').textContent,
                        ready:window.screenshotPendingFetches===0 && document.fonts.status==='loaded' && stage.clientWidth===640 &&
                            stage.clientHeight===480 && !!stage.querySelector('.widget') &&
                            [...stage.querySelectorAll('img')].every(i=>i.complete && i.naturalWidth>0),
                        html:stage.innerHTML};
                """)
                if state["status"].startswith("Could not start:"):
                    raise RuntimeError(f"{name}: {state['status']}")
                if state["ready"] and state["status"].startswith(status + " ·"):
                    png = driver.find_element(By.ID, "viewport").screenshot_as_png
                    fingerprint = hashlib.sha256(state["html"].encode() + png).digest()
                    if fingerprint != previous:
                        previous, since = fingerprint, time.monotonic()
                    elif time.monotonic() - since >= 1:
                        (directory / f"{name}.png").write_bytes(png)
                        break
                else:
                    previous = None
                time.sleep(.2)
            else:
                raise RuntimeError(f"{name}: render did not settle: {state['status']}")
            # Release hosted session before starting a genuinely independent Wasm page.
            driver.get("about:blank")


def provenance(directory, build_dir, skip_build):
    commit = text(["git", "-C", ROOT, "rev-parse", "HEAD"])
    dirty = bool(text(["git", "-C", ROOT, "status", "--porcelain", "--untracked-files=normal"]))
    lines = ["GUI boundary backend screenshots", f"Commit: {commit}", f"Working tree dirty: {dirty}",
             f"OS: {platform.freedesktop_os_release().get('PRETTY_NAME', platform.platform())}",
             f"Architecture: {platform.machine()}", f"Build directory: {build_dir}",
             f"Reused existing binaries (--skip-build): {skip_build}",
             "Native profile: Release, Clang 19, FLTK + SDL + Rev (bundled GLEW/FreeType)",
             "Wasm profile: Release, distribution Emscripten",
             "Application: fresh initial state, 640x480 logical pixels, scale 1",
             "X11: use Xvfb 1280x900x24, 96 DPI; native window client pixels",
             "Terminal: xterm, 80x31 cells, DejaVu Sans Mono 10pt; includes status row",
             "Browser: Chromium, 640x512 viewport; captured 640x480 application area",
             f"LIBGL_ALWAYS_SOFTWARE: {os.environ.get('LIBGL_ALWAYS_SOFTWARE', '(unset)')}", ""]
    for tool in ("cmake", "clang++-19", "emcc", "chromium", "chromedriver", "xterm"):
        lines.append(text([tool, "-version" if tool == "xterm" else "--version"]).splitlines()[0])
    import selenium
    lines.append(f"Selenium: {selenium.__version__}")
    packages = ["libfltk1.3-dev", "libsdl2-dev", "libgl1-mesa-dri", "xvfb", "imagemagick",
                "fonts-dejavu-core", "fonts-liberation"]
    result = subprocess.run(["dpkg-query", "-W", "-f=${binary:Package} ${Version}\n", *packages],
                            capture_output=True, text=True, timeout=10)
    lines.extend(["", "Distribution packages (where installed):", result.stdout.strip(), "", "Images (SHA256):"])
    for name in BACKENDS:
        path = directory / f"{name}.png"
        width, height, colors = image_info(path)
        if colors < 4 or (name != "tui" and (width, height) != (640, 480)):
            raise RuntimeError(f"Invalid capture: {path.name} ({width}x{height}, {colors} colors)")
        lines.append(f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}  {width}x{height}")
    (directory / "BUILD.txt").write_text("\n".join(lines) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "dist/screenshots")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build-screenshots")
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--skip-build", action="store_true", help="Capture existing native/wasm builds; recorded in BUILD.txt")
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    if sys.platform != "linux" or not os.environ.get("DISPLAY"):
        parser.error("Run on Linux under Xvfb as shown in docs/screenshots.md")
    required = ["xdotool", "import", "identify", "convert", "xterm", "chromium", "chromedriver",
                "git", "cmake", "clang++-19", "emcc", "dpkg-query"]
    if not args.skip_build:
        required += ["clang-19", "ninja", "emcmake"]
    missing = [tool for tool in required if not shutil.which(tool)]
    if importlib.util.find_spec("selenium") is None:
        missing.append("python3-selenium")
    if missing:
        parser.error("Missing prerequisites: " + ", ".join(missing) + "; see docs/screenshots.md")
    os.environ["LC_ALL"] = "C.UTF-8"
    os.environ["SDL_VIDEODRIVER"] = "x11"
    args.build_dir, args.output = args.build_dir.resolve(), args.output.resolve()
    if not args.skip_build:
        build(args.build_dir, args.jobs)
    native, wasm = args.build_dir / "native", args.build_dir / "wasm"
    for path in [*(native / name for name in TARGETS), wasm / "gui_web_wasm.js", wasm / "gui_web_wasm.wasm"]:
        if not path.is_file():
            raise RuntimeError(f"Missing build output: {path}")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    # A failed run never replaces the previous successful gallery with partial output.
    with tempfile.TemporaryDirectory(prefix=".screenshots-", dir=args.output.parent) as staging:
        directory = Path(staging)
        command(["xdotool", "mousemove", "1200", "850"])
        for name, target in zip(("fltk", "rev", "sdl"), TARGETS):
            print(f"Capturing {name}", flush=True)
            capture_window([native / target], name, directory)
        print("Capturing tui", flush=True)
        capture_window(["xterm", "-geometry", "80x31+0+0", "-fa", "DejaVu Sans Mono", "-fs", "10",
                        "+sb", "-xrm", "XTerm*cursorBlink:false", "-e", native / "gui_terminal"], "tui", directory)
        print("Capturing framebuffer", flush=True)
        command([native / "gui_framebuffer", "--output", directory / "framebuffer.ppm"])
        command(["convert", directory / "framebuffer.ppm", "-strip", directory / "framebuffer.png"])
        capture_web(native, wasm, directory)
        provenance(directory, args.build_dir, args.skip_build)
        args.output.mkdir(parents=True, exist_ok=True)
        for name in [*(f"{backend}.png" for backend in BACKENDS), "BUILD.txt"]:
            (directory / name).replace(args.output / name)
    print(f"Captured all seven backends: {args.output}", flush=True)


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError, subprocess.SubprocessError) as error:
        sys.exit(f"Screenshot generation failed: {error}")
