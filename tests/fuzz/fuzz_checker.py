"""Breaks real test programs in random ways, then checks that neither compiler crashes and that the checker written
in Jamo reports exactly what the C++ one does. Usage: python fuzz_checker.py <count> [seed]"""
import random
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
JAMO = ROOT / "jamo.exe"
JAMO_CPP = ROOT / "jamo_cpp.exe"
BASES = ["generics", "globals", "map_keys", "lib_grid", "lib_timer", "lib_vec", "lib_list_generic", "register_math", "lib_safe"]
SWAPS = [("int", "string"), ("string", "float"), ("T", "U"), ("A", "B"), ("<int>", "<string>"), ("<int>", ""), ("<", ""), (">", ""),
         ("var ", ""), ("func ", ""), ("end", ""), ("+=", "="), ("return", "print"), ("Box", "Bix"), ("Pair", "Grid"), ("self.", ""),
         ("0", "-1"), ("1", "x"), ("(", ""), (")", ""), (",", ""), ("true", "1"), ("[", ""), ("]", "")]


def mutate(text, rng):
    lines = text.split("\n")
    for _ in range(rng.randrange(1, 4)):
        kind = rng.randrange(10)
        i = rng.randrange(len(lines))
        if kind == 0 and len(lines) > 3:
            del lines[i]
        elif kind == 1:
            lines.insert(i, lines[rng.randrange(len(lines))])
        elif kind == 2:
            j = rng.randrange(len(lines))
            lines[i], lines[j] = lines[j], lines[i]
        else:
            old, new = rng.choice(SWAPS)
            spots = [m.start() for m in re.finditer(re.escape(old), lines[i])]
            if spots:
                at = rng.choice(spots)
                lines[i] = lines[i][:at] + new + lines[i][at + len(old):]
    return "\n".join(lines)


def main():
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 200
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    rng = random.Random(seed)
    problems = 0
    with tempfile.TemporaryDirectory(prefix="jamo check fuzz ") as folder:
        work = Path(folder)
        # The checker written in Jamo, as its own program
        for name in (ROOT / "compiler").glob("*.jamo"):
            shutil.copy(name, work / name.name)
        built = subprocess.run([str(JAMO), "build", str(work / "check.jamo")], capture_output=True, text=True)
        assert built.returncode == 0, built.stderr
        sources = {name: (ROOT / "tests" / "good" / f"{name}.jamo").read_text(encoding="utf-8") for name in BASES}
        for n in range(count):
            base = rng.choice(BASES)
            text = mutate(sources[base], rng)
            source = work / "m.jamo"
            source.write_text(text, encoding="utf-8")
            want = subprocess.run([str(JAMO_CPP), "checked", str(source)], capture_output=True, timeout=60)
            got = subprocess.run([str(work / "check.exe"), str(source)], capture_output=True, timeout=120, cwd=work)
            plain = subprocess.run([str(JAMO), "check", str(source)], capture_output=True, timeout=60)
            got_text = (work / "check.out").read_bytes().replace(b"\r\n", b"\n") if (work / "check.out").exists() else b""
            want_text = want.stdout.replace(b"\r\n", b"\n")
            bad = []
            if want.returncode not in (0,):
                bad.append(f"jamo_cpp checked exit {want.returncode}")
            if got.returncode != 0:
                bad.append(f"check.exe exit {got.returncode}")
            if plain.returncode not in (0, 1):
                bad.append(f"jamo check crashed with {plain.returncode}")
            if got_text != want_text:
                bad.append("the two checkers differ")
            (work / "check.out").unlink(missing_ok=True)
            if bad:
                problems += 1
                keep = Path(__file__).parent / f"check_fail_{seed}_{n}.jamo"
                keep.write_text(text, encoding="utf-8")
                print(f"PROBLEM {n} from {base}: {bad} (kept as {keep.name})")
                if "the two checkers differ" in bad:
                    a = want_text.decode("utf-8", "replace").split("\n")
                    b = got_text.decode("utf-8", "replace").split("\n")
                    for k, (x, y) in enumerate(zip(a, b)):
                        if x != y:
                            print("  C++ :", x[:220])
                            print("  Jamo:", y[:220])
                            break
                    else:
                        print(f"  lengths {len(a)} and {len(b)}")
                if problems >= 5:
                    break
    print(f"{count} broken programs, {problems} problems")


if __name__ == "__main__":
    main()
