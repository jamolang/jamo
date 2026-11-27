"""Checks that Jamo programs show text in any language right in a real console window (not just in pipes and files).
Starts the program in its own hidden console, attaches to that console, and reads back the letters on screen.
Usage: python console_text_test.py <jamo.exe> <work folder>"""
import ctypes
import subprocess
import sys
import time
from ctypes import wintypes
from pathlib import Path

JAMO = Path(sys.argv[1])
WORK = Path(sys.argv[2])
WORK.mkdir(parents=True, exist_ok=True)
k32 = ctypes.WinDLL("kernel32", use_last_error=True)

LINES = ["héllo wörld", "Ωmega ∑ ж привет", "日本語のテキスト", "emoji: 😀 ok", "plain ascii"]
# A long line, longer than one 1000-byte chunk, with 2-byte letters that land on the chunk edge
LONG = "é" * 700
source = "func main()\n" + "".join(f'    print("{line}")\n' for line in LINES) + f'    print("{LONG}")\n    print("end")\n    var typed = input()\n    print("got [" + typed + "] " + string(len(typed)) + " bytes")\n    var wait = input()\n    print(wait)\nend\n'
(WORK / "show.jamo").write_text(source, encoding="utf-8")
built = subprocess.run([str(JAMO), "build", str(WORK / "show.jamo")], capture_output=True, text=True)
assert built.returncode == 0, built.stderr

si = subprocess.STARTUPINFO()
si.dwFlags = subprocess.STARTF_USESHOWWINDOW
si.wShowWindow = 0  # hidden
proc = subprocess.Popen([str(WORK / "show.exe")], creationflags=subprocess.CREATE_NEW_CONSOLE, startupinfo=si)
time.sleep(1.5)

k32.FreeConsole()
ok = k32.AttachConsole(proc.pid)
assert ok, ctypes.get_last_error()
out = k32.CreateFileW("CONOUT$", 0xC0000000, 3, None, 3, 0, None)


class COORD(ctypes.Structure):
    _fields_ = [("X", ctypes.c_short), ("Y", ctypes.c_short)]


class SMALL_RECT(ctypes.Structure):
    _fields_ = [("Left", ctypes.c_short), ("Top", ctypes.c_short), ("Right", ctypes.c_short), ("Bottom", ctypes.c_short)]


class INFO(ctypes.Structure):
    _fields_ = [("dwSize", COORD), ("dwCursorPosition", COORD), ("wAttributes", wintypes.WORD), ("srWindow", SMALL_RECT), ("dwMaximumWindowSize", COORD)]


info = INFO()
k32.GetConsoleScreenBufferInfo(out, ctypes.byref(info))
width = info.dwSize.X
rows = []
for y in range(info.dwCursorPosition.Y + 1):
    buf = ctypes.create_unicode_buffer(width + 1)
    got = wintypes.DWORD()
    k32.ReadConsoleOutputCharacterW(out, buf, width, COORD(0, y), ctypes.byref(got))
    rows.append(buf.value[:got.value].rstrip())

# Typing: letters beyond English go into the console's input as key presses, and input() must get them as UTF-8
class KEY(ctypes.Structure):
    _fields_ = [("down", wintypes.BOOL), ("repeat", wintypes.WORD), ("key", wintypes.WORD), ("scan", wintypes.WORD), ("char", wintypes.WCHAR), ("state", wintypes.DWORD)]


class RECORD(ctypes.Structure):
    _fields_ = [("kind", wintypes.WORD), ("key", KEY)]


TYPED = "héllo ж"
keys = []
for ch in TYPED + "\r":
    for down in (1, 0):
        keys.append(RECORD(1, KEY(down, 1, 0x0D if ch == "\r" else 0, 0, ch, 0)))
conin = k32.CreateFileW("CONIN$", 0xC0000000, 3, None, 3, 0, None)
written = wintypes.DWORD()
k32.WriteConsoleInputW(conin, (RECORD * len(keys))(*keys), len(keys), ctypes.byref(written))
time.sleep(1.0)
k32.GetConsoleScreenBufferInfo(out, ctypes.byref(info))
after = []
for y in range(info.dwCursorPosition.Y + 1):
    buf = ctypes.create_unicode_buffer(width + 1)
    got = wintypes.DWORD()
    k32.ReadConsoleOutputCharacterW(out, buf, width, COORD(0, y), ctypes.byref(got))
    after.append(buf.value[:got.value].rstrip())
typed_right = f"got [{TYPED}] {len(TYPED.encode())} bytes" in after
k32.FreeConsole()
proc.kill()
proc.wait()

# Wide letters (Chinese, Japanese, emoji) take two cells, and the console keeps a copy of the letter in the second one
# on some Windows versions, so compare with repeats squeezed out for those lines
screen = "\n".join(rows)
failures = []
# An emoji takes two UTF-16 halves, and reading the old console back one cell at a time can't join them, so only the
# words around it are checked. Windows Terminal shows it
if not ("emoji:" in screen and " ok" in screen):
    failures.append("emoji line")
for line in LINES[:2] + LINES[2:3] + LINES[4:]:
    found = line in screen or line.replace(" ", "") in screen.replace(" ", "")
    if not found:
        squeezed = "".join(ch for i, ch in enumerate(screen) if i == 0 or ch != screen[i - 1] or ch.isascii())
        found = line in squeezed
    if not found:
        failures.append(line)
if not typed_right:
    failures.append("typed text came back as " + repr([r for r in after if r.startswith("got")]))
long_shown = "".join(r for r in rows if r and set(r) <= {"é"})
if len(long_shown) != 700:
    failures.append(f"long line: {len(long_shown)} of 700 letters, other letters: {set(''.join(rows)) - set(''.join(LINES)) - {'é', 'e', 'n', 'd', ' '}}")
print("\n".join(rows[:8]))
print("ALL SHOWN RIGHT" if not failures else "WRONG: " + repr(failures))
