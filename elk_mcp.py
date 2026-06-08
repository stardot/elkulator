#!/usr/bin/env python3
"""
elk_mcp.py — MCP server for the Elkulator Acorn Electron emulator.

Elkulator must be started with the --control flag pointing to a socket path:

    ./elkulator --control /tmp/elkulator.sock

The server then connects to that socket and exposes two tools to Claude:

    type_basic(program)   — inject a BASIC program and RUN it
    read_output(timeout)  — read text the Electron has printed since last call

The socket is a raw byte stream: bytes sent → virtual keypresses,
bytes received ← OSWRCH output.  No framing protocol needed.
"""

import os
import socket
import time
import threading

from mcp.server.fastmcp import FastMCP

# ------------------------------------------------------------------ #
# Configuration                                                        #
# ------------------------------------------------------------------ #

SOCKET_PATH = os.environ.get("ELK_SOCKET", "/tmp/elkulator.sock")

# ------------------------------------------------------------------ #
# Socket connection (lazy, reconnects on each tool call)              #
# ------------------------------------------------------------------ #

_sock = None
_sock_lock = threading.Lock()
_output_buf = bytearray()
_buf_lock = threading.Lock()
_reader_thread = None


def _connect():
    global _sock, _reader_thread
    if _sock is not None:
        return
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(SOCKET_PATH)
    s.settimeout(None)
    _sock = s
    t = threading.Thread(target=_reader_loop, daemon=True)
    t.start()
    _reader_thread = t


def _reader_loop():
    global _sock
    while True:
        try:
            chunk = _sock.recv(256)
            if not chunk:
                break
            with _buf_lock:
                _output_buf.extend(chunk)
        except Exception:
            break


def _send(data: bytes):
    with _sock_lock:
        _connect()
        _sock.sendall(data)


def _drain_output() -> str:
    with _buf_lock:
        text = _output_buf.decode("ascii", errors="replace")
        _output_buf.clear()
    return text


# ------------------------------------------------------------------ #
# MCP server                                                           #
# ------------------------------------------------------------------ #

mcp = FastMCP("elkulator")


@mcp.tool()
def type_text(text: str) -> str:
    """Send a string of characters to the Electron as virtual keypresses.

    Each byte is injected at the emulated keyboard timing (≈60 ms per key).
    Use \\r or \\n for Return.  The function returns immediately; the
    Electron processes the keys asynchronously.

    For entering BASIC programs, prefer type_basic() which handles NEW/RUN.
    """
    _send(text.encode("ascii", errors="replace"))
    return f"Queued {len(text)} characters."


@mcp.tool()
def type_basic(program: str) -> str:
    """Type a complete BASIC program into the Electron and RUN it.

    Pass plain text; one statement per line.  Line numbers are added
    automatically if absent.  The emulator clears any existing program
    (NEW), enters the lines, then sends RUN.

    Example program:
        10 PRINT "HELLO, WORLD!"
        20 FOR I=1 TO 5
        30 PRINT I
        40 NEXT I

    Returns immediately.  Use read_output() after a delay to see results.
    """
    lines = [l for l in program.splitlines() if l.strip()]
    numbered = []
    for i, line in enumerate(lines):
        stripped = line.strip()
        if stripped and not stripped[0].isdigit():
            stripped = f"{(i + 1) * 10} {stripped}"
        numbered.append(stripped)

    payload = "NEW\r" + "".join(l + "\r" for l in numbered) + "RUN\r"
    _send(payload.encode("ascii", errors="replace"))
    return f"Injected {len(numbered)} lines + RUN ({len(payload)} chars queued)."


@mcp.tool()
def read_output(wait_seconds: float = 2.0) -> str:
    """Read text the Electron has printed since the last call.

    Waits up to wait_seconds for output to arrive, then returns everything
    buffered so far.  Call after type_basic() or type_text() to see results.

    Returns the raw text as printed by the Electron (may include control
    characters such as \\r\\n or escape sequences for colour/cursor moves).
    """
    # Make sure we're connected so the reader thread is running
    with _sock_lock:
        _connect()

    deadline = time.monotonic() + wait_seconds
    while time.monotonic() < deadline:
        with _buf_lock:
            if _output_buf:
                break
        time.sleep(0.05)

    # Give a short extra window for trailing output
    time.sleep(0.1)
    return _drain_output() or "(no output)"


@mcp.tool()
def send_key(key: str) -> str:
    """Send a single special key to the Electron.

    Supported values: 'RETURN', 'ESCAPE', 'DELETE', 'BREAK' (soft reset
    via Escape), or any single printable ASCII character.
    """
    mapping = {
        "RETURN":  "\r",
        "ENTER":   "\r",
        "ESCAPE":  "\x1b",
        "ESC":     "\x1b",
        "DELETE":  "\x7f",
        "DEL":     "\x7f",
        "BREAK":   "\x1b",   # Escape = soft break in Elkulator
    }
    ch = mapping.get(key.upper(), key[0] if key else "")
    if ch:
        _send(ch.encode("ascii", errors="replace"))
    return f"Sent: {repr(ch)}"


if __name__ == "__main__":
    mcp.run()
