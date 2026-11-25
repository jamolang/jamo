"""Random Jamo programs with a Python twin, using globals, generics, loops and calls with side effects.
For each one: Jamo's answers must match Python's, both compilers must make the same .exe, and --no-optimize
must give the same answers. Usage: python fuzz_programs.py <count> [seed]"""
import random
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
JAMO = ROOT / "jamo.exe"
JAMO_CPP = ROOT / "jamo_cpp.exe"
LIMIT = 100003


def jmod(a, b):
    """Jamo's %, which keeps the left side's sign"""
    r = abs(a) % abs(b)
    return -r if a < 0 else r


class Gen:
    def __init__(self, rng):
        self.rng = rng
        self.jamo = []
        self.py = []
        self.locals = []      # Names that can be changed
        self.reads = []       # Names that can be read, including loop variables
        self.counter = 0

    def both(self, indent, jamo, py=None):
        self.jamo.append("    " * indent + jamo)
        self.py.append("    " * indent + (py if py is not None else jamo))

    def value(self, depth=2):
        """An int expression, as (jamo, python)"""
        r = self.rng
        choices = ["num", "num"]
        names = self.reads + ["g0", "g1", "g2"]
        if names:
            choices += ["name"] * 3
        if depth > 0:
            choices += ["op", "op", "mod", "call", "pick"]
        kind = r.choice(choices)
        if kind == "num":
            n = r.randrange(-50, 300)
            return str(n), str(n)
        if kind == "name":
            name = r.choice(names)
            return name, name
        if kind == "op":
            a, pa = self.value(depth - 1)
            b, pb = self.value(depth - 1)
            op = r.choice(["+", "-", "*"])
            return f"({a} {op} {b})", f"w({pa} {op} {pb})"
        if kind == "mod":
            a, pa = self.value(depth - 1)
            m = r.randrange(2, 40)
            return f"({a} % {m})", f"jmod({pa}, {m})"
        if kind == "call":
            k = r.randrange(1, 9)
            return f"bump({k})", f"bump({k})"
        a, pa = self.value(depth - 1)
        b, pb = self.value(depth - 1)
        c, pc = self.condition(depth - 1)
        return f"pick({a}, {b}, {c})", f"pick({pa}, {pb}, {pc})"

    def condition(self, depth=1):
        a, pa = self.value(depth)
        b, pb = self.value(depth)
        op = self.rng.choice(["<", ">", "<=", ">=", "==", "!="])
        return f"{a} {op} {b}", f"{pa} {op} {pb}"

    def keep_small(self, name, indent):
        self.both(indent, f"{name} = {name} % {LIMIT}", f"{name} = jmod({name}, {LIMIT})")

    def statement(self, indent, depth):
        r = self.rng
        kinds = ["print", "print", "assign", "update", "global", "box"]
        if depth < 2:
            kinds += ["if", "for", "while"]
        kind = r.choice(kinds)
        if kind == "print" or not self.locals and kind in ("assign", "update"):
            v, pv = self.value()
            self.both(indent, f"print({v})", f"say({pv})")
        elif kind == "assign":
            name = r.choice(self.locals)
            v, pv = self.value()
            self.both(indent, f"{name} = {v}", f"{name} = {pv}")
            self.keep_small(name, indent)
        elif kind == "update":
            name = r.choice(self.locals)
            v, pv = self.value()
            op = r.choice(["+=", "-="])
            self.both(indent, f"{name} {op} {v}", f"{name} {op} {pv}")
            self.keep_small(name, indent)
        elif kind == "global":
            name = r.choice(["g0", "g1", "g2"])
            v, pv = self.value()
            op = r.choice(["+=", "-=", "="])
            self.both(indent, f"{name} {op} {v}", f"{name} {op} {pv}")
            self.keep_small(name, indent)
        elif kind == "box":
            v, pv = self.value()
            self.both(indent, f"box.put({v})", f"box.append({pv})")
            i, pi = self.value(1)
            self.both(indent, f"print(box.get({i}))", f"say(box_get(box, {pi}))")
        elif kind == "if":
            c, pc = self.condition()
            self.both(indent, f"if {c}", f"if {pc}:")
            self.block(indent + 1, depth + 1)
            if r.random() < 0.5:
                c2, pc2 = self.condition()
                self.both(indent, f"elif {c2}", f"elif {pc2}:")
                self.block(indent + 1, depth + 1)
            if r.random() < 0.5:
                self.both(indent, "else", "else:")
                self.block(indent + 1, depth + 1)
            self.both(indent, "end", "pass")
        elif kind == "for":
            var = f"i{depth}"
            n = r.randrange(0, 12)
            self.both(indent, f"for {var} in 0..{n}", f"for {var} in range({n}):")
            self.reads.append(var)
            if r.random() < 0.4:
                m = r.randrange(2, 5)
                self.both(indent + 1, f"if {var} % {m} == 0", f"if jmod({var}, {m}) == 0:")
                self.both(indent + 2, r.choice(["continue", "break"]))
                self.both(indent + 1, "end", "pass")
            self.block(indent + 1, depth + 1)
            self.reads.remove(var)
            self.both(indent, "end", "pass")
        else:
            self.counter += 1
            var = f"w{self.counter}"
            n = r.randrange(0, 10)
            self.both(indent, f"var {var} = {n}", f"{var} = {n}")
            self.both(indent, f"while {var} > 0", f"while {var} > 0:")
            self.both(indent + 1, f"{var} -= 1")
            # The counter is only changed by its own -= 1, so the loop always ends
            self.block(indent + 1, depth + 1)
            self.both(indent, "end", "pass")

    def block(self, indent, depth):
        for _ in range(self.rng.randrange(1, 4)):
            self.statement(indent, depth)

    def program(self):
        r = self.rng
        globals_ = [r.randrange(-20, 100) for _ in range(3)]
        self.jamo += ["struct Box<T>", "    items: [T]", "end", "",
                      "func Box<T>.put(item: T)", "    self.items.add(item)", "end", "",
                      "func Box<T>.get(i: int): T", "    var n = len(self.items)", "    return self.items[((i % n) + n) % n]", "end", "",
                      "func pick<T>(a: T, b: T, first: bool): T", "    if first", "        return a", "    end", "    return b", "end", ""]
        for i, g in enumerate(globals_):
            self.jamo.append(f"var g{i} = {g}")
        self.jamo += ["", "func bump(n: int): int", "    g0 += n", "    g0 = g0 % 1000", "    return g0 % 7", "end", "", "func main()",
                      "    var box = Box<int>{items: [0]}"]
        self.py += ["import sys", "out = []", "def say(v): out.append(str(v))",
                    "def jmod(a, b):", "    r = abs(a) % abs(b)", "    return -r if a < 0 else r",
                    "def pick(a, b, first): return a if first else b",
                    "def w(x): return ((x + 2 ** 63) % 2 ** 64) - 2 ** 63",
                    "def box_get(items, i):", "    n = len(items)", "    return items[jmod(jmod(i, n) + n, n)]"]
        for i, g in enumerate(globals_):
            self.py.append(f"g{i} = {g}")
        self.py += ["def bump(n):", "    global g0", "    g0 += n", "    g0 = jmod(g0, 1000)", "    return jmod(g0, 7)",
                    "def main():", "    global g0, g1, g2", "    box = [0]"]
        for k in range(r.randrange(1, 4)):
            name = f"l{k}"
            v, pv = self.value(1)
            self.both(1, f"var {name} = {v}", f"{name} = {pv}")
            self.locals.append(name)
            self.reads.append(name)
        for _ in range(r.randrange(4, 12)):
            self.statement(1, 0)
        self.both(1, 'print(pick("first", "second", g1 > g2))', 'say(pick("first", "second", g1 > g2))')
        self.jamo.append("end")
        self.py += ["main()", "print('\\n'.join(out))"]
        return "\n".join(self.jamo) + "\n", "\n".join(self.py) + "\n"


def run(command, **kw):
    return subprocess.run(command, capture_output=True, text=True, timeout=60, **kw)


def main():
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 100
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    problems = 0
    with tempfile.TemporaryDirectory(prefix="jamo fuzz ") as folder:
        work = Path(folder)
        for n in range(count):
            jamo_text, py_text = Gen(random.Random(seed * 100000 + n)).program()
            source = work / "p.jamo"
            source.write_text(jamo_text, encoding="utf-8")
            want = run([sys.executable, "-c", py_text]).stdout.replace("\r\n", "\n").strip()
            answers = {}
            exes = {}
            for name, compiler, flags in (("jamo", JAMO, []), ("cpp", JAMO_CPP, []), ("plain", JAMO, ["--no-optimize"])):
                built = run([str(compiler), "build", *flags, str(source)])
                if built.returncode:
                    answers[name] = "BUILD " + built.stderr[:300]
                    continue
                exes[name] = source.with_suffix(".exe").read_bytes()
                answers[name] = run([str(source.with_suffix(".exe"))]).stdout.replace("\r\n", "\n").strip()
            bad = [name for name in answers if answers[name] != want]
            if exes.get("jamo") != exes.get("cpp"):
                bad.append("the two compilers made different .exe files")
            if bad:
                problems += 1
                keep = Path(__file__).parent / f"fuzz_fail_{seed}_{n}.jamo"
                keep.write_text(jamo_text, encoding="utf-8")
                (keep.with_suffix(".py")).write_text(py_text, encoding="utf-8")
                print(f"PROBLEM program {n}: {bad} (kept as {keep.name})")
                print("  wanted:", want.replace("\n", " ")[:200])
                for name in answers:
                    print(f"  {name}:", answers[name].replace("\n", " ")[:200])
                if problems >= 3:
                    break
    print(f"{count} programs, {problems} problems")


if __name__ == "__main__":
    main()
