"""Runs every Jamo test with one command: python tests/run_tests.py. For developing Jamo only, and users never need it."""
import concurrent.futures
import ctypes
import ctypes.wintypes
import datetime
import http.server
import importlib.util
import json
import math
import os
import random
import re
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import zipfile
import zlib
from decimal import Decimal
from pathlib import Path

# --- Setup ---
ROOT = Path(__file__).resolve().parent.parent
GOOD = ROOT / "tests" / "good"
BAD = ROOT / "tests" / "bad"
MODULES = ROOT / "tests" / "modules"
UNIT = ROOT / "tests" / "unit"
# jamo.exe is the compiler written in Jamo, and the one every test uses. JAMO_COMPILER can name another one to test.
# jamo_cpp.exe is the C++ compiler, kept to build from scratch, and the Phase 6 comparisons take their dumps from it
CPP_JAMO = ROOT / "jamo_cpp.exe"
JAMO = Path(os.environ["JAMO_COMPILER"]) if os.environ.get("JAMO_COMPILER") else ROOT / "jamo.exe"
GPP_FOLDER = Path(os.environ.get("LOCALAPPDATA", "")) / "Microsoft/WinGet/Packages/BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe/mingw64/bin"
DEFENDER = Path(r"C:\Program Files\Windows Defender\MpCmdRun.exe")
SOURCES = ["errors", "lexer", "parser", "checker", "codegen", "runtime"]
RANDOM_PROGRAMS = 300
RANDOM_FLOAT_PROGRAMS = 120
RANDOM_TEXT_PROGRAMS = 40
SCIENTIFIC_FROM = 1e16
GARBAGE_FILES = 1500
INT_BITS = 64
SEED = 32
STILL_RUNNING_WAIT = 0.5
EXIT_STACK_OVERFLOW = 0xC00000FD
MEMORY_LIMIT_MB = 200
CLEANER_MODES = (["--gc-off"], [], ["--gc-test"], ["--gc-test", "--gc-check"], ["--no-optimize"], ["--no-optimize", "--gc-test", "--gc-check"])
SLOW_RUN_LIMIT = 300
STEADY_LIMIT_MB = 64
SIDE_BY_SIDE = 6
# WinHTTP's and Winsock's own threads and connections move the handle count by up to about 4 either way, the same
# after 2,000 requests as after 8,000, and after 1,500 links as after 300, so the network tests allow a little more
NET_HANDLE_WOBBLE = 4
CREATE_SUSPENDED = 0x4
JOB_EXTENDED_LIMITS = 9
JOB_LIMITS_SIZE = 144
JOB_FLAGS_AT = 16
JOB_MEMORY_AT = 112
JOB_LIMIT_PROCESS_MEMORY = 0x100

failures = []


def report(name, passed, details=""):
    print(("ok   " if passed else "FAIL ") + name)
    if not passed:
        failures.append(name)
        if details:
            print("     " + details.replace("\n", "\n     ")[:1500])


def clean(text):
    return text.replace("\r\n", "\n")


def run(command, cwd=None, env=None, timeout=60):
    return subprocess.run(command, cwd=cwd, env=env, capture_output=True, text=True, timeout=timeout, encoding="utf-8", errors="replace")


def wrap(value):
    value &= (1 << INT_BITS) - 1
    return value - (1 << INT_BITS) if value >= 1 << (INT_BITS - 1) else value


def build_compiler():
    env = dict(os.environ)
    env["PATH"] = str(GPP_FOLDER) + os.pathsep + env["PATH"]
    result = subprocess.run(["cmd", "/c", str(ROOT / "build.bat")], cwd=ROOT, env=env, capture_output=True, text=True)
    warnings = "warning:" in result.stderr
    report("jamo.exe builds with no warnings", result.returncode == 0 and not warnings, result.stdout + result.stderr)
    if result.returncode != 0:
        sys.exit(1)


# --- Each part by itself, in build order ---
def test_units(work):
    env = dict(os.environ)
    env["PATH"] = str(GPP_FOLDER) + os.pathsep + env["PATH"]
    for part, count in (("lexer", 2), ("parser", 3), ("checker", 4), ("codegen", 6)):
        exe = work / f"test_{part}.exe"
        files = [str(UNIT / f"test_{part}.cpp")] + [str(ROOT / "src" / f"{name}.cpp") for name in SOURCES[:count]]
        built = subprocess.run(["g++", "-std=c++20", "-g", "-D_GLIBCXX_ASSERTIONS", *files, "-o", str(exe)], env=env, capture_output=True, text=True)
        if built.returncode != 0:
            report(f"{part} unit tests build", False, built.stderr)
            continue
        result = run([str(exe)], timeout=180)
        report(f"{part} unit tests", result.returncode == 0, result.stdout)


# --- Programs with their expected output ---
def test_good_programs(work):
    programs = sorted(GOOD.glob("*.jamo"))
    report("at least 5 good programs exist", len(programs) >= 5)
    for source in programs:
        copy = work / source.name
        shutil.copy(source, copy)
        want = clean(source.with_suffix(".out").read_text(encoding="utf-8"))
        exit_file = source.with_suffix(".code")
        want_code = int(exit_file.read_text(encoding="utf-8")) if exit_file.exists() else 0
        built = run([str(JAMO), "build", str(copy)])
        if built.returncode != 0:
            report(f"good: {source.stem}", False, built.stderr)
            continue
        ran = run([str(copy.with_suffix(".exe"))], cwd=work)
        passed = clean(ran.stdout) == want and ran.returncode == want_code
        report(f"good: {source.stem}", passed, f"code {ran.returncode}, wanted {want_code}\ngot:\n{ran.stdout}\nwanted:\n{want}")


# --- Programs with their expected errors ---
def test_bad_programs(work):
    programs = sorted(BAD.glob("*.jamo"))
    for source in programs:
        copy = work / source.name
        shutil.copy(source, copy)
        exe = copy.with_suffix(".exe")
        result = run([str(JAMO), "build", str(copy)])
        want = clean(source.with_suffix(".err").read_text(encoding="utf-8"))
        passed = result.returncode == 1 and clean(result.stderr) == want and not exe.exists()
        report(f"bad: {source.stem}", passed, f"code {result.returncode}, exe written: {exe.exists()}\ngot:\n{result.stderr}\nwanted:\n{want}")


# Starts a program with a memory limit, so running out of memory can't slow down the whole PC
def run_with_memory_limit(exe, megabytes):
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.CreateJobObjectW.restype = ctypes.c_void_p
    kernel32.SetInformationJobObject.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p, ctypes.c_ulong]
    kernel32.AssignProcessToJobObject.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    kernel32.CloseHandle.argtypes = [ctypes.c_void_p]
    ctypes.windll.ntdll.NtResumeProcess.argtypes = [ctypes.c_void_p]
    limits = bytearray(JOB_LIMITS_SIZE)
    struct.pack_into("<I", limits, JOB_FLAGS_AT, JOB_LIMIT_PROCESS_MEMORY)
    struct.pack_into("<Q", limits, JOB_MEMORY_AT, megabytes * 1024 * 1024)
    job = kernel32.CreateJobObjectW(None, None)
    buffer = (ctypes.c_char * len(limits)).from_buffer(limits)
    assert kernel32.SetInformationJobObject(job, JOB_EXTENDED_LIMITS, buffer, len(limits))
    process = subprocess.Popen([str(exe)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, creationflags=CREATE_SUSPENDED, text=True)
    try:
        assert kernel32.AssignProcessToJobObject(job, int(process._handle))
        ctypes.windll.ntdll.NtResumeProcess(int(process._handle))
        out, err = process.communicate(timeout=60)
    finally:
        if process.poll() is None:
            process.kill()
        kernel32.CloseHandle(job)
    return process.returncode, out, err


# --- Programs made of several files. Each folder has main.jamo, then main.out or main.err, and main.runerr if it stops with a run error ---
def test_modules(work):
    folders = sorted(path for path in MODULES.iterdir() if path.is_dir())
    report("module programs exist", len(folders) >= 5)
    for folder in folders:
        copy = work / "modules" / folder.name
        shutil.copytree(folder, copy)
        exe = copy / "main.exe"
        built = run([str(JAMO), "build", str(copy / "main.jamo")])
        wanted_error = folder / "main.err"
        if wanted_error.exists():
            want = clean(wanted_error.read_text(encoding="utf-8"))
            report(f"module: {folder.name}", built.returncode == 1 and clean(built.stderr) == want and not exe.exists(), f"code {built.returncode}\ngot:\n{built.stderr}\nwanted:\n{want}")
            continue
        if built.returncode != 0:
            report(f"module: {folder.name}", False, built.stderr)
            continue
        ran = run([str(exe)])
        want = clean((folder / "main.out").read_text(encoding="utf-8"))
        run_error = folder / "main.runerr"
        want_error = clean(run_error.read_text(encoding="utf-8")) if run_error.exists() else ""
        want_code = 1 if run_error.exists() else 0
        passed = clean(ran.stdout) == want and clean(ran.stderr) == want_error and ran.returncode == want_code
        report(f"module: {folder.name}", passed, f"code {ran.returncode}\ngot:\n{ran.stdout}\n{ran.stderr}\nwanted:\n{want}\n{want_error}")


# --- Every good program must act the same in all of the cleaner's modes ---
def test_cleaner_modes(work):
    folder = work / "cleaner modes"
    folder.mkdir()
    programs = []
    for source in sorted(GOOD.glob("*.jamo")):
        shutil.copy(source, folder / source.name)
        exit_file = source.with_suffix(".code")
        programs.append((source.stem, folder / source.name, source.with_suffix(".out"), int(exit_file.read_text(encoding="utf-8")) if exit_file.exists() else 0))
    for module in sorted(path for path in MODULES.iterdir() if path.is_dir() and (path / "main.out").exists()):
        shutil.copytree(module, folder / module.name)
        programs.append((f"module {module.name}", folder / module.name / "main.jamo", module / "main.out", 1 if (module / "main.runerr").exists() else 0))
    problems = []

    def try_program(name, source, out_file, want_code):
        want = clean(out_file.read_text(encoding="utf-8"))
        first_error = None
        for flags in CLEANER_MODES:
            built = run([str(JAMO), "build", *flags, str(source)])
            try:
                ran = run([str(source.with_suffix(".exe"))], cwd=source.parent, timeout=SLOW_RUN_LIMIT)
            except subprocess.TimeoutExpired:
                problems.append(f"{name} {flags}: took too long")
                continue
            if first_error is None:
                first_error = ran.stderr
            if built.returncode != 0 or ran.returncode != want_code or clean(ran.stdout) != want or ran.stderr != first_error:
                problems.append(f"{name} {flags}: code {ran.returncode}, wanted {want_code}\n{ran.stderr[:300]}\n{ran.stdout[:300]}")
    with concurrent.futures.ThreadPoolExecutor(SIDE_BY_SIDE) as pool:
        list(pool.map(lambda program: try_program(*program), programs))
    report(f"{len(programs)} good programs act the same in all {len(CLEANER_MODES)} cleaner modes", not problems, f"{len(problems)} problems\n" + "\n".join(problems[:6]))


# --- Memory stays steady in programs that run for a long time, and big programs still work ---
def test_cleaner_long_runs(work):
    programs = {
        "strings": ('func main()\n    var total = 0\n    for i in 0..3000000\n        var text = "item " + string(i) + " of many, with some more words to fill it up"\n        total += len(text)\n    end\n    print(total)\nend\n', sum(len(f"item {i} of many, with some more words to fill it up") for i in range(3000000))),
        "arrays": ('func main()\n    var total = 0\n    for i in 0..30000\n        var items: [int] = []\n        for j in 0..1000\n            items.add(j)\n        end\n        total += items.pop()\n    end\n    print(total)\nend\n', 30000 * 999),
        "arrays of structs": ('struct Item\n    name: string\n    tags: [string]\nend\n\nfunc main()\n    var keep: [Item] = []\n    var total = 0\n    for i in 0..1000000\n        var item = Item{name: string(i), tags: ["a", "b"]}\n        if i % 100000 == 0\n            keep.add(item)\n        end\n        total += len(item.tags)\n    end\n    print(total + len(keep))\nend\n', 2000000 + 10),
    }
    for name, (source, want) in programs.items():
        path = work / f"long {name}.jamo"
        path.write_text(source, encoding="utf-8")
        run([str(JAMO), "build", str(path)])
        code, out, err = run_with_memory_limit(path.with_suffix(".exe"), STEADY_LIMIT_MB)
        report(f"memory stays under {STEADY_LIMIT_MB} MB: {name}", code == 0 and clean(out) == f"{want}\n", f"code {code}, {out[:200]} {err[:300]}")
        run([str(JAMO), "build", "--gc-off", str(path)])
        code, _, _ = run_with_memory_limit(path.with_suffix(".exe"), STEADY_LIMIT_MB)
        report(f"the same program runs out of memory with the cleaner off: {name}", code != 0, f"code {code}")

    # The sizes the stress programs had before the slowest mode needed them smaller
    (work / "big.jamo").write_text('struct Item\n    id: int\n    tags: [string]\nend\n\nfunc main()\n    var numbers: [int] = []\n    for i in 0..1000000\n        numbers.add(i)\n    end\n    print(numbers[999999])\n    var words: [string] = []\n    for i in 0..200000\n        words.add(string(i))\n    end\n    print(len(string(words)))\n    var items: [Item] = []\n    for i in 0..200000\n        items.add(Item{id: i, tags: []})\n    end\n    var total = 0\n    for item in items\n        total += item.id\n    end\n    print(total)\nend\n', encoding="utf-8")
    result = run([str(JAMO), "run", str(work / "big.jamo")], timeout=SLOW_RUN_LIMIT)
    want_length = len(str([str(i) for i in range(200000)]))
    report("big arrays and structs with the cleaner on", result.returncode == 0 and clean(result.stdout) == f"999999\n{want_length}\n{sum(range(200000))}\n", f"{result.returncode} {result.stdout} {result.stderr}")


# --- Jamo's own allocator for small blocks ---
# Python's answer for tests/good/cleaner_mixed_sizes.jamo
def mixed_sizes_answer(source, slots, steps):
    sizes = [int(word) for word in source.split("var sizes = [")[1].split("]")[0].split(", ")]
    counts = [int(word) for word in source.split("var counts = [")[1].split("]")[0].split(", ")]
    seed = 12345
    total = 0
    for _ in range(steps):
        seed = (seed * 1103515245 + 12345) % 2147483648
        seed = (seed * 1103515245 + 12345) % 2147483648
        size = sizes[seed // 65536 % len(sizes)]
        seed = (seed * 1103515245 + 12345) % 2147483648
        count = counts[seed // 65536 % len(counts)]
        total += len(str(seed)) + 1 + size + count
    return f"0\n{total}\n"


def test_small_blocks(work):
    folder = work / "small blocks"
    folder.mkdir()
    source = (GOOD / "cleaner_mixed_sizes.jamo").read_text(encoding="utf-8")
    report("cleaner_mixed_sizes.out is Python's answer", clean((GOOD / "cleaner_mixed_sizes.out").read_text(encoding="utf-8")) == mixed_sizes_answer(source, 30, 300))

    # Far more boxes and steps, so every chunk stays partly in use and blocks come back through the free lists
    path = folder / "mixed big.jamo"
    path.write_text(source.replace("var slots = 30", "var slots = 3000").replace("var steps = 300", "var steps = 300000"), encoding="utf-8")
    run([str(JAMO), "build", str(path)])
    code, out, err = run_with_memory_limit(path.with_suffix(".exe"), STEADY_LIMIT_MB)
    report(f"mixed sizes with reuse, under {STEADY_LIMIT_MB} MB", code == 0 and clean(out) == mixed_sizes_answer(source, 3000, 300000), f"code {code}, {out[:200]} {err[:300]}")
    path = folder / "mixed every time.jamo"
    path.write_text(source.replace("var slots = 30", "var slots = 60").replace("var steps = 300", "var steps = 1500"), encoding="utf-8")
    run([str(JAMO), "build", "--gc-test", str(path)])
    ran = run([str(path.with_suffix(".exe"))], timeout=SLOW_RUN_LIMIT)
    report("mixed sizes, cleaning at every allocation", ran.returncode == 0 and clean(ran.stdout) == mixed_sizes_answer(source, 60, 1500), f"code {ran.returncode}, {ran.stdout[:200]} {ran.stderr[:300]}")

    # Each size in turn makes far more than the limit in total. Without empty chunks being shared, each size would keep its own memory
    path = folder / "sizes in turn.jamo"
    path.write_text('func main()\n    var total = 0\n    for size in 0..32\n        var base = "x"\n        for i in 0..size * 16\n            base = base + "x"\n        end\n        for i in 0..6000000 / (size * 16 + 64)\n            var text = base + "y"\n            total += len(text)\n        end\n    end\n    print(total)\nend\n', encoding="utf-8")
    want = sum((size * 16 + 2) * (6000000 // (size * 16 + 64)) for size in range(32))
    run([str(JAMO), "build", str(path)])
    code, out, err = run_with_memory_limit(path.with_suffix(".exe"), STEADY_LIMIT_MB)
    report(f"memory stays under {STEADY_LIMIT_MB} MB as sizes change", code == 0 and clean(out) == f"{want}\n", f"code {code}, {out[:200]} {err[:300]}")

    # Memory held by dead small blocks must go back when a large block can't get any, which is the clean and retry before giving up
    path = folder / "small then large.jamo"
    path.write_text('func main()\n    var words: [string] = []\n    for i in 0..200000\n        words.add("some words in a small block " + string(i))\n    end\n    print(len(words))\n    words = []\n    var text = "0123456789"\n    for i in 0..22\n        text = text + text\n    end\n    print(len(text))\nend\n', encoding="utf-8")
    run([str(JAMO), "build", str(path)])
    code, out, err = run_with_memory_limit(path.with_suffix(".exe"), STEADY_LIMIT_MB + 16)
    report("dead small blocks make room for a large block", code == 0 and clean(out) == f"200000\n{10 * 2 ** 22}\n", f"code {code}, {out[:200]} {err[:300]}")

    # A large live block makes the next clean far away, so getting a chunk fails first, and only the clean and retry saves it
    path = folder / "chunk retry.jamo"
    path.write_text('func main()\n    var text = "0123456789"\n    for i in 0..21\n        text = text + text\n    end\n    var copy = text\n    for i in 0..6\n        copy = text + "x"\n    end\n    var total = len(copy)\n    for i in 0..2000000\n        var small = "some words in a small block " + string(i)\n        total += len(small)\n    end\n    print(total)\nend\n', encoding="utf-8")
    want = 10 * 2 ** 21 + 1 + sum(28 + len(str(i)) for i in range(2000000))
    run([str(JAMO), "build", str(path)])
    code, out, err = run_with_memory_limit(path.with_suffix(".exe"), STEADY_LIMIT_MB)
    report("a clean and retry when a chunk can't be had", code == 0 and clean(out) == f"{want}\n", f"code {code}, {out[:200]} {err[:300]}")

    # Running out with small blocks gives the same clear error
    path = folder / "too many.jamo"
    path.write_text('func main()\n    var keep: [string] = []\n    while true\n        keep.add("some words in a small block")\n    end\nend\n', encoding="utf-8")
    run([str(JAMO), "build", str(path)])
    code, _, err = run_with_memory_limit(path.with_suffix(".exe"), STEADY_LIMIT_MB)
    report("running out of memory with small blocks gives the clear error", code == 1 and clean(err) == "too many.jamo line 4: Error: the program ran out of memory\n", f"code {code}, {err[:300]}")

    # --gc-check must never reuse a block, so a program that's steady normally has to run out of memory with it
    path = folder / "never reused.jamo"
    path.write_text('func main()\n    var total = 0\n    for i in 0..3000000\n        var text = "item " + string(i) + " of many, with some more words to fill it up"\n        total += len(text)\n    end\n    print(total)\nend\n', encoding="utf-8")
    run([str(JAMO), "build", "--gc-check", str(path)])
    code, out, err = run_with_memory_limit(path.with_suffix(".exe"), STEADY_LIMIT_MB)
    report("--gc-check keeps freed blocks instead of reusing them", code == 1 and "ran out of memory" in err, f"code {code}, {out[:200]} {err[:300]}")

    # Jamo code can't read a block before it's filled, but the cleaner can. New arrays here reuse the blocks of arrays that held
    # references, and those places are then reused for holders of large text. Leftover references would keep all that text in use
    path = folder / "zeroed.jamo"
    path.write_text('struct Holder\n    data: string\nend\n\nfunc main()\n    var large = "0123456789"\n    for i in 0..11\n        large = large + large\n    end\n    var pins: [[Holder]] = []\n    var keep: [[int]] = []\n    var total = 0\n    for round in 0..12\n        for i in 0..3000\n            var old = [Holder{data: ""}, Holder{data: ""}, Holder{data: ""}, Holder{data: ""}]\n            if i % 20 == 0\n                pins.add(old)\n            end\n        end\n        for i in 0..250\n            var filler = large + "x"\n            total += len(filler)\n        end\n        for i in 0..500\n            var items: [int] = []\n            keep.add(items)\n        end\n        for i in 0..1000\n            var holder = Holder{data: large + "y"}\n            total += len(holder.data)\n        end\n    end\n    print(total + len(keep) + len(pins))\nend\n', encoding="utf-8")
    want = 12 * (250 * 20481 + 1000 * 20481) + 12 * 500 + 12 * 150
    run([str(JAMO), "build", str(path)])
    code, out, err = run_with_memory_limit(path.with_suffix(".exe"), STEADY_LIMIT_MB)
    report("reused blocks that can hold references come back zeroed", code == 0 and clean(out) == f"{want}\n", f"code {code}, {out[:200]} {err[:300]}")


# --- How much speed the cleaner costs, tracked but not pass or fail ---
def test_cleaner_speed(work):
    print("\nThe cleaner's speed:")
    source = work / "cleaner speed.jamo"
    source.write_text('struct Item\n    name: string\n    value: int\nend\n\nfunc main()\n    var keep: [Item] = []\n    for i in 0..100000\n        keep.add(Item{name: "kept", value: i})\n    end\n    var total = 0\n    for i in 0..3000000\n        var item = Item{name: string(i), value: i}\n        total += item.value\n    end\n    print(total + len(keep))\nend\n', encoding="utf-8")
    for label, flags in (("off", ["--gc-off"]), ("on", [])):
        run([str(JAMO), "build", *flags, str(source)])
        started = time.perf_counter()
        run([str(source.with_suffix(".exe"))], timeout=SLOW_RUN_LIMIT)
        print(f"  making 6 million small blocks, cleaner {label}: {time.perf_counter() - started:.3f} s")


# --- Warnings, and the calls behind a run error ---
def test_warnings_and_calls(work):
    folder = work / "warnings"
    folder.mkdir()
    source = folder / "warn.jamo"
    source.write_text('func f(unused_input: int): int\n    var unused = 1\n    return 2\n    print(3)\nend\n\nfunc main()\n    var total = 0\n    total += 1\n    var assigned = 0\n    assigned = 1\n    for i in 0..3\n        var inner = 1\n        break\n        print(i)\n        print(4)\n    end\n    print(f(1))\nend\n', encoding="utf-8")
    want = (
        "warn.jamo line 2: Warning: unused is never used.\n        var unused = 1\n            ^^^^^^\nUse it, or remove it.\n"
        "warn.jamo line 4: Warning: This code can never run, because the code above it always leaves first.\n        print(3)\n        ^^^^^\n"
        "warn.jamo line 10: Warning: assigned is never used.\n        var assigned = 0\n            ^^^^^^^^\nUse it, or remove it.\n"
        "warn.jamo line 13: Warning: inner is never used.\n            var inner = 1\n                ^^^^^\nUse it, or remove it.\n"
        "warn.jamo line 15: Warning: This code can never run, because the code above it always leaves first.\n            print(i)\n            ^^^^^\n"
    )
    for command in ("check", "build"):
        result = run([str(JAMO), command, str(source)])
        report(f"{command} shows warnings, and still succeeds", result.returncode == 0 and clean(result.stderr) == want, f"{result.returncode}\n{result.stderr}\nwanted:\n{want}")
    result = run([str(source.with_suffix(".exe"))])
    report("a program with warnings still runs", result.returncode == 0 and clean(result.stdout) == "2\n", result.stdout)
    source.write_text("func main()\n    var a = 1\n    print(nope)\nend\n", encoding="utf-8")
    result = run([str(JAMO), "check", str(source)])
    report("warnings stay out of the way of errors", "Warning" not in result.stderr and result.returncode == 1, result.stderr)
    # A generic used with two types gives its warnings once, not once for each copy
    source.write_text("func pick<T>(item: T): T\n    var spare = 1\n    return item\n    print(2)\nend\n\nfunc main()\n    print(pick(1))\n    print(pick(\"a\"))\nend\n", encoding="utf-8")
    want = (
        "warn.jamo line 2: Warning: spare is never used.\n        var spare = 1\n            ^^^^^\nUse it, or remove it.\n"
        "warn.jamo line 4: Warning: This code can never run, because the code above it always leaves first.\n        print(2)\n        ^^^^^\n"
    )
    result = run([str(JAMO), "check", str(source)])
    report("a generic's warnings show once, from its first copy", result.returncode == 0 and clean(result.stderr) == want, f"{result.returncode}\n{result.stderr}\nwanted:\n{want}")
    # A run error while the variables outside functions are being set up says so in its list of calls
    (folder / "setup.jamo").write_text("var items = make_items()\n\nfunc make_items(): [int]\n    var made: [int] = []\n    print(made[3])\n    return made\nend\n\nfunc main()\n    print(len(items))\nend\n", encoding="utf-8")
    result = run([str(JAMO), "run", str(folder / "setup.jamo")])
    want = "setup.jamo line 5: Error: index 3 is outside this array, which has 0 items\n    called from setting up the variables outside functions, setup.jamo line 1\n"
    report("a run error while setting up variables outside functions", result.returncode == 1 and clean(result.stderr) == want and result.stdout == "", f"{result.returncode}\n{result.stderr}\nwanted:\n{want}")

    (folder / "chain.jamo").write_bytes((GOOD / "call_chain_error.jamo").read_bytes())
    result = run([str(JAMO), "run", str(folder / "chain.jamo")])
    want = "chain.jamo line 6: Error: division by zero\n    called from divide, chain.jamo line 10\n    called from main, chain.jamo line 14\n"
    report("a run error shows the calls that led there", result.returncode == 1 and clean(result.stderr) == want, f"{result.returncode}\n{result.stderr}\nwanted:\n{want}")
    (folder / "deep.jamo").write_text("func down(n: int): int\n    return 1 / n + down(n - 1)\nend\n\nfunc main()\n    print(down(50))\nend\n", encoding="utf-8")
    result = run([str(JAMO), "run", str(folder / "deep.jamo")])
    want = "deep.jamo line 2: Error: division by zero\n" + "    called from down, deep.jamo line 2\n" * 10 + "    and more calls before those\n"
    report("a very long chain of calls gets cut short", result.returncode == 1 and clean(result.stderr) == want, f"{result.returncode}\n{result.stderr}")


# --- The VS Code extension, without VS Code ---
# --- Every .exe has the PE checksum Windows' own MapFileAndCheckSumW works out. Without one, Windows Defender's
# machine learning took Jamo programs for malware ---
def test_checksums(work):
    folder = work / "checksums"
    folder.mkdir()
    (folder / "hello.jamo").write_text('func main()\n    print("hi")\nend\n', encoding="utf-8")
    imagehlp = ctypes.WinDLL("imagehlp")
    for compiler in (JAMO, CPP_JAMO):
        for window in (False, True):
            built = run([str(compiler), "build"] + (["--window"] if window else []) + [str(folder / "hello.jamo")])
            in_file = ctypes.wintypes.DWORD()
            worked_out = ctypes.wintypes.DWORD()
            imagehlp.MapFileAndCheckSumW(str(folder / "hello.exe"), ctypes.byref(in_file), ctypes.byref(worked_out))
            report(f"{compiler.name} writes the right checksum{' for a window program' if window else ''}", built.returncode == 0 and in_file.value != 0 and in_file.value == worked_out.value, f"{built.stderr} {in_file.value} {worked_out.value}")
    in_file = ctypes.wintypes.DWORD()
    worked_out = ctypes.wintypes.DWORD()
    imagehlp.MapFileAndCheckSumW(str(JAMO), ctypes.byref(in_file), ctypes.byref(worked_out))
    report("jamo.exe itself has the right checksum", in_file.value != 0 and in_file.value == worked_out.value, f"{in_file.value} {worked_out.value}")


# --- Text in any language shows right in a real console, not only in pipes and files ---
def test_console_text(work):
    for compiler in (JAMO, CPP_JAMO):
        result = run([sys.executable, str(ROOT / "tests" / "console_text.py"), str(compiler), str(work / f"console {compiler.stem}")], env={**os.environ, "PYTHONIOENCODING": "utf-8"}, timeout=120)
        report(f"a real console shows accented, Greek, Cyrillic and Japanese text and long lines, and typing them works, from {compiler.name}", result.stdout.strip().endswith("ALL SHOWN RIGHT"), result.stdout[-600:] + result.stderr[-400:])


# --- jamo get, from a stand-in for GitHub on 127.0.0.1 ---
def test_get_command(work):
    site = work / "package site"
    def put(path, text):
        (site / path).parent.mkdir(parents=True, exist_ok=True)
        (site / path).write_text(text, encoding="utf-8", newline="")
    put("sam/shapes/HEAD/jamo-package.txt", "# shapes: a two-file package\nshapes.jamo\n\narea.jamo\n")
    put("sam/shapes/HEAD/shapes.jamo", 'use "area.jamo"\n\nfunc square_area(side: int): int\n    return area(side, side)\nend\n')
    put("sam/shapes/HEAD/area.jamo", "func area(w: int, h: int): int\n    return w * h\nend\n")
    put("sam/greet/HEAD/greet.jamo", 'func greet(name: string): string\n    return "Hi " + name\nend\n')
    put("sam/sneaky/HEAD/jamo-package.txt", "ok.jamo\n../evil.jamo\n")
    put("sam/sneaky/HEAD/ok.jamo", "func ok(): int\n    return 1\nend\n")
    put("sam/halfway/HEAD/jamo-package.txt", "here.jamo\nmissing.jamo\n")
    put("sam/halfway/HEAD/here.jamo", "func here(): int\n    return 1\nend\n")

    class Quiet(http.server.SimpleHTTPRequestHandler):
        def __init__(self, *args, **kwargs):
            super().__init__(*args, directory=str(site), **kwargs)

        def log_message(self, *args):
            pass

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Quiet)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        env = {**os.environ, "JAMO_PACKAGE_SITE": f"http://127.0.0.1:{server.server_address[1]}"}
        folder = work / "get test"
        folder.mkdir()
        got = run([str(JAMO), "get", "sam/shapes"], cwd=folder, env=env)
        saved = sorted(p.name for p in (folder / "packages" / "shapes").glob("*")) if (folder / "packages" / "shapes").exists() else []
        report("jamo get fetches every file a package lists", got.returncode == 0 and saved == ["area.jamo", "shapes.jamo"] and 'use "packages/shapes/shapes.jamo"' in got.stdout, f"{got.returncode} {got.stdout} {got.stderr} {saved}")
        (folder / "main.jamo").write_text('use "packages/shapes/shapes.jamo"\nuse "packages/greet/greet.jamo"\n\nfunc main()\n    print(square_area(7))\n    print(greet("Jo"))\nend\n', encoding="utf-8")
        got = run([str(JAMO), "get", "sam/greet"], cwd=folder, env=env)
        report("jamo get fetches project.jamo when there's no list", got.returncode == 0 and (folder / "packages" / "greet" / "greet.jamo").exists() and "Got 1 file " in got.stdout, f"{got.returncode} {got.stdout} {got.stderr}")
        ran = run([str(JAMO), "run", str(folder / "main.jamo")], cwd=folder)
        report("a program uses packages it got, and their own files find each other", ran.returncode == 0 and clean(ran.stdout) == "49\nHi Jo\n", f"{ran.returncode} {ran.stdout} {ran.stderr}")
        got = run([str(JAMO), "get", "sam/sneaky"], cwd=folder, env=env)
        report("jamo get refuses a list that names a file outside its folder, and saves nothing", got.returncode == 1 and "../evil.jamo" in got.stderr and not (folder / "packages" / "sneaky").exists() and not (folder / "packages" / "evil.jamo").exists(), f"{got.returncode} {got.stderr}")
        got = run([str(JAMO), "get", "sam/halfway"], cwd=folder, env=env)
        report("jamo get saves nothing when one listed file is missing", got.returncode == 1 and "missing.jamo" in got.stderr and "404" in got.stderr and not (folder / "packages" / "halfway").exists(), f"{got.returncode} {got.stderr}")
        got = run([str(JAMO), "get", "sam/nowhere"], cwd=folder, env=env)
        report("jamo get says when a project isn't there", got.returncode == 1 and "nowhere" in got.stderr and "404" in got.stderr, f"{got.returncode} {got.stderr}")
        for spec in ("shapes", "sam/shapes/extra", "../x/y", "sam/.hidden", "sam/sp ace", "/shapes"):
            got = run([str(JAMO), "get", spec], cwd=folder, env=env)
            report(f"jamo get refuses the name {spec!r}", got.returncode == 1 and "jamo get needs a GitHub name and project" in got.stderr, f"{got.returncode} {got.stderr}")
        got = run([str(CPP_JAMO), "get", "sam/shapes"], cwd=folder, env=env)
        report("the C++ backup says to use jamo.exe for jamo get", got.returncode == 1 and "jamo.exe" in got.stderr, got.stderr)
    finally:
        server.shutdown()
        server.server_close()


def test_vscode_extension(work):
    spec = importlib.util.spec_from_file_location("make_vsix", ROOT / "vscode" / "make_vsix.py")
    packer = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(packer)
    vsix = work / "jamo.vsix"
    packer.make_vsix(vsix)
    with zipfile.ZipFile(vsix) as archive:
        names = archive.namelist()
        package = json.loads(archive.read("extension/package.json"))
        grammar = json.loads(archive.read("extension/jamo.tmLanguage.json"))
        json.loads(archive.read("extension/language-configuration.json"))
    needed = ["extension.vsixmanifest", "[Content_Types].xml", "extension/package.json", "extension/extension.js", "extension/jamo.tmLanguage.json", "extension/language-configuration.json"]
    report("the extension packs into one file", all(name in names for name in needed) and package["main"] == "./extension.js", str(names))
    bad_patterns = []

    def walk(item):
        if isinstance(item, dict):
            for key, value in item.items():
                if key in ("match", "begin", "end"):
                    try:
                        re.compile(value.replace("\\\\", "\\\\"))
                    except re.error as problem:
                        bad_patterns.append(f"{value}: {problem}")
                walk(value)
        elif isinstance(item, list):
            for value in item:
                walk(value)

    walk(grammar)
    report("the extension's color patterns are all valid", not bad_patterns, str(bad_patterns))
    words = " ".join(str(item) for item in grammar["repository"]["keyword"]["patterns"])
    lexer = (ROOT / "src" / "lexer.cpp").read_text(encoding="utf-8")
    keywords = re.findall(r'if \(word == "(\w+)"\)', lexer)
    missing = [word for word in keywords if not re.search(rf"\b{word}\b", words)]
    report("the extension colors every keyword", len(keywords) > 20 and not missing, str(missing))

    node = shutil.which("node")
    if not node:
        print("  NOT TESTED: the underlines, since Node.js wasn't found")
        return
    folder = work / "extension test"
    folder.mkdir()
    (folder / "wrong.jamo").write_text("func main()\n    var unused = 1\n    print(scor)\n    print(1 + true)\nend\n", encoding="utf-8")
    result = run([str(JAMO), "check", str(folder / "wrong.jamo")])
    (folder / "output.txt").write_text(result.stderr, encoding="utf-8", newline="")
    (folder / "warned.jamo").write_text("func main()\n    var unused = 1\nend\n", encoding="utf-8")
    result = run([str(JAMO), "check", str(folder / "warned.jamo")])
    (folder / "warned.txt").write_text(result.stderr, encoding="utf-8", newline="")
    script = folder / "read.js"
    script.write_text('const fs = require("fs");\nconst { read_check_output } = require(process.argv[2]);\nconsole.log(JSON.stringify([read_check_output(fs.readFileSync("output.txt", "utf8")), read_check_output(fs.readFileSync("warned.txt", "utf8"))]));\n', encoding="utf-8")
    result = run([node, str(script), str(ROOT / "vscode" / "extension.js")], cwd=folder)
    errors, warnings = json.loads(result.stdout)
    report("the extension reads jamo check's errors", [(e["line"], e["column"], e["length"], e["is_warning"]) for e in errors] == [(2, 10, 4, False), (3, 14, 4, False)] and errors[0]["words"] == "nothing has this name: scor", str(errors))
    report("the extension reads jamo check's warnings", [(e["line"], e["column"], e["length"], e["is_warning"], e["hint"]) for e in warnings] == [(1, 8, 6, True, "Use it, or remove it.")], str(warnings))

    # Hover and go to definition read the code itself. Each question is (line, word, after a dot) and the answer the
    # file name, line and kind of what it finds, written out by hand
    libraries_packed = sorted(name for name in names if name.startswith("extension/libraries/"))
    report("the extension carries every library", libraries_packed == sorted(f"extension/libraries/{p.name}" for p in (ROOT / "libraries").glob("*.jamo")), str(libraries_packed))
    (folder / "helper.jamo").write_text("# Adds two numbers\nfunc add(a: int, b: int): int\n    return a + b\nend\n", encoding="utf-8")
    (folder / "game.jamo").write_text(
        'use "list.jamo"\nuse "helper.jamo"\n\nstruct Player\n    name: string  # shown on top\n    score: int\nend\n\nenum Mood\n    CALM\nend\n\n'
        "const SPEED = 4\n\nfunc Player.win(points: int)\n    self.score += points\nend\n\nfunc main()\n    var p = Player{name: \"a\", score: 0}\n"
        "    p.win(SPEED)\n    var names = [\"b\"]\n    var keys = [1]\n    sort_by(names, keys)\n    for item in names\n        print(add(1, 2))\n    end\n"
        "    var m = Mood.CALM\nend\n\nfunc other()\n    var p = 2\n    for k in 0..2\n        print(k)\n    end\n    for k in 0..3\n        print(k)\n    end\nend\n", encoding="utf-8")
    questions = [(20, "win", True), (20, "SPEED", False), (20, "p", False), (23, "sort_by", False), (25, "add", False), (24, "item", False),
                 (27, "CALM", True), (27, "Mood", False), (14, "points", False), (15, "self", False), (19, "name", True), (31, "p", False), (25, "print", False), (36, "k", False)]
    script = folder / "define.js"
    script.write_text(
        'const fs = require("fs");\nconst path = require("path");\nconst ext = require(process.argv[2]);\n'
        'const main = path.resolve("game.jamo");\nconst items = ext.gather(main, (f) => { try { return fs.readFileSync(f, "utf8"); } catch { return null; } });\n'
        'const questions = JSON.parse(process.argv[3]);\n'
        'console.log(JSON.stringify(questions.map(([line, word, dot]) => ext.find_definitions(items, word, main, line, dot).map((i) => [path.basename(i.file), i.line, i.kind, i.doc]))));\n',
        encoding="utf-8")
    result = run([node, str(script), str(ROOT / "vscode" / "extension.js"), json.dumps(questions)], cwd=folder)
    try:
        answers = json.loads(result.stdout)
    except json.JSONDecodeError:
        answers = result.stdout + result.stderr
    sort_line = next(i for i, line in enumerate((ROOT / "libraries" / "list.jamo").read_text(encoding="utf-8").splitlines()) if line.startswith("func sort_by<T>"))
    wanted = [
        [["game.jamo", 14, "method", ""]], [["game.jamo", 12, "constant", ""]], [["game.jamo", 19, "variable", ""]],
        [["list.jamo", sort_line, "function", "Sorts items by int keys, smallest first"]], [["helper.jamo", 1, "function", "Adds two numbers"]],
        [["game.jamo", 24, "variable", ""]], [["game.jamo", 9, "member", ""]], [["game.jamo", 8, "enum", ""]],
        [["game.jamo", 14, "input", ""]], [["game.jamo", 14, "input", ""]], [["game.jamo", 4, "field", "shown on top"]],
        [["game.jamo", 31, "variable", ""]], [], [["game.jamo", 35, "variable", ""]],
    ]
    report("the extension's hover and go to definition find the right place", answers == wanted, f"{answers}\nwanted {wanted}")


# --- Every example, with typed input. A second .in file runs the same program again, in the same folder ---
def test_examples(work):
    sources = sorted((ROOT / "examples").glob("*.jamo"))
    report("the six examples exist", {path.stem for path in sources} >= {"guessing_game", "calculator", "todo_list", "text_adventure", "word_counter", "prime_finder"}, str(sources))
    for source in sources:
        folder = work / "examples" / source.stem
        folder.mkdir(parents=True)
        shutil.copy(source, folder / source.name)
        shutil.copy(ROOT / "tests" / "examples" / "sample.txt", folder / "sample.txt")
        built = run([str(JAMO), "build", str(folder / source.name)])
        report(f"example builds with no warnings: {source.stem}", built.returncode == 0 and built.stderr == "", built.stderr)
        for typed in sorted((ROOT / "tests" / "examples").glob(f"{source.stem}*.in")):
            result = subprocess.run([str(folder / f"{source.stem}.exe")], input=typed.read_bytes(), cwd=folder, capture_output=True, timeout=60)
            want = clean(typed.with_suffix(".out").read_text(encoding="utf-8"))
            got = clean(result.stdout.decode(errors="replace"))
            report(f"example runs: {typed.stem}", result.returncode == 0 and got == want, f"code {result.returncode} {result.stderr}\ngot:\n{got}\nwanted:\n{want}")


# --- Every code sample in the docs, so they can't go out of date ---
def test_docs(work):
    block = re.compile(r"^```(\S*)[ ]?(\S*)\n(.*?)^```$", re.S | re.M)
    for document in ("README.md", "GUIDE.md"):
        text = clean((ROOT / document).read_text(encoding="utf-8"))
        blocks = [(kind, extra, body) for kind, extra, body in block.findall(text)]
        folder = work / "docs" / document
        folder.mkdir(parents=True)
        problems = []
        samples = 0
        for index, (kind, extra, body) in enumerate(blocks):
            if kind != "jamo":
                continue
            if extra.startswith("file="):
                (folder / extra[5:]).write_text(body, encoding="utf-8")
                continue
            samples += 1
            if extra == "check-only":
                (folder / "main.jamo").write_text(body, encoding="utf-8")
                checked = run([str(JAMO), "check", str(folder / "main.jamo")])
                if checked.returncode != 0 or checked.stderr:
                    problems.append(f"sample {samples} doesn't check cleanly:\n{checked.stderr}")
                continue
            source = folder / "main.jamo"
            source.write_text(body, encoding="utf-8")
            typed = out = error = None
            for later_kind, _, later_body in blocks[index + 1:]:
                if later_kind == "jamo":
                    break
                if later_kind == "input":
                    typed = later_body
                elif later_kind == "output":
                    out = later_body
                elif later_kind == "error":
                    error = later_body
            if out is None and error is None and samples > 0:
                problems.append(f"sample {samples} has no output to compare:\n{body}")
                continue
            result = subprocess.run([str(JAMO), "run", str(source)], input=(typed or "").encode(), cwd=folder, capture_output=True, timeout=60)
            got_out = clean(result.stdout.decode(errors="replace"))
            got_error = clean(result.stderr.decode(errors="replace"))
            if error is not None:
                if result.returncode == 0 or got_error != error:
                    problems.append(f"sample {samples}: code {result.returncode}\ngot:\n{got_error}\nwanted:\n{error}")
            elif result.returncode != 0 or got_out != out or got_error != "":
                problems.append(f"sample {samples}: code {result.returncode}\n{got_error}\ngot:\n{got_out}\nwanted:\n{out}\n{body}")
        report(f"every code sample in {document} works ({samples})", samples > 0 and not problems, "\n".join(problems[:3]))


# --- Calling Windows: many kinds of input and callbacks, a million times, with the cleaner running ---
def test_windows_calls(work):
    source = work / "windows calls.jamo"
    source.write_text('''extern "user32.dll" func CallWindowProcW(procedure: int, window: int, message: i32, first: int, second: int): int
extern "msvcrt.dll" func ldexp(x: float, power: i32): float
extern "kernel32.dll" func MulDiv(number: i32, times: i32, over: i32): i32

func on_call(window: int, message: int, first: int, second: int): int
    var text = string(first) + "-" + string(second)
    return window + message + len(text)
end

func main()
    var rounds = int(args()[0])
    var total = 0
    var kept: [string] = []
    var handler = callback(on_call)
    for i in 0..rounds
        total += CallWindowProcW(handler, i, 3, i * 2, 7)
        total += int(ldexp(float(i % 100), 2))
        total += MulDiv(i % 1000, 3, 1)
        if i % 1000 == 0
            kept.add(f"round {i}")
        end
    end
    print(total)
    print(len(kept))
    print(kept[len(kept) - 1])
end
''', encoding="utf-8")

    def expected(rounds):
        total = 0
        for i in range(rounds):
            total += i + 3 + len(f"{i * 2}-7")
            total += (i % 100) * 4
            total += (i % 1000) * 3
        kept = (rounds + 999) // 1000
        return f"{total}\n{kept}\nround {(kept - 1) * 1000}\n"

    for flags, rounds in (([], 1000000), (["--gc-test"], 20000), (["--gc-test", "--gc-check"], 5000), (["--no-optimize"], 100000)):
        run([str(JAMO), "build", *flags, str(source)])
        result = run([str(source.with_suffix(".exe")), str(rounds)], timeout=SLOW_RUN_LIMIT)
        report(f"Windows calls and callbacks stay right: {rounds} rounds {' '.join(flags) or 'normal'}", result.returncode == 0 and clean(result.stdout) == expected(rounds), f"{result.returncode} {result.stdout} {result.stderr[:300]}")


# --- Windows: open, close, input, resizing and timing. Every test window is quiet and off-screen ---
WINDOW_TEST = """use "window.jamo"
extern "user32.dll" func PostMessageW(window: int, message: i32, first: int, second: int): bool
extern "user32.dll" func GetGuiResources(process: int, kind: i32): i32
extern "kernel32.dll" func GetCurrentProcess(): int

func quiet_window(title: string): Window
    var w = open_window(title, 400, 300)
    w.quiet()
    w.move(-3000, -3000)
    w.is_open()
    return w
end

func main()
    var w = quiet_window("warm up")
    w.close()
    var before = GetGuiResources(GetCurrentProcess(), 1)
    for i in 0..100
        var each = quiet_window(f"window {i}")
        each.close()
    end
    print(GetGuiResources(GetCurrentProcess(), 1) - before)

    w = quiet_window("input")
    print(w.width())
    PostMessageW(w.handle, 256, 65, 0)
    w.is_open()
    print(f"a down {w.key_down("a")}, pressed {w.key_pressed("a")}")
    w.is_open()
    print(f"a down {w.key_down("a")}, pressed {w.key_pressed("a")}")
    PostMessageW(w.handle, 257, 65, 0)
    w.is_open()
    print(f"a down {w.key_down("a")}, released {w.key_released("a")}")
    PostMessageW(w.handle, 256, 37, 0)
    PostMessageW(w.handle, 256, 32, 0)
    w.is_open()
    print(f"left {w.key_down("left")}, space {w.key_down("space")}, right {w.key_down("right")}")
    PostMessageW(w.handle, 8, 0, 0)
    w.is_open()
    print(f"after losing focus: left {w.key_down("left")}")
    PostMessageW(w.handle, 512, 0, 70 * 65536 + 50)
    PostMessageW(w.handle, 513, 1, 70 * 65536 + 50)
    w.is_open()
    print(f"mouse {w.mouse_x()}, {w.mouse_y()}, left button {w.mouse_down("left")}, right button {w.mouse_down("right")}")
    PostMessageW(w.handle, 514, 0, 70 * 65536 + 50)
    PostMessageW(w.handle, 512, 0, 65535 * 65536 + 65526)
    PostMessageW(w.handle, 522, 120 * 65536, 0)
    PostMessageW(w.handle, 522, 120 * 65536, 0)
    w.is_open()
    print(f"mouse {w.mouse_x()}, {w.mouse_y()}, left button {w.mouse_down("left")}, wheel {w.wheel()}")
    PostMessageW(w.handle, 522, (65536 - 120) * 65536, 0)
    w.is_open()
    print(f"wheel {w.wheel()}")
    w.is_open()
    print(f"wheel {w.wheel()}")

    w.resize(640, 360)
    w.is_open()
    print(f"resized {w.resized()} to {w.width()} by {w.height()}")
    ShowWindow(w.handle, 7)
    w.is_open()
    print(f"minimized {w.minimized()}")
    ShowWindow(w.handle, 4)
    w.is_open()
    print(f"minimized {w.minimized()}, still {w.width()} by {w.height()}")
    for i in 0..20
        w.resize(200 + i * 10, 150 + i * 5)
        w.is_open()
    end
    print(f"after 20 resizes: {w.width()} by {w.height()}")
    w.set_title("renamed")

    var frames = 0
    var total = 0.0
    while w.is_open() and frames < 60
        w.wait_frame(60)
        if frames > 0
            total += w.frame_time()
        end
        frames += 1
    end
    var average = total / 59.0
    print(f"steady frames: {average > 0.0150 and average < 0.0185}")

    PostMessageW(w.handle, 16, 0, 0)
    print(f"open after the close button: {w.is_open()}")
    print(f"still open: {w.is_open()}")
    w.close()
    print("closed")
end
"""
WINDOW_WANT = """0
400
a down true, pressed true
a down true, pressed false
a down false, released true
left true, space true, right false
after losing focus: left false
mouse 50, 70, left button true, right button false
mouse -10, -1, left button false, wheel 2
wheel -1
wheel 0
resized true to 640 by 360
minimized true
minimized false, still 640 by 360
after 20 resizes: 390 by 245
steady frames: true
open after the close button: false
still open: false
closed
"""


def test_window_library(work):
    spec = importlib.util.spec_from_file_location("embed", ROOT / "libraries" / "embed.py")
    embed = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(embed)
    built_in = (ROOT / "src" / "libraries.cpp").read_text(encoding="utf-8").replace("\r\n", "\n")
    report("src/libraries.cpp matches the libraries", built_in == embed.make_source(), "Run python libraries/embed.py")
    folder = work / "window tests"
    folder.mkdir()
    source = folder / "window test.jamo"
    source.write_text(WINDOW_TEST, encoding="utf-8")
    for flags in ([], ["--gc-test"], ["--gc-test", "--gc-check"]):
        built = run([str(JAMO), "build", *flags, str(source)])
        if built.returncode != 0:
            report(f"window checks {' '.join(flags) or 'normal'}", False, built.stderr)
            continue
        result = run([str(source.with_suffix(".exe"))], timeout=SLOW_RUN_LIMIT)
        report(f"window checks: 100 windows, input, resizing, minimizing, focus and timing {' '.join(flags) or 'normal'}", result.returncode == 0 and clean(result.stdout) == WINDOW_WANT, f"{result.returncode} {result.stderr}\ngot:\n{result.stdout}")
    for name, body, want in (
        ("close twice", "w.close()\n    w.close()", "This window is already closed."),
        ("use after closing", "w.close()\n    print(w.key_down(\"a\"))", "This window is closed, so it can't be used."),
        ("unknown key", "print(w.key_down(\"banana\"))", "window doesn't know the key banana."),
    ):
        path = folder / f"{name}.jamo"
        path.write_text(f'use "window.jamo"\n\nfunc main()\n    var w = open_window("x", 200, 100)\n    w.quiet()\n    w.move(-3000, -3000)\n    w.is_open()\n    {body}\nend\n', encoding="utf-8")
        result = run([str(JAMO), "run", str(path)])
        report(f"a clear error for {name}", result.returncode == 1 and want in result.stderr and "window.jamo (built in) line" in result.stderr and f"called from main, {name}.jamo line {8 if name == 'unknown key' else 9}" in result.stderr, f"{result.returncode} {result.stderr}")


# --- 2D drawing: pixels read back, PNG files of every kind, speed, and a long run ---
def png_file(width, height, color_type, rows, level=6, palette=b"", see_through=b"", interlaced=0, depth=8):
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)
    header = struct.pack(">IIBBBBB", width, height, depth, color_type, 0, 0, interlaced)
    body = chunk(b"IHDR", header)
    if palette:
        body += chunk(b"PLTE", palette)
    if see_through:
        body += chunk(b"tRNS", see_through)
    packed = zlib.compress(b"".join(rows), level)
    middle = len(packed) // 2
    return b"\x89PNG\r\n\x1a\n" + body + chunk(b"IDAT", packed[:middle]) + chunk(b"IDAT", packed[middle:]) + chunk(b"IEND", b"")


def png_cases(rng):
    """Each PNG kind, with the red, green, blue and alpha bytes Python says it holds."""
    cases = {}
    for color_type, channels in ((0, 1), (2, 3), (4, 2), (6, 4), (3, 1)):
        for level in (0, 1, 9):
            width, height = rng.randrange(1, 40), rng.randrange(1, 30)
            raw = [bytes(rng.randrange(256) if color_type != 3 else rng.randrange(6) for _ in range(width * channels)) for _ in range(height)]
            palette = bytes(rng.randrange(256) for _ in range(18)) if color_type == 3 else b""
            see_through = bytes((255, 0, 128)) if color_type == 3 else b""
            rows = []
            for y, row in enumerate(raw):
                kind = y % 5
                above = raw[y - 1] if y else bytes(len(row))
                encoded = bytearray()
                for x, value in enumerate(row):
                    left = row[x - channels] if x >= channels else 0
                    up = above[x]
                    corner = above[x - channels] if x >= channels and y else 0
                    guess = left + up - corner
                    paeth = min((abs(guess - left), 0, left), (abs(guess - up), 1, up), (abs(guess - corner), 2, corner))[2]
                    encoded.append((value - (0, left, up, (left + up) // 2, paeth)[kind]) % 256)
                rows.append(bytes([kind]) + bytes(encoded))
            pixels = bytearray()
            for row in raw:
                for x in range(width):
                    part = row[x * channels:(x + 1) * channels]
                    if color_type == 0:
                        pixels += bytes((part[0], part[0], part[0], 255))
                    elif color_type == 2:
                        pixels += part + b"\xff"
                    elif color_type == 4:
                        pixels += bytes((part[0], part[0], part[0], part[1]))
                    elif color_type == 6:
                        pixels += part
                    else:
                        index = part[0]
                        pixels += palette[index * 3:index * 3 + 3] + bytes((see_through[index] if index < len(see_through) else 255,))
            cases[f"type{color_type}_level{level}"] = (png_file(width, height, color_type, rows, level, palette, see_through), width, height, bytes(pixels))
    return cases


GFX_TEST = """use "gfx.jamo"

func show_color(c: Color): string
    return f"{int(c.red * 255.0 + 0.5)} {int(c.green * 255.0 + 0.5)} {int(c.blue * 255.0 + 0.5)}"
end

func main()
    var w = open_window("gfx test", 320, 240)
    w.quiet()
    w.move(-3000, -3000)
    w.is_open()
    var g = start_gfx(w)
    g.clear(rgb(10, 20, 30))
    print(show_color(g.read_pixel(0, 0)))
    g.rect(10.0, 10.0, 50.0, 30.0, rgb(250, 100, 0))
    print(show_color(g.read_pixel(10, 10)))
    print(show_color(g.read_pixel(59, 39)))
    print(show_color(g.read_pixel(60, 40)))
    print(show_color(g.read_pixel(9, 10)))
    g.rect(100.0, 100.0, 20.0, 20.0, rgba(255, 255, 255, 128))
    print(show_color(g.read_pixel(110, 110)))
    g.circle(200.0, 60.0, 30.0, rgb(0, 255, 0))
    print(show_color(g.read_pixel(200, 60)))
    print(show_color(g.read_pixel(200, 85)))
    print(show_color(g.read_pixel(178, 38)))
    g.line(0.0, 200.0, 320.0, 200.0, 4.0, rgb(255, 0, 255))
    print(show_color(g.read_pixel(160, 200)))
    print(show_color(g.read_pixel(160, 205)))
    g.text("Jamo", 10.0, 150.0, 32.0, rgb(255, 255, 0))
    var lit = 0
    for x in 10..90
        for y in 150..190
            if g.read_pixel(x, y).red > 0.5
                lit += 1
            end
        end
    end
    print(f"text lit {lit > 300 and lit < 2500}")
    print(f"text width {g.text_width("Jamo", 32.0) > 40.0 and g.text_width("Jamo", 32.0) < 120.0}")
    var image = g.load_image("dot.png")
    g.image(image, 250.0, 150.0)
    print(show_color(g.read_pixel(250, 150)))
    print(show_color(g.read_pixel(253, 152)))
    g.image_sized(image, 0.0, 0.0, 40.0, 40.0)
    print(show_color(g.read_pixel(5, 5)))
    g.show()
    w.resize(200, 100)
    w.is_open()
    g.show()
    g.clear(rgb(1, 2, 3))
    print(f"after resize {w.width()} {w.height()} {show_color(g.read_pixel(199, 99))}")
    var timing = memory(32)
    QueryPerformanceFrequency(timing + 8)
    QueryPerformanceCounter(timing)
    var shapes = int(args()[0])
    for frame in 0..30
        g.clear(rgb(0, 0, 0))
        for i in 0..shapes
            var x = float((i * 37 + frame * 3) % 200)
            var y = float((i * 13) % 100)
            if i % 2 == 0
                g.rect(x, y, 4.0, 4.0, rgb(i % 256, 100, 200))
            else
                g.circle(x, y, 3.0, rgb(200, i % 256, 100))
            end
        end
        g.show()
        w.is_open()
    end
    QueryPerformanceCounter(timing + 16)
    var seconds = float(peek_i64(timing, 16) - peek_i64(timing, 0)) / float(peek_i64(timing, 8))
    print(f"frames a second with {shapes} shapes: {int(30.0 / seconds)}")
    g.close()
    w.close()
end
"""
GFX_WANT_START = """10 20 30
250 100 0
250 100 0
10 20 30
10 20 30
133 138 143
0 255 0
0 255 0
10 20 30
255 0 255
10 20 30
text lit true
text width true
255 0 0
0 0 255
255 0 0
after resize 200 100 1 2 3
"""


def test_gfx(work):
    folder = work / "gfx tests"
    folder.mkdir()
    rows = []
    for y in range(4):
        rows.append(b"\0" + b"".join(bytes((255, 0, 0, 255)) if x < 2 else bytes((0, 0, 255, 255)) for x in range(4)))
    (folder / "dot.png").write_bytes(png_file(4, 4, 6, rows))
    source = folder / "gfx test.jamo"
    source.write_text(GFX_TEST, encoding="utf-8")
    for flags in ([], ["--gc-test", "--gc-check"]):
        built = run([str(JAMO), "build", *flags, str(source)])
        result = run([str(source.with_suffix(".exe")), "100" if flags else "10000"], cwd=folder, timeout=SLOW_RUN_LIMIT) if built.returncode == 0 else built
        got = clean(result.stdout)
        report(f"gfx pixels, text, images and resizing read back right {' '.join(flags) or 'normal'}", result.returncode == 0 and got.startswith(GFX_WANT_START), f"{result.returncode} {result.stderr[:400]}\ngot:\n{got}")
        if not flags and got.startswith(GFX_WANT_START):
            speed = got.splitlines()[-1]
            print(f"  {speed}")
            report("10,000 shapes hold at least 30 frames a second", int(speed.split()[-1]) >= 30, speed)

    # Every kind of PNG, compared with what Python put in it
    rng = random.Random(SEED)
    cases = png_cases(rng)
    lines = ['use "png.jamo"', "", "func main()"]
    for name, (data, width, height, _) in cases.items():
        (folder / f"{name}.png").write_bytes(data)
        lines += [f'    var {name} = decode_png(read_file("{name}.png"))', f"    var {name}_sum = 0", f"    for i in 0..{width * height * 4}", f"        {name}_sum = ({name}_sum * 31 + peek_u8({name}.pixels, i)) % 1000000007", "    end", f'    print(f"{name} {{{name}.width}} {{{name}.height}} {{{name}_sum}}")']
    lines.append("end")
    want = ""
    for name, (_, width, height, pixels) in cases.items():
        total = 0
        for value in pixels:
            total = (total * 31 + value) % 1000000007
        want += f"{name} {width} {height} {total}\n"
    (folder / "pngs.jamo").write_text("\n".join(lines) + "\n", encoding="utf-8")
    result = run([str(JAMO), "run", str(folder / "pngs.jamo")], cwd=folder, timeout=SLOW_RUN_LIMIT)
    report(f"{len(cases)} PNG files of every kind read the same as Python made them", result.returncode == 0 and clean(result.stdout) == want, f"{result.returncode} {result.stderr[:300]}\ngot:\n{result.stdout[:600]}\nwanted:\n{want[:600]}")

    # Damaged PNG files stop with clear errors, never a crash
    good = cases["type6_level9"][0]
    damaged = {
        "empty": (b"", "damaged"),
        "not a png": (b"GIF89a" + bytes(40), "damaged"),
        "cut short": (good[:len(good) // 2], "damaged"),
        "no data end": (good[:-12], "damaged"),
        "huge chunk": (good[:8] + struct.pack(">I", 999999) + good[12:], "damaged"),
        "bad zlib": (good[:33] + struct.pack(">I", 5) + b"IDAT" + b"\x00\x00\x00\x00\x00" + b"\0\0\0\0" + good[-12:], "damaged"),
        "16 bits": (png_file(2, 2, 2, [b"\0" + bytes(12)] * 2, depth=16), "8 bits"),
        "interlaced": (png_file(2, 2, 2, [b"\0" + bytes(6)] * 2, interlaced=1), "interlaced"),
        "too little data": (png_file(4, 4, 2, [b"\0" + bytes(12)] * 2), "damaged"),
        "bad filter": (png_file(2, 1, 2, [b"\x07" + bytes(6)]), "filter"),
        "random bytes": (good[:41] + bytes(rng.randrange(256) for _ in range(60)) + good[-12:], "damaged"),
    }
    problems = []
    for name, (data, words) in damaged.items():
        (folder / "bad.png").write_bytes(data)
        (folder / "bad.jamo").write_text('use "png.jamo"\n\nfunc main()\n    var picture = decode_png(read_file("bad.png"))\n    print(picture.width)\nend\n', encoding="utf-8")
        result = run([str(JAMO), "run", str(folder / "bad.jamo")], cwd=folder)
        if result.returncode != 1 or words not in result.stderr:
            problems.append(f"{name}: code {result.returncode} {result.stderr[:200]}")
    report(f"{len(damaged)} damaged PNG files give clear errors", not problems, "\n".join(problems))

    # Closed things give clear errors
    for name, body, words in (
        ("draw after closing", "g.close()\n    g.rect(0.0, 0.0, 1.0, 1.0, rgb(0, 0, 0))", "This drawing is closed"),
        ("close twice", "g.close()\n    g.close()", "This drawing is already closed"),
        ("image after closing", 'var image = g.load_image("dot.png")\n    image.close()\n    g.image(image, 0.0, 0.0)', "This image is closed"),
    ):
        path = folder / f"{name}.jamo"
        path.write_text(f'use "gfx.jamo"\n\nfunc main()\n    var w = open_window("x", 100, 100)\n    w.quiet()\n    w.move(-3000, -3000)\n    w.is_open()\n    var g = start_gfx(w)\n    {body}\nend\n', encoding="utf-8")
        result = run([str(JAMO), "run", str(path)], cwd=folder)
        report(f"gfx gives a clear error for {name}", result.returncode == 1 and words in result.stderr, f"{result.returncode} {result.stderr[:300]}")

    # A long run keeps memory steady, and starting and stopping drawing leaks nothing. The graphics driver
    # uses memory inside the program too, so this measures memory over time instead of setting a hard limit
    long_run = folder / "long run.jamo"
    long_run.write_text('''use "gfx.jamo"
extern "kernel32.dll" func GetCurrentProcess(): int
extern "kernel32.dll" func K32GetProcessMemoryInfo(process: int, counters: int, size: i32): bool

func memory_used(): int
    var counters = memory(80)
    K32GetProcessMemoryInfo(GetCurrentProcess(), counters, 80)
    return peek_i64(counters, 72) / 1048576
end

func main()
    var w = open_window("long", 160, 120)
    w.quiet()
    w.move(-3000, -3000)
    for round in 0..20
        w.is_open()
        var g = start_gfx(w)
        g.close()
    end
    w.is_open()
    var g = start_gfx(w)
    var early = 0
    for frame in 0..20000
        g.clear(rgb(frame % 256, 0, 0))
        g.text(f"frame {frame}", 4.0, 4.0, 14.0, rgb(255, 255, 255))
        g.circle(float(frame % 160), 60.0, 10.0, rgb(0, 200, 255))
        g.show()
        w.is_open()
        if frame == 2000
            early = memory_used()
        end
    end
    var late = memory_used()
    print(f"{early} {late}")
    g.close()
    w.close()
end
''', encoding="utf-8")
    result = run([str(JAMO), "run", str(long_run)], timeout=SLOW_RUN_LIMIT)
    numbers = clean(result.stdout).split()
    steady = result.returncode == 0 and len(numbers) == 2 and int(numbers[1]) - int(numbers[0]) <= 8
    if len(numbers) == 2:
        print(f"  memory after 2,000 frames: {numbers[0]} MB, after 20,000: {numbers[1]} MB")
    report("20 restarts and 20,000 frames keep memory steady", steady, f"{result.returncode} {result.stdout} {result.stderr[:300]}")


# --- Sound: tones, WAV files and mixing, all checked against Python. Every test is quiet ---
SOUND_RATE = 44100
SOUND_HASH = 1000000007
SOUND_WAVES = ("sine", "square", "saw", "triangle", "noise")


def trunc_div(a, b):
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b > 0) else -q


def sound_sine(x):
    sign = 1.0
    t = x
    if t >= 0.5:
        t = t - 0.5
        sign = -1.0
    if t > 0.25:
        t = 0.5 - t
    a = t * 6.283185307179586
    a2 = a * a
    return sign * a * (1.0 - a2 / 6.0 * (1.0 - a2 / 20.0 * (1.0 - a2 / 42.0 * (1.0 - a2 / 72.0 * (1.0 - a2 / 110.0)))))


def py_tone(frequency, seconds, wave):
    """The clip tone() should make: interleaved left and right samples, with 32768 added"""
    frames = int(seconds * 44100.0 + 0.5)
    step = frequency / 44100.0
    phase, seed, fade, samples = 0.0, 12345, 220, []
    for f in range(frames):
        if wave == "sine":
            value = sound_sine(phase)
        elif wave == "square":
            value = 1.0 if phase < 0.5 else -1.0
        elif wave == "saw":
            value = 2.0 * phase - 1.0
        elif wave == "triangle":
            value = 4.0 * phase - 1.0 if phase < 0.5 else 3.0 - 4.0 * phase
        else:
            seed = (seed * 1103515245 + 12345) % 2147483648
            value = float(seed) / 1073741824.0 - 1.0
        if f < fade:
            value = value * float(f) / float(fade)
        if frames - 1 - f < fade:
            value = value * float(frames - 1 - f) / float(fade)
        sample = int(value * 16384.0) + 32768
        samples += [sample, sample]
        phase = phase + step
        if phase >= 1.0:
            phase = phase - 1.0
    return samples


def sound_hash(values):
    total = 0
    for value in values:
        total = (total * 31 + value) % SOUND_HASH
    return total


def wav_file(kind, bits, channels, rate, frames, extensible=False, extra_chunk=False):
    """A WAV file and the 16-bit left and right samples sound.jamo should read from it"""
    width = bits // 8
    body = bytearray()
    lefts, rights = [], []
    for f in range(frames):
        pair = []
        for c in range(channels):
            spot = (f * 7 + c * 3) % 97
            if kind == 3:
                value = struct.unpack("<f", struct.pack("<f", math.sin(f * 0.05 + c) * 1.1))[0]
                if f == 5:
                    value = 1.0
                if f == 6:
                    value = -1.0
                body += struct.pack("<f", value)
                pair.append(-32768 if not value >= -1.0 else 32767 if value > 1.0 else int(value * 32767.0))
            elif bits == 8:
                raw = (spot * 2 + f) % 256
                body.append(raw)
                pair.append((raw - 128) * 256)
            else:
                raw = (f * 2654435761 + c * 40503 + spot) % (1 << bits)
                body += raw.to_bytes(width, "little")
                top = raw >> (bits - 16)
                pair.append(top - 65536 if top > 32767 else top)
        lefts.append(pair[0])
        rights.append(pair[-1])
    tag = 65534 if extensible else kind
    fmt = struct.pack("<HHIIHH", tag, channels, rate, rate * width * channels, width * channels, bits)
    if extensible:
        fmt += struct.pack("<HHI", 22, bits, 3 if channels == 2 else 4) + struct.pack("<H", kind) + bytes(14)
    chunks = b"fmt " + struct.pack("<I", len(fmt)) + fmt
    if extra_chunk:
        chunks += b"LIST" + struct.pack("<I", 5) + b"hello" + b"\0"
    chunks += b"data" + struct.pack("<I", len(body)) + bytes(body)
    data = b"RIFF" + struct.pack("<I", 4 + len(chunks)) + b"WAVE" + chunks
    if rate == SOUND_RATE:
        out = lefts, rights
    else:
        count = frames * SOUND_RATE // rate
        out_left, out_right = [], []
        for f in range(count):
            spot = f * rate
            first, part = spot // SOUND_RATE, spot % SOUND_RATE
            second = min(first + 1, frames - 1)
            out_left.append(trunc_div(lefts[first] * (SOUND_RATE - part) + lefts[second] * part, SOUND_RATE))
            out_right.append(trunc_div(rights[first] * (SOUND_RATE - part) + rights[second] * part, SOUND_RATE))
        out = out_left, out_right
    samples = []
    for left, right in zip(*out):
        samples += [left + 32768, right + 32768]
    return data, samples


class PyVoice:
    def __init__(self, samples, volume=1.0, looping=False):
        self.samples, self.volume, self.looping = samples, volume, looping
        self.place, self.playing = 0, True


def py_fill(voices, master):
    """One 512-frame buffer as sound.jamo mixes it"""
    mix = [0] * 1024
    for voice in voices:
        if not voice.playing:
            continue
        level = int(voice.volume * master * 32768.0)
        frames = len(voice.samples) // 2
        at = 0
        while at < 512 and voice.playing:
            count = min(frames - voice.place, 512 - at)
            for f in range(count):
                mix[(at + f) * 2] += (voice.samples[(voice.place + f) * 2] - 32768) * level
                mix[(at + f) * 2 + 1] += (voice.samples[(voice.place + f) * 2 + 1] - 32768) * level
            at += count
            voice.place += count
            if voice.place == frames:
                if voice.looping:
                    voice.place = 0
                else:
                    voice.playing = False
    out = []
    for value in mix:
        value = max(-32768, min(32767, trunc_div(value, 32768)))
        out.append((value + 65536) % 65536)
    return out


SOUND_TEST = """use "sound.jamo"
extern "winmm.dll" func waveOutGetVolume(handle: int, volume: int): i32
extern "winmm.dll" func timeBeginPeriod(period: i32): i32
extern "kernel32.dll" func Sleep(milliseconds: i32)
extern "kernel32.dll" func QueryPerformanceCounter(place: int): bool
extern "kernel32.dll" func QueryPerformanceFrequency(place: int): bool
extern "kernel32.dll" func GetCurrentProcess(): int
extern "kernel32.dll" func GetProcessHandleCount(process: int, count: int): bool
extern "kernel32.dll" func K32GetProcessMemoryInfo(process: int, counters: int, size: i32): bool

func used(): string
    var count = memory(8)
    GetProcessHandleCount(GetCurrentProcess(), count)
    var counters = memory(80)
    K32GetProcessMemoryInfo(GetCurrentProcess(), counters, 80)
    return f"{peek_i32(count, 0)} {peek_i64(counters, 72) / 1024}"
end

func clip_hash(clip: Clip): int
    var total = 0
    for i in 0..clip.frames * 2
        total = (total * 31 + peek_u16(clip.samples, i * 2)) % 1000000007
    end
    return total
end

func buffer_hash(s: Sound): int
    var buffer = memory(2048)
    s.fill(buffer)
    var total = 0
    for i in 0..1024
        total = (total * 31 + peek_u16(buffer, i * 2)) % 1000000007
    end
    return total
end

func now(clock: int): float
    QueryPerformanceCounter(clock)
    return float(peek_i64(clock, 0)) / float(peek_i64(clock, 8))
end

# Plays sounds no variable keeps, so only the player holds them
func start_unkept(s: Sound)
    for i in 0..5
        s.play(tone(200.0 + float(i) * 100.0, 0.02, "triangle"))
    end
end

func main()
    var s = start_sound()
    s.quiet()
    if not s.has_device()
        print("no device")
        return
    end
    var level = memory(8)
    waveOutGetVolume(s.device, level)
    print(f"quiet volume {peek_i32(level, 0)}")
    if peek_i32(level, 0) != 0 or s.silent
        # Never play anything out loud, so stop before any sound starts
        s.close()
        return
    end

    # Tones and WAV files
    for wave in ["sine", "square", "saw", "triangle", "noise"]
        var clip = tone(523.25, 0.1, wave)
        print(f"{wave} {clip.frames} {clip_hash(clip)}")
    end
    print(tone(440.0, 1.5, "sine").seconds())
    for name in WAV_NAMES
        var clip = load_sound(f"{name}.wav")
        print(f"{name} {clip.frames} {clip_hash(clip)}")
    end

    # Mixing, volume, looping, stopping and clipping
    var a = tone(440.0, 0.05, "sine")
    var b = tone(1000.0, 0.005, "square")
    var c = load_sound("mix.wav")
    s.set_volume(0.8)
    var va = s.play(a)
    va.set_volume(0.5)
    var vb = s.play_loop(b)
    vb.set_volume(0.25)
    s.play(c)
    print(f"mix 1 {buffer_hash(s)}")
    vb.stop()
    for i in 2..6
        print(f"mix {i} {buffer_hash(s)} {va.is_playing()}")
    end
    s.update()
    print(f"left {s.playing()}")
    s.set_volume(4.0)
    for i in 0..8
        s.play(a)
    end
    print(f"loud {buffer_hash(s)}")
    s.stop_all()
    s.set_volume(1.0)

    # At most 64 at once: the oldest gives way
    var first = s.play(a)
    for i in 0..69
        s.play(a)
    end
    print(f"most {s.playing()} {first.is_playing()}")
    s.stop_all()

    # Sounds nothing else holds stay whole while the cleaner runs
    start_unkept(s)
    var junk: [string] = []
    for i in 0..3000
        junk.add(f"junk {i}")
        if i % 100 == 0
            junk = []
        end
    end
    print(f"kept {buffer_hash(s)} {buffer_hash(s)}")
    s.stop_all()

    # The speakers really play, and many overlapping sounds keep up at 60 frames a second
    timeBeginPeriod(1)
    var clock = memory(16)
    QueryPerformanceFrequency(clock + 8)
    var clips = [tone(330.0, 1.2, "sine"), tone(660.0, 0.8, "square"), tone(990.0, 1.5, "triangle"), tone(120.0, 2.0, "noise")]
    s.update()
    var start = now(clock)
    var played_at_start = s.played()
    var gaps_at_start = s.gaps()
    var slowest = 0.0
    var most = 0
    for frame in 0..FRAMES
        var before = now(clock)
        s.play(clips[frame % 4])
        s.update()
        var took = now(clock) - before
        if took > slowest
            slowest = took
        end
        if s.playing() > most
            most = s.playing()
        end
        while now(clock) - start < float(frame + 1) / 60.0
            Sleep(1)
        end
    end
    var seconds = now(clock) - start
    var played = float(s.played() - played_at_start) / 44100.0
    print(f"overlap most {most} gaps {s.gaps() - gaps_at_start}")
    print(f"timing {seconds} {played} {slowest}")
    s.close()

    # Opening and closing the speakers again and again leaks nothing. Memory goes up and down as the cleaner runs,
    # so it's looked at every 50 rounds, and only its low points must stay level
    for round in 0..REOPEN_ROUNDS
        if round == 21
            print(f"before {used()}")
        end
        if round % 50 == 0
            print(f"sample {used()}")
        end
        var again = start_sound()
        again.quiet()
        again.play(a)
        again.update()
        again.close()
    end
    print(f"after {used()}")

    # A missing device runs on in silence
    var none = start_sound_device(999)
    print(f"none {none.has_device()} {none.play(a).is_playing()} {none.playing()}")
    none.update()
    none.close()
end
"""


def test_sound(work):
    folder = work / "sound tests"
    folder.mkdir()
    rng = random.Random(SEED)
    want = ["quiet volume 0"]
    for wave in SOUND_WAVES:
        samples = py_tone(523.25, 0.1, wave)
        want.append(f"{wave} {len(samples) // 2} {sound_hash(samples)}")
    want.append("1.5")
    kinds = {
        "pcm8_mono": (1, 8, 1, 44100, False), "pcm16_mono": (1, 16, 1, 44100, False), "pcm16_stereo": (1, 16, 2, 44100, True),
        "pcm24_stereo": (1, 24, 2, 44100, False), "pcm32_mono": (1, 32, 1, 44100, False), "float_stereo": (3, 32, 2, 44100, False),
        "pcm16_22050": (1, 16, 2, 22050, False), "pcm16_48000": (1, 16, 1, 48000, True), "pcm8_8000": (1, 8, 2, 8000, False),
        "extensible16": (1, 16, 2, 44100, False), "extensible_float_48000": (3, 32, 2, 48000, False),
    }
    for name, (kind, bits, channels, rate, extra) in kinds.items():
        data, samples = wav_file(kind, bits, channels, rate, rng.randrange(900, 3000), "extensible" in name, extra)
        (folder / f"{name}.wav").write_bytes(data)
        want.append(f"{name} {len(samples) // 2} {sound_hash(samples)}")
    mix_data, mix_samples = wav_file(1, 16, 2, 44100, 700)
    (folder / "mix.wav").write_bytes(mix_data)

    a, b = py_tone(440.0, 0.05, "sine"), py_tone(1000.0, 0.005, "square")
    va, vb, vc = PyVoice(a, 0.5), PyVoice(b, 0.25, True), PyVoice(mix_samples)
    want.append(f"mix 1 {sound_hash(py_fill([va, vb, vc], 0.8))}")
    vb.playing = False
    for i in range(2, 6):
        want.append(f"mix {i} {sound_hash(py_fill([va, vc], 0.8))} {'true' if va.playing else 'false'}")
    want.append("left 0")
    want.append(f"loud {sound_hash(py_fill([PyVoice(a) for _ in range(8)], 4.0))}")
    want.append("most 64 false")
    unkept = [PyVoice(py_tone(200.0 + float(i) * 100.0, 0.02, "triangle")) for i in range(5)]
    want.append(f"kept {sound_hash(py_fill(unkept, 1.0))} {sound_hash(py_fill(unkept, 1.0))}")
    want.append("overlap most 64 gaps 0")

    source = folder / "sound test.jamo"
    for flags in ([], ["--gc-test", "--gc-check"]):
        frames = 600 if not flags else 180
        source.write_text(SOUND_TEST.replace("WAV_NAMES", "[" + ", ".join(f'"{name}"' for name in kinds) + "]").replace("FRAMES", str(frames)).replace("REOPEN_ROUNDS", "121" if flags else "2001"), encoding="utf-8")
        built = run([str(JAMO), "build", *flags, str(source)])
        result = run([str(source.with_suffix(".exe"))], cwd=folder, timeout=SLOW_RUN_LIMIT) if built.returncode == 0 else built
        lines = clean(result.stdout).splitlines()
        mode = " ".join(flags) or "normal"
        if lines[:1] == ["no device"]:
            report(f"sound checks {mode}: this PC has no sound device, so they didn't run", False)
            continue
        report(f"tones, {len(kinds)} kinds of WAV file, mixing, volume, looping and clipping match Python {mode}", result.returncode == 0 and lines[:len(want) - 1] == want[:-1],
               f"{result.returncode} {result.stderr[:400]}\n" + "\n".join(f"{'  ' if g == w else '! '}{g} | want {w}" for g, w in zip(lines, want)))
        report(f"{frames} frames of overlapping sounds run with no gaps {mode}", len(lines) > len(want) and lines[len(want) - 1] == want[-1], "\n".join(lines[len(want) - 1:]))
        timing = lines[len(want)].split() if len(lines) > len(want) else []
        if len(timing) == 4:
            seconds, played, slowest = (float(x) for x in timing[1:])
            print(f"  {seconds:.2f} s of frames, the speakers played {played:.2f} s, the slowest update took {slowest * 1000:.2f} ms")
            report(f"the speakers keep pace with the clock {mode}", abs(played - seconds) < 0.15, f"{seconds} {played}")
            report(f"updates stay short {mode}", slowest < 0.008, f"{slowest}")
        counts = [line.split()[1:] for line in lines if line.startswith(("before ", "after "))]
        samples = [int(line.split()[2]) for line in lines if line.startswith("sample ")]
        half = len(samples) // 2
        # A cleaner cycle here is hundreds of rounds, so the low points still move a little. A real leak moves them by many MB
        memory_level = len(samples) >= 4 and min(samples[half:]) - min(samples[1:half]) < 1024
        steady = len(counts) == 2 and int(counts[1][0]) - int(counts[0][0]) <= 2 and (bool(flags) or memory_level)
        # --gc-check keeps every freed block on purpose, so there only handles are counted
        rounds = 120 if flags else 2000
        report(f"opening and closing the speakers {rounds} times leaks no handles{'' if flags else ' or memory'} {mode}", steady, f"{counts} {samples}")
        report(f"a missing sound device gives a note and runs on {mode}", lines[-2:] == ["Note: there's no sound device 999 to use, so the program runs without sound.", "none false false 0"], "\n".join(lines[-2:]))

    # Damaged WAV files and misuse stop with clear errors, never a crash
    good, _ = wav_file(1, 16, 2, 44100, 50)
    data_at = good.index(b"data")
    damaged = {
        "empty": (b"", "doesn't start like one"),
        "not a wav": (b"RIFF" + bytes(4) + b"AVI " + bytes(40), "doesn't start like one"),
        "no format": (good[:12] + good[data_at:], "comes before its format"),
        "no sound": (good[:data_at], "no sound part"),
        "short format": (good[:12] + b"fmt " + struct.pack("<I", 8) + good[20:28], "too short"),
        "three channels": (good[:22] + struct.pack("<H", 3) + good[24:], "3 channels"),
        "12 bits": (good[:34] + struct.pack("<H", 12) + good[36:], "can't read yet"),
        "adpcm": (good[:20] + struct.pack("<H", 2) + good[22:], "can't read yet"),
        "rate zero": (good[:24] + struct.pack("<I", 0) + good[28:], "isn't usable"),
        "random bytes": (good[:12] + bytes(rng.randrange(256) for _ in range(80)), "WAV"),
    }
    problems = []
    for name, (data, words) in damaged.items():
        (folder / "bad.wav").write_bytes(data)
        (folder / "bad.jamo").write_text('use "sound.jamo"\n\nfunc main()\n    var clip = load_sound("bad.wav")\n    print(clip.frames)\nend\n', encoding="utf-8")
        result = run([str(JAMO), "run", str(folder / "bad.jamo")], cwd=folder)
        if result.returncode != 1 or words not in result.stderr or "sound.jamo (built in)" not in result.stderr:
            problems.append(f"{name}: code {result.returncode} {result.stdout[:100]} {result.stderr[:200]}")
    (folder / "cut.wav").write_bytes(good[:-41])
    (folder / "cut.jamo").write_text('use "sound.jamo"\n\nfunc main()\n    print(load_sound("cut.wav").frames)\nend\n', encoding="utf-8")
    result = run([str(JAMO), "run", str(folder / "cut.jamo")], cwd=folder)
    if clean(result.stdout) != "39\n":
        problems.append(f"cut short: {result.returncode} {result.stdout} {result.stderr[:200]}")
    report(f"{len(damaged)} damaged WAV files give clear errors, and a cut-short one plays what's there", not problems, "\n".join(problems))

    for name, body, words in (
        ("close twice", "s.close()\n    s.close()", "This sound player is already closed."),
        ("play after closing", 's.close()\n    s.play(tone(440.0, 0.1, "sine"))', "This sound player is closed, so it can't be used."),
        ("a bad volume", "s.set_volume(-0.5)", "A volume goes from 0.0 to 4.0, not -0.5."),
        ("an unknown wave", 'tone(440.0, 0.1, "wobble")', "sound doesn't know the wave wobble."),
        ("a bad frequency", 'tone(0.0, 0.1, "sine")', "A tone's frequency goes from above 0.0 to 20000.0, not 0.0."),
        ("a bad length", 'tone(440.0, -1.0, "sine")', "A tone lasts from 0.0 to 600.0 seconds, not -1.0."),
    ):
        path = folder / f"{name}.jamo"
        path.write_text(f'use "sound.jamo"\n\nfunc main()\n    var s = start_sound()\n    s.quiet()\n    {body}\nend\n', encoding="utf-8")
        result = run([str(JAMO), "run", str(path)], cwd=folder)
        report(f"sound gives a clear error for {name}", result.returncode == 1 and words in result.stderr and "sound.jamo (built in) line" in result.stderr, f"{result.returncode} {result.stderr[:300]}")


# --- UI: test code clicks and types through every control with real window messages, then resizes ---
UI_TEST = """use "ui.jamo"
extern "user32.dll" func PostMessageW(window: int, message: i32, first: int, second: int): bool

struct Form
    name: string
    music: bool
    effects: bool
    volume: float
    level: int
    saved: int
    cancelled: int
    places: [float]
end

func draw(f: Form, ui: Ui, g: Gfx, w: Window)
    g.clear(ui.theme.background)
    ui.begin()
    ui.title("Settings")
    ui.panel(16.0, 64.0, float(w.width()) - 32.0, float(w.height()) - 80.0)
    f.places = []
    f.name = ui.text_box(f.name)
    keep(f, ui)
    f.music = ui.checkbox("Music", f.music)
    keep(f, ui)
    f.effects = ui.checkbox("Sound effects", f.effects)
    keep(f, ui)
    f.volume = ui.slider("Volume", f.volume, 0.0, 1.0)
    keep(f, ui)
    f.level = ui.list(["Meadow", "Caves", "Sky Fortress", "Lava Lake", "Ice", "Desert", "Moon", "Space"], f.level, 4)
    keep(f, ui)
    ui.columns(2)
    if ui.button("Cancel")
        f.cancelled += 1
    end
    keep(f, ui)
    if ui.button("Save")
        f.saved += 1
    end
    keep(f, ui)
    ui.finish()
    g.show()
end

func keep(f: Form, ui: Ui)
    f.places.add(ui.last_x)
    f.places.add(ui.last_y)
    f.places.add(ui.last_width)
    f.places.add(ui.last_height)
end

func spot(x: float, y: float): int
    return int(y) * 65536 + int(x)
end

func middle(f: Form, control: int): int
    return spot(f.places[control * 4] + f.places[control * 4 + 2] / 2.0, f.places[control * 4 + 1] + f.places[control * 4 + 3] / 2.0)
end

func step(f: Form, ui: Ui, g: Gfx, w: Window)
    w.is_open()
    draw(f, ui, g, w)
end

func click_at(f: Form, ui: Ui, g: Gfx, w: Window, place: int)
    PostMessageW(w.handle, 512, 0, place)
    PostMessageW(w.handle, 513, 1, place)
    step(f, ui, g, w)
    PostMessageW(w.handle, 514, 0, place)
    step(f, ui, g, w)
end

func key(f: Form, ui: Ui, g: Gfx, w: Window, code: int)
    PostMessageW(w.handle, 256, code, 0)
    step(f, ui, g, w)
    PostMessageW(w.handle, 257, code, 0)
    step(f, ui, g, w)
end

func type_text(f: Form, ui: Ui, g: Gfx, w: Window, words: string)
    for i in 0..len(words)
        PostMessageW(w.handle, 258, byte(words, i), 0)
    end
    step(f, ui, g, w)
end

func near(a: float, b: float): bool
    return a - b < 0.01 and b - a < 0.01
end

func main()
    var w = open_window("ui test", 480, 560)
    w.quiet()
    w.move(-3000, -3000)
    w.is_open()
    var g = start_gfx(w)
    var ui = start_ui(g)
    var f = Form{name: "Jam", music: false, effects: false, volume: 0.5, level: -1, saved: 0, cancelled: 0, places: []}
    step(f, ui, g, w)
    print(f"text box {f.places[0]} {f.places[1]} {f.places[2]} {f.places[3]}")

    # Text box: click, type, Backspace, Home, End, the arrows and Delete
    type_text(f, ui, g, w, "nothing has focus")
    print(f"before {f.name} {ui.focus}")
    click_at(f, ui, g, w, spot(f.places[0] + f.places[2] - 20.0, f.places[1] + 18.0))
    type_text(f, ui, g, w, "mo rocks")
    print(f"typed {f.name} {ui.focus}")
    key(f, ui, g, w, 8)
    key(f, ui, g, w, 8)
    key(f, ui, g, w, 36)
    type_text(f, ui, g, w, "X")
    key(f, ui, g, w, 35)
    key(f, ui, g, w, 37)
    key(f, ui, g, w, 46)
    print(f"edited {f.name}")
    click_at(f, ui, g, w, spot(f.places[0] + 13.0, f.places[1] + 18.0))
    type_text(f, ui, g, w, "<")
    print(f"clicked at the front {f.name}")
    type_text(f, ui, g, w, "wwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwww")
    print(f"long text scrolls {ui.view > 0} {len(f.name)}")
    g.save_bmp("long text.bmp")

    # Checkboxes: clicks on the box and on the words
    click_at(f, ui, g, w, spot(f.places[8] + 10.0, f.places[9] + 14.0))
    print(f"effects {f.effects}")
    click_at(f, ui, g, w, spot(f.places[8] + 80.0, f.places[9] + 14.0))
    print(f"effects {f.effects}")
    click_at(f, ui, g, w, middle(f, 1))
    print(f"music {f.music} focus {ui.focus}")

    # Tab and Shift+Tab move the keyboard, and Space works the checkbox it lands on
    key(f, ui, g, w, 9)
    key(f, ui, g, w, 32)
    print(f"tab {ui.focus} effects {f.effects}")
    PostMessageW(w.handle, 256, 16, 0)
    key(f, ui, g, w, 9)
    key(f, ui, g, w, 9)
    PostMessageW(w.handle, 257, 16, 0)
    step(f, ui, g, w)
    print(f"shift tab {ui.focus}")
    key(f, ui, g, w, 9)
    key(f, ui, g, w, 9)
    key(f, ui, g, w, 9)
    print(f"tab to the slider {ui.focus}")

    # Slider: drag along the track, then the arrows, End and Home
    var track_x = f.places[12] + f.places[14] * 0.35 + 10.0
    var track_width = f.places[14] - f.places[14] * 0.35 - 76.0
    var y = f.places[13] + 18.0
    PostMessageW(w.handle, 512, 0, spot(track_x + 2.0, y))
    PostMessageW(w.handle, 513, 1, spot(track_x + 2.0, y))
    step(f, ui, g, w)
    PostMessageW(w.handle, 512, 1, spot(track_x + track_width * 0.75 + 0.5, y))
    step(f, ui, g, w)
    print(f"dragged {near(f.volume, 0.75)}")
    PostMessageW(w.handle, 512, 1, spot(track_x + track_width + 60.0, y))
    step(f, ui, g, w)
    PostMessageW(w.handle, 514, 0, spot(track_x + track_width + 60.0, y))
    step(f, ui, g, w)
    print(f"dragged past the end {f.volume}")
    key(f, ui, g, w, 37)
    key(f, ui, g, w, 37)
    print(f"left twice {near(f.volume, 0.9)}")
    key(f, ui, g, w, 36)
    print(f"home {f.volume}")
    key(f, ui, g, w, 39)
    print(f"right {near(f.volume, 0.05)}")
    click_at(f, ui, g, w, spot(f.places[12] + 10.0, y))
    print(f"a click on the words only takes the keyboard {near(f.volume, 0.05)} {ui.focus}")

    # List: click a row, arrows scroll it into view, End, the wheel and Home
    click_at(f, ui, g, w, spot(f.places[16] + 40.0, f.places[17] + 4.0 + 30.0 + 15.0))
    print(f"list {f.level} focus {ui.focus}")
    key(f, ui, g, w, 40)
    key(f, ui, g, w, 40)
    key(f, ui, g, w, 40)
    print(f"down 3 {f.level} scroll {ui.scrolls[4]}")
    key(f, ui, g, w, 35)
    print(f"end {f.level} scroll {ui.scrolls[4]}")
    PostMessageW(w.handle, 512, 0, middle(f, 4))
    PostMessageW(w.handle, 522, 2 * 120 * 65536, middle(f, 4))
    step(f, ui, g, w)
    print(f"wheel scroll {ui.scrolls[4]} level {f.level}")
    click_at(f, ui, g, w, spot(f.places[16] + 40.0, f.places[17] + 4.0 + 15.0))
    print(f"clicked the top row {f.level}")
    key(f, ui, g, w, 36)
    print(f"home {f.level}")

    # Buttons: a click, a press that slides off, and Enter with the keyboard
    click_at(f, ui, g, w, middle(f, 6))
    print(f"saved {f.saved} cancelled {f.cancelled}")
    PostMessageW(w.handle, 512, 0, middle(f, 5))
    PostMessageW(w.handle, 513, 1, middle(f, 5))
    step(f, ui, g, w)
    PostMessageW(w.handle, 512, 1, spot(10.0, 10.0))
    PostMessageW(w.handle, 514, 0, spot(10.0, 10.0))
    step(f, ui, g, w)
    print(f"slid off {f.cancelled} focus {ui.focus}")
    click_at(f, ui, g, w, spot(f.places[20] + 4.0, f.places[21] + f.places[23] + 40.0))
    print(f"clicked nothing {ui.focus}")
    PostMessageW(w.handle, 256, 16, 0)
    key(f, ui, g, w, 9)
    key(f, ui, g, w, 9)
    PostMessageW(w.handle, 257, 16, 0)
    step(f, ui, g, w)
    key(f, ui, g, w, 13)
    print(f"enter {ui.focus} cancelled {f.cancelled} saved {f.saved}")
    key(f, ui, g, w, 32)
    print(f"space {f.cancelled}")
    key(f, ui, g, w, 9)
    key(f, ui, g, w, 9)
    print(f"tab wraps around {ui.focus}")
    PostMessageW(w.handle, 256, 16, 0)
    key(f, ui, g, w, 9)
    PostMessageW(w.handle, 257, 16, 0)
    step(f, ui, g, w)
    g.save_bmp("focus.bmp")

    # Layouts follow the window's size
    for size in [[300, 460], [760, 520], [480, 560]]
        w.resize(size[0], size[1])
        step(f, ui, g, w)
        step(f, ui, g, w)
        print(f"{size[0]} wide: text box {f.places[2]}, buttons {f.places[22]} at {f.places[20]} and {f.places[24]}, list {f.places[18]}")
        g.save_bmp(f"size {size[0]}.bmp")
    end
    var accent = g.read_pixel(int(f.places[8]) + 11, int(f.places[9]) + 6)
    print(f"checked box {int(accent.red * 255.0 + 0.5)} {int(accent.green * 255.0 + 0.5)} {int(accent.blue * 255.0 + 0.5)}")
    var button = g.read_pixel(int(f.places[24]) + 6, int(f.places[25]) + 4)
    print(f"button {int(button.red * 255.0 + 0.5)} {int(button.green * 255.0 + 0.5)} {int(button.blue * 255.0 + 0.5)}")

    # Restyling
    ui.theme = dark_theme()
    ui.theme.accent = rgb(230, 120, 40)
    step(f, ui, g, w)
    var restyled = g.read_pixel(int(f.places[8]) + 11, int(f.places[9]) + 6)
    print(f"restyled {int(restyled.red * 255.0 + 0.5)} {int(restyled.green * 255.0 + 0.5)} {int(restyled.blue * 255.0 + 0.5)}")
    g.save_bmp("dark.bmp")
    g.close()
    w.close()
end
"""


def ui_want():
    def layout(width):
        inner = float(width - 64)
        half = (inner - 8.0) / 2.0
        return f"{width} wide: text box {py_float(inner)}, buttons {py_float(half)} at {py_float(32.0)} and {py_float(32.0 + half + 8.0)}, list {py_float(inner)}"
    return [
        "text box 32.0 80.0 416.0 36.0",
        "before Jam -1",
        "typed Jammo rocks 0",
        "edited XJammo ro",
        "clicked at the front <XJammo ro",
        "long text scrolls true 50",
        "effects true",
        "effects false",
        "music true focus 1",
        "tab 2 effects true",
        "shift tab 0",
        "tab to the slider 3",
        "dragged true",
        "dragged past the end 1.0",
        "left twice true",
        "home 0.0",
        "right true",
        "a click on the words only takes the keyboard true 3",
        "list 1 focus 4",
        "down 3 4 scroll 1",
        "end 7 scroll 4",
        "wheel scroll 2 level 7",
        "clicked the top row 2",
        "home 0",
        "saved 1 cancelled 0",
        "slid off 0 focus 5",
        "clicked nothing -1",
        "enter 5 cancelled 1 saved 1",
        "space 2",
        "tab wraps around 0",
        layout(300), layout(760), layout(480),
        "checked box 22 112 90",
        "button 224 228 223",
        "restyled 230 120 40",
    ]


def py_float(value):
    text = f"{value:.6f}".rstrip("0")
    return text + "0" if text.endswith(".") else text


def test_ui(work):
    folder = work / "ui tests"
    folder.mkdir()
    source = folder / "ui test.jamo"
    source.write_text(UI_TEST, encoding="utf-8")
    want = ui_want()
    for flags in ([], ["--gc-test", "--gc-check"]):
        built = run([str(JAMO), "build", *flags, str(source)])
        result = run([str(source.with_suffix(".exe"))], cwd=folder, timeout=SLOW_RUN_LIMIT) if built.returncode == 0 else built
        got = clean(result.stdout).splitlines()
        report(f"test code clicks and types through every control, and layouts follow resizing {' '.join(flags) or 'normal'}", result.returncode == 0 and got == want,
               f"{result.returncode} {result.stderr[:400]}\n" + "\n".join(f"{'  ' if g == w else '! '}{g} | want {w}" for g, w in zip(got + [''] * len(want), want)))
    shutil.copytree(folder, Path(tempfile.gettempdir()) / "jamo ui frames", dirs_exist_ok=True, ignore=shutil.ignore_patterns("*.exe", "*.jamo"))

    for name, body, words in (
        ("no columns", "ui.columns(0)", "ui needs at least 1 column, not 0."),
        ("a slider the wrong way round", "ui.slider(\"x\", 0.5, 1.0, 0.0)", "A slider's high end must be above its low end"),
        ("a list with no rows", "ui.list([\"a\"], 0, 0)", "A list shows at least 1 row, not 0."),
    ):
        path = folder / f"{name}.jamo"
        path.write_text(f'use "ui.jamo"\n\nfunc main()\n    var w = open_window("x", 200, 100)\n    w.quiet()\n    w.move(-3000, -3000)\n    w.is_open()\n    var ui = start_ui(start_gfx(w))\n    ui.begin()\n    {body}\nend\n', encoding="utf-8")
        result = run([str(JAMO), "run", str(path)], cwd=folder)
        report(f"ui gives a clear error for {name}", result.returncode == 1 and words in result.stderr and "ui.jamo (built in) line" in result.stderr, f"{result.returncode} {result.stderr[:300]}")


# --- 3D: pixels read back for depth, light, fog and textures, a camera that goes anywhere, and models ---
G3D_TEST = """use "g3d.jamo"

func shown(g: Gfx, x: int, y: int): string
    var c = g.read_pixel(x, y)
    return f"{int(c.red * 255.0 + 0.5)} {int(c.green * 255.0 + 0.5)} {int(c.blue * 255.0 + 0.5)}"
end

func flat_light(g3: G3d)
    g3.ambient(rgb(255, 255, 255))
    g3.sun(vec3(0.0, -1.0, 0.0), rgb(0, 0, 0))
    g3.no_fog()
    g3.lamp_off(0)
end

func one(): Vec3
    return vec3(1.0, 1.0, 1.0)
end

func zero(): Vec3
    return vec3(0.0, 0.0, 0.0)
end

func main()
    var shapes = int(args()[0])
    var w = open_window("g3d test", 320, 240)
    w.quiet()
    w.move(-3000, -3000)
    w.is_open()
    var g = start_gfx(w)
    var g3 = start_3d(g)
    var cube = g3.cube()
    var ball = g3.sphere(16)
    var ground = g3.plane(1.0)
    var halves = g3.load_texture("halves.png")
    var red = rgb(220, 40, 30)
    var blue = rgb(30, 60, 210)

    # Depth: the nearer cube wins whichever is drawn first
    flat_light(g3)
    g3.camera(vec3(0.0, 0.0, 5.0), zero())
    g.clear(rgb(0, 0, 0))
    g3.draw(cube, vec3(0.0, 0.0, 1.0), zero(), one(), red)
    g3.draw(cube, vec3(0.0, 0.0, -1.0), zero(), vec3(8.0, 8.0, 1.0), blue)
    print(f"near first {shown(g, 160, 120)} edge {shown(g, 60, 120)}")
    g.clear(rgb(0, 0, 0))
    g3.draw(cube, vec3(0.0, 0.0, -1.0), zero(), vec3(8.0, 8.0, 1.0), blue)
    g3.draw(cube, vec3(0.0, 0.0, 1.0), zero(), one(), red)
    print(f"far first {shown(g, 160, 120)} edge {shown(g, 60, 120)}")
    g.clear(rgb(0, 0, 0))
    g3.draw(cube, vec3(0.0, 0.0, 1.0), zero(), one(), red)
    g.clear(rgb(9, 9, 9))
    g3.draw(cube, vec3(0.0, 0.0, -1.0), zero(), vec3(8.0, 8.0, 1.0), blue)
    print(f"clear wipes depth {shown(g, 160, 120)}")

    # Sunlight: full on a face turned to it, none on a face turned away
    g3.ambient(rgb(0, 0, 0))
    g3.sun(vec3(0.0, 0.0, -1.0), rgb(255, 255, 255))
    g.clear(rgb(0, 0, 0))
    g3.draw(cube, zero(), zero(), one(), red)
    print(f"sun on {shown(g, 160, 120)}")
    g3.sun(vec3(0.0, 0.0, 1.0), rgb(255, 255, 255))
    g.clear(rgb(0, 0, 0))
    g3.draw(cube, zero(), zero(), one(), red)
    print(f"sun behind {shown(g, 160, 120)}")
    g3.sun(vec3(0.0, 0.0, -1.0), rgb(255, 255, 255))
    g.clear(rgb(0, 0, 0))
    g3.draw(cube, zero(), vec3(0.0, 180.0, 0.0), vec3(3.0, 0.5, 1.0), red)
    print(f"turned and stretched {shown(g, 160, 120)}")

    # A lamp: brightest close by, fading out to its reach
    g3.sun(vec3(0.0, -1.0, 0.0), rgb(0, 0, 0))
    g3.lamp(1, vec3(0.0, 0.0, 3.0), rgb(255, 255, 255), 100.0)
    g.clear(rgb(0, 0, 0))
    g3.draw(cube, zero(), zero(), one(), rgb(200, 200, 200))
    print(f"lamp {shown(g, 160, 120)}")
    g3.lamp(1, vec3(0.0, 0.0, 3.0), rgb(255, 255, 255), 2.0)
    g.clear(rgb(0, 0, 0))
    g3.draw(cube, zero(), zero(), one(), rgb(200, 200, 200))
    print(f"lamp out of reach {shown(g, 160, 120)}")
    g3.lamp(1, vec3(0.0, 0.0, 3.0), rgb(255, 255, 255), 100.0)
    g3.lamp_off(1)
    g.clear(rgb(0, 0, 0))
    g3.draw(cube, zero(), zero(), one(), rgb(200, 200, 200))
    print(f"lamp off {shown(g, 160, 120)}")

    # A stretched mesh bends its normals the other way: a triangle facing the camera with its normal
    # tilted 45 degrees up, stretched 2 tall, then turned onto its side, should light as if tilted about 27 degrees
    var tilted = g3.mesh([-1.0, -1.0, 0.0, 0.0, 0.7071, 0.7071, 0.0, 0.0, 1.0, -1.0, 0.0, 0.0, 0.7071, 0.7071, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.7071, 0.7071, 0.0, 0.0])
    g3.sun(vec3(0.0, 0.0, -1.0), rgb(255, 255, 255))
    g.clear(rgb(0, 0, 0))
    g3.draw(tilted, zero(), vec3(0.0, 0.0, 90.0), vec3(1.0, 2.0, 1.0), rgb(200, 200, 200))
    print(f"stretched normal {shown(g, 160, 120)}")
    g3.sun(vec3(0.0, -1.0, 0.0), rgb(0, 0, 0))
    print(f"meshes {cube.count} {ball.count} {ground.count}")

    # Fog: far things take its color
    flat_light(g3)
    g3.fog(rgb(128, 128, 128), 1.0, 2.0)
    g.clear(rgb(0, 0, 0))
    g3.draw(cube, zero(), zero(), one(), red)
    print(f"fog {shown(g, 160, 120)}")
    g3.fog(rgb(128, 128, 128), 50.0, 60.0)
    g.clear(rgb(0, 0, 0))
    g3.draw(cube, zero(), zero(), one(), red)
    print(f"no fog yet {shown(g, 160, 120)}")
    g3.no_fog()

    # A texture, seen from straight above: red on the left half, blue on the right
    g3.camera(vec3(0.0, 3.0, 0.0), zero())
    g.clear(rgb(0, 0, 0))
    g3.draw_textured(ground, halves, zero(), zero(), vec3(4.0, 1.0, 4.0))
    print(f"texture {shown(g, 80, 120)} and {shown(g, 240, 120)}")
    var tiled = g3.plane(2.0)
    g.clear(rgb(0, 0, 0))
    g3.draw_textured(tiled, halves, zero(), zero(), vec3(4.0, 1.0, 4.0))
    print(f"texture repeats {shown(g, 125, 120)} {shown(g, 195, 120)} {shown(g, 230, 120)}")

    # 2D drawing goes on top of 3D, and 3D still works after it
    g3.camera(vec3(0.0, 0.0, 5.0), zero())
    g.clear(rgb(0, 0, 0))
    g3.draw(cube, zero(), zero(), one(), red)
    g.rect(150.0, 110.0, 20.0, 20.0, rgb(0, 255, 0))
    g3.draw(ball, vec3(-1.5, 0.0, 0.0), zero(), one(), blue)
    print(f"2D on top {shown(g, 160, 120)}, ball {shown(g, 93, 120)}")
    g.show()
    g.clear(rgb(0, 0, 0))
    g.rect(100.0, 60.0, 120.0, 120.0, rgb(0, 255, 0))
    g3.draw(cube, zero(), zero(), one(), red)
    print(f"3D over earlier 2D {shown(g, 160, 120)} {shown(g, 105, 65)}")
    g.save_bmp("mixed.bmp")

    # Resizing keeps the picture's shape: the front of the cube stays square
    w.resize(480, 200)
    g.show()
    w.is_open()
    g.show()
    g.clear(rgb(0, 0, 0))
    g3.draw(cube, zero(), zero(), one(), red)
    var across = 0
    var down = 0
    for x in 0..480
        if shown(g, x, 100) == "220 40 30"
            across += 1
        end
    end
    for y in 0..200
        if shown(g, 240, y) == "220 40 30"
            down += 1
        end
    end
    print(f"square after resizing {across - down < 3 and down - across < 3} {down > 20}")
    w.resize(320, 240)
    g.show()
    w.is_open()
    g.show()

    # The camera anywhere, even straight up or down, still sees the cube it looks at
    var seed = 7
    var missed = 0
    for i in 0..300
        seed = (seed * 1103515245 + 12345) % 2147483648
        var a = float(seed % 10000) / 10000.0 * 6.283185307179586
        seed = (seed * 1103515245 + 12345) % 2147483648
        var b = float(seed % 10000) / 10000.0 * 2.0 - 1.0
        seed = (seed * 1103515245 + 12345) % 2147483648
        var far = 1.5 + float(seed % 10000) / 10000.0 * 78.0
        if i % 50 == 0
            b = 1.0
        elif i % 50 == 1
            b = -1.0
        end
        var side = sqrt(1.0 - b * b)
        var target = vec3(float(i % 7) * 3.0, float(i % 5) - 2.0, float(i % 3) * -4.0)
        g3.camera(vec3(target.x + cos(a) * side * far, target.y + b * far, target.z + sin(a) * side * far), target)
        g.clear(rgb(0, 0, 0))
        g3.draw(cube, target, vec3(float(i * 37 % 360), float(i * 11 % 360), 0.0), one(), red)
        if shown(g, 160, 120) != "220 40 30"
            missed += 1
        end
    end
    print(f"camera anywhere missed {missed}")
    g3.camera(vec3(1.0, 1.0, 1.0), vec3(1.0, 1.0, 1.0))
    g3.draw(cube, zero(), zero(), one(), red)
    g3.camera(vec3(1000000.0, -1000000.0, 3.0), zero())
    g3.draw(cube, zero(), zero(), one(), red)
    g3.camera(vec3(0.1, 0.1, 0.1), zero())
    g3.draw(cube, zero(), zero(), one(), red)
    g.show()
    print("odd cameras drew without trouble")

    # Models from OBJ files
    var tower = g3.load_obj("tower.obj")
    g3.camera(vec3(0.0, 1.0, 6.0), vec3(0.0, 1.0, 0.0))
    g.clear(rgb(0, 0, 0))
    g3.draw(tower, zero(), zero(), one(), red)
    print(f"tower {tower.count} {shown(g, 160, 120)} {shown(g, 160, 80)}")
    var shaped = g3.load_obj("shaped.obj")
    print(f"shaped {shaped.count}")
    print(f"above the roof {shown(g, 160, 50)}")
    var card = g3.load_obj("card.obj")
    var updown = g3.load_texture("updown.png")
    g3.camera(vec3(0.0, 0.0, 3.0), zero())
    g.clear(rgb(0, 0, 0))
    g3.draw_textured(card, updown, zero(), zero(), one())
    print(f"card top {shown(g, 160, 70)} bottom {shown(g, 160, 170)}")

    # A busy scene holds a measured frame rate
    var clock = memory(24)
    QueryPerformanceFrequency(clock + 16)
    g3.fog(rgb(150, 190, 230), 20.0, 80.0)
    g3.sun(vec3(-0.4, -1.0, -0.3), rgb(200, 200, 200))
    g3.ambient(rgb(70, 70, 70))
    g3.lamp(0, vec3(0.0, 2.0, 0.0), rgb(255, 150, 60), 12.0)
    QueryPerformanceCounter(clock)
    var frames = 60
    if shapes < 500
        frames = 5
    end
    for frame in 0..frames
        g3.camera(vec3(cos(float(frame) * 0.05) * 30.0, 12.0, sin(float(frame) * 0.05) * 30.0), zero())
        g.clear(rgb(150, 190, 230))
        g3.draw(ground, zero(), zero(), vec3(200.0, 1.0, 200.0), rgb(90, 130, 80))
        for i in 0..shapes
            var x = float(i % 50) * 2.0 - 50.0
            var z = float(i / 50) * 2.0 - 40.0
            if i % 10 == 0
                g3.draw(ball, vec3(x, 0.6, z), zero(), one(), blue)
            else
                g3.draw(cube, vec3(x, 0.5, z), vec3(0.0, float(i + frame), 0.0), vec3(0.8, 1.0, 0.8), red)
            end
        end
        g.text(f"frame {frame}", 8.0, 8.0, 16.0, rgb(0, 0, 0))
        g.show()
        w.is_open()
    end
    QueryPerformanceCounter(clock + 8)
    var seconds = float(peek_i64(clock, 8) - peek_i64(clock, 0)) / float(peek_i64(clock, 16))
    g.save_bmp("busy.bmp")
    g3.close()
    g.close()
    w.close()
    print(f"{shapes} shapes: {int(float(frames) / seconds)} frames a second")
end
"""

G3D_WANT = [
    "near first 220 40 30 edge 30 60 210",
    "far first 220 40 30 edge 30 60 210",
    "clear wipes depth 30 60 210",
    "sun on 220 40 30",
    "sun behind 0 0 0",
    "turned and stretched 220 40 30",
]

CARD_OBJ = "v -1 -1 0\nv 1 -1 0\nv 1 1 0\nv -1 1 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nf 1/1 2/2 3/3 4/4\n"

G3D_WANT_MORE = [
    "lamp off 0 0 0",
    "meshes 36 3072 6",
    "texture repeats 0 0 255 255 0 0 0 0 255",
    "3D over earlier 2D 220 40 30 0 255 0",
    "shaped 12",
    "above the roof 0 0 0",
    "card top 0 200 0 bottom 230 210 0",
]

G3D_WANT_REST = [
    "fog 128 128 128",
    "no fog yet 220 40 30",
    "texture 255 0 0 and 0 0 255",
    "2D on top 0 255 0, ball 30 60 210",
    "square after resizing true true",
    "camera anywhere missed 0",
    "odd cameras drew without trouble",
]

TOWER_OBJ = """# A box with a pointed roof: four-cornered sides, three-cornered roof, no normals
o tower
v -0.5 0 0.5
v 0.5 0 0.5
v 0.5 0 -0.5
v -0.5 0 -0.5
v -0.5 1.5 0.5
v 0.5 1.5 0.5
v 0.5 1.5 -0.5
v -0.5 1.5 -0.5
v 0 0.24e1 0
f 1 2 6 5
f 2 3 7 6
f 3 4 8 7
f 4 1 5 8
f 5 6 9
f 6 7 9
f 7 8 9
f 8 5 9
"""

SHAPED_OBJ = "mtllib x.mtl\r\nv 0 0 0\r\nv +1.5 0 0\r\nv 0 1.0E+0 0\r\nv 1 1 -0.25\r\nvt 0 0\r\nvt 1 0\r\nvt 0.5 1\r\nvn 0 0 1\r\ng part\r\nusemtl stone\r\ns off\r\nf 1/1/1 2/2/1 3/3/1\r\nf -4//-1 -3//-1 -1//-1 -2//-1\r\nf\t1/1\t2/2\t4/3  # tabs and a comment\r\n"


def test_g3d(work):
    folder = work / "g3d tests"
    folder.mkdir()
    rows = [b"\0" + b"".join(bytes((255, 0, 0, 255)) if x < 32 else bytes((0, 0, 255, 255)) for x in range(64)) for _ in range(64)]
    (folder / "halves.png").write_bytes(png_file(64, 64, 6, rows))
    (folder / "tower.obj").write_text(TOWER_OBJ, encoding="utf-8")
    (folder / "shaped.obj").write_bytes(SHAPED_OBJ.encode())
    (folder / "card.obj").write_text(CARD_OBJ, encoding="utf-8")
    rows = [b"\0" + bytes((0, 200, 0, 255) if y < 32 else (230, 210, 0, 255)) * 64 for y in range(64)]
    (folder / "updown.png").write_bytes(png_file(64, 64, 6, rows))
    source = folder / "g3d test.jamo"
    source.write_text(G3D_TEST, encoding="utf-8")
    for flags in ([], ["--gc-test", "--gc-check"]):
        mode = " ".join(flags) or "normal"
        built = run([str(JAMO), "build", *flags, str(source)])
        result = run([str(source.with_suffix(".exe")), "100" if flags else "2500"], cwd=folder, timeout=SLOW_RUN_LIMIT) if built.returncode == 0 else built
        got = clean(result.stdout).splitlines()

        def line(prefix):
            return next((g for g in got if g.startswith(prefix)), "")
        near, far = line("lamp ").split()[-3:], line("lamp out of reach").split()[-3:]
        # 200 lit by (1 - 2.5 / 100) squared at the cube's front, 2.5 from the lamp
        lamp_ok = len(near) == 3 and all(abs(int(v) - 190) <= 2 for v in near) and far == ["0", "0", "0"]
        # 200 times the tilted normal's z: (0, 0.354, 0.707) made 1 long is about (0, 0.447, 0.894)
        bent = line("stretched normal").split()[-3:]
        bent_ok = len(bent) == 3 and all(abs(int(v) - 179) <= 2 for v in bent)
        tower = line("tower ").split()
        missing = [want for want in G3D_WANT + G3D_WANT_REST + G3D_WANT_MORE if want not in got]
        report(f"3D pixels read back right for depth, light, fog, textures, 2D on top, models and resizing {mode}", result.returncode == 0 and not missing,
               f"{result.returncode} {result.stderr[:400]}\nmissing: {missing}\n" + "\n".join(got))
        report(f"a lamp lights by distance, stops at its reach, and turns off {mode}", lamp_ok and "lamp off 0 0 0" in got, line("lamp"))
        report(f"stretched meshes bend their normals the right way {mode}", bent_ok, line("stretched normal"))
        report(f"300 camera spots, straight up and down too, all see their cube {mode}", "camera anywhere missed 0" in got, line("camera anywhere"))
        report(f"OBJ models: quads, triangles, negative numbers, exponents, texture spots, normals and comments {mode}",
               tower[:5] == ["tower", "36", "220", "40", "30"] and tower[5:8] != ["0", "0", "0"] and "shaped 12" in got, " ".join(tower))
        speed = got[-1] if got else ""
        if speed.endswith("frames a second"):
            print(f"  {speed}")
            if not flags:
                report("a scene of 2,500 shapes holds at least 30 frames a second", int(speed.split()[-4]) >= 30, speed)
        else:
            report(f"a scene holds at least 30 frames a second {mode}", False, speed)
    shutil.copytree(folder, Path(tempfile.gettempdir()) / "jamo g3d frames", dirs_exist_ok=True, ignore=shutil.ignore_patterns("*.exe", "*.jamo", "*.obj", "*.png"))

    # Bad model files stop with clear errors that name the line
    damaged = {
        "empty": ("", "has no faces"),
        "only comments": ("# nothing\n\n", "has no faces"),
        "short point": ("v 1 2\n", "line 1 can't be read as a model: a point needs x, y and z"),
        "word for a number": ("v 1 x 3\n", "line 1 can't be read as a model: x isn't a number"),
        "two corners": ("v 0 0 0\nv 1 0 0\nf 1 2\n", "line 3 can't be read as a model: a face needs at least 3 corners"),
        "missing point": ("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 9\n", "line 4 can't be read as a model: there's no point number 9"),
        "point zero": ("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 0 1 2\n", "there's no point number 0"),
        "missing texture spot": ("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1/4 2/1 3/1\n", "there's no texture spot number 4"),
        "missing normal": ("v 0 0 0\nv 1 0 0\nv 0 1 0\nvn 0 0 1\nf 1//1 2//2 3//1\n", "line 5 can't be read as a model: there's no normal number 2"),
        "four parts": ("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1/1/1/1 2 3\n", "more than 3 parts"),
        "huge power": ("v 1e999 0 0\n", "1e999 isn't a number"),
        "half a number": ("v 1.5x 0 0\n", "1.5x isn't a number"),
        "far below": ("v 0 0 0\nv 1 0 0\nv 0 1 0\nf -1 -2 -9\n", "there's no point number -9"),
    }
    problems = []
    for name, (text, words) in damaged.items():
        (folder / "bad.obj").write_text(text, encoding="utf-8")
        (folder / "bad.jamo").write_text('use "g3d.jamo"\n\nfunc main()\n    var w = open_window("x", 100, 100)\n    w.quiet()\n    w.move(-3000, -3000)\n    w.is_open()\n    var g3 = start_3d(start_gfx(w))\n    print(g3.load_obj("bad.obj").count)\nend\n', encoding="utf-8")
        result = run([str(JAMO), "run", str(folder / "bad.jamo")], cwd=folder)
        if result.returncode != 1 or words not in result.stderr or "g3d.jamo (built in)" not in result.stderr:
            problems.append(f"{name}: code {result.returncode} {result.stdout[:100]} {result.stderr[:200]}")
    report(f"{len(damaged)} bad OBJ files give clear errors that name the line", not problems, "\n".join(problems))

    for name, body, words in (
        ("drawing a closed mesh", "var m = g3.cube()\n    m.close()\n    g3.draw(m, vec3(0.0, 0.0, 0.0), vec3(0.0, 0.0, 0.0), vec3(1.0, 1.0, 1.0), rgb(0, 0, 0))", "This mesh is closed, so it can't be drawn."),
        ("closing a mesh twice", "var m = g3.cube()\n    m.close()\n    m.close()", "This mesh is already closed."),
        ("a closed texture", 'var t = g3.load_texture("halves.png")\n    t.close()\n    g3.draw_textured(g3.cube(), t, vec3(0.0, 0.0, 0.0), vec3(0.0, 0.0, 0.0), vec3(1.0, 1.0, 1.0))', "This texture is closed, so it can't be drawn."),
        ("using 3D after closing", "g3.close()\n    g3.cube()", "This 3D drawing is closed, so it can't be used."),
        ("closing 3D twice", "g3.close()\n    g3.close()", "This 3D drawing is already closed."),
        ("3D after its drawing closed", "g.close()\n    g3.cube()", "This drawing is closed, so it can't be used."),
        ("a bad lens", "g3.lens(200.0, 0.1, 100.0)", "A lens sees from above 0.0 to below 180.0 degrees, not 200.0."),
        ("a bad lamp", "g3.lamp(4, vec3(0.0, 0.0, 0.0), rgb(0, 0, 0), 1.0)", "g3d has lamps 0 to 3, not 4."),
        ("backward fog", "g3.fog(rgb(0, 0, 0), 10.0, 5.0)", "Fog must end farther than it starts, but they're 10.0 and 5.0."),
        ("a broken mesh", "g3.mesh([1.0, 2.0])", "a multiple of 24 floats, not 2."),
        ("a sphere with no detail", "g3.sphere(2)", "A sphere's detail goes from 3 to 64, not 2."),
    ):
        path = folder / f"{name}.jamo"
        path.write_text(f'use "g3d.jamo"\n\nfunc main()\n    var w = open_window("x", 100, 100)\n    w.quiet()\n    w.move(-3000, -3000)\n    w.is_open()\n    var g = start_gfx(w)\n    var g3 = start_3d(g)\n    {body}\nend\n', encoding="utf-8")
        result = run([str(JAMO), "run", str(path)], cwd=folder)
        report(f"g3d gives a clear error for {name}", result.returncode == 1 and words in result.stderr, f"{result.returncode} {result.stderr[:300]}")

    # Memory stays steady over many frames, restarts and meshes made and closed
    long_run = folder / "long run.jamo"
    long_run.write_text('''use "g3d.jamo"
extern "kernel32.dll" func GetCurrentProcess(): int
extern "kernel32.dll" func K32GetProcessMemoryInfo(process: int, counters: int, size: i32): bool

func memory_used(): int
    var counters = memory(80)
    K32GetProcessMemoryInfo(GetCurrentProcess(), counters, 80)
    return peek_i64(counters, 72) / 1048576
end

func main()
    var w = open_window("long", 160, 120)
    w.quiet()
    w.move(-3000, -3000)
    w.is_open()
    var g = start_gfx(w)
    for round in 0..20
        var again = start_3d(g)
        again.cube()
        again.close()
    end
    var g3 = start_3d(g)
    var cube = g3.cube()
    var early = 0
    for frame in 0..10000
        var ball = g3.sphere(8)
        g.clear(rgb(0, 0, frame % 256))
        g3.camera(vec3(3.0, 2.0, 4.0), vec3(0.0, 0.0, 0.0))
        g3.draw(cube, vec3(0.0, 0.0, 0.0), vec3(0.0, float(frame), 0.0), vec3(1.0, 1.0, 1.0), rgb(200, 80, 60))
        g3.draw(ball, vec3(1.5, 0.0, 0.0), vec3(0.0, 0.0, 0.0), vec3(1.0, 1.0, 1.0), rgb(60, 80, 200))
        ball.close()
        g.text(f"frame {frame}", 4.0, 4.0, 12.0, rgb(255, 255, 255))
        g.show()
        w.is_open()
        if frame == 1000
            early = memory_used()
        end
    end
    print(f"{early} {memory_used()}")
end
''', encoding="utf-8")
    result = run([str(JAMO), "run", str(long_run)], timeout=SLOW_RUN_LIMIT)
    numbers = clean(result.stdout).split()
    if len(numbers) == 2:
        print(f"  memory after 1,000 frames: {numbers[0]} MB, after 10,000: {numbers[1]} MB")
    report("20 restarts, 10,000 frames and 10,000 meshes made and closed keep memory steady", result.returncode == 0 and len(numbers) == 2 and int(numbers[1]) - int(numbers[0]) <= 8, f"{result.returncode} {result.stdout} {result.stderr[:300]}")


# --- Net: web requests against a local test server on 127.0.0.1 only. Nothing reaches the internet ---
NET_BIG = bytes((i * 7 + i // 256) % 251 + 1 for i in range(1_500_000))


class NetTestHandler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def answer(self, status, body, kind="text/plain; charset=utf-8"):
        self.send_response(status)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == "/hello":
            self.answer(200, b"Hello from the test server")
        elif self.path == "/missing":
            self.answer(404, b"not here")
        elif self.path == "/words":
            self.answer(200, "héllo wörld, 日本".encode())
        elif self.path == "/big":
            self.answer(200, NET_BIG)
        elif self.path == "/empty":
            self.answer(204, b"")
        elif self.path.startswith("/query?"):
            self.answer(200, self.path.encode())
        elif self.path == "/trickle":
            body = b"0123456789" * 10
            self.send_response(200)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            for i in range(0, len(body), 10):
                self.wfile.write(body[i:i + 10])
                self.wfile.flush()
                time.sleep(0.25)
        elif self.path.startswith("/pause"):
            time.sleep(0.5)
            self.answer(200, ("paused " + self.path).encode())
        elif self.path == "/slow":
            time.sleep(4)
            self.answer(200, b"too late")
        elif self.path == "/drop":
            self.connection.shutdown(socket.SHUT_RDWR)
            self.connection.close()
        elif self.path == "/garbage":
            self.wfile.write(b"this is not http\r\n\r\n")
            self.wfile.flush()
            self.connection.close()
        else:
            self.answer(500, b"unknown")

    def do_POST(self):
        body = self.rfile.read(int(self.headers.get("Content-Length", 0)))
        self.answer(201, b"got " + str(len(body)).encode() + b" bytes: " + body + b" / " + self.headers.get("Content-Type", "").encode())


class QuietServer(http.server.ThreadingHTTPServer):
    def handle_error(self, request, client_address):
        pass  # The timeout test hangs up on purpose, so the server can't send its late answer


NET_TEST = """use "net.jamo"
use "time.jamo"
extern "kernel32.dll" func GetCurrentProcess(): int
extern "kernel32.dll" func GetProcessHandleCount(process: int, count: int): bool
extern "kernel32.dll" func K32GetProcessMemoryInfo(process: int, counters: int, size: i32): bool

func handle_count(): int
    var count = memory(8)
    GetProcessHandleCount(GetCurrentProcess(), count)
    return peek_i32(count, 0)
end

func show(r: Response): string
    return f"{r.status} [{r.text}] [{r.error}]"
end

func main()
    var site = args()[0]
    var closed_port = args()[1]
    var net = start_net()
    print(show(net.get(f"{site}/hello")))
    print(show(net.get(f"{site}/missing")))
    print(show(net.get(f"{site}/words")))
    print(show(net.get(f"{site}/empty")))
    print(show(net.get(f"{site}/query?a=1&b=two")))
    print(show(net.post(f"{site}/echo", "name=Jamo&score=99")))
    print(show(net.post(f"{site}/echo", "")))
    var big = net.get(f"{site}/big")
    var total = 0
    for i in 0..len(big.text)
        total = (total * 31 + byte(big.text, i)) % 1000000007
    end
    print(f"big {big.status} {len(big.text)} {total}")
    print(show(net.get(f"http://127.0.0.1:{closed_port}/hello")))
    print(show(net.get(f"{site}/drop")))
    print(show(net.get(f"{site}/garbage")))
    net.set_timeout(1.0)
    var asked = seconds_since_start()
    print(show(net.get(f"{site}/slow")))
    var waited = seconds_since_start() - asked
    print(f"gave up after about a second: {waited > 0.9 and waited < 1.6}")
    var trickle = net.get(f"{site}/trickle")
    print(f"a slow answer that keeps coming isn't cut off: {trickle.status} {len(trickle.text)} [{trickle.error}]")
    net.set_timeout(30.0)
    print(show(net.get(f"{site}/hello")))

    # Many requests leak nothing: handles stay the same, and memory's highest point in the second half
    # is no higher than in the first. Memory rises and falls as the cleaner runs, so single points can't say
    var rounds = int(args()[2])
    var handles = 0
    var first_peak = 0
    var second_peak = 0
    var counters = memory(80)
    for i in 0..rounds
        if i == 50
            handles = handle_count()
        end
        var r = net.get(f"{site}/hello")
        if r.status != 200
            print(f"round {i} failed: {show(r)}")
        end
        if i >= 100 and i % 10 == 0
            K32GetProcessMemoryInfo(GetCurrentProcess(), counters, 80)
            var now = peek_i64(counters, 72) / 1024
            if i < rounds / 2 and now > first_peak
                first_peak = now
            end
            if i >= rounds / 2 and now > second_peak
                second_peak = now
            end
        end
    end
    print(f"before {handles} {first_peak}")
    print(f"after {handle_count()} {second_peak}")
    net.close()
end
"""


NET_BACKGROUND_TEST = """use "net.jamo"
extern "kernel32.dll" func Sleep(milliseconds: i32)
extern "kernel32.dll" func QueryPerformanceCounter(place: int): bool
extern "kernel32.dll" func QueryPerformanceFrequency(place: int): bool
extern "kernel32.dll" func GetCurrentProcess(): int
extern "kernel32.dll" func GetProcessHandleCount(process: int, count: int): bool
extern "kernel32.dll" func K32GetProcessMemoryInfo(process: int, counters: int, size: i32): bool

func now(clock: int): float
    QueryPerformanceCounter(clock)
    return float(peek_i64(clock, 0)) / float(peek_i64(clock, 8))
end

func handle_count(): int
    var count = memory(8)
    GetProcessHandleCount(GetCurrentProcess(), count)
    return peek_i32(count, 0)
end

func show(r: Response): string
    return f"{r.status} [{r.text}] [{r.error}]"
end

# Runs frames until every request is done, and gives back how many frames that took and the slowest
func wait_all(requests: [Request], clock: int): [float]
    var frames = 0.0
    var slowest = 0.0
    var left = len(requests)
    while left > 0
        left = 0
        var before = now(clock)
        for r in requests
            if not r.done()
                left += 1
            end
        end
        var took = now(clock) - before
        if took > slowest
            slowest = took
        end
        frames += 1.0
        Sleep(5)
    end
    return [frames, slowest]
end

func main()
    var site = args()[0]
    var closed_port = args()[1]
    var rounds = int(args()[2])
    var clock = memory(16)
    QueryPerformanceFrequency(clock + 8)
    var net = start_net()

    # Many at once, while frames keep going
    var requests: [Request] = []
    for i in 0..20
        requests.add(net.start_get(f"{site}/pause{i}"))
    end
    var big = net.start_get(f"{site}/big")
    requests.add(big)
    var sent = net.start_post(f"{site}/echo", "hello in the background")
    requests.add(sent)
    var start = now(clock)
    var waited = wait_all(requests, clock)
    var seconds = now(clock) - start
    var right = 0
    for i in 0..20
        if requests[i].response().text == f"paused /pause{i}"
            right += 1
        end
    end
    print(f"together {right} of 20 right, in under 5 s {seconds < 5.0}, frames kept going {waited[0] > 20.0}")
    print(f"took {seconds}")
    print(f"no frame waited {waited[1] < 0.05}")
    var text = big.response().text
    var total = 0
    for i in 0..len(text)
        total = (total * 31 + byte(text, i)) % 1000000007
    end
    print(f"big {big.response().status} {len(text)} {total}")
    print(show(sent.response()))

    # Problems come back as errors, the same as waiting requests
    var problems = [net.start_get(f"http://127.0.0.1:{closed_port}/x"), net.start_get(f"{site}/drop"), net.start_get(f"{site}/garbage"), net.start_get(f"{site}/missing")]
    wait_all(problems, clock)
    for r in problems
        print(show(r.response()))
    end
    net.set_timeout(1.0)
    var slow = net.start_get(f"{site}/slow")
    wait_all([slow], clock)
    print(show(slow.response()))
    net.set_timeout(30.0)
    var dropped = net.start_get(f"{site}/slow")
    dropped.done()
    dropped.cancel()
    print(f"{dropped.done()} {show(dropped.response())}")

    # Many requests, some cancelled, leak nothing, and every block is given back once WinHTTP has closed its handle
    var handles = 0
    var first_peak = 0
    var second_peak = 0
    var counters = memory(80)
    var batch: [Request] = []
    for i in 0..rounds
        batch.add(net.start_get(f"{site}/hello"))
        if i % 50 == 25
            var stopped = net.start_get(f"{site}/slow")
            stopped.done()
            stopped.cancel()
        end
        if len(batch) == 20
            wait_all(batch, clock)
            for r in batch
                if r.response().status != 200
                    print(f"round {i} failed: {show(r.response())}")
                end
            end
            batch = []
        end
        if i == 40
            handles = handle_count()
        end
        if i >= rounds / 5 and i % 20 == 0
            K32GetProcessMemoryInfo(GetCurrentProcess(), counters, 80)
            var memory_now = peek_i64(counters, 72) / 1024
            if i < rounds / 2 and memory_now > first_peak
                first_peak = memory_now
            end
            if i >= rounds / 2 and memory_now > second_peak
                second_peak = memory_now
            end
        end
    end
    wait_all(batch, clock)
    var tries = 0
    while net.closing_count() > 0 and tries < 400
        Sleep(5)
        tries += 1
    end
    print(f"still closing {net.closing_count()}")
    print(f"before {handles} {first_peak}")
    print(f"after {handle_count()} {second_peak}")
    net.close()
end
"""

NET_QUIT_TEST = """use "net.jamo"

func main()
    var net = start_net()
    var requests: [Request] = []
    for i in 0..10
        requests.add(net.start_get(f"{args()[0]}/slow"))
    end
    requests[0].done()
    print("leaving with requests still running")
end
"""


def net_want(port, closed):
    big_hash = 0
    for b in NET_BIG:
        big_hash = (big_hash * 31 + b) % 1000000007
    where = f"127.0.0.1:{port}"
    return [
        "200 [Hello from the test server] []",
        "404 [not here] []",
        "200 [héllo wörld, 日本] []",
        "204 [] []",
        "200 [/query?a=1&b=two] []",
        "201 [got 18 bytes: name=Jamo&score=99 / text/plain; charset=utf-8] []",
        "201 [got 0 bytes:  / text/plain; charset=utf-8] []",
        f"big 200 {len(NET_BIG)} {big_hash}",
        f"0 [] [couldn't connect to 127.0.0.1:{closed}]",
        f"0 [] [{where} closed the connection or sent an answer that isn't valid HTTP]",
        f"0 [] [{where} closed the connection or sent an answer that isn't valid HTTP]",
        f"0 [] [{where} took too long to answer]",
        "gave up after about a second: true",
        "a slow answer that keeps coming isn't cut off: 200 100 []",
        "200 [Hello from the test server] []",
    ]


def test_net(work):
    folder = work / "net tests"
    folder.mkdir()
    server = QuietServer(("127.0.0.1", 0), NetTestHandler)
    port = server.server_address[1]
    threading.Thread(target=server.serve_forever, daemon=True).start()
    spare = socket.socket()
    spare.bind(("127.0.0.1", 0))
    closed = spare.getsockname()[1]
    spare.close()
    try:
        source = folder / "net test.jamo"
        source.write_text(NET_TEST, encoding="utf-8")
        want = net_want(port, closed)
        for flags, rounds in (([], 2000), (["--gc-test", "--gc-check"], 120)):
            mode = " ".join(flags) or "normal"
            built = run([str(JAMO), "build", *flags, str(source)])
            try:
                result = run([str(source.with_suffix(".exe")), f"http://127.0.0.1:{port}", str(closed), str(rounds)], cwd=folder, timeout=SLOW_RUN_LIMIT) if built.returncode == 0 else built
            except subprocess.TimeoutExpired:
                result = subprocess.CompletedProcess([], -1, "", f"still running after {SLOW_RUN_LIMIT} seconds")
            got = clean(result.stdout).splitlines()
            report(f"web requests: text, status codes, UTF-8, a large answer, posts, and clear errors for a closed port, a dropped connection, bad HTTP and a timeout {mode}",
                   result.returncode == 0 and got[:len(want)] == want,
                   f"{result.returncode} {result.stderr[:400]}\n" + "\n".join(f"{'  ' if g == w else '! '}{g} | want {w}" for g, w in zip(got + [""] * len(want), want)))
            counts = [line.split()[1:] for line in got if line.startswith(("before ", "after "))]
            failed = [line for line in got if line.startswith("round ")]
            steady = len(counts) == 2 and not failed and int(counts[1][0]) - int(counts[0][0]) <= NET_HANDLE_WOBBLE and (bool(flags) or int(counts[1][1]) - int(counts[0][1]) < 2048)
            report(f"{rounds} requests all work and leak no handles{'' if flags else ' or memory'} {mode}", steady, f"{counts} {failed[:3]}")

        # Background requests: many at once while frames keep going, errors, cancelling and no leaks
        source = folder / "net background.jamo"
        source.write_text(NET_BACKGROUND_TEST, encoding="utf-8")
        where = f"127.0.0.1:{port}"
        bad_http = f"0 [] [{where} closed the connection or sent an answer that isn't valid HTTP]"
        background_want = [
            "together 20 of 20 right, in under 5 s true, frames kept going true",
            "no frame waited true",
            want[7],
            "201 [got 23 bytes: hello in the background / text/plain; charset=utf-8] []",
            f"0 [] [couldn't connect to 127.0.0.1:{closed}]",
            bad_http,
            bad_http,
            "404 [not here] []",
            f"0 [] [{where} took too long to answer]",
            "true 0 [] [the request was cancelled]",
            "still closing 0",
        ]
        for flags, rounds in (([], 1000), (["--gc-test", "--gc-check"], 100)):
            mode = " ".join(flags) or "normal"
            built = run([str(JAMO), "build", *flags, str(source)])
            try:
                result = run([str(source.with_suffix(".exe")), f"http://127.0.0.1:{port}", str(closed), str(rounds)], cwd=folder, timeout=SLOW_RUN_LIMIT) if built.returncode == 0 else built
            except subprocess.TimeoutExpired:
                result = subprocess.CompletedProcess([], -1, "", f"still running after {SLOW_RUN_LIMIT} seconds")
            got = [line for line in clean(result.stdout).splitlines() if not line.startswith("took ")]
            print("  20 at once took " + next((line[5:8] for line in clean(result.stdout).splitlines() if line.startswith("took ")), "?") + " s")
            report(f"background requests: 20 at once while frames keep going, a large answer, a post, errors, a timeout and cancelling {mode}",
                   result.returncode == 0 and got[:len(background_want)] == background_want,
                   f"{result.returncode} {result.stderr[:400]}\n" + "\n".join(f"{'  ' if g == w else '! '}{g} | want {w}" for g, w in zip(got + [""] * len(background_want), background_want)))
            counts = [line.split()[1:] for line in got if line.startswith(("before ", "after "))]
            failed = [line for line in got if line.startswith("round ")]
            steady = len(counts) == 2 and not failed and int(counts[1][0]) - int(counts[0][0]) <= NET_HANDLE_WOBBLE and (bool(flags) or int(counts[1][1]) - int(counts[0][1]) < 2048)
            report(f"{rounds} background requests all work and leak no handles{'' if flags else ' or memory'} {mode}", steady, f"{counts} {failed[:3]}")

        # Leaving the program with requests still running doesn't crash
        source = folder / "net quit.jamo"
        source.write_text(NET_QUIT_TEST, encoding="utf-8")
        for flags in ([], ["--gc-test", "--gc-check"]):
            built = run([str(JAMO), "build", *flags, str(source)])
            result = run([str(source.with_suffix(".exe")), f"http://127.0.0.1:{port}"], cwd=folder, timeout=60) if built.returncode == 0 else built
            report(f"leaving the program during requests doesn't crash {' '.join(flags) or 'normal'}", result.returncode == 0 and clean(result.stdout) == "leaving with requests still running\n" and not result.stderr, f"{result.returncode} {result.stdout} {result.stderr[:300]}")
    finally:
        server.shutdown()
        server.server_close()

    for name, body, words in (
        ("a web address without http", 'net.get("ftp://127.0.0.1/")', "net takes web addresses starting with http:// or https://, not ftp://127.0.0.1/."),
        ("no server name", 'net.get("http:///page")', "The web address http:///page has no server name."),
        ("a bad port", 'net.get("http://127.0.0.1:99999/")', "has a port above 65535."),
        ("a bad timeout", "net.set_timeout(0.0)", "A timeout goes from above 0.0 to 3600.0 seconds, not 0.0."),
        ("using a closed net", 'net.close()\n    net.get("http://127.0.0.1:1/")', "This net is closed, so it can't be used."),
        ("closing twice", "net.close()\n    net.close()", "This net is already closed."),
        ("reading a request before it's done", 'var r = net.start_get("http://127.0.0.1:1/")\n    print(r.response().status)', "This request hasn't finished yet. Check done() first."),
    ):
        path = folder / f"{name}.jamo"
        path.write_text(f'use "net.jamo"\n\nfunc main()\n    var net = start_net()\n    {body}\nend\n', encoding="utf-8")
        result = run([str(JAMO), "run", str(path)], cwd=folder)
        report(f"net gives a clear error for {name}", result.returncode == 1 and words in result.stderr and "net.jamo (built in) line" in result.stderr, f"{result.returncode} {result.stderr[:300]}")

    # memory_text copies bytes from memory into text
    path = folder / "memory text.jamo"
    path.write_text('func main()\n    var place = memory(8)\n    poke_u8(place, 0, 74)\n    poke_u8(place, 1, 97)\n    poke_u8(place, 2, 109)\n    print(memory_text(place, 3))\n    print(len(memory_text(place, 0)))\n    print(len(memory_text(place, -5)))\nend\n', encoding="utf-8")
    result = run([str(JAMO), "run", str(path)], cwd=folder)
    report("memory_text copies bytes into text", clean(result.stdout) == "Jam\n0\n0\n", f"{result.returncode} {result.stdout} {result.stderr[:200]}")


# --- Links: TCP between programs on this PC only, on 127.0.0.1 ---
LINKS_TEST = """use "net.jamo"
extern "ws2_32.dll" func setsockopt(s: int, level: i32, name: i32, value: int, length: i32): i32
extern "kernel32.dll" func Sleep(milliseconds: i32)
extern "winmm.dll" func timeBeginPeriod(period: i32): i32
extern "kernel32.dll" func GetCurrentProcess(): int
extern "kernel32.dll" func GetProcessHandleCount(process: int, count: int): bool

func handle_count(): int
    var count = memory(8)
    GetProcessHandleCount(GetCurrentProcess(), count)
    return peek_i32(count, 0)
end

# Frames until the listener has a new link and the other end has connected
func pair(net: Net, listener: Listener, port: int): [Link]
    var client = net.connect("127.0.0.1", port)
    var server: [Link] = []
    var frames = 0
    while (len(server) == 0 or not client.connected()) and frames < 2000
        for link in listener.new_links()
            server.add(link)
        end
        Sleep(1)
        frames += 1
    end
    return [client, server[0]]
end

# Frames until a link has given this many messages
func gather(link: Link, other: Link, count: int): [string]
    var got: [string] = []
    var frames = 0
    while len(got) < count and frames < 5000
        other.messages()
        for m in link.messages()
            got.add(m)
        end
        Sleep(1)
        frames += 1
    end
    return got
end

func main()
    timeBeginPeriod(1)
    var port = int(args()[0])
    var closed_port = int(args()[1])
    var rounds = int(args()[2])
    var net = start_net()
    var listener = net.listen_here(port)
    var both = pair(net, listener, port)
    var client = both[0]
    var server = both[1]
    print(f"connected {client.connected()} {server.connected()}")

    client.send("hello")
    client.send("")
    client.send("héllo wörld")
    var got = gather(server, client, 3)
    print(f"server got {len(got)}: [{got[0]}] [{got[1]}] [{got[2]}]")

    var big = ""
    var piece = "0123456789abcdefghijklmnopqrstuvwxyz"
    while len(big) < 1000000
        big = big + piece + big
    end
    server.send(big)
    for i in 0..200
        server.send(f"message {i}")
    end
    got = gather(client, server, 201)
    var in_order = len(got) == 201
    for i in 0..200
        if in_order and got[i + 1] != f"message {i}"
            in_order = false
        end
    end
    print(f"client got {len(got)}, big one {len(got[0]) == len(big) and got[0] == big}, in order {in_order}")

    # With small buffers, 16 MB sent before the other side reads anything goes out in many parts
    var small = memory(8)
    poke_i32(small, 0, 4096)
    setsockopt(server.socket, 65535, 4097, small, 4)
    setsockopt(client.socket, 65535, 4098, small, 4)
    for i in 0..16
        server.send(f"{i} {big}")
    end
    got = gather(client, server, 16)
    var whole = len(got) == 16
    for i in 0..16
        if whole and got[i] != f"{i} {big}"
            whole = false
        end
    end
    print(f"16 MB in parts arrived whole {whole}")

    client.close()
    var frames = 0
    while server.is_open() and frames < 2000
        server.messages()
        Sleep(1)
        frames += 1
    end
    print(f"server sees: {server.problem()}")
    server.send("nobody hears this")
    print(f"sending to a lost link does nothing: {len(server.messages())}")

    var nowhere = net.connect("127.0.0.1", closed_port)
    frames = 0
    while nowhere.is_open() and frames < 5000
        nowhere.messages()
        Sleep(1)
        frames += 1
    end
    print(f"closed port: {nowhere.problem()} {nowhere.connected()}")

    var handles = 0
    for i in 0..rounds
        if i == 20
            handles = handle_count()
        end
        var two = pair(net, listener, port)
        two[0].send("ping")
        var answer = gather(two[1], two[0], 1)
        if len(answer) != 1 or answer[0] != "ping"
            print(f"round {i} failed")
        end
        two[0].close()
        two[1].close()
    end
    print(f"handles before {handles} after {handle_count()}")
    var left_open = net.connect("127.0.0.1", port)
    left_open.send("still here when the program ends")
    print("done")
end
"""

LINKS_PEER_TEST = """use "net.jamo"
extern "kernel32.dll" func Sleep(milliseconds: i32)
extern "winmm.dll" func timeBeginPeriod(period: i32): i32

func main()
    timeBeginPeriod(1)
    var net = start_net()
    var link = net.connect("localhost", int(args()[0]))
    var got: [string] = []
    var frames = 0
    while len(got) < 3 and frames < 5000
        for m in link.messages()
            got.add(m)
        end
        Sleep(1)
        frames += 1
    end
    print(f"got {len(got)}: [{got[0]}] [{got[1]}] [{got[2]}]")
    link.send("from jamo")
    frames = 0
    while link.is_open() and frames < 5000
        link.messages()
        Sleep(1)
        frames += 1
    end
    print(f"problem: {link.problem()}")
end
"""


def links_frame(text):
    data = text.encode()
    return struct.pack("<I", len(data)) + data


def links_peer(server, seen):
    """A Python program on the other end: splits a message byte by byte, sends two in one go, reads one, then sends a header that's too big"""
    conn, _ = server.accept()
    with conn:
        for b in links_frame("split up"):
            conn.sendall(bytes([b]))
            time.sleep(0.003)
        conn.sendall(links_frame("one") + links_frame("two"))
        header = b""
        while len(header) < 4:
            header += conn.recv(4 - len(header))
        size = struct.unpack("<I", header)[0]
        body = b""
        while len(body) < size:
            body += conn.recv(size - len(body))
        seen.append(body.decode())
        conn.sendall(struct.pack("<I", 20_000_000))
        time.sleep(1)


def free_port():
    probe = socket.socket()
    probe.bind(("127.0.0.1", 0))
    port = probe.getsockname()[1]
    probe.close()
    return port


def test_links(work):
    folder = work / "link tests"
    folder.mkdir()
    source = folder / "links.jamo"
    source.write_text(LINKS_TEST, encoding="utf-8")
    want = [
        "connected true true",
        "server got 3: [hello] [] [héllo wörld]",
        "client got 201, big one true, in order true",
        "16 MB in parts arrived whole true",
        "server sees: the other side closed the connection",
        "sending to a lost link does nothing: 0",
        "closed port: couldn't connect (error 10061) false",
    ]
    for flags, rounds in (([], 300), (["--gc-test", "--gc-check"], 40)):
        mode = " ".join(flags) or "normal"
        built = run([str(JAMO), "build", *flags, str(source)])
        try:
            result = run([str(source.with_suffix(".exe")), str(free_port()), str(free_port()), str(rounds)], cwd=folder, timeout=SLOW_RUN_LIMIT) if built.returncode == 0 else built
        except subprocess.TimeoutExpired:
            result = subprocess.CompletedProcess([], -1, "", f"still running after {SLOW_RUN_LIMIT} seconds")
        got = clean(result.stdout).splitlines()
        report(f"links: messages both ways, empty, UTF-8, 1 MB, 200 in a row, a hang-up, and a closed port {mode}", result.returncode == 0 and got[:len(want)] == want,
               f"{result.returncode} {result.stderr[:400]}\n" + "\n".join(f"{'  ' if g == w else '! '}{g} | want {w}" for g, w in zip(got + [""] * len(want), want)))
        counts = next((line.split() for line in got if line.startswith("handles before")), [])
        failed = [line for line in got if line.startswith("round ")]
        steady = len(counts) == 5 and not failed and int(counts[4]) - int(counts[2]) <= NET_HANDLE_WOBBLE
        report(f"{rounds} links made and closed leak no handles, and the program ends with one still open {mode}", steady and got[-1:] == ["done"], f"{counts} {failed[:3]} {got[-1:]}")

    # A Python program on the other end splits messages up and joins them together
    server = socket.socket()
    server.bind(("127.0.0.1", 0))
    server.listen(1)
    seen = []
    peer = threading.Thread(target=links_peer, args=(server, seen), daemon=True)
    peer.start()
    try:
        source = folder / "links peer.jamo"
        source.write_text(LINKS_PEER_TEST, encoding="utf-8")
        result = run([str(JAMO), "run", str(source), str(server.getsockname()[1])], cwd=folder, timeout=60)
        peer.join(10)
    finally:
        server.close()
    got = clean(result.stdout).splitlines()
    report("links keep messages whole however TCP splits or joins them, and refuse a message over 16 MB",
           result.returncode == 0 and got == ["got 3: [split up] [one] [two]", "problem: the other side sent a message over 16 MB, so the link was closed"] and seen == ["from jamo"],
           f"{result.returncode} {result.stderr[:300]}\n{got}\n{seen}")

    taken = socket.socket()
    taken.bind(("127.0.0.1", 0))
    taken.listen(1)
    busy = taken.getsockname()[1]
    try:
        for name, body, words in (
            ("a port in use", f"net.listen_here({busy})", f"Port {busy} is already in use by another program."),
            ("a bad port", "net.listen_here(70000)", "A port is a number from 1 to 65535, not 70000."),
            ("a bad address", 'net.connect("my friend", 5000)', "net connects to addresses like 192.168.1.20 or localhost, not my friend."),
            ("an address out of range", 'net.connect("1.2.3.300", 5000)', "not 1.2.3.300."),
            ("sending on a closed link", f'var link = net.connect("127.0.0.1", {busy})\n    link.close()\n    link.send("x")', "This link is closed, so it can't send."),
            ("closing a listener twice", f"var l = net.listen_here({free_port()})\n    l.close()\n    l.close()", "This listener is already closed."),
        ):
            path = folder / f"{name}.jamo"
            path.write_text(f'use "net.jamo"\n\nfunc main()\n    var net = start_net()\n    {body}\nend\n', encoding="utf-8")
            result = run([str(JAMO), "run", str(path)], cwd=folder)
            report(f"links give a clear error for {name}", result.returncode == 1 and words in result.stderr and "net.jamo (built in) line" in result.stderr, f"{result.returncode} {result.stderr[:300]}")
    finally:
        taken.close()


# --- The Phase 5 samples, played by test code. Each runs from a copy that's off-screen and muted, with
# a key that saves a frame. The game files themselves have no test code ---
SAMPLE_FRAMES = Path(tempfile.gettempdir()) / "jamo sample frames"
WM_CLOSE, WM_KEYDOWN, WM_KEYUP, WM_CHAR, WM_MOUSEMOVE, WM_LBUTTONDOWN, WM_LBUTTONUP = 0x10, 0x100, 0x101, 0x102, 0x200, 0x201, 0x202


class SampleRun:
    """Starts a sample's test copy, finds its window, and sends it input"""

    def __init__(self, exe, title, folder, args=()):
        self.folder = folder
        self.process = subprocess.Popen([str(exe), *args], cwd=folder, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8", errors="replace")
        self.user32 = ctypes.windll.user32
        self.user32.FindWindowW.restype = ctypes.c_void_p
        self.user32.PostMessageW.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_size_t, ctypes.c_ssize_t]
        self.window = None
        for _ in range(400):
            self.window = self.user32.FindWindowW(None, title)
            if self.window or self.process.poll() is not None:
                break
            time.sleep(0.025)
        time.sleep(0.3)

    def post(self, message, first=0, second=0):
        self.user32.PostMessageW(self.window, message, first, second)

    def key(self, code, wait=0.08):
        self.post(WM_KEYDOWN, code)
        time.sleep(wait)
        self.post(WM_KEYUP, code)
        time.sleep(wait)

    def type(self, text):
        for letter in text:
            self.post(WM_CHAR, ord(letter))
            time.sleep(0.03)
        time.sleep(0.1)

    def click(self, x, y):
        spot = y * 65536 + x
        self.post(WM_MOUSEMOVE, 0, spot)
        self.post(WM_LBUTTONDOWN, 1, spot)
        time.sleep(0.06)
        self.post(WM_LBUTTONUP, 0, spot)
        time.sleep(0.06)

    def snap(self, name):
        """Presses 0, which the test copy answers by saving the frame"""
        target = self.folder / "snap.bmp"
        if target.exists():
            target.unlink()
        self.key(0x30)
        for _ in range(200):
            if target.exists() and target.stat().st_size > 54:
                time.sleep(0.1)
                SAMPLE_FRAMES.mkdir(exist_ok=True)
                shutil.copy(target, SAMPLE_FRAMES / f"{name}.bmp")
                return True
            time.sleep(0.02)
        return False

    def finish(self, timeout=60):
        if self.process.poll() is None:
            self.post(WM_CLOSE)
        try:
            out, err = self.process.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            self.process.kill()
            out, err = self.process.communicate()
            err += " (still running, so it was stopped)"
        return self.process.returncode, clean(out), err


def sample_copy(name, folder, changes):
    """The sample with its window off-screen, its sound muted and key 0 saving a frame, plus any other changes"""
    text = (ROOT / "examples" / f"{name}.jamo").read_text(encoding="utf-8")
    lines = text.split("\n")
    out = []
    for line in lines:
        if line.strip().startswith("screen.show()"):
            out.append(line.replace("screen.show()", 'if window.key_pressed("0")'))
            out.append(line.replace("screen.show()", '    screen.save_bmp("snap.bmp")'))
            out.append(line.replace("screen.show()", "end"))
        out.append(line)
        if "= open_window(" in line:
            out.append("    window.quiet()")
            out.append("    window.move(-3000, -3000)")
        if "= start_sound()" in line:
            out.append("    speakers.quiet()")
    text = "\n".join(out)
    for old, new in changes:
        assert text.count(old) >= 1, f"{name}: {old}"
        text = text.replace(old, new)
    path = folder / f"{name}.jamo"
    path.write_text(text, encoding="utf-8")
    return path


PAGE_BODY = b"<html>\n<head><title> The Test Page </title></head>\n<body>\n<p>Hello from Python.</p>\n\n<p>Second line.</p>\n</body>\n</html>\n"


def test_samples(work):
    folder = work / "samples"
    folder.mkdir()
    if SAMPLE_FRAMES.exists():
        shutil.rmtree(SAMPLE_FRAMES)

    # Bouncing balls: clicks add balls, every bounce plays a note, and C clears them
    path = sample_copy("bouncing_balls", folder, [
        ("        var voice = speakers.play(ball.beep)\n", '        var voice = speakers.play(ball.beep)\n        print("bounce")\n'),
        ("    speakers.close()\nend", '    print(f"balls {len(balls)} gaps {speakers.gaps()}")\n    speakers.close()\nend'),
    ])
    built = run([str(JAMO), "build", str(path)])
    play = SampleRun(path.with_suffix(".exe"), "Bouncing Balls", folder) if built.returncode == 0 else None
    if play:
        for spot in ((200, 150), (600, 120), (400, 300)):
            play.click(*spot)
        time.sleep(1.5)
        busy = play.snap("bouncing balls")
        play.key(ord("C"))
        time.sleep(0.3)
        empty = play.snap("bouncing balls cleared")
        code, out, err = play.finish()
        bounces = out.count("bounce\n")
        report("bouncing balls runs, plays a note on bounces, and clears with C", code == 0 and busy and empty and bounces >= 5 and out.endswith("balls 0 gaps 0\n"), f"{code} {err[:300]} bounces {bounces} {out[-80:]}")
    else:
        report("bouncing balls builds", False, built.stderr)

    # Breakout: a bot in the test copy follows the ball, and the game is run on a fixed clock so it's quick
    path = sample_copy("breakout", folder, [
        ("        var step = min(window.frame_time(), 0.05)\n", "        var step = 1.0 / 60.0\n"),
        ("        window.wait_frame(60)\n", ""),
        ("        screen.clear(rgb(16, 18, 28))\n", "        screen.clear(rgb(16, 18, 28))\n        if game.waiting and not game.over\n            launch(game)\n        end\n        game.paddle_x = clamp(game.ball_x + (random_float() - 0.5) * 70.0, 60.0, 740.0)\n"),
        ('        if window.key_pressed("0")\n', '        if game.over\n            print(f"over won {game.won} score {game.score} balls {game.lives}")\n            screen.save_bmp("end.bmp")\n            break\n        end\n        if window.key_pressed("0")\n'),
    ])
    built = run([str(JAMO), "build", str(path)])
    play = SampleRun(path.with_suffix(".exe"), "Breakout", folder) if built.returncode == 0 else None
    if play:
        time.sleep(0.3)
        middle = play.snap("breakout playing")
        try:
            play.process.wait(timeout=180)
        except subprocess.TimeoutExpired:
            pass
        code, out, err = play.finish()
        if (folder / "end.bmp").exists():
            shutil.copy(folder / "end.bmp", SAMPLE_FRAMES / "breakout end.bmp")
        report("breakout is played to the end by test code and every brick is cleared", code == 0 and middle and "over won true score 2100" in out, f"{code} {err[:300]} {out[-200:]}")
    else:
        report("breakout builds", False, built.stderr)

    # The UI form: Tab, typing, arrow keys, Space and Enter fill it in and save it
    path = sample_copy("ui_form", folder, [("        window.wait_frame(60)\n    end\nend", "        window.wait_frame(60)\n    end\n    print(message)\nend")])
    built = run([str(JAMO), "build", str(path)])
    play = SampleRun(path.with_suffix(".exe"), "Sign Up", folder) if built.returncode == 0 else None
    if play:
        play.key(0x09)
        play.type("Sam")
        play.key(0x09)
        for _ in range(3):
            play.key(0x27)
        play.key(0x09)
        play.key(0x09)
        play.key(0x20)
        play.key(0x09)
        play.key(0x28)
        play.key(0x28)
        play.key(0x09)
        play.key(0x09)
        filled = play.snap("ui form filled in")
        play.key(0x0D)
        time.sleep(0.2)
        saved = play.snap("ui form saved")
        code, out, err = play.finish()
        report("the UI form is filled in and saved with the keyboard", code == 0 and filled and saved and out == "Saved: Sam, 27, Green Giants.\n", f"{code} {err[:300]} {out}")
    else:
        report("the UI form builds", False, built.stderr)

    # The spinning scene: the camera moves, and the arrow keys change how it flies
    path = sample_copy("spinning_scene", folder, [])
    built = run([str(JAMO), "build", str(path)])
    play = SampleRun(path.with_suffix(".exe"), "Spinning Scene", folder) if built.returncode == 0 else None
    if play:
        time.sleep(0.5)
        first = play.snap("spinning scene")
        play.post(WM_KEYDOWN, 0x28)
        time.sleep(1.0)
        play.post(WM_KEYUP, 0x28)
        second = play.snap("spinning scene later")
        code, out, err = play.finish()
        differ = first and second and (SAMPLE_FRAMES / "spinning scene.bmp").read_bytes() != (SAMPLE_FRAMES / "spinning scene later.bmp").read_bytes()
        report("the spinning scene runs and its camera moves", code == 0 and differ, f"{code} {err[:300]}")
    else:
        report("the spinning scene builds", False, built.stderr)

    # Fetch page: reads a page from a local test server
    class PageHandler(http.server.BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def do_GET(self):
            body = PAGE_BODY
            self.send_response(200)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    server = QuietServer(("127.0.0.1", 0), PageHandler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        address = f"http://127.0.0.1:{server.server_address[1]}/"
        shutil.copy(ROOT / "examples" / "fetch_page.jamo", folder / "fetch_page.jamo")
        result = run([str(JAMO), "run", str(folder / "fetch_page.jamo"), address], cwd=folder)
        want = f"Fetching {address}\nStatus 200, {len(PAGE_BODY)} bytes\nTitle: The Test Page\n  <html>\n  <head><title> The Test Page </title></head>\n  <body>\n  <p>Hello from Python.</p>\n  <p>Second line.</p>\n  ... 9 lines in all\n"
        report("fetch page reads a page from the local test server", result.returncode == 0 and clean(result.stdout) == want, f"{result.returncode} {result.stderr[:300]}\n{result.stdout}")
        result = run([str(JAMO), "run", str(folder / "fetch_page.jamo"), f"http://127.0.0.1:{free_port()}/"], cwd=folder)
        report("fetch page explains a server that isn't there", result.returncode == 0 and "That didn't work: couldn't connect to 127.0.0.1:" in result.stdout, result.stdout)
    finally:
        server.shutdown()
        server.server_close()


# --- Dividing by constants, which uses a multiply instead of idiv, checked against Python ---
def test_divide_by_constants(work):
    low, high = -(1 << 63), (1 << 63) - 1
    rng = random.Random(SEED)
    divisors = list(range(2, 130)) + [255, 256, 257, 1000, 1024, 3600, 65535, 65536, 1000003, 2147483647, 2147483648, 4294967296, 10 ** 12, 10 ** 18, high, 1 << 62, (1 << 62) + 1]
    numbers = [0, 1, -1, 2, -2, 7, -7, 100, -100, high, low, high - 1, low + 1, 1 << 32, -(1 << 32)] + [rng.randrange(low, high) for _ in range(30)] + [rng.randrange(-100000, 100000) for _ in range(30)]

    def trunc_div(a, b):
        q = abs(a) // abs(b)
        return q if (a >= 0) == (b > 0) else -q
    lines = ["func main()", "    var numbers: [int] = []"]
    lines += [f"    numbers.add({n})" for n in numbers]
    lines += ["    for n in numbers"] + [f'        print(f"{{n / {d}}} {{n % {d}}}")' for d in divisors] + ["    end", "end"]
    source = work / "divide by constants.jamo"
    source.write_text("\n".join(lines) + "\n", encoding="utf-8")
    want = "".join(f"{trunc_div(n, d)} {n - trunc_div(n, d) * d}\n" for n in numbers for d in divisors)
    for flags in ([], ["--no-optimize"]):
        built = run([str(JAMO), "build", *flags, str(source)])
        result = run([str(source.with_suffix(".exe"))], cwd=work) if built.returncode == 0 else built
        got = clean(result.stdout)
        wrong = [(g, w) for g, w in zip(got.splitlines(), want.splitlines()) if g != w]
        report(f"{len(numbers) * len(divisors)} divisions and remainders by constants match Python {' '.join(flags) or 'optimized'}", got == want, f"{result.returncode} {result.stderr[:200]} {wrong[:3]}")


# --- dict, builder and bytes, checked against Python ---
def test_dict_builder_bytes(work):
    folder = work / "dict builder bytes"
    folder.mkdir()
    rng = random.Random(SEED)
    lines = ['use "dict.jamo"', 'use "builder.jamo"', 'use "bytes.jamo"', "", "func main()", "    var d = new_dict()", "    var t = new_text_dict()"]
    want = []
    model, text_model = {}, {}
    for step in range(4000):
        key = f"k{rng.randrange(600)}"
        action = rng.random()
        if action < 0.5:
            value = rng.randrange(-10 ** 12, 10 ** 12)
            lines.append(f'    d.set("{key}", {value})')
            lines.append(f'    t.set("{key}", "v{value}")')
            model[key] = value
            text_model[key] = f"v{value}"
        elif action < 0.7:
            lines.append(f'    print(d.remove("{key}"))')
            lines.append(f'    t.remove("{key}")')
            want.append("true" if key in model else "false")
            model.pop(key, None)
            text_model.pop(key, None)
        else:
            lines.append(f'    print(d.get_or("{key}", -1))')
            lines.append(f'    print(t.get_or("{key}", "none"))')
            want.append(str(model.get(key, -1)))
            want.append(text_model.get(key, "none"))
        if step % 500 == 0:
            lines.append("    print(d.size())")
            want.append(str(len(model)))
    lines.append("    var keys = d.key_list()")
    lines.append("    var total = 0")
    lines.append("    for key in keys")
    lines.append("        total += d.get(key)")
    lines.append("    end")
    lines.append('    print(f"{len(keys)} {total}")')
    want.append(f"{len(model)} {sum(model.values())}")
    # The builder: many pieces, then one text
    lines += ["    var b = new_builder()", "    for i in 0..20000", '        b.add(f"{i},")', "        if i % 1000 == 0", '            b.line("")', "        end", "    end", "    var built = b.text()", '    print(f"{b.length()} {len(built)} {byte(built, 0)} {byte(built, len(built) - 1)}")']
    built = "".join(f"{i}," + ("\n" if i % 1000 == 0 else "") for i in range(20000))
    want.append(f"{len(built)} {len(built)} {built.encode()[0]} {built.encode()[-1]}")
    # Bytes: numbers in, patched, padded, saved and read back
    lines += ["    var by = new_bytes()", '    by.add_text("JAMO")', "    by.add_u8(255)", "    by.add_u16(65535)", "    by.add_u32(4294967295)", "    by.add_u32(-1)", "    by.add_u64(-2)", "    by.add_u32(0)", "    by.set_u32(19, 123456789)", "    by.pad_to(32)", '    by.save("out.bin")', '    var back = load_bytes("out.bin")', '    print(f"{back.length()} {back.get_u8(4)} {back.get_u16(5)} {back.get_u32(7)} {back.get_u32(11)} {back.get_u64(15)} {back.get_u32(19)}")', "end"]
    expected_file = b"JAMO" + bytes([255]) + (65535).to_bytes(2, "little") + (4294967295).to_bytes(4, "little") + (4294967295).to_bytes(4, "little") + (2 ** 64 - 2).to_bytes(8, "little") + (0).to_bytes(4, "little")
    expected_file = expected_file[:19] + (123456789).to_bytes(4, "little") + expected_file[23:]
    expected_file += bytes((-len(expected_file)) % 32)
    # The u32 at 19 was written over the top half of the u64 at 15
    want.append(f"{len(expected_file)} 255 65535 4294967295 4294967295 {int.from_bytes(expected_file[15:23], 'little', signed=True)} 123456789")
    source = folder / "libraries.jamo"
    source.write_text("\n".join(lines) + "\n", encoding="utf-8")
    for flags in ([], ["--gc-test", "--gc-check"]):
        built_exe = run([str(JAMO), "build", *flags, str(source)])
        result = run([str(source.with_suffix(".exe"))], cwd=folder, timeout=SLOW_RUN_LIMIT) if built_exe.returncode == 0 else built_exe
        got = clean(result.stdout).splitlines()
        file_ok = (folder / "out.bin").exists() and (folder / "out.bin").read_bytes() == expected_file
        report(f"dict and text dict match a Python dict over 4000 steps, the builder and bytes match Python {' '.join(flags) or 'normal'}", result.returncode == 0 and got == want and file_ok,
               f"{result.returncode} {result.stderr[:300]} file {file_ok}\n" + "\n".join(f"{g} | {w}" for g, w in zip(got, want) if g != w)[:800])
    for name, body, words in (
        ("a missing key", 'var d = new_dict()\n    print(d.get("nope"))', "This dict has no key nope."),
        ("a byte too big", "var b = new_bytes()\n    b.add_u8(256)", "256 doesn't fit in a byte, which goes from 0 to 255."),
        ("reading past the end", "var b = new_bytes()\n    b.add_u8(1)\n    print(b.get_u32(0))", "There's no room for 4 bytes at 0: this holds 1 bytes."),
    ):
        path = folder / f"{name}.jamo"
        path.write_text(f'use "dict.jamo"\nuse "bytes.jamo"\n\nfunc main()\n    {body}\nend\n', encoding="utf-8")
        result = run([str(JAMO), "run", str(path)], cwd=folder)
        report(f"dict and bytes give a clear error for {name}", result.returncode == 1 and words in result.stderr, f"{result.returncode} {result.stderr[:300]}")


# --- Phase 6 step 1: the lexer written in Jamo gives exactly the C++ lexer's tokens and errors ---
LEXER_EDGES = {
    "bom": b"\xef\xbb\xbffunc main()\n    print(1)\nend\n",
    "largest": b"func main()\n    print(-9223372036854775808)\n    print(9223372036854775807)\n    print(9223372036854775808)\n    print(92233720368547758070)\nend\n",
    "floats": b"func main()\n    print(0.1)\n    print(1.5)\n    print(123456789012345678901234567890.5)\n    print(0." + b"0" * 400 + b"1)\n    print(" + b"9" * 400 + b".0)\n    print(5.)\n    print(.5)\n    print(1..2)\n    print(7.x)\nend\n",
    "float limits": b"func main()\n    print(" + str(2 ** 1024 - 2 ** 970).encode() + b".0)\n    print(" + str(2 ** 1024 - 2 ** 970 - 1).encode() + b".0)\n    print(0." + b"0" * 323 + b"2470328229206232720882)\n    print(0." + b"0" * 323 + b"2470328229206232720883)\nend\n",
    "text": b'func main()\n    print("a\\nb\\tc\\rd\\"e\\\\f\\q")\n    print("never closed\n    print(f"x {1 + 2} y {{z}} {f"in {3}"} }")\n    print(f"bad } here")\n    print(f"open {1\n    print("caf\xc3\xa9")\nend\n',
    "symbols": b"func main()\n    var a = [1, 2]\n    a[0] += 1\n    a[1] -= 1\n    print(a[0] == 1 != 2 <= 3 >= 4 < 5 > 6 % 7)\n    x = @ $ ` ? ! ~ ^ & |\n    \x01\x7f\xff\nend\n",
    "brackets": b"func main()\n    var a = [1,\n        2,\n    print(a\n    var b = (1 +\n        2)\nend\n",
    "many errors": b"func main()\n" + b"    $ @ ? ` ~ ^ & |\n" * 6 + b"end\n",
    "crlf": b"func main()\r\n    print(1)\r\nend\r\n",
    "carry on": b"func main()\n    var x = 1 +\n        2 *\n        3\n    var y = x ==\n        9 and\n        not\n        true\n    var z = 1,\n        2\nend\n",
    "empty": b"",
    "dot at end": b"func main()\n    print(1.",
    "escape at end": b'func main()\n    print("abc\\',
}


def test_self_hosted_lexer(work):
    folder = work / "self-hosted lexer"
    folder.mkdir()
    built = run([str(JAMO), "build", str(ROOT / "compiler" / "tokens.jamo")])
    if built.returncode != 0:
        report("the Jamo-written lexer builds", False, built.stderr)
        return
    shutil.copy(ROOT / "compiler" / "tokens.exe", folder / "tokens.exe")
    files = {}
    for part in ("tests/good", "tests/bad", "tests/modules", "examples", "libraries", "compiler"):
        for path in (ROOT / part).rglob("*.jamo"):
            files[str(path.relative_to(ROOT))] = path.read_bytes()
    for name in ("GUIDE.md", "README.md", "LANGUAGE.md"):
        for i, block in enumerate(re.findall(r"```jamo[^\n]*\n(.*?)```", (ROOT / name).read_text(encoding="utf-8"), re.S)):
            files[f"{name} sample {i}"] = block.encode()
    files.update({f"edge: {name}": data for name, data in LEXER_EDGES.items()})
    rng = random.Random(SEED)
    pieces = [b"func", b"main", b"(", b")", b"\n", b"    ", b"var", b"x", b"=", b"1", b"1.5", b'"', b'f"', b"{", b"}", b"[", b"]", b"+", b"-", b"#", b".", b"..", b"\\", b"\t", b"\r", b"end", b"if", b"9" * 25, b"\xc3\xa9", b"\x00", b"==", b"+="]
    for i in range(400):
        files[f"garbage {i}"] = bytes(rng.randrange(256) for _ in range(rng.randrange(1, 300))) if i % 2 else b"".join(rng.choice(pieces) for _ in range(rng.randrange(1, 120)))
    different = []
    for name, data in files.items():
        source = folder / "case.jamo"
        source.write_bytes(data)
        want = run_bytes([str(CPP_JAMO), "tokens", str(source)]).replace(b"\r\n", b"\n")
        output = folder / "tokens.out"
        if output.exists():
            output.unlink()
        ran = subprocess.run([str(folder / "tokens.exe"), str(source)], cwd=folder, capture_output=True, timeout=60)
        got = output.read_bytes() if output.exists() else b"no output: " + ran.stderr[:300]
        if got != want:
            different.append(f"{name}: {got[:150]!r} | {want[:150]!r}")
    report(f"the Jamo-written lexer gives the C++ lexer's exact tokens and errors for {len(files)} files, guide samples, edge cases and garbage", not different, "\n".join(different[:5]))
    # The same in the strictest cleaner mode
    built = run([str(JAMO), "build", "--gc-test", "--gc-check", str(ROOT / "compiler" / "tokens.jamo")])
    shutil.copy(ROOT / "compiler" / "tokens.exe", folder / "tokens strict.exe")
    run([str(JAMO), "build", str(ROOT / "compiler" / "tokens.jamo")])
    source = folder / "case.jamo"
    source.write_bytes((ROOT / "compiler" / "lexer.jamo").read_bytes()[:5000])
    want = run_bytes([str(CPP_JAMO), "tokens", str(source)]).replace(b"\r\n", b"\n")
    ran = subprocess.run([str(folder / "tokens strict.exe"), str(source)], cwd=folder, capture_output=True, timeout=SLOW_RUN_LIMIT)
    got = (folder / "tokens.out").read_bytes() if (folder / "tokens.out").exists() else b""
    report("the Jamo-written lexer gives the same tokens with --gc-test --gc-check", built.returncode == 0 and ran.returncode == 0 and got == want, f"{built.stderr[:200]} {ran.returncode} {ran.stderr[:200]}")


def run_bytes(command):
    return subprocess.run(command, capture_output=True, timeout=60).stdout


# --- Phase 6 step 2: the parser written in Jamo gives exactly the C++ parser's tree and errors ---
PARSER_EDGES = {
    "deep parens": b"func main()\n    print(" + b"(" * 300 + b"1" + b")" * 300 + b")\nend\n",
    "deep not": b"func main()\n    print(" + b"not " * 300 + b"true)\nend\n",
    "deep minus": b"func main()\n    print(" + b"-" * 300 + b"1)\nend\n",
    "long sum": b"func main()\n    print(" + b" + ".join([b"1"] * 1500) + b")\nend\n",
    "deep type": b"func main()\n    var a: " + b"[" * 300 + b"int" + b"]" * 300 + b" = []\nend\n",
    "deep blocks": b"func main()\n" + b"".join(b"    " * (i + 1) + b"if true\n" for i in range(250)) + b"".join(b"    " * (250 - i) + b"end\n" for i in range(250)) + b"end\n",
    "long index": b"func main()\n    print(a" + b"[0]" * 1100 + b")\nend\n",
    "long dots": b"func main()\n    print(a" + b".b" * 1100 + b")\nend\n",
    "recovery": b"func main()\n    var = 1\n    if x\n        print(\n    end\n    while\n    end\n    for i 0..3\n        print(i)\n    end\n    x %= 2\n    3 + 4\nend\nfunc broken(\n    print(1)\nend\nfunc ok()\n    print(2)\nend\n",
    "case outside": b"func main()\n    case 1\n    if true\n    case 2\n    end\nend\n",
    "end with more after it": b"struct A\n    b: int\nend junk\nenum Mood\n    CALM\nend more\nstruct C\n    d: int\nend\nfunc main()\nend\n",
    "else twice": b"func main()\n    if a\n    else\n    else\n    end\n    match x\n    else\n    case 1\n    end\nend\n",
    "every part": b'use "x.jamo"\nstruct A\n    b: [int]\n    c: B\nend\nenum Mood\n    CALM\nend\nconst N = 2 * 3\nextern "k.dll" func F(a: i32, b: f32, c: int): f32\nfunc A.m(x: int): [string]\n    var v: [[A]] = []\n    v[0][1].c.d = A{b: [1], c: f(1, 2.5)}\n    for i in v\n        match i\n            case Mood.CALM\n                break\n            else\n                continue\n        end\n    end\n    if not a and b or c == -d\n        return [string(1), f"x{y}z", len("q")]\n    elif e\n        print(int(f) + float(g) % 2)\n    else\n        x += 1\n    end\n    while true\n        a.b(c)\n    end\nend\n',
}


def test_self_hosted_parser(work):
    folder = work / "self-hosted parser"
    folder.mkdir()
    built = run([str(JAMO), "build", str(ROOT / "compiler" / "tree.jamo")])
    if built.returncode != 0:
        report("the Jamo-written parser builds", False, built.stderr)
        return
    shutil.copy(ROOT / "compiler" / "tree.exe", folder / "tree.exe")
    files = {}
    for part in ("tests/good", "tests/bad", "tests/modules", "examples", "libraries", "compiler"):
        for path in (ROOT / part).rglob("*.jamo"):
            files[str(path.relative_to(ROOT))] = path.read_bytes()
    for name in ("GUIDE.md", "README.md", "LANGUAGE.md"):
        for i, block in enumerate(re.findall(r"```jamo[^\n]*\n(.*?)```", (ROOT / name).read_text(encoding="utf-8"), re.S)):
            files[f"{name} sample {i}"] = block.encode()
    files.update({f"edge: {name}": data for name, data in {**LEXER_EDGES, **PARSER_EDGES}.items()})
    rng = random.Random(SEED)
    words = [b"func", b"main", b"(", b")", b"\n", b"    ", b"var", b"x", b"=", b"1", b"2.5", b'"hi"', b'f"a{x}b"', b"{", b"}", b"[", b"]", b"+", b"-", b"*", b"%", b"==", b"<", b"and", b"or", b"not",
             b".", b"..", b",", b":", b"end", b"if", b"elif", b"else", b"while", b"for", b"in", b"match", b"case", b"return", b"break", b"print", b"struct", b"enum", b"const", b"use", b"extern",
             b"int", b"float", b"string", b"len", b"true", b"+=", b"Enemy{a: 1}", b"items[0]", b"a.b(1)"]
    real_files = list(files.values())
    for i in range(450):
        if i % 3 == 0:
            data = bytes(rng.randrange(256) for _ in range(rng.randrange(1, 300)))
        elif i % 3 == 1:
            data = b" ".join(rng.choice(words) for _ in range(rng.randrange(1, 150)))
        else:
            parts = rng.choice(real_files).split(b" ")
            for _ in range(rng.randrange(1, 6)):
                parts[rng.randrange(len(parts))] = rng.choice(words)
            data = b" ".join(parts)
        files[f"garbage {i}"] = data
    different = []
    for name, data in files.items():
        source = folder / "case.jamo"
        source.write_bytes(data)
        want = run_bytes([str(CPP_JAMO), "ast", str(source)]).replace(b"\r\n", b"\n")
        output = folder / "tree.out"
        if output.exists():
            output.unlink()
        ran = subprocess.run([str(folder / "tree.exe"), str(source)], cwd=folder, capture_output=True, timeout=120)
        got = output.read_bytes() if output.exists() else b"no output: " + ran.stderr[:300]
        if got != want:
            different.append(f"{name}: {got[:150]!r} | {want[:150]!r}")
    report(f"the Jamo-written parser gives the C++ parser's exact tree and errors for {len(files)} files, guide samples, edge cases and garbage", not different, "\n".join(different[:5]))
    built = run([str(JAMO), "build", "--gc-test", "--gc-check", str(ROOT / "compiler" / "tree.jamo")])
    shutil.copy(ROOT / "compiler" / "tree.exe", folder / "tree strict.exe")
    run([str(JAMO), "build", str(ROOT / "compiler" / "tree.jamo")])
    source = folder / "case.jamo"
    source.write_bytes(PARSER_EDGES["every part"])
    want = run_bytes([str(CPP_JAMO), "ast", str(source)]).replace(b"\r\n", b"\n")
    ran = subprocess.run([str(folder / "tree strict.exe"), str(source)], cwd=folder, capture_output=True, timeout=SLOW_RUN_LIMIT)
    got = (folder / "tree.out").read_bytes() if (folder / "tree.out").exists() else b""
    report("the Jamo-written parser gives the same tree with --gc-test --gc-check", built.returncode == 0 and ran.returncode == 0 and got == want, f"{built.stderr[:200]} {ran.returncode} {ran.stderr[:200]}")


# --- Phase 6 step 3: the checker written in Jamo gives exactly the C++ checker's checked tree, errors and warnings ---
CHECKER_EDGES = {
    "no main": b"func other()\nend\n",
    "main is wrong": b"func main(a: int): int\n    return a\nend\n",
    "constants": b"const A = 1\nconst B = A * 2 + -3\nconst C = \"x\" + \"y\"\nconst D = not true\nconst E = 1.5 * 2.0\nenum Mood\n    CALM\nend\nconst F = Mood.CALM\nfunc main()\n    print(B)\n    print(C)\n    print(D)\n    print(E)\n    print(F == Mood.CALM)\n    var A = 2\nend\n",
    "warnings": b"func main()\n    var a = 1\n    var b = 2\n    var zebra = 3\n    var apple = 4\n    if true\n        var c = 1\n    end\n    for i in 0..3\n        var d = i\n    end\n    return\n    print(b)\nend\n",
    "methods and fields": b"struct Enemy\n    health: int\n    name: string\n    friends: [Enemy]\nend\nfunc Enemy.hit(amount: int): bool\n    self.health -= amount\n    return self.health <= 0\nend\nfunc main()\n    var e = Enemy{health: 3, name: \"a\", friends: []}\n    e.friends.add(Enemy{name: \"b\", health: 1, friends: []})\n    print(e.hit(1))\n    print(e.friends[0].name)\n    for f in e.friends\n        print(f.health)\n    end\n    match e.name\n        case \"a\"\n            print(1)\n        else\n            print(2)\n    end\nend\n",
}

CHECKER_SETUP = b'struct A\n    b: int\n    c: int\nend\nenum Mood\n    CALM\n    WILD\nend\nconst N = 2 * 3\nfunc f(x: int): int\n    return x\nend\nfunc A.m(y: int): int\n    return y\nend\nfunc print_nothing()\nend\n'
CHECKER_NAMES = b'    var a = 1\n    var s = "abc"\n    var arr = [1]\n    var t = A{b: 1, c: 2}\n'
CHECKER_MISTAKES = [
    "var a = 1", "var a = 2", "var u = 3", "N = 4", "zz = 1", "print(aa)", "print(1 + 1.0)", 'print("a" + 1)', "print(1 < 2 < 3)", "print([1] == [1])",
    "print(A{b: 1, c: 2} == A{b: 1, c: 2})", "print(1 / 0)", "a /= 0", "print(not 1)", "print(f)", "print(Mood)", "print(Mood.CLAM)", "print(g(1))", "print(f(1, 2))",
    'print(f("x"))', "print(a(1))", "print(len(1))", "print(int(1))", "print(float(true))", "var e = []", 'var s = "abc"', 's[0] = "x"', 's -= "y"', "for i in 0..3\n        i = 2\n    end",
    "for k in 5\n    end", "for j in 1.5..2\n    end", "if 1\n    end", 'while "x"\n    end', "break", "continue", "match 1.5\n        case 1\n    end", "match Mood.CALM\n    end",
    'match a\n        case 1\n        case 1\n        case a\n        case "x"\n        case -1\n        case Mood.CALM\n    end', 'var arr = [1, "x"]', 'arr.add("y")', "arr.add(1, 2)", "arr.pop(1)",
    "arr.push(1)", "var t = A{b: 1, b: 2, z: 3}", "t.zz = 1", "t.mm(1)", "t.m()", 't.m("x")', "print(true.b)", "print(t[0])", 'print(arr["x"])', "print(callback(f))",
    "print(callback(nothing_here))", "print(callback(1))", "print(callback(A.m))", "print(read_file(1))", 'print(byte("a"))', "print(peek_f64(1, 2))", "poke_f32(1, 2, 3)",
    "print(9223372036854775808)", "print(-9223372036854775808)", 'print(-"x")', "print(print_nothing())", "var n = print_nothing()", "return 1", "print(f\"{print_nothing()}\")",
    "var w: [int] = [1.5]", 'var v: string = 1', "t.b = 1.5", "t.b += \"x\"", "print(A{b: 1})", "print(Nope{b: 1})", "var q: Nope = 1", "print(memory(\"x\"))", "print(args(1))",
    "print([print_nothing()])", "print(1 and true)", "print(true == 1)", "print(1 == 1.0)", "a = 1.5", "print(-true)", "x.y.z = 1", "print(\"abc\"[1.5])", "print(len(print_nothing()))",
]
for i in range(0, len(CHECKER_MISTAKES), 8):
    CHECKER_EDGES[f"mistakes {i // 8}"] = CHECKER_SETUP + b"func main()\n" + CHECKER_NAMES + b"".join(b"    " + line.encode() + b"\n" for line in CHECKER_MISTAKES[i:i + 8]) + b"end\n"
DECLARATIONS = b'struct A\n    b: int\n    b: float\n    c: Missing\n    d: [[Gone]]\nend\nstruct Loop\n    me: Loop\nend\nstruct Ring\n    next: Ringer\nend\nstruct Ringer\n    back: Ring\nend\nstruct Fine\n    more: [Fine]\nend\nenum Mood\n    CALM\n    CALM\nend\nenum Empty\nend\nconst N = 2 * 3\nconst M = M + 1\nconst P = f(1)\nconst Q = "a" + 1\nconst R = S\nconst S = R\nfunc f(x: int): int\n    if x > 1\n        return 1\n    end\nend\nfunc Gone.m()\nend\nfunc A.m(y: int): int\n    self = A{b: 1, c: 2, d: []}\n    return y\nend\nfunc main(x: int): int\n    return x\nend\nfunc main()\nend\nextern "a.dll" func W(a: string): string\nextern "a.dll" func W(a: int): int\nextern "b.dll" func W(a: string): string\nextern "a.dll" func W(a: string, b: int): string\nextern "a.dll" func W(a: string)\nextern "a.dll" func W(a: string): string\nextern "a.dll" func main(): int\nfunc input()\nend\nstruct f\n    a: int\nend\nfunc g(a: int, a: int)\nend\nfunc h(): int\n    while true\n        return 1\n    end\nend\nfunc k(): int\n    if true\n        return 1\n    elif false\n        print(1)\n    else\n        return 2\n    end\nend\nfunc l(m: Mood): int\n    match m\n        case Mood.CALM\n            return 1\n    end\nend\n'
SPLIT = DECLARATIONS.index(b"func f(x: int)")
CHECKER_EDGES["declaration mistakes"] = DECLARATIONS[:SPLIT] + b"func main()\nend\n"
CHECKER_EDGES["function mistakes"] = DECLARATIONS[SPLIT:]
# Several names just as close, so "Did you mean" must pick the first in sorted order, like std::map
TIED = [b"zzm", b"zzb", b"zzx", b"zza", b"zzk", b"zzc", b"zzy", b"zzd"]
CHECKER_EDGES["tied names"] = (b"".join(b"func " + name + b"()\nend\nstruct S" + name + b"\n    a: int\nend\n" for name in TIED) + b"struct Many\n" + b"".join(b"    " + name + b": int\n" for name in TIED) + b"end\n"
                               + b"func Many.zzq()\nend\nfunc Many.zzr()\nend\nfunc main()\n" + b"".join(b"    var " + name + b" = 1\n" for name in TIED)
                               + b"    zzq()\n    print(zzq)\n    var t: Szzq = 1\n    print(Szzq{a: 1})\n    var m = Many{zzm: 1, zzb: 1, zzx: 1, zza: 1, zzk: 1, zzc: 1, zzy: 1, zzd: 1}\n    print(m.zzq)\n    m.zzs()\n" + b"".join(b"    print(" + name + b")\n" for name in TIED) + b"end\n")


KEYWORDS = {b"func", b"end", b"var", b"if", b"elif", b"else", b"while", b"for", b"in", b"match", b"case", b"return", b"break", b"continue", b"print", b"struct", b"enum",
            b"const", b"use", b"extern", b"and", b"or", b"not", b"true", b"false"}
OPENS_OR_CLOSES = re.compile(rb"^\s*(func|end|if|elif|else|while|for|match|case|struct|enum|extern)\b")


def mutated(data, rng):
    """A real program with a few names, types or values swapped, or a plain line taken out, so it still parses but the checker has more to find"""
    words = [w for w in set(re.findall(rb"[A-Za-z_][A-Za-z_0-9]*", data)) if w not in KEYWORDS] or [b"x"]
    words.sort()
    swaps = [b"int", b"float", b"string", b"bool", b"1", b"1.5", b'"x"', b"true", b"self", b"main", b"len", b"Missing", b"0"]
    lines = data.split(b"\n")
    for _ in range(rng.randrange(1, 4)):
        i = rng.randrange(len(lines))
        if rng.randrange(4) == 0:
            if lines[i].strip() and not OPENS_OR_CLOSES.match(lines[i]):
                lines.pop(i)
            continue
        found = [m for m in re.finditer(rb"[A-Za-z_][A-Za-z_0-9]*|[0-9]+", lines[i]) if m.group() not in KEYWORDS]
        if not found:
            continue
        spot = rng.choice(found)
        before = lines[i][spot.start() - 1:spot.start()] if spot.start() else b""
        if spot.group()[:1].isdigit() and before == b".":
            continue
        new = rng.choice(words) if rng.randrange(2) else rng.choice(swaps)
        lines[i] = lines[i][:spot.start()] + new + lines[i][spot.end():]
    return b"\n".join(lines)


def test_self_hosted_checker(work):
    folder = work / "self-hosted checker"
    folder.mkdir()
    built = run([str(JAMO), "build", str(ROOT / "compiler" / "check.jamo")])
    if built.returncode != 0:
        report("the Jamo-written checker builds", False, built.stderr)
        return
    shutil.copy(ROOT / "compiler" / "check.exe", folder / "check.exe")
    # Files that use others are checked where they are, so their uses are found
    in_place = []
    for part in ("tests/good", "tests/bad", "tests/modules", "examples", "libraries", "compiler"):
        in_place += sorted((ROOT / part).rglob("*.jamo"))
    files = {}
    for name in ("GUIDE.md", "README.md", "LANGUAGE.md"):
        for i, block in enumerate(re.findall(r"```jamo[^\n]*\n(.*?)```", (ROOT / name).read_text(encoding="utf-8"), re.S)):
            files[f"{name} sample {i}"] = block.encode()
    files.update({f"edge: {name}": data for name, data in {**PARSER_EDGES, **CHECKER_EDGES}.items()})
    rng = random.Random(SEED)
    real_files = [path.read_bytes() for path in in_place if b"func main" in path.read_bytes()] + list(CHECKER_EDGES.values())
    for i in range(600):
        files[f"changed {i}"] = mutated(rng.choice(real_files), rng)

    def compare(name, source):
        want = run_bytes([str(CPP_JAMO), "checked", str(source)]).replace(b"\r\n", b"\n")
        output = folder / "check.out"
        if output.exists():
            output.unlink()
        try:
            ran = subprocess.run([str(folder / "check.exe"), str(source)], cwd=folder, capture_output=True, timeout=120)
        except subprocess.TimeoutExpired:
            different.append(f"{name}: took longer than 2 minutes")
            return
        got = output.read_bytes() if output.exists() else b"no output: " + ran.stderr[:300]
        if got != want:
            different.append(f"{name}: {got[:150]!r} | {want[:150]!r}")

    different = []
    # The same file used with different capitals is loaded once, like Windows
    capitals = folder / "capitals"
    capitals.mkdir()
    (capitals / "main.jamo").write_bytes(b'use "helper.jamo"\nuse "HELPER.jamo"\nuse "Sub/Deep.jamo"\nuse "sub/deep.JAMO"\nfunc main()\n    print(helped())\n    print(deep())\nend\n')
    (capitals / "helper.jamo").write_bytes(b'use "sub/DEEP.jamo"\nfunc helped(): int\n    return 1\nend\n')
    (capitals / "sub").mkdir()
    (capitals / "sub" / "deep.jamo").write_bytes(b'use "../Helper.jamo"\nfunc deep(): int\n    return 2\nend\n')
    in_place.append(capitals / "main.jamo")
    for path in in_place:
        compare(str(path.relative_to(ROOT) if path.is_relative_to(ROOT) else path.name), path)
    for name, data in files.items():
        source = folder / "case.jamo"
        source.write_bytes(data)
        compare(name, source)
    report(f"the Jamo-written checker gives the C++ checker's exact checked tree, errors and warnings for {len(in_place) + len(files)} files, guide samples, edge cases and changed programs",
           not different, "\n".join(different[:5]))
    built = run([str(JAMO), "build", "--gc-test", "--gc-check", str(ROOT / "compiler" / "check.jamo")])
    shutil.copy(ROOT / "compiler" / "check.exe", folder / "check strict.exe")
    run([str(JAMO), "build", str(ROOT / "compiler" / "check.jamo")])
    source = folder / "case.jamo"
    source.write_bytes(CHECKER_EDGES["declaration mistakes"] + CHECKER_EDGES["mistakes 0"])
    want = run_bytes([str(CPP_JAMO), "checked", str(source)]).replace(b"\r\n", b"\n")
    (folder / "check.out").unlink(missing_ok=True)
    ran = subprocess.run([str(folder / "check strict.exe"), str(source)], cwd=folder, capture_output=True, timeout=SLOW_RUN_LIMIT)
    got = (folder / "check.out").read_bytes() if (folder / "check.out").exists() else b""
    report("the Jamo-written checker gives the same answer with --gc-test --gc-check", built.returncode == 0 and ran.returncode == 0 and got == want, f"{built.stderr[:200]} {ran.returncode} {ran.stderr[:200]}")


# --- Phase 6 step 4: the code generator written in Jamo makes exactly the C++ one's machine code ---
DIVISORS = [2, 3, 5, 6, 7, 9, 10, 11, 12, 25, 60, 100, 125, 641, 1000, 4096, 65537, 1000000007, 4294967296, 4611686018427387904, 9223372036854775807]
CODEGEN_EDGES = {
    "constants": b"func main()\n    print(-9223372036854775807 - 1)\n    print(9223372036854775807 + 1)\n    print(3000000000 * 4000000000)\n    print((0 - 9223372036854775807 - 1) / -1)\n    print(7 % -1)\n    print(-7 / 2)\n    print(-7 % 2)\n    print(0.0 / 0.0)\n    print(1.0 / 0.0)\n    print(-1.0 / 0.0)\n    print(-0.0)\n    print(0.0 * -1.0)\n    print(0.1 + 0.2)\n    print(1.0 / 3.0 < 0.34)\n    print(not (1 < 2) or 3 >= 3 and 2 != 2)\n    print(1.5 == 1.5 and 2.5 != 2.5)\n    print(true == false)\n    print(-(-(5)))\n    print(not not true)\n    var nan = 0.0 / 0.0\n    print(nan == nan)\n    print(nan != nan)\n    print(nan < 1.0)\n    print(2147483647 + 0)\n    print(2147483648 + 0)\n    print(-2147483648 + 0)\n    print(-2147483649 + 0)\n    print(1000000000000000000000000000000.0 * 1000000000000000000000000000000.0 * 10000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000.0)\n    print(5.0 % 3.0)\nend\n",
    "divide by constants": b"func main()\n    var values = [0, 1, -1, 7, -7, 100, -100, 9223372036854775807, -9223372036854775807 - 1, 123456789, -987654321]\n    for v in values\n" + b"".join(f"        print(v / {d})\n        print(v % {d})\n".encode() for d in DIVISORS) + b"        print(v / -3)\n        print(v % -3)\n    end\nend\n",
    "registers": b"func f(a: int, b: int, c: int, d: int, e: int, g: int): int\n    var t = 0\n    for i in 0..10\n        t += a + b\n        t += c\n        while t > 1000\n            t -= d + e + g\n        end\n    end\n    var x = a\n    var y = a\n    var z = a\n    print(x + y + z)\n    return t\nend\nfunc main()\n    print(f(1, 2, 3, 4, 5, 6))\n    var ties1 = 1\n    var ties2 = 2\n    var ties3 = 3\n    var ties4 = 4\n    var ties5 = 5\n    print(ties1 + ties2 + ties3 + ties4 + ties5)\n    print(ties1 + ties2 + ties3 + ties4 + ties5)\n    print(ties5 + ties4 + ties3 + ties2 + ties1)\nend\n",
    "big frame": b"func big(): int\n" + b"".join(f"    var v{i} = {i}\n".encode() for i in range(1200)) + b"    return v0 + v1199\nend\nfunc main()\n    print(big())\nend\n",
    "control": b'enum Mood\n    CALM\n    WILD\n    SAD\nend\nfunc pick(m: Mood): string\n    match m\n        case Mood.CALM\n            return "c"\n        case Mood.WILD\n            return "w"\n        else\n            return "s"\n    end\nend\nfunc main()\n    for i in 0..10\n        if i == 3\n            continue\n        elif i == 8\n            break\n        end\n        match i % 3\n            case 0\n                print("zero")\n            case 1\n                print("one")\n        end\n    end\n    var word = "b"\n    match word\n        case "a"\n            print(1)\n        case "b"\n            print(2)\n    end\n    print(pick(Mood.WILD))\n    var n = 0\n    while true\n        n += 1\n        if n > 5 and not (n == 7)\n            break\n        end\n    end\n    while false\n        print(1)\n    end\n    if false\n        print(2)\n    else\n        print(3)\n    end\n    return\n    print(4)\nend\n',
    "show": b"struct Pet\n    name: string\n    age: int\n    weight: float\n    happy: bool\n    friends: [Pet]\n    mood: Mood\nend\nenum Mood\n    CALM\n    WILD\nend\nfunc main()\n    var p = Pet{name: \"a\\tb\\n\", age: 3, weight: 1.5, happy: true, friends: [], mood: Mood.WILD}\n    p.friends.add(Pet{name: \"c\", age: 1, weight: 0.25, happy: false, friends: [p], mood: Mood.CALM})\n    print(p)\n    print([[1, 2], [3]])\n    print([[\"x\"]])\n    print(Mood.CALM)\n    print(f\"{p.friends[0].mood} {[1.5]} {p.age}\")\n    print(string([true]))\n    var grid: [[float]] = [[]]\n    grid[0].add(2.0)\n    grid[0][0] *= 3.0\n    p.age += 4\n    p.name = p.name + \"!\"\n    print(grid)\n    print(p.age)\nend\n",
    "externs": b'extern "user32.dll" func GetSystemMetrics(index: i32): i32\nextern "kernel32.dll" func GetTickCount64(): int\nextern "kernel32.dll" func Sleep(ms: int)\nextern "user32.dll" func MessageBeep(kind: int): bool\nextern "gdi32.dll" func Thing(a: int, b: float, c: f32, d: i32, e: f32, f: float, g: bool, h: int): f32\nextern "OpenGL" func glClearColor(r: f32, g: f32, b: f32, a: f32)\nextern "OpenGL" func glGetError(): i32\nextern "kernel32.dll" func EnumSystemLocalesW(f: int, flags: int): bool\nfunc on_one(a: int): bool\n    return true\nend\nfunc on_two(a: int, b: bool): int\n    return a\nend\nfunc main()\n    if false\n        print(GetSystemMetrics(0))\n        print(Thing(1, 2.0, 3.0, 4, 5.0, 6.0, true, 8))\n        glClearColor(0.1, 0.2, 0.3, 1.0)\n        print(glGetError())\n        Sleep(0)\n        print(MessageBeep(0))\n        print(EnumSystemLocalesW(callback(on_two), 0))\n        print(callback(on_one) != 0)\n        print(callback(on_two))\n    end\n    print(GetTickCount64() > 0)\nend\n',
    "builtins": b'func main()\n    var m = memory(64)\n    poke_i64(m, 0, -5)\n    poke_i32(m, 8, -6)\n    poke_u16(m, 12, 65535)\n    poke_u8(m, 14, 200)\n    poke_f64(m, 16, 2.5)\n    poke_f32(m, 24, 0.5)\n    print(peek_i64(m, 0))\n    print(peek_i32(m, 8))\n    print(peek_u16(m, 12))\n    print(peek_u8(m, 14))\n    print(peek_f64(m, 16))\n    print(peek_f32(m, 24))\n    print(memory_text(m, -1))\n    print(len(memory_text(m, 3)))\n    print(byte("abc", 2))\n    print(wide("hi") != 0)\n    print(exists("nope.txt"))\n    print(len(args()))\n    var s = "ab"\n    print(s[1])\n    print(int("42") + 1)\n    print(float("1.5") * 2.0)\n    print(int(2.9))\n    print(float(3))\n    print(string(1.25) + string(true) + string(7))\n    var a = [1, 2, 3]\n    print(a.pop())\n    a[0] += 10\n    a[1] = a[0] / 3\n    print(a)\n    print(len(a))\n    if false\n        write_file("x.txt", "y")\n        print(read_file("x.txt"))\n        print(input())\n        fail("no")\n    end\nend\n',
    "deep loops": b"func main()\n    var a = 1\n    var b1 = 1\n    var b2 = 1\n    var b3 = 1\n    var b4 = 1\n    while false\n        while false\n            while false\n                b1 = b1 + b1 + b1\n                b2 = b2 + b2 + b2\n                b3 = b3 + b3 + b3\n                b4 = b4 + b4 + b4\n                while false\n                    print(a)\n                end\n            end\n        end\n    end\n    print(a + b1 + b2 + b3 + b4)\nend\n",
    "unused functions": b"func never(): int\n    return 1\nend\nfunc also_never()\n    print(never())\nend\nstruct S\n    a: int\nend\nfunc S.used(): int\n    return self.a\nend\nfunc S.unused(): int\n    return 2\nend\nfunc main()\n    var s = S{a: 1}\n    print(s.used())\nend\n",
}


def test_self_hosted_codegen(work):
    folder = work / "self-hosted code generator"
    folder.mkdir()
    built = run([str(JAMO), "build", str(ROOT / "compiler" / "code.jamo")])
    if built.returncode != 0:
        report("the Jamo-written code generator builds", False, built.stderr)
        return
    shutil.copy(ROOT / "compiler" / "code.exe", folder / "code.exe")
    different = []
    compared = 0

    # Stops at the first difference, so a planted bug that hangs can't take hours
    def compare(name, source, flags):
        nonlocal compared
        if different:
            return
        want = subprocess.run([str(CPP_JAMO), "code", str(flags), str(source)], capture_output=True, timeout=120)
        if want.returncode != 0:
            return
        compared += 1
        output = folder / "code.out"
        output.unlink(missing_ok=True)
        try:
            ran = subprocess.run([str(folder / "code.exe"), str(flags), str(source)], cwd=folder, capture_output=True, timeout=30)
        except subprocess.TimeoutExpired:
            different.append(f"{name} flags {flags}: took longer than 30 seconds")
            return
        got = output.read_bytes() if output.exists() else b"no output: " + ran.stderr[:300]
        if got != want.stdout.replace(b"\r\n", b"\n"):
            different.append(f"{name} flags {flags}")

    in_place = []
    for part in ("tests/good", "tests/modules", "examples", "compiler"):
        in_place += sorted((ROOT / part).rglob("*.jamo"))
    for i, path in enumerate(in_place):
        for flags in (0, 8) if i % 4 else (0, 1, 2, 4, 8, 9, 16, 17):
            compare(str(path.relative_to(ROOT)), path, flags)
    files = {}
    for name in ("GUIDE.md", "README.md", "LANGUAGE.md"):
        for i, block in enumerate(re.findall(r"```jamo[^\n]*\n(.*?)```", (ROOT / name).read_text(encoding="utf-8"), re.DOTALL)):
            files[f"{name} sample {i}"] = block.encode()
    files.update({f"edge: {name}": data for name, data in CODEGEN_EDGES.items()})
    rng = random.Random(SEED)
    for i in range(150):
        files[f"random math {i}"] = random_program(rng)[0].encode()
        files[f"random floats {i}"] = random_float_program(rng)[0].encode()
    for name, data in files.items():
        source = folder / "case.jamo"
        source.write_bytes(data)
        for flags in (0, 8):
            compare(name, source, flags)
    report(f"the Jamo-written code generator makes exactly the C++ one's machine code, strings and imports for {compared} builds of files, guide samples, edge cases and random programs",
           compared > 500 and not different, f"{compared} compared\n" + "\n".join(different[:5]))
    built = run([str(JAMO), "build", "--gc-test", "--gc-check", str(ROOT / "compiler" / "code.jamo")])
    shutil.copy(ROOT / "compiler" / "code.exe", folder / "code strict.exe")
    run([str(JAMO), "build", str(ROOT / "compiler" / "code.jamo")])
    source = folder / "case.jamo"
    source.write_bytes(CODEGEN_EDGES["show"])
    want = run_bytes([str(CPP_JAMO), "code", "0", str(source)]).replace(b"\r\n", b"\n")
    (folder / "code.out").unlink(missing_ok=True)
    ran = subprocess.run([str(folder / "code strict.exe"), "0", str(source)], cwd=folder, capture_output=True, timeout=SLOW_RUN_LIMIT)
    got = (folder / "code.out").read_bytes() if (folder / "code.out").exists() else b""
    report("the Jamo-written code generator gives the same answer with --gc-test --gc-check", built.returncode == 0 and ran.returncode == 0 and got == want, f"{built.stderr[:200]} {ran.returncode} {ran.stderr[:200]}")


# --- Phase 6 step 5: the PE writer written in Jamo makes exactly the C++ one's .exe files ---
FLAG_WORDS = {1: "--gc-test", 2: "--gc-check", 4: "--gc-off", 8: "--no-optimize", 16: "--window"}


def test_self_hosted_pe(work):
    folder = work / "self-hosted pe"
    folder.mkdir()
    built = run([str(JAMO), "build", str(ROOT / "compiler" / "exe.jamo")])
    if built.returncode != 0:
        report("the Jamo-written PE writer builds", False, built.stderr)
        return
    tool = folder / "exe.exe"
    shutil.copy(ROOT / "compiler" / "exe.exe", tool)
    different = []
    compared = 0

    # Builds the source both ways and compares the .exe files. Stops at the first difference, so a hang can't take hours
    def compare(name, source, flags):
        nonlocal compared
        if different:
            return
        words = [word for bit, word in FLAG_WORDS.items() if flags & bit]
        if run([str(JAMO), "build", *words, str(source)], timeout=120).returncode != 0:
            return
        want = source.with_suffix(".exe").read_bytes()
        compared += 1
        output = source.parent / "out.exe"
        output.unlink(missing_ok=True)
        try:
            ran = subprocess.run([str(tool), str(flags), str(source)], cwd=source.parent, capture_output=True, timeout=30)
        except subprocess.TimeoutExpired:
            different.append(f"{name} flags {flags}: took longer than 30 seconds")
            return
        got = output.read_bytes() if output.exists() else b"no output: " + ran.stderr[:300]
        if got != want:
            different.append(f"{name} flags {flags}: {len(got)} bytes against {len(want)}")

    # Copies, since building writes the .exe beside the source
    copies = folder / "copies"
    for part in ("tests/good", "tests/modules", "examples", "compiler"):
        shutil.copytree(ROOT / part, copies / part, ignore=shutil.ignore_patterns("*.exe", "*.out", "__pycache__"))
    sources = sorted(copies.rglob("*.jamo"))
    for i, path in enumerate(sources):
        for flags in (0, 8) if i % 4 else (0, 1, 2, 4, 8, 9, 16, 17):
            compare(str(path.relative_to(copies)), path, flags)
    cases = {}
    for name in ("GUIDE.md", "README.md", "LANGUAGE.md"):
        for i, block in enumerate(re.findall(r"```jamo[^\n]*\n(.*?)```", (ROOT / name).read_text(encoding="utf-8"), re.DOTALL)):
            cases[f"{name} sample {i}"] = block.encode()
    cases.update({f"edge: {name}": data for name, data in CODEGEN_EDGES.items()})
    # One DLL written with different capitals shares one import entry, like Windows
    cases["edge: dll capitals"] = (b'extern "KERNEL32.DLL" func Sleep(ms: int)\nextern "user32.dll" func GetSystemMetrics(index: i32): i32\n'
                                   b'extern "User32.DLL" func GetDoubleClickTime(): i32\nextern "Kernel32.dll" func GetTickCount64(): int\n'
                                   b'func main()\n    Sleep(0)\n    print(GetSystemMetrics(0) >= 0)\n    print(GetDoubleClickTime() > 0)\n    print(GetTickCount64() > 0)\nend\n')
    rng = random.Random(SEED)
    for i in range(60):
        cases[f"random math {i}"] = random_program(rng)[0].encode()
        cases[f"random floats {i}"] = random_float_program(rng)[0].encode()
    for name, data in cases.items():
        source = folder / "case.jamo"
        source.write_bytes(data)
        compare(name, source, 0)
    report(f"the Jamo-written PE writer makes exactly the C++ one's .exe for {compared} builds of files, guide samples, edge cases and random programs",
           compared > 300 and not different, f"{compared} compared\n" + "\n".join(different[:5]))
    if different:
        return

    # The compiler written in Jamo builds itself: the C++ build, its build of itself, and that one's build of itself are the same file
    stages = folder / "stages"
    shutil.copytree(ROOT / "compiler", stages, ignore=shutil.ignore_patterns("*.exe", "*.out"))
    run([str(JAMO), "build", str(stages / "exe.jamo")])
    names = []
    for stage in (2, 3):
        ran = subprocess.run([str(stages / "exe.exe"), "0", str(stages / "exe.jamo")], cwd=stages, capture_output=True, timeout=SLOW_RUN_LIMIT)
        if ran.returncode != 0:
            break
        (stages / f"stage {stage}.exe").write_bytes((stages / "out.exe").read_bytes())
        shutil.copy(stages / f"stage {stage}.exe", stages / "exe.exe")
        names.append(f"stage {stage}.exe")
    stage_1 = (stages / "exe.jamo").with_suffix(".exe")
    same = len(names) == 2 and (stages / "stage 2.exe").read_bytes() == (stages / "stage 3.exe").read_bytes()
    run([str(JAMO), "build", str(stages / "exe.jamo")])
    same = same and stage_1.read_bytes() == (stages / "stage 2.exe").read_bytes()
    report("the compiler written in Jamo builds itself, and the C++ build, its build and that build's build are the same file", same, f"{names}")
    built = run([str(JAMO), "build", "--gc-test", "--gc-check", str(ROOT / "compiler" / "exe.jamo")])
    shutil.copy(ROOT / "compiler" / "exe.exe", folder / "exe strict.exe")
    run([str(JAMO), "build", str(ROOT / "compiler" / "exe.jamo")])
    source = folder / "case.jamo"
    source.write_bytes(CODEGEN_EDGES["show"])
    run([str(JAMO), "build", str(source)])
    (folder / "out.exe").unlink(missing_ok=True)
    ran = subprocess.run([str(folder / "exe strict.exe"), "0", str(source)], cwd=folder, capture_output=True, timeout=SLOW_RUN_LIMIT)
    got = (folder / "out.exe").read_bytes() if (folder / "out.exe").exists() else b""
    report("the Jamo-written PE writer gives the same .exe with --gc-test --gc-check", built.returncode == 0 and ran.returncode == 0 and got == source.with_suffix(".exe").read_bytes(), f"{built.stderr[:200]} {ran.returncode} {ran.stderr[:200]}")


# --- Phase 6 step 6: the jamo command written in Jamo, and the self-compile test ---
def command_cases(folder):
    (folder / "hello.jamo").write_text('func main()\n    print("hi")\n    var unused = 1\nend\n', encoding="utf-8")
    (folder / "bad.jamo").write_text("func main()\n    print(x)\nend\n", encoding="utf-8")
    (folder / "args.jamo").write_text("func main()\n    for a in args()\n        print(a)\n    end\n    print(10 / (len(args()) - 1))\nend\n", encoding="utf-8")
    (folder / "deep.jamo").write_text("func down(n: int): int\n    return down(n + 1) + 1\nend\nfunc main()\n    print(down(0))\nend\n", encoding="utf-8")
    (folder / "notes.txt").write_text("x", encoding="utf-8")
    (folder / "Upper.JAMO").write_text("func main()\nend\n", encoding="utf-8")
    (folder / "taken").mkdir()
    return [
        [], ["help"], ["version"], ["help", "x"], ["frob"], ["frob", "x"], ["build"], ["build", "--gc-test"],
        ["build", "hello.jamo"], ["build", "hello.jamo", "extra"], ["check", "hello.jamo"], ["check", "bad.jamo"], ["build", "bad.jamo"],
        ["build", "notes.txt"], ["build", "Upper.JAMO"], ["build", "missing.jamo"], ["run", "hello.jamo"],
        ["run", "--gc-test", "--no-optimize", "hello.jamo"], ["run", "args.jamo", "a b", 'q"uote', "back\\\\", 'x\\"y', ""],
        ["run", "args.jamo", "one"], ["new", "taken"], ["new", "fresh"], ["new", "deep/er\\one"], ["check", "fresh/main.jamo"],
        ["--gc-test", "build", "hello.jamo"], ["build", "--wrong", "hello.jamo"], ["run"], ["build", "--gc-off", "--gc-check", "hello.jamo"],
        ["build", "--no-optimize", "args.jamo"], ["build", "--gc-test", "--gc-check", "args.jamo"], ["run", "deep.jamo"],
        ["build", "--window", "hello.jamo"], ["build", "--window", "--gc-test", "args.jamo"], ["run", "--window", "hello.jamo"], ["help"],
        ["new", "--game", "game_one"], ["new", "game_two", "--game"], ["new", "--game", "taken"], ["new", "--game"], ["new", "a", "b"], ["new", "--game", "--game"], ["new", "--window"],
    ]


def test_self_compile(work):
    folder = work / "self compile"
    stages = folder / "stages"
    shutil.copytree(ROOT / "compiler", stages, ignore=shutil.ignore_patterns("*.exe", "*.out"))
    source = stages / "jamo.jamo"
    # Stage 1: the C++ compiler builds the Jamo compiler. Stage 2: stage 1 builds it. Stage 3: stage 2 builds it
    built = run([str(CPP_JAMO), "build", str(source)], timeout=SLOW_RUN_LIMIT)
    if built.returncode != 0:
        report("the C++ compiler builds the compiler written in Jamo", False, built.stderr)
        return
    shutil.copy(source.with_suffix(".exe"), folder / "stage 1.exe")
    for stage in (2, 3):
        built = run([str(folder / f"stage {stage - 1}.exe"), "build", str(source)], timeout=SLOW_RUN_LIMIT)
        if built.returncode != 0:
            report(f"stage {stage - 1} builds the compiler", False, built.stderr)
            return
        shutil.copy(source.with_suffix(".exe"), folder / f"stage {stage}.exe")
    stage_2 = (folder / "stage 2.exe").read_bytes()
    report("the self-compile test: stage 2 and stage 3 are the same, byte for byte", stage_2 == (folder / "stage 3.exe").read_bytes())
    report("stage 1, built by C++, is the same file too", (folder / "stage 1.exe").read_bytes() == stage_2)

    # The compiler built with --gc-test cleans at every allocation, and each clean walks everything in use, so compiling
    # itself takes hours. Normally it compiles the lexer program instead, about 30 s. JAMO_LONG_TESTS=1 does the whole thing
    built = run([str(CPP_JAMO), "build", "--gc-test", str(source)], timeout=SLOW_RUN_LIMIT)
    shutil.copy(source.with_suffix(".exe"), folder / "cleaning.exe")
    shutil.copy(folder / "stage 2.exe", source.with_suffix(".exe"))
    target = source if os.environ.get("JAMO_LONG_TESTS") else stages / "tokens.jamo"
    run([str(folder / "stage 2.exe"), "build", str(target)], timeout=SLOW_RUN_LIMIT)
    wanted = target.with_suffix(".exe").read_bytes()
    target.with_suffix(".exe").unlink()
    started = time.perf_counter()
    again = run([str(folder / "cleaning.exe"), "build", str(target)], timeout=None if os.environ.get("JAMO_LONG_TESTS") else SLOW_RUN_LIMIT)
    seconds = time.perf_counter() - started
    print(f"  the compiler built with --gc-test took {seconds:.1f} s to compile {target.name}")
    report(f"the compiler built with --gc-test compiles {target.name}, giving the same .exe as the normal compiler", built.returncode == 0 and again.returncode == 0 and target.with_suffix(".exe").exists() and target.with_suffix(".exe").read_bytes() == wanted, f"{built.stderr[:300]} {again.stderr[:300]}")

    # Every command acts the same as the C++ jamo.exe: exit codes, the exact bytes printed and the files made
    cases = folder / "commands"
    cases.mkdir()
    commands = command_cases(cases)
    different = []
    for words in commands:
        results = []
        for exe in (CPP_JAMO, folder / "stage 2.exe"):
            for made in ("fresh", "deep", "game_one", "game_two", "a", "--game"):
                shutil.rmtree(cases / made, ignore_errors=True)
            for old in cases.rglob("*.exe"):
                old.unlink()
            ran = subprocess.run([str(exe), *words], cwd=cases, capture_output=True, timeout=120)
            built_files = sorted((str(p.relative_to(cases)), zlib.crc32(p.read_bytes())) for p in cases.rglob("*.exe"))
            made_files = sorted((str(p.relative_to(cases)), zlib.crc32(p.read_bytes())) for p in cases.rglob("main.jamo"))
            results.append((ran.returncode, ran.stdout, ran.stderr, made_files, built_files))
        if results[0] != results[1]:
            different.append(f"{words}: {results[0]!r:.200} | {results[1]!r:.200}")
    report(f"the jamo command written in Jamo acts exactly like the C++ one for {len(commands)} commands", not different, "\n".join(different[:4]))
    # jamo new --game starts from a game that checks with no warnings, and builds as a window program
    shutil.rmtree(cases / "game_one", ignore_errors=True)
    made = run([str(folder / "stage 2.exe"), "new", "--game", str(cases / "game_one")])
    checked = run([str(folder / "stage 2.exe"), "check", str(cases / "game_one" / "main.jamo")])
    built = run([str(folder / "stage 2.exe"), "build", "--window", str(cases / "game_one" / "main.jamo")])
    report("jamo new --game makes a game that checks with no warnings and builds as a window program",
           made.returncode == 0 and checked.returncode == 0 and checked.stderr == "" and built.returncode == 0 and exe_subsystem(cases / "game_one" / "main.exe") == 2, made.stderr + checked.stderr + built.stderr)

    # Garbage never crashes the compiler written in Jamo: every answer is 0 or 1, with nothing left half-written
    rng = random.Random(SEED)
    crashes = []
    garbage = folder / "garbage.jamo"
    for i in range(300):
        parts = rng.choice([p.read_bytes() for p in sorted(GOOD.glob("*.jamo"))[:40]]).split(b" ")
        for _ in range(rng.randrange(1, 6)):
            parts[rng.randrange(len(parts))] = rng.choice([b"(", b")", b"end", b"x", b"\n", b'"', b"[", b"1.5", b"func", b"\xff", b"\x00", b"    "])
        garbage.write_bytes(b" ".join(parts) if i % 3 else bytes(rng.randrange(256) for _ in range(rng.randrange(1, 300))))
        ran = subprocess.run([str(folder / "stage 2.exe"), "build", str(garbage)], capture_output=True, timeout=60)
        if ran.returncode not in (0, 1):
            crashes.append(f"{i}: code {ran.returncode} {ran.stderr[:200]!r}")
    report("300 garbage files never crash the compiler written in Jamo", not crashes and not garbage.with_suffix(".exe.part").exists(), "\n".join(crashes[:4]))

    # Speed against the C++ compiler, reported honestly
    times = {}
    for name, exe in (("C++", CPP_JAMO), ("Jamo", folder / "stage 2.exe")):
        best = 1e9
        for _ in range(5):
            started = time.perf_counter()
            run([str(exe), "build", str(source)], timeout=SLOW_RUN_LIMIT)
            best = min(best, time.perf_counter() - started)
        times[name] = best
    print(f"  building the compiler: C++ {times['C++']:.3f} s, Jamo {times['Jamo']:.3f} s ({times['Jamo'] / times['C++']:.1f}x)")


# --- is_int and is_float: true exactly when int(text) and float(text) would work ---
def python_is_int(text):
    if not re.fullmatch(r"-?[0-9]+", text):
        return False
    return -(2 ** 63) <= int(text) < 2 ** 63


def python_is_float(text):
    if not re.fullmatch(r"-?[0-9]+(\.[0-9]+)?", text):
        return False
    value = float(text)
    return not math.isinf(value) and not (value == 0.0 and re.search(r"[1-9]", text))


def number_texts(rng):
    texts = ["", "-", "0", "-0", "7", "-7", "007", "+5", " 5", "5 ", "5x", "1.", ".5", "-.5", "1.5", "-1.5", "1..5", "1.5.2", "0.0", "00.000", "1e5", "nan", "inf",
             "9223372036854775807", "9223372036854775808", "-9223372036854775808", "-9223372036854775809", "00009223372036854775807", "99999999999999999999",
             "1" + "0" * 308, "1" + "0" * 309, "17976931348623157" + "0" * 292, "17976931348623158" + "0" * 292, "-17976931348623158" + "0" * 292,
             "179769313486231580793728971405303415079934132710037826936173778980444968292764750946649017977587207096330286416692887910946555547851940402630657488671505820681908902000708383676273854845817711531764475730270069855571366959622842914819860834936475292719074168444365510704342711559699508093042880177904174497791",
             "179769313486231580793728971405303415079934132710037826936173778980444968292764750946649017977587207096330286416692887910946555547851940402630657488671505820681908902000708383676273854845817711531764475730270069855571366959622842914819860834936475292719074168444365510704342711559699508093042880177904174497792",
             "0." + "0" * 323 + "25", "0." + "0" * 323 + "247", "0." + "0" * 323 + "2470328229206232720882843964341106861825299013071623822127928412503377536351043759326499181808179961898982823477228588654633283551779698981993873980053909390631503565951557022639229085839244910518443593180284993653615250031937045767824921936562366986365848075700158576926990370631192827955855133292783433840935197801553124659726357957462276646527282722005637400648549997709659947045402082816622623785739345073633900796776193057750674017632467360096895134053553745851666113422376667860416215968046191446729184030053005753084904876539171138659164623952491262365388187963623937328042389101867234849766823508986338858792562830275599565752445550725518931369083625477918694866799496832404970582102851318545139621383772282614543769341253209859132766723632812",
             "0." + "0" * 323 + "2470328229206232720882843964341106861825299013071623822127928412503377536351043759326499181808179961898982823477228588654633283551779698981993873980053909390631503565951557022639229085839244910518443593180284993653615250031937045767824921936562366986365848075700158576926990370631192827955855133292783433840935197801553124659726357957462276646527282722005637400648549997709659947045402082816622623785739345073633900796776193057750674017632467360096895134053553745851666113422376667860416215968046191446729184030053005753084904876539171138659164623952491262365388187963623937328042389101867234849766823508986338858792562830275599565752445550725518931369083625477918694866799496832404970582102851318545139621383772282614543769341253209859132766723632813",
             "0." + "0" * 400, "-0." + "0" * 400 + "1"]
    pieces = ["0", "1", "9", "5", "-", ".", " ", "a", "00", "123"]
    for _ in range(400):
        texts.append("".join(rng.choice(pieces) for _ in range(rng.randrange(1, 8))))
    return texts


def test_number_checks(work):
    folder = work / "number checks"
    folder.mkdir()
    texts = number_texts(random.Random(SEED))
    listed = ", ".join('"' + text + '"' for text in texts)
    source = folder / "checks.jamo"
    source.write_text('use "text.jamo"\n\nfunc main()\n    for text in [' + listed + ']\n        print(f"{is_int(text)} {is_float(text)}")\n    end\nend\n', encoding="utf-8")
    built = run([str(JAMO), "build", str(source)])
    got = clean(run([str(source.with_suffix(".exe"))]).stdout).splitlines() if built.returncode == 0 else []
    want = [f"{str(python_is_int(t)).lower()} {str(python_is_float(t)).lower()}" for t in texts]
    different = [f"{t[:40]!r}: got {g}, want {w}" for t, g, w in zip(texts, got, want) if g != w]
    report(f"is_int and is_float agree with the rules for {len(texts)} texts, including the edges of 64 bits and of floats", built.returncode == 0 and len(got) == len(texts) and not different, built.stderr[:300] + "\n".join(different[:5]))
    # And int() and float() really do work exactly when they say so, at the edges
    wrong = []
    for i, text in enumerate(texts[:60]):
        for kind in ("int", "float"):
            program = folder / f"{kind}_{i}.jamo"
            program.write_text(f'func main()\n    print({kind}(read_file("text.txt")))\nend\n', encoding="utf-8")
            (folder / "text.txt").write_text(text, encoding="utf-8")
            run([str(JAMO), "build", str(program)])
            works = run([str(program.with_suffix(".exe"))], cwd=folder).returncode == 0
            said = (python_is_int if kind == "int" else python_is_float)(text)
            if works != said:
                wrong.append(f"{kind}({text[:40]!r}) works: {works}, said: {said}")
    report("int() and float() work exactly when is_int and is_float say they will, on 60 edge cases", not wrong, "\n".join(wrong[:5]))


# --- Window programs: jamo build --window makes a program with no console, whose run errors show in a message box ---
class StartupInfo(ctypes.Structure):
    _fields_ = [("cb", ctypes.wintypes.DWORD), ("reserved", ctypes.wintypes.LPWSTR), ("desktop", ctypes.wintypes.LPWSTR), ("title", ctypes.wintypes.LPWSTR)] + \
        [(name, ctypes.wintypes.DWORD) for name in ("x", "y", "width", "height", "columns", "rows", "fill", "flags")] + \
        [("show", ctypes.wintypes.WORD), ("reserved2_size", ctypes.wintypes.WORD), ("reserved2", ctypes.c_void_p), ("stdin", ctypes.wintypes.HANDLE), ("stdout", ctypes.wintypes.HANDLE), ("stderr", ctypes.wintypes.HANDLE)]


class ProcessInfo(ctypes.Structure):
    _fields_ = [("process", ctypes.wintypes.HANDLE), ("thread", ctypes.wintypes.HANDLE), ("process_id", ctypes.wintypes.DWORD), ("thread_id", ctypes.wintypes.DWORD)]


def exe_subsystem(path):
    data = path.read_bytes()
    header = struct.unpack_from("<I", data, 60)[0]
    return struct.unpack_from("<H", data, header + 24 + 68)[0]


def message_box_on_hidden_desktop(exe, title):
    """Runs exe on a desktop of its own, so nothing shows on the real screen. Reads the message box called title, closes it,
    and gives back its text and the exit code"""
    user32, kernel32 = ctypes.windll.user32, ctypes.windll.kernel32
    user32.CreateDesktopW.restype = ctypes.wintypes.HANDLE
    user32.FindWindowExW.restype = ctypes.wintypes.HWND
    user32.GetDlgItem.restype = ctypes.wintypes.HWND
    name = f"jamo_test_{os.getpid()}"
    desktop = user32.CreateDesktopW(name, None, None, 0, 0x10000000, None)
    found = {}

    def watch():
        user32.SetThreadDesktop(desktop)
        deadline = time.time() + 20
        while time.time() < deadline:
            box = user32.FindWindowExW(None, None, None, title)
            if box:
                text = ctypes.create_unicode_buffer(8192)
                user32.GetWindowTextW(user32.GetDlgItem(box, 0xFFFF), text, 8192)
                found["text"] = text.value
                user32.PostMessageW(box, 0x0010, 0, 0)
                return
            time.sleep(0.05)

    startup = StartupInfo()
    startup.cb = ctypes.sizeof(startup)
    startup.desktop = name
    info = ProcessInfo()
    if not kernel32.CreateProcessW(str(exe), None, None, None, False, 0, None, str(exe.parent), ctypes.byref(startup), ctypes.byref(info)):
        user32.CloseDesktop(desktop)
        return None, None
    watcher = threading.Thread(target=watch)
    watcher.start()
    watcher.join()
    kernel32.WaitForSingleObject(info.process, 20000)
    code = ctypes.wintypes.DWORD()
    kernel32.GetExitCodeProcess(info.process, ctypes.byref(code))
    kernel32.TerminateProcess(info.process, 99)
    kernel32.CloseHandle(info.process)
    kernel32.CloseHandle(info.thread)
    user32.CloseDesktop(desktop)
    return found.get("text"), code.value


def test_window_programs(work):
    folder = work / "window programs"
    folder.mkdir()
    source = folder / "boom.jamo"
    source.write_text('func half(n: int): int\n    return 10 / n\nend\n\nfunc main()\n    print("starting")\n    print(half(len(args())))\nend\n', encoding="utf-8")
    run([str(JAMO), "build", str(source)])
    console = run([str(source.with_suffix(".exe"))])
    report("a normal build is a console program", exe_subsystem(source.with_suffix(".exe")) == 3)
    built = run([str(JAMO), "build", "--window", str(source)])
    report("jamo build --window makes a window program, with no console", built.returncode == 0 and exe_subsystem(source.with_suffix(".exe")) == 2, built.stderr)
    text, code = message_box_on_hidden_desktop(source.with_suffix(".exe"), "boom stopped")
    report("a window program's run error shows in a message box, with the same words as on the console, and exit code 1",
           text is not None and text.replace("\r\n", "\n") == console.stderr.replace("\r\n", "\n") and code == 1, f"{text!r} {code} {console.stderr!r}")
    # A long error is cut short at 4095 bytes, not past the end of the space kept for it
    long = folder / "long.jamo"
    long.write_text('func main()\n    fail("' + "x" * 5000 + '")\nend\n', encoding="utf-8")
    run([str(JAMO), "build", "--window", str(long)])
    text, code = message_box_on_hidden_desktop(long.with_suffix(".exe"), "long stopped")
    report("a very long run error in a window program is cut to 4095 bytes", text is not None and len(text.encode("utf-8")) == 4095 and text.startswith("long.jamo line 2: Error: xxx") and code == 1, f"{len(text or '')} {code}")
    # A window program that finishes normally shows nothing and exits with 0
    fine = folder / "fine.jamo"
    fine.write_text('func main()\n    print("all good")\nend\n', encoding="utf-8")
    run([str(JAMO), "build", "--window", str(fine)])
    finished = run([str(fine.with_suffix(".exe"))])
    # With no console it has nowhere to print, but when its output is sent somewhere, like here, print still works
    report("a window program that finishes normally exits with 0, and print still works when its output is sent somewhere", finished.returncode == 0 and clean(finished.stdout) == "all good\n", f"{finished.returncode} {finished.stdout!r}")


# --- The download: release/make_release.py makes a zip with jamo.exe and scripts to install it ---
def test_release(work):
    folder = work / "release"
    made = run([sys.executable, str(ROOT / "release" / "make_release.py"), str(folder)])
    zips = list(folder.glob("jamo-*-windows.zip"))
    report("make_release.py makes the download", made.returncode == 0 and len(zips) == 1, made.stdout + made.stderr)
    if not zips:
        return
    with zipfile.ZipFile(zips[0]) as archive:
        names = archive.namelist()
        archive.extractall(folder)
    top = zips[0].stem
    wanted = {f"{top}/{name}" for name in ("jamo.exe", "install.cmd", "uninstall.cmd", "README.txt", "GUIDE.md", "LANGUAGE.md")}
    wanted |= {f"{top}/examples/{path.name}" for path in (ROOT / "examples").glob("*.jamo")}
    unpacked = folder / top
    report("the download holds jamo.exe, the install scripts, a README, the guide, the language rules and every example, and nothing else",
           set(names) == wanted and (unpacked / "jamo.exe").read_bytes() == JAMO.read_bytes(), f"{sorted(set(names) ^ wanted)}")
    report(f"the download is small: {zips[0].stat().st_size // 1024} KB", zips[0].stat().st_size < 1024 * 1024)
    scripts_crlf = all(b"\r\n" in (unpacked / name).read_bytes() and b"\n" not in (unpacked / name).read_bytes().replace(b"\r\n", b"") for name in ("install.cmd", "uninstall.cmd", "README.txt"))
    report("the scripts and README in the download have Windows line endings", scripts_crlf)
    # Installs into a folder with a space and an apostrophe, without touching this user's PATH
    target = folder / "Sam's programs" / "Jamo"
    installed = run([str(unpacked / "install.cmd"), str(target), "--no-path"])
    version = run([str(target / "jamo.exe"), "version"]) if (target / "jamo.exe").exists() else installed
    report("install.cmd copies jamo.exe into the folder it's given, even with a space and an apostrophe in its name",
           installed.returncode == 0 and version.returncode == 0 and version.stdout.startswith("Jamo "), installed.stdout + installed.stderr)
    game = folder / "first game"
    started = run([str(target / "jamo.exe"), "new", "--game", str(game)])
    built = run([str(target / "jamo.exe"), "build", "--window", str(game / "main.jamo")])
    report("the installed jamo starts a game and builds it as a window program", started.returncode == 0 and built.returncode == 0 and exe_subsystem(game / "main.exe") == 2, started.stderr + built.stderr)
    removed = run([str(unpacked / "uninstall.cmd"), str(target), "--no-path"])
    report("uninstall.cmd removes jamo.exe and its folder, and leaves the user's programs alone",
           removed.returncode == 0 and not target.exists() and (game / "main.jamo").exists(), removed.stdout + removed.stderr)
    missing = folder / "lonely"
    missing.mkdir()
    shutil.copy(unpacked / "install.cmd", missing / "install.cmd")
    alone = run([str(missing / "install.cmd"), str(folder / "nowhere"), "--no-path"])
    report("install.cmd says clearly when jamo.exe isn't next to it", alone.returncode == 1 and "can't find jamo.exe" in alone.stdout, alone.stdout)


# --- More of the standard library: list, files, json, term, log and test, and lerp, sleep_ms and time_ms ---
LIBRARY_SMALL = 'use "math.jamo"\nuse "time.jamo"\nuse "list.jamo"\n\nfunc main()\n    print(lerp(0.0, 10.0, 0.25))\n    print(lerp(4.0, 2.0, 1.5))\n    var start = time_ms()\n    sleep_ms(30)\n    print(time_ms() - start >= 25)\n    var n = [5, 7, 9]\n    print(contains_int(n, 7))\n    print(index_of_int(n, 9))\n    print(index_of_int(n, 4))\n    insert_int(n, 0, 1)\n    insert_int(n, 4, 11)\n    insert_int(n, 2, 6)\n    print(n)\n    print(remove_int(n, 0))\n    print(remove_int(n, len(n) - 1))\n    print(n)\n    var s: [string] = []\n    insert_string(s, 0, "b")\n    insert_string(s, 0, "a")\n    print(s)\n    print(contains_string(s, "c"))\n    print(remove_string(s, 1))\n    print(s)\n    remove_int(n, 9)\nend\n'
LIBRARY_FILES = 'use "files.jamo"\n\nfunc main()\n    print(path_join("saves", "slot1.txt"))\n    print(path_join("saves\\\\", "slot1.txt"))\n    print(path_join("", "a.txt"))\n    print(path_name("games\\\\art\\\\ship.png"))\n    print(path_name("ship.png"))\n    print(path_folder("games\\\\art\\\\ship.png"))\n    print(path_folder("ship.png"))\n    print(path_ending("games\\\\ship.PNG"))\n    print(path_ending("games.v2\\\\README"))\n    print(path_ending(".hidden"))\n    print(make_folder("box\\\\inner\\\\deep"))\n    print(is_folder("box\\\\inner"))\n    write_file("box\\\\a.txt", "hello")\n    write_file("box\\\\b.txt", "")\n    print(file_size("box\\\\a.txt"))\n    print(file_size("box\\\\b.txt"))\n    print(file_size("box\\\\missing.txt"))\n    print(file_size("box"))\n    print(list_files("box"))\n    print(copy_file("box\\\\a.txt", "box\\\\c.txt"))\n    print(read_file("box\\\\c.txt"))\n    print(move_file("box\\\\c.txt", "box\\\\inner\\\\moved.txt"))\n    print(list_files("box\\\\inner"))\n    print(copy_file("box\\\\nope.txt", "box\\\\x.txt"))\n    print(delete_folder("box\\\\inner"))\n    print(delete_file("box\\\\inner\\\\moved.txt"))\n    print(delete_folder("box\\\\inner\\\\deep"))\n    print(delete_folder("box\\\\inner"))\n    print(list_files("box"))\n    print(list_files("not here"))\n    write_file("box\\\\café 😀.txt", "x")\n    write_file("names.txt", join(list_files("box"), "|"))\n    print(len(current_folder()) > 3)\nend\n'
LIBRARY_THREE = 'use "term.jamo"\nuse "log.jamo"\nuse "test.jamo"\nuse "files.jamo"\nuse "json.jamo"\nuse "list.jamo"\nuse "math.jamo"\nuse "dict.jamo"\nuse "builder.jamo"\nuse "bytes.jamo"\n\nfunc main()\n    print(pad_right("ab", 5) + "|")\n    print(pad_left("ab", 5) + "|")\n    print(table([["Name", "Score"], ["Sam", "120"], ["Alexandra", "7"]]))\n    print(progress_bar(0, 10, 10))\n    print(progress_bar(5, 10, 10))\n    print(progress_bar(13, 10, 10))\n    print(progress_bar(1, 0, 4))\n    print(len(colored("hi", "red")))\n    print(len(bold("hi")))\n    var picked = menu("Pick a food", ["Apples", "Pears", "Plums"])\n    print(f"picked {picked}")\n    var log = new_log("game.log")\n    log.info("started")\n    log.debug("hidden")\n    log.set_level("debug")\n    log.debug("shown")\n    log.screen(false)\n    log.error("only in the file")\n    var t = new_tests()\n    t.equal_int("two", 1 + 1, 2)\n    t.equal_text("text", "a" + "b", "ab")\n    t.check("true", true)\n    t.equal_float("close", 0.1 + 0.2, 0.3, 0.000001)\n    t.equal_int("wrong on purpose", 2 + 2, 5)\n    t.finish()\n    print("never shown")\nend\n'
LIBRARY_SMALL_OUT = "2.5\n1.0\ntrue\ntrue\n2\n-1\n[1, 5, 6, 7, 9, 11]\n1\n11\n[5, 6, 7, 9]\n[\"a\", \"b\"]\nfalse\nb\n[\"a\"]\n"
LIBRARY_FILES_OUT = ("saves\\slot1.txt\nsaves\\slot1.txt\na.txt\nship.png\nship.png\ngames\\art\n\n.PNG\n\n\ntrue\ntrue\n5\n0\n-1\n-1\n[\"a.txt\", \"b.txt\", \"inner\"]\ntrue\nhello\ntrue\n"
                     "[\"deep\", \"moved.txt\"]\nfalse\nfalse\ntrue\ntrue\ntrue\n[\"a.txt\", \"b.txt\"]\n[]\ntrue\n")
LIBRARY_THREE_OUT = ("ab   |\n   ab|\nName       Score\n---------  -----\nSam        120\nAlexandra  7\n[----------] 0%\n[#####-----] 50%\n[##########] 100%\n[----] 0%\n11\n10\n"
                     "Pick a food\n  1. Apples\n  2. Pears\n  3. Plums\nPick 1 to 3:\nPick 1 to 3:\nPick 1 to 3:\npicked 1\n[time] INFO  started\n[time] DEBUG shown\n"
                     "FAIL wrong on purpose: got 4, wanted 5\n1 of 5 checks failed.\n")
JSON_SAMPLE = '''use "json.jamo"

func main()
    var save = json_object()
    save.set("name", json_text("Sam"))
    save.set("level", json_int(3))
    var items = json_list()
    items.add(json_text("sword"))
    items.add(json_number(2.5))
    save.set("items", items)
    save.set("empty", json_list())
    print(to_json_pretty(save))
    var back = parse_json(to_json(save))
    print(back.get("level").as_int() + 1)
    print(back.get("items").at(0).text)
    print(back.get("missing").is_null())
    print(parse_json("[1,\\n2,\\n]").error)
end
'''
JSON_SAMPLE_OUT = '{\n  "name": "Sam",\n  "level": 3,\n  "items": [\n    "sword",\n    2.5\n  ],\n  "empty": []\n}\n4\nsword\ntrue\nline 3: I didn\'t expect ] here.\n'
JSON_RUN = '''use "json.jamo"

func main()
    var out = new_builder()
    for text in split(read_file("json_cases.txt"), "\\n")
        var value = parse_json(text)
        if value.error != ""
            out.line("ERROR")
        else
            out.line(to_json(value))
        end
    end
    write_file("json_got.txt", out.text())
end
'''


def without_times(text):
    return re.sub(r"\[\d\d:\d\d:\d\d\]", "[time]", text)


def test_more_libraries(work):
    folder = work / "more libraries"
    folder.mkdir()
    for name, source, want, given, want_code in (("list, math and time", LIBRARY_SMALL, LIBRARY_SMALL_OUT, "", 1), ("files", LIBRARY_FILES, LIBRARY_FILES_OUT, "", 0),
                                                 ("term, log and test", LIBRARY_THREE, LIBRARY_THREE_OUT, "x\n9\n2\n", 1)):
        path = folder / (name.split(",")[0].split(" ")[0] + ".jamo")
        path.write_text(source, encoding="utf-8")
        run([str(JAMO), "build", str(path)])
        ran = subprocess.run([str(path.with_suffix(".exe"))], cwd=folder, input=given, capture_output=True, text=True, encoding="utf-8", timeout=60)
        got = without_times(clean(ran.stdout))
        report(f"{name}: every function gives the answers worked out by hand", got == want and ran.returncode == want_code, f"code {ran.returncode}\n{got}\n{ran.stderr[:300]}")
    log_file = folder / "game.log"
    logged = without_times(log_file.read_bytes().decode("utf-8")) if log_file.exists() else ""
    report("log adds to its file, with the time and level, and only the levels asked for", logged == "[time] INFO  started\r\n[time] DEBUG shown\r\n[time] ERROR only in the file\r\n", repr(logged))
    names = folder / "names.txt"
    report("list_files gives back names in other languages and emoji exactly", names.exists() and names.read_text(encoding="utf-8") == "a.txt|b.txt|caf\u00e9 \U0001F600.txt", names.read_text(encoding="utf-8") if names.exists() else "")
    # json against Python's json module, on hundreds of texts, valid and not
    cases = json_cases(random.Random(5))
    (folder / "json_cases.txt").write_text("\n".join(cases), encoding="utf-8", newline="\n")
    program = folder / "json_run.jamo"
    program.write_text(JSON_RUN, encoding="utf-8")
    run([str(JAMO), "run", str(program)], cwd=folder, timeout=SLOW_RUN_LIMIT)
    got = (folder / "json_got.txt").read_text(encoding="utf-8").split("\n")[:-1] if (folder / "json_got.txt").exists() else []
    wanted = ["ERROR" if c == "[" * 600 + "]" * 600 else json_want(c) for c in cases]
    different = [f"{c[:60]!r}: got {g[:60]!r}, want {w[:60]!r}" for c, g, w in zip(cases, got, wanted) if g != w]
    report(f"json reads and writes {len(cases)} texts exactly like Python, and finds the {wanted.count('ERROR')} that aren't JSON", len(got) == len(cases) and not different, "\n".join(different[:4]))
    sample = folder / "pretty.jamo"
    sample.write_text(JSON_SAMPLE, encoding="utf-8")
    ran = run([str(JAMO), "run", str(sample)])
    report("json writes pretty JSON, reads its own back, and says which line a mistake is on", clean(ran.stdout) == JSON_SAMPLE_OUT, clean(ran.stdout) + ran.stderr[:300])


# --- For the json test: Python's json module, written the way json.jamo writes it ---
def json_number_like_jamo(value):
    if math.isnan(value) or math.isinf(value):
        return "null"
    if -1e15 < value < 1e15 and float(int(value)) == value:
        return str(int(value))
    return float_text(value)


def json_escape_like_jamo(text):
    out = ['"']
    for letter in text:
        code = ord(letter)
        if letter == '"':
            out.append('\\"')
        elif letter == "\\":
            out.append("\\\\")
        elif letter == "\n":
            out.append("\\n")
        elif letter == "\r":
            out.append("\\r")
        elif letter == "\t":
            out.append("\\t")
        elif code < 32:
            out.append("\\u00" + "0123456789abcdef"[code // 16] + "0123456789abcdef"[code % 16])
        else:
            out.append(letter)
    out.append('"')
    return "".join(out)


def json_like_jamo(value):
    if value is None:
        return "null"
    if value is True:
        return "true"
    if value is False:
        return "false"
    if isinstance(value, (int, float)):
        return json_number_like_jamo(float(value))
    if isinstance(value, str):
        return json_escape_like_jamo(value)
    if isinstance(value, list):
        return "[" + ",".join(json_like_jamo(v) for v in value) + "]"
    return "{" + ",".join(json_escape_like_jamo(k) + ":" + json_like_jamo(v) for k, v in value.items()) + "}"


def json_reject_constant(name):
    raise ValueError(name)


def json_too_big_or_small(text):
    """json.jamo refuses numbers that overflow, or that aren't 0 but round to 0, like float() does"""
    for token in re.findall(r"-?(?:0|[1-9]\d*)(?:\.\d+)?(?:[eE][+-]?\d+)?", text):
        value = float(token)
        if math.isinf(value) or (value == 0.0 and re.search(r"[1-9]", re.split(r"[eE]", token)[0])):
            return True
    return False


def json_want(text):
    old_limit = sys.getrecursionlimit()
    sys.setrecursionlimit(20000)
    try:
        value = json.loads(text, parse_constant=json_reject_constant)
        if json_too_big_or_small(text):
            return "ERROR"
        return json_like_jamo(value)
    except (ValueError, RecursionError):
        return "ERROR"
    finally:
        sys.setrecursionlimit(old_limit)


def json_cases(rng):
    words = ["a", "Sam", "caf\u00e9", "\U0001F600", "tab\there", "quote\"", "back\\slash", "", "line\nbreak", "\x01"]

    def random_value(depth):
        kind = rng.randrange(8 if depth < 4 else 5)
        if kind == 0:
            return None
        if kind == 1:
            return rng.random() < 0.5
        if kind == 2:
            return rng.choice([0, 1, -1, 42, 1000000, -999999999, 2 ** 53, 0.5, -2.25, 3.14159, 1e20, 1.5e-7, 123456.789])
        if kind == 3:
            return rng.randrange(-10 ** 9, 10 ** 9)
        if kind == 4:
            return rng.choice(words)
        if kind == 5:
            return [random_value(depth + 1) for _ in range(rng.randrange(0, 4))]
        return {rng.choice(words) + str(i): random_value(depth + 1) for i in range(rng.randrange(0, 4))}

    cases = ['{"a": 1, "b": [true, false, null], "c": "x"}', "[]", "{}", "  [ 1 , 2 ]  ", "0", "-0", "1e3", "1.5E+2", "2.5e-3", "1e400", "1e-400", "-1e-400", "0e5", "123456789012345678",
             '"\\u00e9\\ud83d\\ude00\\n\\t\\"\\\\\\/\\b\\f"', '"\\u0041"', "[1,]", "{,}", '{"a" 1}', '{"a":1,}', "[1 2]", "01", "1.", ".5", "-", "+1", "tru", "nul", '"abc', '"a\tb"', "[",
             "{", '{"a":', '"\\x"', '"\\u12"', "[1] 2", "", "  ", "NaN", "Infinity", '{"a":1,"a":2}', '{"k": {"deep": [1, {"x": []}]}}', "[" * 400 + "]" * 400, "[" * 600 + "]" * 600]
    for _ in range(400):
        cases.append(json.dumps(random_value(0), ensure_ascii=rng.random() < 0.5))
    for _ in range(100):
        text = list(json.dumps(random_value(0)))
        if text:
            text[rng.randrange(len(text))] = rng.choice(list('{}[],:"\\ x01e.-'))
        cases.append("".join(text))
    return [c for c in cases if "\n" not in c]


# --- The other commands ---
# --- jamo watch: builds and runs again on each change, and keeps the last working version on a mistake ---
def test_watch(work):
    folder = work / "watch test"
    folder.mkdir()
    main = folder / "main.jamo"
    part = folder / "part.jamo"
    log = folder / "log.txt"
    part.write_text('func word(): string\n    return "one"\nend\n', encoding="utf-8")
    main.write_text('use "part.jamo"\n\nfunc main()\n    print("v1 " + word())\nend\n', encoding="utf-8")
    with log.open("wb") as out:
        watcher = subprocess.Popen([str(JAMO), "watch", str(main)], stdout=out, stderr=subprocess.STDOUT, cwd=folder)

    def wait_for(words, seconds=20):
        end = time.time() + seconds
        while time.time() < end:
            if words in log.read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n"):
                return True
            time.sleep(0.1)
        return False

    def change(path, text):
        time.sleep(0.6)
        path.write_text(text, encoding="utf-8")

    try:
        report("watch runs the program", wait_for("v1 one"))
        report("watch says when the program finished", wait_for("(It finished with exit code 0. Waiting for a change.)"))
        change(main, 'use "part.jamo"\n\nfunc main()\n    print("v2 " + word())\nend\n')
        report("watch builds and runs again on a change", wait_for("v2 one"))
        change(part, 'func word(): string\n    return "two"\nend\n')
        report("watch notices a change in a file the program uses", wait_for("v2 two"))
        change(main, 'func main()\n    print("looping")\n    var n = 0\n    while true\n        n += 1\n    end\nend\n')
        report("watch starts a program that keeps going", wait_for("looping"))
        change(main, 'func main()\n    print(nope)\nend\n')
        report("watch shows a mistake and keeps the last working version", wait_for("nothing has this name: nope") and wait_for("The last version that worked is still running."))
        running = subprocess.run(["tasklist", "/fi", "imagename eq main.exe"], capture_output=True, text=True).stdout
        report("the last working version really is still running", "main.exe" in running, running)
        change(main, 'func main()\n    print("fixed")\nend\n')
        report("watch stops the old one and runs the fixed one", wait_for("fixed"))
    finally:
        subprocess.run(["taskkill", "/f", "/t", "/pid", str(watcher.pid)], capture_output=True)
        subprocess.run(["taskkill", "/f", "/im", "main.exe"], capture_output=True)


def test_commands(work):
    folder = work / "windows words"
    folder.mkdir()
    (folder / "notes.txt").write_bytes(b"one two\r\n\r\nthree\r\n")
    result = run([str(JAMO), "run", str(ROOT / "examples" / "word_counter.jamo"), "notes.txt"], cwd=folder)
    report("word_counter handles Windows line endings", clean(result.stdout) == "Lines: 3\nWords: 3\nBytes: 18\n", result.stdout + result.stderr)
    (ROOT / "examples" / "word_counter.exe").unlink(missing_ok=True)
    (work / "windows lines.jamo").write_bytes(b'func main()\r\n    print("oops)\r\nend\r\n')
    result = run([str(JAMO), "check", str(work / "windows lines.jamo")])
    report("a marker stops at the line's end in a file with Windows line endings", '\n              ^^^^^^\n' in clean(result.stderr), result.stderr)
    result = run([str(JAMO), "version"])
    report("version", result.returncode == 0 and clean(result.stdout).startswith("Jamo ") and result.stderr == "", result.stdout)
    result = run([str(JAMO), "help"])
    report("help lists every command", result.returncode == 0 and all(f"jamo {word}" in result.stdout for word in ("run", "build", "check", "new", "version", "help")), result.stdout)

    folder = work / "check folder"
    folder.mkdir()
    (folder / "fine.jamo").write_text("func main()\n    print(1)\nend\n", encoding="utf-8")
    (folder / "wrong.jamo").write_text("func main()\n    print(nope)\nend\n", encoding="utf-8")
    result = run([str(JAMO), "check", str(folder / "fine.jamo")])
    report("check passes a good program and writes no .exe", result.returncode == 0 and result.stdout == "" and not (folder / "fine.exe").exists(), f"{result.returncode} {result.stderr}")
    result = run([str(JAMO), "check", str(folder / "wrong.jamo")])
    report("check shows mistakes and writes no .exe", result.returncode == 1 and "nothing has this name: nope" in result.stderr and not (folder / "wrong.exe").exists(), f"{result.returncode} {result.stderr}")
    result = run([str(JAMO), "check", str(folder / "missing.jamo")])
    report("check on a missing file", result.returncode == 1 and "can't read" in result.stderr, result.stderr)

    result = run([str(JAMO), "new", "my game"], cwd=work)
    report("new makes a folder", result.returncode == 0 and (work / "my game" / "main.jamo").exists(), f"{result.returncode} {result.stderr}")
    result = run([str(JAMO), "run", str(work / "my game" / "main.jamo")])
    report("the new program runs", result.returncode == 0 and clean(result.stdout) == "Hello from Jamo!\n", f"{result.returncode} {result.stdout} {result.stderr}")
    before = (work / "my game" / "main.jamo").read_bytes()
    result = run([str(JAMO), "new", "my game"], cwd=work)
    report("new won't touch a folder that exists", result.returncode == 1 and "already exists" in result.stderr and (work / "my game" / "main.jamo").read_bytes() == before, f"{result.returncode} {result.stderr}")


# --- math.jamo, time.jamo and text.jamo. Answers come from Python ---
LIBRARY_MODES = ([], ["--gc-test", "--gc-check"])
BITS_HELPER = """func bits(x: float): int
    var place = memory(8)
    poke_f64(place, 0, x)
    return peek_i64(place, 0)
end
"""


def float_bits(value):
    return struct.unpack("<q", struct.pack("<d", value))[0]


def bits_float(number):
    return struct.unpack("<d", struct.pack("<q", number))[0]


def run_library_program(work, name, source, flags, args=()):
    """Builds and runs one test program. Gives back (exit code, output, errors), with code None if it didn't build or hung."""
    folder = work / "library tests"
    folder.mkdir(exist_ok=True)
    path = folder / f"{name}.jamo"
    path.write_text(source, encoding="utf-8")
    built = run([str(JAMO), "build", *flags, str(path)])
    if built.returncode != 0:
        return None, "", built.stderr
    try:
        ran = run([str(path.with_suffix(".exe")), *args], cwd=folder, timeout=SLOW_RUN_LIMIT)
    except subprocess.TimeoutExpired:
        return None, "", "took too long"
    return ran.returncode, clean(ran.stdout), clean(ran.stderr)


def check_library_misuse(work, library, cases):
    """Each case is (name, lines in main, message). The program must stop with that run error, naming main's line."""
    for name, body, message in cases:
        lines = body.split("\n")
        source = f'use "{library}"\n\nfunc main()\n' + "".join(f"    {line}\n" for line in lines) + "end\n"
        code, out, errors = run_library_program(work, f"misuse {name}", source, [])
        line = 3 + len(lines)
        passed = code == 1 and f"Error: {message}\n" in errors and f"called from main, misuse {name}.jamo line {line}\n" in errors and errors.startswith(f"{library} (built in) line ")
        report(f"{library} misuse stops clearly: {name}", passed, f"code {code}\n{errors}\nwanted: {message}")


def math_cases(rng):
    """Lines of a Jamo program with what each prints: a string for an exact answer, or a float that may be off by a few bits."""
    floats = [0.0, -0.0, 0.5, -0.5, 1.5, 2.5, -2.5, 3.5, -3.5, 0.49999999999999994, -0.49999999999999994, 4503599627370495.5, 4503599627370497.0, -9223372036854775808.0,
              9223372036854774784.0, -9223372036854774784.0, 1e-300, 123.0, -7.25, 0.1, 1e15 + 0.5, 2.0 ** 52 + 1.0, -(2.0 ** 52) - 1.0, 0.9999999999999999, -0.9999999999999999]
    floats += [rng.uniform(-1000, 1000) for _ in range(60)] + [rng.randrange(-4000, 4000) / 4 for _ in range(30)] + [rng.uniform(-1e18, 1e18) for _ in range(20)]
    cases = []
    for value in floats:
        v = float_literal(value)
        cases.append((f"print(bits(abs({v})))", str(float_bits(abs(value)))))
        cases.append((f"print(floor({v}))", str(math.floor(value))))
        cases.append((f"print(ceil({v}))", str(math.ceil(value))))
        cases.append((f"print(round({v}))", str(int(Decimal(value).quantize(Decimal(1), rounding="ROUND_HALF_UP")))))
        if value >= 0:
            cases.append((f"print(bits(sqrt({v})))", str(float_bits(math.sqrt(value)))))
        if abs(value) < 1e6:
            cases.append((f"print(bits(sin({v})))", math.sin(value)))
            cases.append((f"print(bits(cos({v})))", math.cos(value)))
            cases.append((f"print(bits(tan({v})))", math.tan(value)))
    for _ in range(80):
        a, b, c = (rng.choice(floats) for _ in range(3))
        low, high = min(b, c), max(b, c)
        x, y, lo, hi = float_literal(a), float_literal(b), float_literal(low), float_literal(high)
        cases.append((f"print(bits(min({x}, {y})))", str(float_bits(b if b < a else a))))
        cases.append((f"print(bits(max({x}, {y})))", str(float_bits(b if b > a else a))))
        cases.append((f"print(bits(clamp({x}, {lo}, {hi})))", str(float_bits(low if a < low else high if a > high else a))))
        cases.append((f"print(bits(atan2({x}, {y})))", math.atan2(a, b)))
        base, power = abs(a) % 50 + 0.5, rng.uniform(-8, 8)
        cases.append((f"print(bits(pow({float_literal(base)}, {float_literal(power)})))", base ** power))
    ints = [0, 1, -1, 5, -5, 2 ** 63 - 1, -(2 ** 63), -(2 ** 63) + 1] + [rng.randrange(-(2 ** 63), 2 ** 63) for _ in range(40)] + [rng.randrange(-100, 100) for _ in range(40)]
    for value in ints:
        cases.append((f"print(abs_int({value}))", str(wrap(abs(value)))))
    for _ in range(80):
        a, b, c = (rng.choice(ints) for _ in range(3))
        low, high = min(b, c), max(b, c)
        cases.append((f"print(min_int({a}, {b}))", str(min(a, b))))
        cases.append((f"print(max_int({a}, {b}))", str(max(a, b))))
        cases.append((f"print(clamp_int({a}, {low}, {high}))", str(min(max(a, low), high))))
    cases.append(("print(bits(pi()))", str(float_bits(math.pi))))
    cases.append(("print(bits(sqrt(2.0)))", str(float_bits(math.sqrt(2.0)))))
    return cases


def close_enough(got, want):
    return got == want or abs(got - want) <= 4 * math.ulp(want)


def test_math(work):
    cases = math_cases(random.Random(SEED))
    # g3d and gfx declare sqrt, sin, cos and tan too, which is fine since they match
    source = 'use "math.jamo"\nuse "g3d.jamo"\n\n' + BITS_HELPER + "\nfunc main()\n" + "".join(f"    {line}\n" for line, _ in cases) + "end\n"
    for flags in LIBRARY_MODES:
        code, out, errors = run_library_program(work, "math", source, flags)
        got = out.split("\n")
        wrong = []
        for i, (line, want) in enumerate(cases):
            have = got[i] if i < len(got) else "(nothing)"
            if isinstance(want, str):
                ok = have == want
            else:
                ok = re.fullmatch(r"-?\d+", have) is not None and close_enough(bits_float(int(have)), want)
            if not ok:
                wrong.append(f"{line}: got {have}, wanted {want}")
        report(f"math gives Python's answers for {len(cases)} sums {' '.join(flags) or 'normal'}", code == 0 and not wrong, f"code {code} {errors[:300]}\n" + "\n".join(wrong[:8]))
    check_library_misuse(work, "math.jamo", [
        ("floor too big", "print(floor(100000000000000000000.0))", "floor needs a float that fits in an int, but this is 1.0e+20."),
        ("ceil too small", "print(ceil(-10000000000000000000.0))", "ceil needs a float that fits in an int, but this is -1.0e+19."),
        ("round nan", "var zero = 0.0\nprint(round(zero / zero))", "round needs a float that fits in an int, but this is nan."),
        ("round 2 to the 63", "print(round(9223372036854775808.0))", "round needs a float that fits in an int, but this is 9.223372e+18."),
        ("clamp backward", "print(clamp(1.0, 2.0, 1.5))", "clamp needs low <= high, but they're 2.0 and 1.5."),
        ("clamp_int backward", "print(clamp_int(1, 5, 4))", "clamp_int needs low <= high, but they're 5 and 4."),
    ])


class PyRandom:
    """math.jamo's generator, worked out again in Python: a 64-bit LCG, using the top 32 bits of each step."""
    def __init__(self, seed):
        self.state = seed % 2 ** 64

    def bits(self):
        self.state = (self.state * 6364136223846793005 + 1442695040888963407) % 2 ** 64
        return self.state >> 32

    def int(self, low, high):
        count = high - low + 1
        if count < 2 ** 63:
            extra = 2 ** 63 % count
            while True:
                drawn = self.bits() * 2 ** 31
                drawn += self.bits() // 2
                if drawn < 2 ** 63 - extra:
                    return low + drawn % count
        while True:
            drawn = self.bits() * 2 ** 32
            drawn = wrap(drawn + self.bits())
            if low <= drawn <= high:
                return drawn

    def float(self):
        drawn = self.bits() * 2 ** 21
        drawn += self.bits() // 2 ** 11
        return drawn / 2 ** 53


RANDOM_RANGES = [(1, 6), (0, 1), (-3, 3), (5, 5), (0, 9), (-1000000, 1000000), (0, 2 ** 40), (0, 2 ** 62 + 12345), (-(2 ** 62), 2 ** 62), (-(2 ** 63), 2 ** 63 - 1),
                 (-(2 ** 63), 0), (-1, 2 ** 63 - 1), (2 ** 63 - 2, 2 ** 63 - 1), (-(2 ** 63), -(2 ** 63) + 2)]
RANDOM_SPREAD_DRAWS = 600000
TIME_SLEEP = 0.25


def test_random_and_time(work):
    # A seed repeats exactly, and every draw matches Python's copy of the generator
    rng = random.Random(SEED)
    seeds = [0, 1, 42, -1, 2 ** 63 - 1, -(2 ** 63)] + [rng.randrange(-(2 ** 63), 2 ** 63) for _ in range(4)]
    lines, want = [], []
    for seed in seeds:
        generator = PyRandom(seed)
        lines.append(f"random_seed({seed})")
        for _ in range(3):
            for low, high in RANDOM_RANGES:
                lines.append(f"print(random_int({low}, {high}))")
                want.append(str(generator.int(low, high)))
            lines.append("print(bits(random_float()))")
            want.append(str(float_bits(generator.float())))
    lines.append("random_seed(7)\nvar first = random_int(1, 1000000)\nrandom_seed(7)\nprint(random_int(1, 1000000) == first)")
    want.append("true")
    source = 'use "math.jamo"\n\n' + BITS_HELPER + "\nfunc main()\n" + "".join(f"    {line}\n" for line in "\n".join(lines).split("\n")) + "end\n"
    for flags in LIBRARY_MODES:
        code, out, errors = run_library_program(work, "random seeds", source, flags)
        got = out.split("\n")[:-1]
        wrong = [f"draw {i}: got {a}, wanted {b}" for i, (a, b) in enumerate(zip(got, want)) if a != b]
        report(f"random numbers match Python's copy for {len(seeds)} seeds {' '.join(flags) or 'normal'}", code == 0 and len(got) == len(want) and not wrong, f"code {code} {errors[:300]} {len(got)} lines, wanted {len(want)}\n" + "\n".join(wrong[:6]))

    # Spread: each side of a die and each tenth of 0.0 to 1.0 comes up about as often, from the clock's seed
    spread = f'''use "math.jamo"

func main()
    var sides = [0, 0, 0, 0, 0, 0]
    var tenths = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
    var outside = 0
    var low = 1.0
    var high = 0.0
    var total = 0.0
    for i in 0..{RANDOM_SPREAD_DRAWS}
        var side = random_int(1, 6)
        if side < 1 or side > 6
            outside += 1
        else
            sides[side - 1] += 1
        end
        var x = random_float()
        if x < 0.0 or x >= 1.0
            outside += 1
        else
            tenths[int(x * 10.0)] += 1
        end
        total += x
        low = min(low, x)
        high = max(high, x)
        var small = random_int(-2, 2)
        if small < -2 or small > 2
            outside += 1
        end
    end
    print(outside)
    print(sides)
    print(tenths)
    print(bits(low))
    print(bits(high))
    print(bits(total))
    print(random_int(1, 1000000000))
end

''' + BITS_HELPER
    firsts = []
    for flags in LIBRARY_MODES:
        code, out, errors = run_library_program(work, "random spread", spread, flags)
        lines = out.split("\n")
        problems = []
        if code != 0 or len(lines) < 7:
            problems.append(f"code {code} {errors[:300]} {out[:300]}")
        else:
            if lines[0] != "0":
                problems.append(f"{lines[0]} draws were outside their range")
            sides = json.loads(lines[1])
            tenths = json.loads(lines[2])
            # Chi-squared: 5 and 9 degrees of freedom, beaten by chance about once in 10,000 runs
            for counts, limit in ((sides, 25.7), (tenths, 33.7)):
                expect = RANDOM_SPREAD_DRAWS / len(counts)
                score = sum((count - expect) ** 2 / expect for count in counts)
                if sum(counts) != RANDOM_SPREAD_DRAWS or score > limit:
                    problems.append(f"uneven spread {counts}, score {score:.1f}")
            low, high, total = (bits_float(int(lines[i])) for i in (3, 4, 5))
            if not (0.0 <= low < 0.0001 and 0.9999 < high < 1.0 and abs(total / RANDOM_SPREAD_DRAWS - 0.5) < 0.003):
                problems.append(f"floats from {low} to {high}, average {total / RANDOM_SPREAD_DRAWS}")
            firsts.append(lines[6])
        report(f"random numbers spread evenly over {RANDOM_SPREAD_DRAWS} draws {' '.join(flags) or 'normal'}", not problems, "\n".join(problems))
    # Without a seed, each run starts somewhere new
    starts = set()
    for _ in range(3):
        code, out, errors = run_library_program(work, "random clock", 'use "math.jamo"\n\nfunc main()\n    print(random_int(1, 1000000000))\n    print(bits(random_float()))\nend\n\n' + BITS_HELPER, [])
        starts.add(out)
    report("random numbers seed from the clock, so runs differ", len(starts) == 3 and len(set(firsts)) == len(firsts), f"{starts} {firsts}")
    check_library_misuse(work, "math.jamo", [
        ("random_int backward", "print(random_int(3, 2))", "random_int needs low <= high, but they're 3 and 2."),
        ("random_int far backward", "print(random_int(9223372036854775807, -9223372036854775808))", "random_int needs low <= high, but they're 9223372036854775807 and -9223372036854775808."),
    ])

    # Time: seconds since the start count from when the process began, only go up, and follow sleep
    clock = f'''use "time.jamo"

func main()
    print(seconds_since_start())
    var before = seconds_since_start()
    sleep({TIME_SLEEP})
    var after = seconds_since_start()
    print(after - before)
    sleep(0.0)
    var backward = 0
    var last = seconds_since_start()
    for i in 0..200000
        var each = seconds_since_start()
        if each < last
            backward += 1
        end
        last = each
    end
    print(backward)
    var moment = now()
    print(moment)
    print(moment.date_text())
    print(moment.time_text())
    print(moment.weekday_name())
    var start = seconds_since_start()
    sleep(0.0004)
    print(seconds_since_start() - start)
end
'''
    for flags in LIBRARY_MODES:
        started = time.time()
        before_now = datetime.datetime.now()
        code, out, errors = run_library_program(work, "clock", clock, flags)
        after_now = datetime.datetime.now()
        took = time.time() - started
        lines = out.split("\n")
        problems = []
        if code != 0 or len(lines) < 8:
            problems.append(f"code {code} {errors[:300]} {out[:300]}")
        else:
            first, slept, backward = float(lines[0]), float(lines[1]), lines[2]
            if not 0.0 <= first <= took:
                problems.append(f"started at {first} seconds, but the whole run took {took:.3f}")
            if not TIME_SLEEP - 0.002 <= slept < TIME_SLEEP + 0.5:
                problems.append(f"sleep({TIME_SLEEP}) took {slept} seconds")
            if backward != "0":
                problems.append(f"seconds went backward {backward} times")
            found = re.fullmatch(r"DateTime\{year: (\d+), month: (\d+), day: (\d+), weekday: (\d+), hour: (\d+), minute: (\d+), second: (\d+), millisecond: (\d+)\}", lines[3])
            if not found:
                problems.append(f"now() showed {lines[3]}")
            else:
                year, month, day, weekday, hour, minute, second, millisecond = (int(part) for part in found.groups())
                moment = datetime.datetime(year, month, day, hour, minute, second, millisecond * 1000)
                if not before_now - datetime.timedelta(seconds=1) <= moment <= after_now + datetime.timedelta(seconds=1):
                    problems.append(f"now() is {moment}, but Python saw {before_now} to {after_now}")
                if weekday != (moment.weekday() + 1) % 7:
                    problems.append(f"weekday {weekday} on {moment}")
                names = ["Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"]
                if lines[4:7] != [moment.strftime("%Y-%m-%d"), moment.strftime("%H:%M:%S"), names[weekday]]:
                    problems.append(f"texts {lines[4:7]} for {moment}")
            if not 0.0 <= float(lines[7]) < 0.1:
                problems.append(f"sleep(0.0004) took {lines[7]} seconds")
        report(f"time counts seconds, sleeps and knows the date {' '.join(flags) or 'normal'}", not problems, "\n".join(problems))
    # The count starts when the process does, not at the first call. Windows' own Sleep waits first
    late = 'extern "kernel32.dll" func Sleep(milliseconds: i32)\nuse "time.jamo"\n\nfunc main()\n    Sleep(300)\n    print(seconds_since_start())\nend\n'
    started = time.time()
    code, out, errors = run_library_program(work, "late clock", late, [])
    took = time.time() - started
    report("seconds_since_start counts from when the program started", code == 0 and 0.3 <= float(out or "0") <= took, f"code {code} {errors[:300]} printed {out!r}, run took {took:.3f}")
    check_library_misuse(work, "time.jamo", [
        ("sleep backward", "sleep(-0.5)", "sleep needs from 0.0 to 1000000000.0 seconds, not -0.5."),
        ("sleep nan", "var zero = 0.0\nsleep(zero / zero)", "sleep needs from 0.0 to 1000000000.0 seconds, not nan."),
        ("sleep forever", "sleep(2000000000.0)", "sleep needs from 0.0 to 1000000000.0 seconds, not 2000000000.0."),
    ])


CODES_HELPER = """func codes(t: string): string
    var out = ""
    for i in 0..len(t)
        out = out + string(byte(t, i)) + " "
    end
    return out
end
"""
TEXT_LETTERS = ["a", "b", "c", "z", "A", "Z", "@", "[", "`", "{", ",", " ", " ", "\t", "\n", "\r", "é", "\\", "\"", "ab", "ba"]
TEXT_CASES = 300
TEXT_BIG = 200000
TEXT_BIG_CLEANER = 3000
TEXT_BIG_SECONDS = 10


def jamo_text(text):
    """A Jamo string literal for any text."""
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n").replace("\t", "\\t").replace("\r", "\\r") + '"'


def codes(text):
    return "".join(f"{b} " for b in text.encode("utf-8"))


def random_text(rng, most=12):
    return "".join(rng.choice(TEXT_LETTERS) for _ in range(rng.randrange(most + 1)))


def text_cases(rng):
    """Lines of a Jamo program with the lines each prints, worked out in Python."""
    cases = []
    for _ in range(TEXT_CASES):
        t = random_text(rng)
        b = t.encode("utf-8")
        if b and rng.random() < 0.6:
            start = rng.randrange(len(b))
            part_bytes = b[start:start + rng.randrange(1, 4)]
            try:
                part = part_bytes.decode("utf-8")
            except UnicodeDecodeError:
                part = random_text(rng, 3)
        else:
            part = random_text(rng, 3)
        p = part.encode("utf-8")
        T, P = jamo_text(t), jamo_text(part)
        cases.append((f"print(find({T}, {P}))", [str(b.find(p))]))
        cases.append((f"print(contains({T}, {P}))", ["true" if p in b else "false"]))
        cases.append((f"print(starts_with({T}, {P}))", ["true" if b.startswith(p) else "false"]))
        cases.append((f"print(ends_with({T}, {P}))", ["true" if b.endswith(p) else "false"]))
        start = rng.randrange(len(b) + 1)
        stop = rng.randrange(start, len(b) + 1)
        cases.append((f"print(codes(slice({T}, {start}, {stop})))", [codes_bytes(b[start:stop])]))
        by = part if p else ","
        pieces = b.split(by.encode("utf-8"))
        cases.append((f"show_all(split({T}, {jamo_text(by)}))", [str(len(pieces))] + [codes_bytes(piece) for piece in pieces]))
        new = random_text(rng, 3)
        cases.append((f"print(codes(replace({T}, {jamo_text(by)}, {jamo_text(new)})))", [codes_bytes(b.replace(by.encode("utf-8"), new.encode("utf-8")))]))
        cases.append((f"print(codes(trim({T})))", [codes_bytes(b.strip(b" \t\r\n"))]))
        cases.append((f"print(codes(upper({T})))", [codes_bytes(twin_change_case(b, True))]))
        cases.append((f"print(codes(lower({T})))", [codes_bytes(twin_change_case(b, False))]))
        times = rng.randrange(5)
        cases.append((f"print(codes(repeat({T}, {times})))", [codes_bytes(b * times)]))
        parts = [random_text(rng, 4) for _ in range(rng.randrange(5))]
        between = random_text(rng, 2)
        joined = between.encode("utf-8").join(part.encode("utf-8") for part in parts)
        array = "[" + ", ".join(jamo_text(part) for part in parts) + "]"
        cases.append((f"var parts{len(cases)}: [string] = {array}\n    print(codes(join(parts{len(cases)}, {jamo_text(between)})))", [codes_bytes(joined)]))
    for size in list(range(12)) + [33, 64, 100, 257]:
        numbers = [rng.choice([rng.randrange(-5, 5), rng.randrange(-(2 ** 63), 2 ** 63), 2 ** 63 - 1, -(2 ** 63)]) for _ in range(size)]
        words = [random_text(rng, 4) for _ in range(size)]
        n, w = len(cases), len(cases) + 1
        cases.append((f"var n{n}: [int] = [{', '.join(map(str, numbers))}]\n    sort_ints(n{n})\n    print(n{n})\n    reverse_ints(n{n})\n    print(n{n})",
                       ["[" + ", ".join(map(str, sorted(numbers))) + "]", "[" + ", ".join(map(str, sorted(numbers, reverse=True))) + "]"]))
        ordered = sorted(words, key=lambda word: word.encode("utf-8"))
        cases.append((f"var w{w}: [string] = [{', '.join(jamo_text(word) for word in words)}]\n    sort_strings(w{w})\n    show_all(w{w})\n    reverse_strings(w{w})\n    show_all(w{w})",
                       [str(size)] + [codes(word) for word in ordered] + [str(size)] + [codes(word) for word in reversed(ordered)]))
    # Ones picked by hand
    for line, want in [('show_all(split("a,b,,c", ","))', ["4", "97 ", "98 ", "", "99 "]), ('show_all(split("", ","))', ["1", ""]), ('show_all(split(",", ","))', ["2", "", ""]),
                       ('print(find("aaab", "aab"))', ["1"]), ('print(find("abc", ""))', ["0"]), ('print(find("", "a"))', ["-1"]), ('print(codes(replace("aaaa", "aa", "b")))', ["98 98 "]),
                       ('print(codes(trim("  \\t\\r\\n  ")))', [""]), ('print(codes(slice("hello", 5, 5)))', [""]), ('print(starts_with("ab", "abc"))', ["false"]), ('print(ends_with("", ""))', ["true"])]:
        cases.append((line, want))
    return cases


def codes_bytes(data):
    return "".join(f"{b} " for b in data)


# Python's twin of text.jamo's upper and lower, from the rules in LANGUAGE terms: a to z, and the two-byte letters
# of Latin-1, Latin Extended-A, Greek and Cyrillic that have a two-byte partner. Anything else, and broken UTF-8, stays
def twin_case_letter(letter, to_upper):
    if to_upper:
        if 224 <= letter <= 254 and letter != 247:
            return letter - 32
        if (256 <= letter <= 311 and letter != 305 and letter % 2 == 1) or (313 <= letter <= 328 and letter % 2 == 0) or (330 <= letter <= 375 and letter % 2 == 1) or (378 <= letter <= 382 and letter % 2 == 0):
            return letter - 1
        if letter == 962:
            return 931
        if 945 <= letter <= 969 or 1072 <= letter <= 1103:
            return letter - 32
        if 1104 <= letter <= 1119:
            return letter - 80
        return letter
    if 192 <= letter <= 222 and letter != 215:
        return letter + 32
    if (256 <= letter <= 311 and letter != 304 and letter % 2 == 0) or (313 <= letter <= 328 and letter % 2 == 1) or (330 <= letter <= 375 and letter % 2 == 0) or (377 <= letter <= 381 and letter % 2 == 1):
        return letter + 1
    if (913 <= letter <= 937 and letter != 930) or 1040 <= letter <= 1071:
        return letter + 32
    if 1024 <= letter <= 1039:
        return letter + 80
    return letter


def twin_change_case(data, to_upper):
    out = bytearray()
    i = 0
    while i < len(data):
        b = data[i]
        if b < 128:
            out.append(b - 32 if to_upper and 97 <= b <= 122 else b + 32 if not to_upper and 65 <= b <= 90 else b)
            i += 1
        elif 192 <= b < 224 and i + 1 < len(data) and 128 <= data[i + 1] < 192:
            letter = twin_case_letter((b - 192) * 64 + data[i + 1] - 128, to_upper)
            out += bytes([192 + letter // 64, 128 + letter % 64])
            i += 2
        else:
            out.append(b)
            i += 1
    return bytes(out)


def big_text_case(size):
    big = f'''use "text.jamo"

func main()
    var t = repeat("ab,", {size})
    var parts = split(t, ",")
    print(len(parts))
    var joined = join(parts, ";")
    print(len(joined))
    var changed = replace(joined, "a", "xyz")
    print(len(changed))
    print(find(changed, "b;xyzb;Q"))
    print(find(changed, "xyzb;xyzb;xyzb;xyzb"))
    print(len(upper(changed)))
    print(slice(changed, 4, 14))
    var numbers: [int] = []
    var x = 12345
    for i in 0..{size}
        x = x * 6364136223846793005 + 1442695040888963407
        numbers.add(x % 1000000)
    end
    sort_ints(numbers)
    var check = 0
    for i in 0..len(numbers)
        check = check * 31 + numbers[i]
    end
    print(check)
    var words: [string] = []
    for i in 0..{size // 4}
        words.add(string(numbers[(i * 7919) % len(numbers)]))
    end
    sort_strings(words)
    print(words[0])
    print(words[len(words) - 1])
    print(words[len(words) / 2])
end
'''
    x, numbers = 12345, []
    for _ in range(size):
        x = wrap(x * 6364136223846793005 + 1442695040888963407)
        numbers.append(x - trunc_div(x, 1000000) * 1000000)
    numbers.sort()
    check = 0
    for number in numbers:
        check = wrap(check * 31 + number)
    words = sorted((str(numbers[(i * 7919) % len(numbers)]) for i in range(size // 4)), key=lambda word: word.encode())
    joined = ";".join(["ab"] * size + [""])
    changed = joined.replace("a", "xyz")
    big_want = f"{size + 1}\n{len(joined)}\n{len(changed)}\n-1\n{changed.find('xyzb;xyzb;xyzb;xyzb')}\n{len(changed)}\n{changed[4:14]}\n{check}\n{words[0]}\n{words[-1]}\n{words[len(words) // 2]}\n"
    return big, big_want


def test_text(work):
    cases = text_cases(random.Random(SEED))
    helpers = CODES_HELPER + """
func show_all(parts: [string])
    print(len(parts))
    for part in parts
        print(codes(part))
    end
end
"""
    source = 'use "text.jamo"\n\n' + helpers + "\nfunc main()\n" + "".join(f"    {line}\n" for line, _ in cases) + "end\n"
    want = [line for _, lines in cases for line in lines]
    for flags in LIBRARY_MODES:
        code, out, errors = run_library_program(work, "text", source, flags)
        got = out.split("\n")[:-1]
        wrong = [f"line {i}: got {a!r}, wanted {b!r}" for i, (a, b) in enumerate(zip(got, want)) if a != b]
        report(f"text tools give Python's answers for {len(cases)} cases {' '.join(flags) or 'normal'}", code == 0 and len(got) == len(want) and not wrong, f"code {code} {errors[:300]} {len(got)} lines, wanted {len(want)}\n" + "\n".join(wrong[:6]))

    # Big text and arrays stay quick, since results are built in memory in one go. The cleaner test modes clean at every step, so they get less
    for flags, size in zip(LIBRARY_MODES, (TEXT_BIG, TEXT_BIG_CLEANER)):
        big, big_want = big_text_case(size)
        started = time.time()
        code, out, errors = run_library_program(work, "big text", big, flags)
        took = time.time() - started
        fast = took < TEXT_BIG_SECONDS or flags
        report(f"text tools handle {size} pieces quickly {' '.join(flags) or 'normal'}", code == 0 and out == big_want and fast, f"code {code} took {took:.1f} s {errors[:300]}\ngot:\n{out[:400]}\nwanted:\n{big_want}")

    check_library_misuse(work, "text.jamo", [
        ("slice past the end", 'print(slice("hello", 2, 9))', "slice needs 0 <= from <= to <= 5, the text's length, but from is 2 and to is 9."),
        ("slice one past the end", 'print(slice("hello", 0, 6))', "slice needs 0 <= from <= to <= 5, the text's length, but from is 0 and to is 6."),
        ("slice before the start", 'print(slice("hello", -1, 2))', "slice needs 0 <= from <= to <= 5, the text's length, but from is -1 and to is 2."),
        ("slice backward", 'print(slice("hello", 3, 2))', "slice needs 0 <= from <= to <= 5, the text's length, but from is 3 and to is 2."),
        ("split by nothing", 'print(split("a,b", ""))', "split needs something to split by, but it was given empty text."),
        ("replace nothing", 'print(replace("abc", "", "x"))', "replace needs some text to look for, but it was given empty text."),
        ("repeat backward", 'print(repeat("ab", -1))', "repeat needs 0 or more times, not -1."),
        ("repeat too long", 'print(repeat("ab", 4611686018427387903))', "repeat would make text too long: 2 bytes 4611686018427387903 times."),
    ])

    # Every built-in library works in one program, sharing the Windows functions they declare
    every = "".join(f'use "{path.name}"\n' for path in sorted((ROOT / "libraries").glob("*.jamo")))
    code, out, errors = run_library_program(work, "every library", every + '\nfunc main()\n    print(join(split("a-b", "-"), "+"))\n    print(round(2.5))\n    print(seconds_since_start() >= 0.0)\nend\n', [])
    report("every built-in library can be used in one program", code == 0 and out == "a+b\n3\ntrue\n", f"code {code} {errors[:500]} {out}")


# --- Special behavior ---
def test_special_cases(work):
    # Running out of memory stops with a clear error
    (work / "memory.jamo").write_text('func main()\n    var text = "x"\n    while true\n        text = text + text\n    end\nend\n', encoding="utf-8")
    run([str(JAMO), "build", str(work / "memory.jamo")])
    code, _, err = run_with_memory_limit(work / "memory.exe", MEMORY_LIMIT_MB)
    report("running out of memory gives a clear error and exit code 1", code == 1 and clean(err) == "memory.jamo line 4: Error: the program ran out of memory\n", f"code {code}, {err}")

    # A float too large for an int stops with a clear error
    (work / "too_large.jamo").write_text("func main()\n    var zero = 0.0\n\n    print(int(1.0 / zero))\nend\n", encoding="utf-8")
    result = run([str(JAMO), "run", str(work / "too_large.jamo")])
    report("a float too large for an int gives a clear error", result.returncode == 1 and clean(result.stderr) == "too_large.jamo line 4: Error: this float doesn't fit in an int\n", f"{result.returncode} {result.stderr}")

    # jamo run passes back the exit code, and output arrives
    (work / "ok.jamo").write_text("func main()\n    print(1)\nend\n", encoding="utf-8")
    result = run([str(JAMO), "run", str(work / "ok.jamo")])
    report("jamo run works", result.returncode == 0 and clean(result.stdout) == "1\n", result.stdout + result.stderr)
    (work / "zero.jamo").write_text("func main()\n    var d = 0\n    print(1 / d)\nend\n", encoding="utf-8")
    result = run([str(JAMO), "run", str(work / "zero.jamo")])
    report("jamo run passes back exit code 1", result.returncode == 1 and clean(result.stderr) == "zero.jamo line 3: Error: division by zero\n", f"{result.returncode} {result.stderr}")

    # Bad commands
    for command in ([], ["build"], ["nope", "x.jamo"], ["build", "missing.jamo"], ["build", str(work / "ok.txt")]):
        result = run([str(JAMO), *command], cwd=work)
        report(f"bad command gives exit 1: {command[:1]}", result.returncode == 1 and result.stderr != "", result.stderr)

    # Runs with no tools on PATH, from a folder with a space
    spaced = work / "folder with space"
    spaced.mkdir()
    shutil.copy(JAMO, spaced / "jamo.exe")
    (spaced / "my program.jamo").write_text('func main()\n    print(f"{7} {1.5}")\nend\n', encoding="utf-8")
    empty_path = {"PATH": "", "SystemRoot": os.environ["SystemRoot"]}
    result = run([str(spaced / "jamo.exe"), "run", "my program.jamo"], cwd=spaced, env=empty_path)
    report("jamo.exe alone runs with an empty PATH, in a folder with a space", result.returncode == 0 and clean(result.stdout) == "7 1.5\n", result.stdout + result.stderr)
    result = run([str(spaced / "my program.exe")], env=empty_path)
    report("the built program runs with an empty PATH", result.returncode == 0 and clean(result.stdout) == "7 1.5\n", result.stdout)

    # An .exe that's still running can't be replaced
    (work / "forever.jamo").write_text("func main()\n    while true\n    end\nend\n", encoding="utf-8")
    run([str(JAMO), "build", str(work / "forever.jamo")])
    running = subprocess.Popen([str(work / "forever.exe")])
    try:
        time.sleep(STILL_RUNNING_WAIT)
        result = run([str(JAMO), "build", str(work / "forever.jamo")])
        report("a clear error when the .exe is still running", result.returncode == 1 and "still running" in result.stderr, f"{result.returncode} {result.stderr}")
    finally:
        running.kill()
        running.wait()
    report("no partial file left behind", not (work / "forever.exe.part").exists())

    # Deep recursion
    (work / "bottomless.jamo").write_text("func down(n: int): int\n    return down(n + 1) + 1\nend\n\nfunc main()\n    print(1)\n    print(down(0))\nend\n", encoding="utf-8")
    run([str(JAMO), "build", str(work / "bottomless.jamo")])
    result = run([str(work / "bottomless.exe")])
    report("running out of stack gives a non-zero exit", result.returncode != 0 and clean(result.stdout) == "1\n", f"code {result.returncode}, stdout {result.stdout!r}")
    (work / "deep.jamo").write_text("func down(n: int): int\n    if n == 0\n        return 0\n    end\n    return down(n - 1) + 1\nend\n\nfunc main()\n    print(down(100000))\nend\n", encoding="utf-8")
    result = run([str(JAMO), "run", str(work / "deep.jamo")])
    report("recursion 100,000 levels deep works", result.returncode == 0 and clean(result.stdout) == "100000\n", f"{result.returncode} {result.stdout}")

    # A frame far larger than a page, using a variable at the far end
    body = "".join(f"    var v{i} = {i}\n" for i in range(5000))
    (work / "big_frame.jamo").write_text("func main()\n" + body + "    print(v0 + v4999)\nend\n", encoding="utf-8")
    result = run([str(JAMO), "run", str(work / "big_frame.jamo")])
    report("a function with 5000 variables", result.returncode == 0 and clean(result.stdout) == "4999\n", f"{result.returncode} {result.stdout}")

    # Same source, same file
    run([str(JAMO), "build", str(work / "ok.jamo")])
    first = (work / "ok.exe").read_bytes()
    run([str(JAMO), "build", str(work / "ok.jamo")])
    report("the same source always gives the same file", first == (work / "ok.exe").read_bytes())
    imports = [word for word in (b"GetStdHandle", b"WriteFile", b"ExitProcess", b"GetProcessHeap", b"HeapAlloc", b"CreateFileW", b"ReadFile", b"CloseHandle", b"GetFileAttributesW", b"GetCommandLineW", b"MultiByteToWideChar", b"HeapFree", b"kernel32.dll") if word in first]
    report("the program only imports the allowed functions", len(imports) == 13 and b".dll" not in first.replace(b"kernel32.dll", b""))


# --- Random math, checked against Python ---
def random_expression(rng, names, depth):
    if depth == 0 or rng.random() < 0.25:
        if names and rng.random() < 0.6:
            name = rng.choice(list(names))
            return name, names[name]
        value = rng.choice([0, 1, 2, 3, 7, 10, 255, 65536, 2147483648, 4294967296, 9223372036854775807, rng.randrange(1000)])
        return str(value), value
    kind = rng.randrange(6)
    left_text, left = random_expression(rng, names, depth - 1)
    if kind == 5:
        return f"(-{left_text})", wrap(-left)
    right_text, right = random_expression(rng, names, depth - 1)
    if kind == 0:
        return f"({left_text} + {right_text})", wrap(left + right)
    if kind == 1:
        return f"({left_text} - {right_text})", wrap(left - right)
    if kind == 2:
        return f"({left_text} * {right_text})", wrap(left * right)
    # Division by something that could be zero has its own test, so this one never divides by zero
    if right == 0:
        return f"({left_text} + {right_text})", wrap(left + right)
    if kind == 4:
        remainder = abs(left) % abs(right)
        return f"({left_text} % {right_text})", wrap(-remainder if left < 0 else remainder)
    answer = abs(left) // abs(right)
    if (left < 0) != (right < 0):
        answer = -answer
    return f"({left_text} / {right_text})", wrap(answer)


def random_program(rng):
    lines, want, names = ["func main()"], [], {}
    for i in range(rng.randrange(4, 14)):
        text, value = random_expression(rng, names, rng.randrange(1, 5))
        if rng.random() < 0.4:
            name = f"v{i}"
            lines.append(f"    var {name} = {text}")
            names[name] = value
        elif rng.random() < 0.3:
            other_text, other = random_expression(rng, names, 2)
            lines.append(f"    print({text} < {other_text})")
            want.append("true" if value < other else "false")
        else:
            lines.append(f"    print({text})")
            want.append(str(value))
    lines.append("end")
    return "\n".join(lines) + "\n", "".join(line + "\n" for line in want)


def test_random_math(work):
    rng = random.Random(SEED)
    wrong = 0
    first_problem = ""
    source = work / "random.jamo"
    for number in range(RANDOM_PROGRAMS):
        text, want = random_program(rng)
        # A literal zero in a divisor is a compile error, which those programs correctly hit
        source.write_text(text, encoding="utf-8")
        built = run([str(JAMO), "build", str(source)])
        if built.returncode != 0 and "divides by zero" in built.stderr:
            continue
        ran = run([str(source.with_suffix(".exe"))]) if built.returncode == 0 else None
        if ran is None or clean(ran.stdout) != want:
            wrong += 1
            if not first_problem:
                first_problem = f"program {number}:\n{text}\n{built.stderr}\ngot:\n{ran.stdout if ran else ''}\nwanted:\n{want}"
    report(f"{RANDOM_PROGRAMS} random programs match Python's answers", wrong == 0, f"{wrong} wrong\n{first_problem}")


# --- Input and files ---
def test_input_and_files(work):
    folder = work / "files with spaces"
    folder.mkdir()
    (folder / "lines.jamo").write_text('func main()\n    var name = input()\n    print(f"[{name}]")\n    var n = int(input()) + 1\n    print(n)\n    print(float(input()) / 2.0)\n    var line = input()\n    while line != ""\n        print(len(line))\n        line = input()\n    end\n    print(input() == "")\nend\n', encoding="utf-8")
    run([str(JAMO), "build", str(folder / "lines.jamo")])
    exe = folder / "lines.exe"
    for name, typed in (("line endings like Unix", "Sam\n41\n2.5\nabc\nde"), ("line endings like Windows", "Sam\r\n41\r\n2.5\r\nabc\r\nde\r\n")):
        result = subprocess.run([str(exe)], input=typed.encode(), capture_output=True, timeout=30)
        report(f"piped input, {name}", result.returncode == 0 and clean(result.stdout.decode()) == "[Sam]\n42\n1.25\n3\n2\ntrue\n", f"{result.returncode} {result.stdout} {result.stderr}")
    long_line = "x" * 100000
    (folder / "long.jamo").write_text("func main()\n    print(len(input()))\n    print(input())\nend\n", encoding="utf-8")
    run([str(JAMO), "build", str(folder / "long.jamo")])
    result = subprocess.run([str(folder / "long.exe")], input=(long_line + "\nnext\n").encode(), capture_output=True, timeout=30)
    report("a line longer than the input buffer", clean(result.stdout.decode()) == "100000\nnext\n", str(result.stdout[:100]))
    result = subprocess.run([str(exe)], input=b"", capture_output=True, timeout=30)
    report("no input stops with a clear error when a number was needed", result.returncode == 1 and clean(result.stderr.decode()) == 'lines.jamo line 4: Error: can\'t turn "" into an int\n', f"{result.returncode} {result.stderr}")

    # Command line inputs, including the awkward ones
    (folder / "show args.jamo").write_text("func main()\n    for item in args()\n        print(f\"<{item}>\")\n    end\n    print(len(args()))\nend\n", encoding="utf-8")
    run([str(JAMO), "build", str(folder / "show args.jamo")])
    inputs = ["plain", "two words", "", 'has "quotes"', "ends with slash\\", "a\\b", 'slash\\"quote', "caf\u00e9", "\U0001F600", "-x=1"]
    want = "".join(f"<{item}>\n" for item in inputs) + f"{len(inputs)}\n"
    result = subprocess.run([str(folder / "show args.exe"), *inputs], capture_output=True, timeout=30)
    report("args reads every kind of input", clean(result.stdout.decode()) == want, f"{result.stdout.decode(errors='replace')}\nwanted:\n{want}")
    result = subprocess.run([str(JAMO), "run", str(folder / "show args.jamo"), *inputs], capture_output=True, timeout=30)
    report("jamo run passes inputs along", clean(result.stdout.decode()) == want, f"{result.stdout.decode(errors='replace')}\nwanted:\n{want}")
    result = subprocess.run([str(folder / "show args.exe")], capture_output=True, timeout=30)
    report("args with no inputs", clean(result.stdout.decode()) == "0\n", str(result.stdout))

    # Files, in a folder and with names that need care
    (folder / "files.jamo").write_text('func main()\n    var name = "my file caf\u00e9.txt"\n    print(exists(name))\n    write_file(name, "first\\nsecond")\n    print(exists(name))\n    print(read_file(name))\n    write_file(name, "short")\n    print(read_file(name))\n    write_file(name, "")\n    print(len(read_file(name)))\n    print(exists("."))\n    var big = "0123456789"\n    for i in 0..17\n        big = big + big\n    end\n    write_file("big.txt", big)\n    print(len(read_file("big.txt")))\n    print(read_file("big.txt") == big)\nend\n', encoding="utf-8")
    result = run([str(JAMO), "run", str(folder / "files.jamo")], cwd=folder)
    report("files can be written, replaced and read back", result.returncode == 0 and clean(result.stdout) == "false\ntrue\nfirst\nsecond\nshort\n0\ntrue\n1310720\ntrue\n", f"{result.returncode} {result.stdout} {result.stderr}")
    report("the file really has that name and text", (folder / "my file caf\u00e9.txt").exists() and (folder / "big.txt").stat().st_size == 1310720)
    (folder / "bad read.jamo").write_text('func main()\n    print("before")\n    print(read_file("nope.txt"))\nend\n', encoding="utf-8")
    result = run([str(JAMO), "run", str(folder / "bad read.jamo")], cwd=folder)
    report("a failed read stops with a clear error", result.returncode == 1 and clean(result.stderr) == "bad read.jamo line 3: Error: can't read the file nope.txt.\n" and clean(result.stdout) == "before\n", f"{result.returncode} {result.stderr}")
    (folder / "bad write.jamo").write_text('func main()\n    write_file("no such folder/a.txt", "x")\nend\n', encoding="utf-8")
    result = run([str(JAMO), "run", str(folder / "bad write.jamo")], cwd=folder)
    report("a failed write stops with a clear error", result.returncode == 1 and clean(result.stderr) == "bad write.jamo line 2: Error: can't write the file no such folder/a.txt.\n", f"{result.returncode} {result.stderr}")
    (folder / "bad number.jamo").write_text('func main()\n    print(float("1.5x"))\nend\n', encoding="utf-8")
    result = run([str(JAMO), "run", str(folder / "bad number.jamo")], cwd=folder)
    report("a failed number conversion stops with a clear error", result.returncode == 1 and clean(result.stderr) == "bad number.jamo line 2: Error: can't turn \"1.5x\" into a float\n", f"{result.returncode} {result.stderr}")


# --- Text turned into numbers, checked against Python ---
def test_text_to_numbers(work):
    rng = random.Random(SEED)
    wrong = 0
    first_problem = ""
    source = work / "text_numbers.jamo"
    for number in range(RANDOM_TEXT_PROGRAMS):
        lines, want = ["func main()"], []
        for _ in range(40):
            kind = rng.randrange(5)
            if kind == 0:
                value = rng.randrange(-(2 ** 63), 2 ** 63)
                lines.append(f'    print(int("{value}"))')
                want.append(str(value))
                continue
            if kind == 1:
                text = float_literal(random_float(rng))
            elif kind == 2:
                text = f"{rng.randrange(10 ** 20)}.{rng.randrange(10 ** 25):025d}"
            elif kind == 3:
                text = str(rng.randrange(-1000, 1000))
            else:
                digits = rng.randrange(1, 900)
                text = "0." + "0" * rng.randrange(0, 300) + "".join(rng.choice("0123456789") for _ in range(digits))
            value = float(text)
            if math.isinf(value) or (value == 0.0 and text.strip("-0.") != "") or (abs(value) < 1e-300):
                continue
            lines.append(f'    print(float("{text}") == {float_literal(value)})')
            want.append("true")
        lines.append("end")
        source.write_text("\n".join(lines) + "\n", encoding="utf-8")
        built = run([str(JAMO), "build", str(source)])
        ran = run([str(source.with_suffix(".exe"))]) if built.returncode == 0 else None
        got = clean(ran.stdout) if ran else ""
        if got != "".join(line + "\n" for line in want):
            wrong += 1
            if not first_problem:
                first_problem = f"program {number}: {built.stderr[:300]} {ran.stderr[:300] if ran else ''}\ngot: {got[:300]}"
    report(f"{RANDOM_TEXT_PROGRAMS} random text-to-number programs match Python's answers", wrong == 0, f"{wrong} wrong\n{first_problem}")


# --- The same random programs with the optimizer off, and programs where it could go wrong ---
def test_no_optimize(work):
    rng = random.Random(SEED + 1)
    source = work / "random_off.jamo"
    wrong = 0
    first_problem = ""
    for _ in range(RANDOM_PROGRAMS // 3):
        text, want = random_program(rng)
        source.write_text(text, encoding="utf-8")
        answers = []
        for flags in ([], ["--no-optimize"]):
            built = run([str(JAMO), "build", *flags, str(source)])
            answers.append(clean(run([str(source.with_suffix(".exe"))]).stdout) if built.returncode == 0 else built.stderr)
        if "divides by zero" in answers[0]:
            continue
        if answers[0] != want or answers[1] != want:
            wrong += 1
            first_problem = first_problem or f"{text}\ngot {answers}\nwanted {want}"
    report("random programs give Python's answers with the optimizer on and off", wrong == 0, first_problem)

    source = work / "unused.jamo"
    source.write_text('func never_called(): int\n    return 12345678\nend\n\nfunc called(): int\n    return 87654321\nend\n\nfunc main()\n    print(called())\n    return\n    print(55443322)\nend\n', encoding="utf-8")
    run([str(JAMO), "build", str(source)])
    optimized = source.with_suffix(".exe").read_bytes()
    report("code that can't run is left out", (87654321).to_bytes(4, "little") in optimized and (12345678).to_bytes(4, "little") not in optimized and (55443322).to_bytes(4, "little") not in optimized)
    run([str(JAMO), "build", "--no-optimize", str(source)])
    report("--no-optimize keeps it", (12345678).to_bytes(4, "little") in source.with_suffix(".exe").read_bytes())
    source = work / "constant.jamo"
    source.write_text("func main()\n    print(1111 * 1111)\nend\n", encoding="utf-8")
    run([str(JAMO), "build", str(source)])
    report("constants get worked out", (1111 * 1111).to_bytes(4, "little") in source.with_suffix(".exe").read_bytes())


# --- Benchmarks against Python, tracked but not pass or fail ---
def test_benchmarks(work):
    print("\nBenchmarks, best of 3 after a first run (time in seconds):")
    programs = {
        "recursion": ("func fib(n: int): int\n    if n < 2\n        return n\n    end\n    return fib(n - 1) + fib(n - 2)\nend\n\nfunc main()\n    print(fib(35))\nend\n", "def fib(n):\n    return n if n < 2 else fib(n - 1) + fib(n - 2)\nprint(fib(35))\n"),
        "loops": ("func main()\n    var total = 0\n    for i in 0..100000000\n        if i % 3 == 0\n            total += i\n        end\n    end\n    print(total)\nend\n", "total = 0\nfor i in range(100000000):\n    if i % 3 == 0:\n        total += i\nprint(total)\n"),
        "array math": ("func main()\n    var items: [int] = []\n    for i in 0..1000000\n        items.add(i)\n    end\n    var total = 0\n    for round in 0..20\n        for i in 0..1000000\n            items[i] = items[i] + round\n            total += items[i]\n        end\n    end\n    print(total)\nend\n", "items = []\nfor i in range(1000000):\n    items.append(i)\ntotal = 0\nfor round in range(20):\n    for i in range(1000000):\n        items[i] = items[i] + round\n        total += items[i]\nprint(total)\n"),
        "game particles": ('use "math.jamo"\n\nstruct Particle\n    x: float\n    y: float\n    vx: float\n    vy: float\n    life: int\nend\n\nfunc Particle.update(gravity: float)\n    self.vy += gravity\n    self.x += self.vx\n    self.y += self.vy\n    if self.y > 600.0\n        self.y = 600.0\n        self.vy = 0.0 - self.vy * 0.8\n    end\n    self.x = clamp(self.x, 0.0, 800.0)\n    self.life -= 1\nend\n\nfunc Particle.speed(): float\n    return sqrt(self.vx * self.vx + self.vy * self.vy)\nend\n\nfunc main()\n    var particles: [Particle] = []\n    for i in 0..5000\n        particles.add(Particle{x: float(i % 800), y: float(i % 300), vx: float(i % 7) - 3.0, vy: 0.0, life: 1000})\n    end\n    var total = 0.0\n    for frame in 0..1000\n        for p in particles\n            p.update(0.2)\n        end\n        total += particles[frame % 5000].speed()\n    end\n    var sum = 0.0\n    for p in particles\n        sum += p.x + p.y\n    end\n    print(int(sum))\n    print(int(total))\nend\n', 'import math\n\n\nclass Particle:\n    __slots__ = ("x", "y", "vx", "vy", "life")\n\n    def __init__(self, x, y, vx, vy, life):\n        self.x, self.y, self.vx, self.vy, self.life = x, y, vx, vy, life\n\n    def update(self, gravity):\n        self.vy += gravity\n        self.x += self.vx\n        self.y += self.vy\n        if self.y > 600.0:\n            self.y = 600.0\n            self.vy = 0.0 - self.vy * 0.8\n        self.x = min(max(self.x, 0.0), 800.0)\n        self.life -= 1\n\n    def speed(self):\n        return math.sqrt(self.vx * self.vx + self.vy * self.vy)\n\n\nparticles = [Particle(float(i % 800), float(i % 300), float(i % 7) - 3.0, 0.0, 1000) for i in range(5000)]\ntotal = 0.0\nfor frame in range(1000):\n    for p in particles:\n        p.update(0.2)\n    total += particles[frame % 5000].speed()\nprint(int(sum(p.x + p.y for p in particles)))\nprint(int(total))\n'),
        "string joining": ('func main()\n    var total = 0\n    for i in 0..3000000\n        var text = "item " + string(i) + " done"\n        total += len(text)\n    end\n    print(total)\nend\n', 'total = 0\nfor i in range(3000000):\n    text = "item " + str(i) + " done"\n    total += len(text)\nprint(total)\n'),
    }

    def best_of(command):
        times, output = [], ""
        run(command, timeout=SLOW_RUN_LIMIT)
        for _ in range(3):
            started = time.perf_counter()
            output = clean(run(command, timeout=SLOW_RUN_LIMIT).stdout)
            times.append(time.perf_counter() - started)
        return min(times), output

    for name, (jamo_source, python_source) in programs.items():
        path = work / f"bench {name}.jamo"
        path.write_text(jamo_source, encoding="utf-8")
        run([str(JAMO), "build", str(path)])
        fast, jamo_output = best_of([str(path.with_suffix(".exe"))])
        run([str(JAMO), "build", "--no-optimize", str(path)])
        plain, _ = best_of([str(path.with_suffix(".exe"))])
        script = work / f"bench {name}.py"
        script.write_text(python_source, encoding="utf-8")
        started = time.perf_counter()
        python_output = clean(run([sys.executable, str(script)], timeout=SLOW_RUN_LIMIT).stdout)
        python_time = time.perf_counter() - started
        print(f"  {name}: Jamo {fast:.3f}, with no optimizing {plain:.3f}, Python {python_time:.3f} ({python_time / fast:.1f}x faster than Python)")
        report(f"benchmark gives Python's answer: {name}", jamo_output == python_output, f"{jamo_output} {python_output}")


# --- Random floats, checked against Python ---
def float_literal(value):
    text = format(Decimal(abs(value)), "f")
    return ("-" if math.copysign(1, value) < 0 else "") + (text if "." in text else text + ".0")


def trim_zeros(digits):
    whole, part = digits.split(".")
    return whole + "." + (part.rstrip("0") or "0")


def float_text(value):
    if math.isnan(value):
        return "nan"
    if math.isinf(value):
        return "inf" if value > 0 else "-inf"
    if abs(value) >= SCIENTIFIC_FROM:
        digits, exponent = f"{value:.6e}".split("e")
        return trim_zeros(digits) + "e" + exponent
    return trim_zeros(f"{value:.6f}")


def random_float(rng):
    kind = rng.randrange(9)
    if kind == 0:
        return rng.uniform(-10, 10)
    if kind == 1:
        return rng.randrange(-4096, 4096) / 2 ** rng.randrange(0, 12)  # Exact ties in rounding
    if kind == 2:
        return struct.unpack("<d", struct.pack("<Q", rng.getrandbits(64) & ~(0x7FF << 52) | rng.randrange(1, 0x7FF) << 52))[0]  # Any finite size
    if kind == 3:
        return rng.randrange(10 ** 7) / 10 ** rng.randrange(0, 9)
    if kind == 4:
        return float(rng.randrange(-(10 ** 17), 10 ** 17))
    if kind == 5:
        return rng.randrange(1, 2 ** 53) * 2.0 ** rng.randrange(-80, -18)  # Around where rounding to six places starts to matter
    if kind == 6:
        if rng.random() < 0.5:
            return float((rng.randrange(10 ** 6, 10 ** 7) * 10 + 5) * 10 ** 12)  # Exact ties in scientific rounding
        return rng.randrange(1, 10 ** 6) * 10.0 ** rng.randrange(10, 300)
    if kind == 7:
        return rng.randrange(1, 2 ** 53) * 2.0 ** rng.randrange(-53, 0)  # Fractions using every bit
    return rng.choice([0.0, -0.0, 1.0, 0.5, 999999.9999995, 0.0000005, 9999999999999998.0, 1e16, 9.9999995e20, 9.9999985e20, 1.7976931348623157e308, 5e-324])


def random_float_program(rng):
    lines, want = ["func main()"], []
    for i in range(30):
        left, right = random_float(rng), random_float(rng)
        a, b = float_literal(left), float_literal(right)
        kind = rng.randrange(8)
        if kind == 0:
            lines.append(f"    print({a} + {b})")
            want.append(float_text(left + right))
        elif kind == 1:
            lines.append(f"    print({a} - {b})")
            want.append(float_text(left - right))
        elif kind == 2:
            lines.append(f"    print({a} * {b})")
            want.append(float_text(left * right))
        elif kind == 3 and right != 0:
            lines.append(f"    print({a} / {b})")
            want.append(float_text(left / right))
        elif kind == 4:
            lines.append(f"    print({a} < {b})")
            want.append("true" if left < right else "false")
        elif kind == 5:
            lines.append(f'    print(string({a}) + "|")')
            want.append(float_text(left) + "|")
        elif kind == 7 and right != 0:
            lines.append(f"    print({a} % {b})")
            want.append(float_text(math.fmod(left, right)))
        elif kind == 6 and abs(left) < 2 ** 63:
            lines.append(f"    print(int({a}))")
            want.append(str(int(left)))
        else:
            lines.append(f"    print({a})")
            want.append(float_text(left))
    number = rng.randrange(-(2 ** 63), 2 ** 63)
    lines.append(f"    print(float({number}))")
    want.append(float_text(float(number)))
    lines.append("end")
    return "\n".join(lines) + "\n", "".join(line + "\n" for line in want)


def test_random_floats(work):
    rng = random.Random(SEED)
    wrong = 0
    first_problem = ""
    source = work / "random_float.jamo"
    for number in range(RANDOM_FLOAT_PROGRAMS):
        text, want = random_float_program(rng)
        source.write_text(text, encoding="utf-8")
        built = run([str(JAMO), "build", str(source)])
        ran = run([str(source.with_suffix(".exe"))]) if built.returncode == 0 else None
        got = clean(ran.stdout) if ran else ""
        if got != want:
            wrong += 1
            if not first_problem:
                pairs = [(index, a, b) for index, (a, b) in enumerate(zip(got.split("\n"), want.split("\n"))) if a != b]
                first_problem = f"program {number}: {built.stderr[:400]}\nfirst differences (line, got, wanted): {pairs[:4]}"
    report(f"{RANDOM_FLOAT_PROGRAMS} random float programs match Python's answers", wrong == 0, f"{wrong} wrong\n{first_problem}")


# --- Garbage must never crash the compiler ---
def test_garbage(work):
    rng = random.Random(SEED)
    words = ["func", "end", "var", "if", "else", "while", "return", "print", "int", "bool", "true", "false", "main", "x", "(", ")", ":", ",", "=", "==", "!=", "<", "+", "-", "*", "/", "\n", "\n", "0", "1", "9223372036854775808", "func main()\n", "\t", "#", "\r\n", '"', '"hi"', '\\', '\\n', "string", "len", "string(", "len(", 'f"', 'f"a{', "{", "}", '}b"', 'f"{1}"', "{{", "}}", "1.5", "0.0", ".", "..", "2.", "float", "float(", "int(", "1.0e5", "and", "or", "not", "elif", "if x\n", "elif x\n", "for", "in", "0..3", "break", "continue", "%", "+=", "-=", "*=", "/=", "for i in 0..3\n", "x += 1\n", "while x\n", "[", "]", "x[0]", "+\n", ",\n", "(\n", ")\n", "[1, 2]", "[]", ".", "x.add(1)", "x.pop()", "[int]", "[[", "]]", ": [int]", "for x in y\n", "struct", "struct S\n", "a: int\n", "S{", "S{a: 1}", "x.a", "x.a = 1\n", "{", "}", "func S.m()\n", "self", "self.a", "x.m()", "enum", "enum E\n", "A\n", "E.A", "match", "match x\n", "case", "case 1\n", "case E.A\n", "use", 'use "', 'use "a.jamo"\n', 'use "garbage.jamo"\n', "input()", "args()", "read_file(", "write_file(", "exists("]
    source = work / "garbage.jamo"
    crashes = 0
    first_crash = ""
    for number in range(GARBAGE_FILES):
        if number % 3 == 0:
            data = bytes(rng.randrange(256) for _ in range(rng.randrange(200)))
        else:
            data = " ".join(rng.choice(words) for _ in range(rng.randrange(60))).encode()
        source.write_bytes(data)
        result = subprocess.run([str(JAMO), "build", str(source)], capture_output=True, timeout=30)
        if result.returncode not in (0, 1):
            crashes += 1
            if not first_crash:
                first_crash = f"exit {result.returncode}: {data!r}"
    report(f"{GARBAGE_FILES} garbage files don't crash the compiler", crashes == 0, f"{crashes} crashed\n{first_crash}")


# --- Speed and size, tracked but not pass or fail ---
def test_speed(work):
    print("\nSpeed and size:")
    fib = "func fib(n: int): int\n    if n < 2\n        return n\n    end\n    return fib(n - 1) + fib(n - 2)\nend\n\nfunc main()\n    print(fib(35))\nend\n"
    source = work / "fib.jamo"
    source.write_text(fib, encoding="utf-8")
    started = time.perf_counter()
    run([str(JAMO), "build", str(source)])
    print(f"  compile fib.jamo: {(time.perf_counter() - started) * 1000:.0f} ms")
    started = time.perf_counter()
    result = run([str(source.with_suffix(".exe"))], timeout=120)
    jamo_seconds = time.perf_counter() - started
    report("fib(35) gives the right answer", clean(result.stdout) == "9227465\n", result.stdout)
    started = time.perf_counter()
    run([sys.executable, "-c", "def fib(n):\n    return n if n < 2 else fib(n - 1) + fib(n - 2)\nprint(fib(35))"], timeout=240)
    python_seconds = time.perf_counter() - started
    print(f"  fib(35): Jamo {jamo_seconds:.3f} s, Python {python_seconds:.3f} s ({python_seconds / jamo_seconds:.0f}x)")
    print(f"  jamo.exe: {JAMO.stat().st_size // 1024} KB")
    print(f"  fib.exe: {source.with_suffix('.exe').stat().st_size} bytes")


def test_defender(work):
    print("\nWindows Defender:")
    target = work / "fib.exe"
    if not DEFENDER.exists():
        print("  NOT TESTED: MpCmdRun.exe wasn't found")
        return
    for path in (target, JAMO):
        result = run([str(DEFENDER), "-Scan", "-ScanType", "3", "-File", str(path), "-DisableRemediation"], timeout=180)
        words = clean(result.stdout).strip().split("\n")
        print(f"  {path.name}: exit {result.returncode}, {words[-1][:80] if words else ''}")
        if result.returncode == 2:
            report(f"Defender doesn't flag {path.name}", False, result.stdout)


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    build_compiler()
    with tempfile.TemporaryDirectory(prefix="jamo tests ") as folder:
        work = Path(folder)
        test_units(work)
        test_good_programs(work)
        test_bad_programs(work)
        test_modules(work)
        test_cleaner_modes(work)
        test_cleaner_long_runs(work)
        test_commands(work)
        test_watch(work)
        test_gfx(work)
        test_sound(work)
        test_ui(work)
        test_g3d(work)
        test_net(work)
        test_links(work)
        test_samples(work)
        test_window_library(work)
        test_windows_calls(work)
        test_docs(work)
        test_examples(work)
        test_checksums(work)
        test_console_text(work)
        test_get_command(work)
        test_vscode_extension(work)
        test_warnings_and_calls(work)
        test_small_blocks(work)
        test_special_cases(work)
        test_input_and_files(work)
        test_text_to_numbers(work)
        test_math(work)
        test_random_and_time(work)
        test_text(work)
        test_number_checks(work)
        test_more_libraries(work)
        test_window_programs(work)
        test_release(work)
        test_random_math(work)
        test_divide_by_constants(work)
        test_dict_builder_bytes(work)
        test_self_hosted_lexer(work)
        test_self_hosted_parser(work)
        test_self_hosted_checker(work)
        test_self_hosted_codegen(work)
        test_self_hosted_pe(work)
        test_self_compile(work)
        test_random_floats(work)
        test_no_optimize(work)
        test_garbage(work)
        test_speed(work)
        test_cleaner_speed(work)
        test_benchmarks(work)
        test_defender(work)
    print()
    if failures:
        print(f"{len(failures)} FAILED: " + ", ".join(failures))
        sys.exit(1)
    print("All tests passed.")


if __name__ == "__main__":
    main()
