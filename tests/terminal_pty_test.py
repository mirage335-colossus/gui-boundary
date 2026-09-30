#!/usr/bin/env python3
"""Exercise the real terminal executable through a bounded POSIX pseudoterminal."""

import errno
import fcntl
import os
import re
import select
import signal
import struct
import subprocess
import sys
import termios
import time


class Screen:
    def __init__(self, columns=80, rows=31):
        self.columns, self.rows = columns, rows
        self.cells = [[" "] * columns for _ in range(rows)]
        self.x = self.y = 0
        self.pending = b""

    def feed(self, chunk):
        self.pending += chunk
        while self.pending:
            if self.pending.startswith(b"\x1b"):
                match = re.match(rb"\x1b\[([0-9;?]*)([@-~])", self.pending)
                if not match:
                    if len(self.pending) > 128:
                        raise AssertionError("Unexpected terminal escape output")
                    return
                argument, command = match.groups()
                self.pending = self.pending[match.end():]
                if command in (b"H", b"f"):
                    values = [int(value or b"1") for value in argument.split(b";")]
                    self.y = max(0, min(self.rows - 1, values[0] - 1))
                    self.x = max(0, min(self.columns - 1, (values[1] if len(values) > 1 else 1) - 1))
                elif command == b"J" and argument == b"2":
                    self.cells = [[" "] * self.columns for _ in range(self.rows)]
                continue
            byte, self.pending = self.pending[0], self.pending[1:]
            if byte == 13:
                self.x = 0
            elif byte == 10:
                self.y = min(self.rows - 1, self.y + 1)
            elif 32 <= byte < 127:
                if self.x < self.columns:
                    self.cells[self.y][self.x] = chr(byte)
                self.x += 1
            else:
                raise AssertionError(f"Renderer emitted an unsafe cell byte: {byte}")

    def text(self):
        return "\n".join("".join(row) for row in self.cells)


class Terminal:
    def __init__(self, executable):
        self.master, self.slave = os.openpty()
        self.original_mode = termios.tcgetattr(self.slave)
        self.original_flags = fcntl.fcntl(self.slave, fcntl.F_GETFL)
        self.screen = Screen()
        self.raw = bytearray()
        self.resize(80, 31)
        self.process = subprocess.Popen([executable], stdin=self.slave, stdout=self.slave,
                                        stderr=self.slave, close_fds=True, start_new_session=True)

    def resize(self, columns, rows):
        fcntl.ioctl(self.slave, termios.TIOCSWINSZ, struct.pack("HHHH", rows, columns, 0, 0))
        self.screen = Screen(columns, rows)

    def send(self, value):
        os.write(self.master, value)

    def read(self, timeout=0.05):
        if not select.select([self.master], [], [], timeout)[0]:
            return
        try:
            chunk = os.read(self.master, 65536)
        except OSError as error:
            if error.errno == errno.EIO:
                return
            raise
        self.raw.extend(chunk)
        self.screen.feed(chunk)

    def until(self, predicate, description, timeout=4):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self.read()
            if predicate(self.screen.text()):
                return
            if self.process.poll() is not None:
                break
        raise AssertionError(description + "\nCurrent terminal:\n" + self.screen.text())

    def finished(self, timeout=4):
        deadline = time.monotonic() + timeout
        while self.process.poll() is None and time.monotonic() < deadline:
            self.read()
        assert self.process.poll() == 0, "Terminal host did not exit successfully"
        while select.select([self.master], [], [], 0)[0]:
            self.read(0)
        assert termios.tcgetattr(self.slave) == self.original_mode, "Terminal modes were not restored"
        assert fcntl.fcntl(self.slave, fcntl.F_GETFL) == self.original_flags, "Descriptor flags were not restored"
        assert b"\x1b[?25h" in self.raw and b"\x1b[?1049l" in self.raw, "Terminal screen/cursor cleanup missing"

    def close(self):
        if self.process.poll() is None:
            self.process.kill()
            self.process.wait(timeout=2)
        os.close(self.master)
        os.close(self.slave)


def interaction(executable):
    terminal = Terminal(executable)
    try:
        terminal.until(lambda text: "Shared controls" in text and "First" in text, "Application did not render")
        assert not termios.tcgetattr(terminal.slave)[3] & termios.ICANON, "Host never entered interactive terminal mode"
        terminal.send(b"\t\x01PTY row")
        terminal.until(lambda text: "PTY row|" in text, "Keyboard editing failed")
        terminal.send(b"\t ")
        terminal.until(lambda text: "[x] Enable action" in text, "Toggle failed")
        terminal.send(b"\t\r")
        terminal.until(lambda text: text.count("PTY row") >= 2, "Button did not add the edited row")
        terminal.send(b"\x1b[Z\x1b[Z\r")
        terminal.until(lambda text: "Enter text" in text and "Cancel" in text, "Prompt was not rendered")
        terminal.send(b"PTY reply\r")
        terminal.until(lambda text: "PTY reply" in text and "Cancel" not in text, "Prompt did not complete")
        terminal.send(b"\rBatched reply\r")
        terminal.until(lambda text: "Batched reply" in text and "Cancel" not in text, "Batched prompt input reached the background editor")
        assert "PTY row" in terminal.screen.text(), "Prompt typeahead changed the background editor"
        terminal.send(b"\t\t\t\r")
        terminal.until(lambda text: "Clear rows" in text, "Menu did not open")
        terminal.send(b"\r")
        terminal.until(lambda text: "No rows" in text, "Menu action did not clear records")
        terminal.send(b"\t\t\t\r")
        terminal.until(lambda text: "A shared modal view" in text, "Shared details overlay did not open")
        terminal.send(b"\x1b")
        terminal.until(lambda text: "A shared modal view" not in text and "Show details" in text, "Escape did not close the shared overlay")
        terminal.send(b"\x0e")
        terminal.until(lambda text: "Shared controls" not in text and "[Other page]" in text, "Page selection failed")
        terminal.send(b"\x10")
        terminal.until(lambda text: "Shared controls" in text, "Returning to controls failed")
        terminal.resize(40, 15)
        terminal.until(lambda text: "Boundary Workshop" in text, "Resize did not produce a bounded frame")
        terminal.send(b"\x11")
        terminal.finished()
    finally:
        terminal.close()


def signal_restoration(executable):
    terminal = Terminal(executable)
    try:
        terminal.until(lambda text: "Shared controls" in text, "Signal fixture did not start")
        terminal.process.send_signal(signal.SIGTERM)
        terminal.finished()
    finally:
        terminal.close()


def output_backpressure(executable):
    terminal = Terminal(executable)
    try:
        terminal.until(lambda text: "Shared controls" in text, "Backpressure fixture did not start")
        # Stop draining output while repeated host resizes create new frames.
        # Input and close must continue independently of a full output channel.
        for index in range(50):
            fcntl.ioctl(terminal.slave, termios.TIOCSWINSZ,
                        struct.pack("HHHH", 31, 80 + index % 2, 0, 0))
            time.sleep(0.015)
        terminal.send(b"\x11")
        terminal.process.wait(timeout=3)
        assert terminal.process.returncode == 0, "Backpressure prevented graceful close"
        assert termios.tcgetattr(terminal.slave) == terminal.original_mode, "Backpressure prevented mode restoration"
        assert fcntl.fcntl(terminal.slave, fcntl.F_GETFL) == terminal.original_flags, "Backpressure leaked descriptor flags"
    finally:
        terminal.close()


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: terminal_pty_test.py /path/to/gui_terminal")
    interaction(sys.argv[1])
    signal_restoration(sys.argv[1])
    output_backpressure(sys.argv[1])
    print("Terminal PTY interaction, signal restoration and backpressure checks passed")
