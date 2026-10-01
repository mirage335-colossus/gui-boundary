#!/usr/bin/env python3
"""Capture failure checks without an X server, toolkits, or browser installed."""

from contextlib import contextmanager
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


SPEC = importlib.util.spec_from_file_location(
    "screenshots", Path(__file__).resolve().parents[1] / "tools/screenshots.py")
screenshots = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(screenshots)


class Clock:
    def __init__(self):
        self.now = 0.0

    def monotonic(self):
        return self.now

    def sleep(self, seconds):
        self.now += seconds


@contextmanager
def running_process(*_args):
    child = mock.Mock()
    child.poll.return_value = None
    yield child


class ScreenshotTests(unittest.TestCase):
    def test_startup_crash_is_reported_before_a_screenshot_is_accepted(self):
        with tempfile.TemporaryDirectory() as tmp, \
                mock.patch.object(screenshots, "windows", return_value=set()):
            with self.assertRaisesRegex(RuntimeError, r"exited before capture \(status 7\)"):
                screenshots.capture_window(
                    [sys.executable, "-c", "raise SystemExit(7)"], "rev", Path(tmp))
            self.assertFalse((Path(tmp) / "rev.png").exists())

    def test_static_blank_window_never_counts_as_a_finished_render(self):
        clock = Clock()
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)

            def capture(_args, **_kwargs):
                (directory / "rev.png").write_bytes(b"unchanging blank pixels")

            with mock.patch.object(screenshots, "process", running_process), \
                    mock.patch.object(screenshots, "windows", side_effect=lambda: {"new"} if clock.now else set()), \
                    mock.patch.object(screenshots, "command", side_effect=capture), \
                    mock.patch.object(screenshots, "image_info", return_value=(640, 480, 1)), \
                    mock.patch.object(screenshots.time, "monotonic", clock.monotonic), \
                    mock.patch.object(screenshots.time, "sleep", clock.sleep):
                with self.assertRaisesRegex(RuntimeError, "no stable, painted window"):
                    screenshots.capture_window(["unused"], "rev", directory)
            self.assertGreaterEqual(clock.now, 30)

    def test_discovery_ignores_existing_windows_and_waits_out_ambiguity(self):
        clock = Clock()
        sightings = iter(({"existing"}, {"existing", "new", "transient"}))

        def windows():
            return next(sightings, {"existing", "new"})

        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            captures = []

            def capture(args, **_kwargs):
                captures.append(args[args.index("-window") + 1])
                (directory / "rev.png").write_bytes(b"stable painted pixels")

            with mock.patch.object(screenshots, "process", running_process), \
                    mock.patch.object(screenshots, "windows", side_effect=windows), \
                    mock.patch.object(screenshots, "command", side_effect=capture), \
                    mock.patch.object(screenshots, "image_info", return_value=(640, 480, 20)), \
                    mock.patch.object(screenshots.time, "monotonic", clock.monotonic), \
                    mock.patch.object(screenshots.time, "sleep", clock.sleep):
                screenshots.capture_window(["unused"], "rev", directory)
            self.assertTrue(captures)
            self.assertEqual(set(captures), {"new"})
            self.assertGreaterEqual(clock.now, 1.2)

    def test_failed_capture_preserves_complete_previous_gallery(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            build = directory / "build"
            output = directory / "gallery"
            output.mkdir()
            expected = {f"{backend}.png": f"old {backend}".encode()
                        for backend in screenshots.BACKENDS}
            expected["BUILD.txt"] = b"old provenance"
            for name, data in expected.items():
                (output / name).write_bytes(data)
            for relative in [*(f"native/{name}" for name in screenshots.TARGETS),
                             "wasm/gui_web_wasm.js", "wasm/gui_web_wasm.wasm"]:
                path = build / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.touch()

            def capture(_args, name, staging):
                (staging / f"{name}.png").write_bytes(b"new capture")
                if name == "sdl":
                    raise RuntimeError("SDL startup failed")

            argv = ["screenshots.py", "--skip-build", "--build-dir", str(build),
                    "--output", str(output)]
            with mock.patch.object(sys, "argv", argv), \
                    mock.patch.dict(screenshots.os.environ, {"DISPLAY": ":test"}), \
                    mock.patch.object(screenshots.shutil, "which", return_value="/available/tool"), \
                    mock.patch.object(screenshots.importlib.util, "find_spec", return_value=object()), \
                    mock.patch.object(screenshots, "command"), \
                    mock.patch.object(screenshots, "capture_window", side_effect=capture):
                with self.assertRaisesRegex(RuntimeError, "SDL startup failed"):
                    screenshots.main()
            self.assertEqual({path.name: path.read_bytes() for path in output.iterdir()}, expected)
            self.assertFalse(list(directory.glob(".screenshots-*")))

    def test_window_inspection_failure_is_not_treated_as_an_empty_display(self):
        failure = subprocess.CompletedProcess([], 2, stdout="", stderr="cannot open display")
        with mock.patch.object(screenshots.subprocess, "run", return_value=failure):
            with self.assertRaisesRegex(RuntimeError, "Cannot inspect X11 windows: cannot open display"):
                screenshots.windows()


if __name__ == "__main__":
    unittest.main()
