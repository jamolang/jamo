"""Jamo speed test: times the same five programs in Jamo, C (gcc -O2) and Python, and draws a chart.

Usage: python bench/run_bench.py [path to jamo.exe]
C needs gcc on the PATH (or JAMO_GCC set to gcc.exe); without it the C column is left out.
Writes bench/results.md and bench/chart.svg. Each time is the best of 5 runs after one warm-up run,
because a brand new .exe's first run is slow on Windows (Defender scans it).
"""
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
JAMO = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "jamo.exe"
GCC = os.environ.get("JAMO_GCC") or shutil.which("gcc")
RUNS = 5

# --- The programs: name -> (Jamo, C, Python). All three print exactly the same thing. ---
PROGRAMS = {
    "Recursion (fib 35)": (
        """func fib(n: int): int
    if n < 2
        return n
    end
    return fib(n - 1) + fib(n - 2)
end

func main()
    print(fib(35))
end
""",
        """#include <stdio.h>
long long fib(long long n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
int main(void) { printf("%lld\\n", fib(35)); return 0; }
""",
        """def fib(n):
    return n if n < 2 else fib(n - 1) + fib(n - 2)
print(fib(35))
""",
    ),
    "Loops (100 million)": (
        """func main()
    var total = 0
    for i in 0..100000000
        if i % 3 == 0
            total += i
        end
    end
    print(total)
end
""",
        """#include <stdio.h>
int main(void) {
    volatile long long n = 100000000;
    long long total = 0;
    for (long long i = 0; i < n; i++) if (i % 3 == 0) total += i;
    printf("%lld\\n", total);
    return 0;
}
""",
        """total = 0
for i in range(100000000):
    if i % 3 == 0:
        total += i
print(total)
""",
    ),
    "Array math (20 million)": (
        """func main()
    var items: [int] = []
    for i in 0..1000000
        items.add(i)
    end
    var total = 0
    for round in 0..20
        for i in 0..1000000
            items[i] = items[i] + round
            total += items[i]
        end
    end
    print(total)
end
""",
        """#include <stdio.h>
#include <stdlib.h>
int main(void) {
    long long n = 1000000, total = 0;
    long long *items = malloc(n * sizeof *items);
    for (long long i = 0; i < n; i++) items[i] = i;
    for (long long round = 0; round < 20; round++)
        for (long long i = 0; i < n; i++) { items[i] = items[i] + round; total += items[i]; }
    printf("%lld\\n", total);
    free(items);
    return 0;
}
""",
        """items = []
for i in range(1000000):
    items.append(i)
total = 0
for round in range(20):
    for i in range(1000000):
        items[i] = items[i] + round
        total += items[i]
print(total)
""",
    ),
    "Game particles (5,000 x 1,000)": (
        """use "math.jamo"

struct Particle
    x: float
    y: float
    vx: float
    vy: float
    life: int
end

func Particle.update(gravity: float)
    self.vy += gravity
    self.x += self.vx
    self.y += self.vy
    if self.y > 600.0
        self.y = 600.0
        self.vy = 0.0 - self.vy * 0.8
    end
    self.x = clamp(self.x, 0.0, 800.0)
    self.life -= 1
end

func Particle.speed(): float
    return sqrt(self.vx * self.vx + self.vy * self.vy)
end

func main()
    var particles: [Particle] = []
    for i in 0..5000
        particles.add(Particle{x: float(i % 800), y: float(i % 300), vx: float(i % 7) - 3.0, vy: 0.0, life: 1000})
    end
    var total = 0.0
    for frame in 0..1000
        for p in particles
            p.update(0.2)
        end
        total += particles[frame % 5000].speed()
    end
    var sum = 0.0
    for p in particles
        sum += p.x + p.y
    end
    print(int(sum))
    print(int(total))
end
""",
        """#include <stdio.h>
#include <stdlib.h>
#include <math.h>
typedef struct { double x, y, vx, vy; long long life; } Particle;
static void update(Particle *p, double gravity) {
    p->vy += gravity;
    p->x += p->vx;
    p->y += p->vy;
    if (p->y > 600.0) { p->y = 600.0; p->vy = 0.0 - p->vy * 0.8; }
    p->x = p->x < 0.0 ? 0.0 : (p->x > 800.0 ? 800.0 : p->x);
    p->life -= 1;
}
int main(void) {
    int n = 5000;
    Particle **ps = malloc(n * sizeof *ps);
    for (int i = 0; i < n; i++) {
        ps[i] = malloc(sizeof **ps);
        *ps[i] = (Particle){ (double)(i % 800), (double)(i % 300), (double)(i % 7) - 3.0, 0.0, 1000 };
    }
    double total = 0.0;
    for (int frame = 0; frame < 1000; frame++) {
        for (int i = 0; i < n; i++) update(ps[i], 0.2);
        Particle *p = ps[frame % n];
        total += sqrt(p->vx * p->vx + p->vy * p->vy);
    }
    double sum = 0.0;
    for (int i = 0; i < n; i++) sum += ps[i]->x + ps[i]->y;
    printf("%lld\\n%lld\\n", (long long)sum, (long long)total);
    return 0;
}
""",
        """import math


class Particle:
    __slots__ = ("x", "y", "vx", "vy", "life")

    def __init__(self, x, y, vx, vy, life):
        self.x, self.y, self.vx, self.vy, self.life = x, y, vx, vy, life

    def update(self, gravity):
        self.vy += gravity
        self.x += self.vx
        self.y += self.vy
        if self.y > 600.0:
            self.y = 600.0
            self.vy = 0.0 - self.vy * 0.8
        self.x = min(max(self.x, 0.0), 800.0)
        self.life -= 1

    def speed(self):
        return math.sqrt(self.vx * self.vx + self.vy * self.vy)


particles = [Particle(float(i % 800), float(i % 300), float(i % 7) - 3.0, 0.0, 1000) for i in range(5000)]
total = 0.0
for frame in range(1000):
    for p in particles:
        p.update(0.2)
    total += particles[frame % 5000].speed()
print(int(sum(p.x + p.y for p in particles)))
print(int(total))
""",
    ),
    "Text joining (3 million)": (
        """func main()
    var total = 0
    for i in 0..3000000
        var text = "item " + string(i) + " done"
        total += len(text)
    end
    print(total)
end
""",
        """#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
    long long total = 0;
    for (long long i = 0; i < 3000000; i++) {
        char num[24];
        int k = snprintf(num, sizeof num, "%lld", i);
        char *text = malloc(5 + k + 5 + 1);
        memcpy(text, "item ", 5);
        memcpy(text + 5, num, k);
        memcpy(text + 5 + k, " done", 6);
        total += (long long)strlen(text);
        free(text);
    }
    printf("%lld\\n", total);
    return 0;
}
""",
        """total = 0
for i in range(3000000):
    text = "item " + str(i) + " done"
    total += len(text)
print(total)
""",
    ),
}


# --- Running ---
def best_time(command):
    subprocess.run(command, check=False, capture_output=True)          # warm-up run
    times, out = [], ""
    for _ in range(RUNS):
        start = time.perf_counter()
        result = subprocess.run(command, check=False, capture_output=True, text=True)
        times.append(time.perf_counter() - start)
        out = result.stdout.replace("\r\n", "\n").strip()
    return min(times), out


def main():
    if not JAMO.exists():
        sys.exit(f"Can't find {JAMO}. Build Jamo first, or pass the path to jamo.exe.")
    rows = []
    with tempfile.TemporaryDirectory(prefix="jamo bench ") as tmp:
        work = Path(tmp)
        for k, (name, (jamo_src, c_src, py_src)) in enumerate(PROGRAMS.items()):
            print(f"{name} ...", flush=True)
            j = work / f"p{k}.jamo"
            j.write_text(jamo_src, encoding="utf-8")
            built = subprocess.run([str(JAMO), "build", str(j)], check=False, capture_output=True, text=True)
            if built.returncode:
                sys.exit(f"Jamo couldn't build {name}:\n{built.stdout}{built.stderr}")
            jt, jout = best_time([str(j.with_suffix(".exe"))])

            ct = None
            if GCC:
                c = work / f"p{k}.c"
                c.write_text(c_src, encoding="utf-8")
                exe = work / f"p{k}_c.exe"
                env = {**os.environ, "PATH": str(Path(GCC).parent) + os.pathsep + os.environ.get("PATH", "")}
                subprocess.run([GCC, "-O2", str(c), "-o", str(exe), "-lm"], check=True, env=env)
                ct, cout = best_time([str(exe)])
                if cout != jout:
                    sys.exit(f"{name}: C printed {cout!r} but Jamo printed {jout!r}")

            p = work / f"p{k}.py"
            p.write_text(py_src, encoding="utf-8")
            pt, pout = best_time([sys.executable, str(p)])
            if pout != jout:
                sys.exit(f"{name}: Python printed {pout!r} but Jamo printed {jout!r}")
            rows.append((name, jt, ct, pt))
            print(f"    Jamo {jt:.3f} s" + (f"   C {ct:.3f} s" if ct is not None else "") + f"   Python {pt:.3f} s")

    write_results(rows)
    write_chart(rows)
    print(f"\nWrote {HERE / 'results.md'} and {HERE / 'chart.svg'}")


# --- Output ---
def write_results(rows):
    has_c = any(r[2] is not None for r in rows)
    lines = [
        "# Jamo speed test",
        "",
        f"Best of {RUNS} runs after a warm-up, in seconds. Made by `python bench/run_bench.py`.",
        "Every program prints the same answer in each language, and the script checks that.",
        "",
        "| Program | Jamo | " + ("C (gcc -O2) | " if has_c else "") + "Python | Jamo vs Python |",
        "|---|---|" + ("---|" if has_c else "") + "---|---|",
    ]
    for name, jt, ct, pt in rows:
        c_cell = (f"{ct:.3f} | " if ct is not None else "- | ") if has_c else ""
        lines.append(f"| {name} | {jt:.3f} | {c_cell}{pt:.3f} | {pt / jt:.0f}x faster |")
    lines += ["", f"Python {sys.version.split()[0]}. Times depend on the PC, so compare the columns, not the numbers.", ""]
    (HERE / "results.md").write_text("\n".join(lines), encoding="utf-8")


def write_chart(rows):
    # Horizontal bars on a log scale, one group per program. Colors pass 4.5:1 on white and dark backgrounds' cards.
    langs = [("Jamo", 1, "#d9480f"), ("C (gcc -O2)", 2, "#5c6c7c"), ("Python", 3, "#2f6fb0")]
    langs = [lang for lang in langs if any(r[lang[1]] is not None for r in rows)]
    import math
    all_t = [r[i] for r in rows for _, i, _ in langs if r[i] is not None]
    lo = 10 ** math.floor(math.log10(min(all_t)))
    hi = 10 ** math.ceil(math.log10(max(all_t)))
    left, width, bar, gap = 230, 470, 16, 4
    group = len(langs) * (bar + gap) + 20
    height = 60 + group * len(rows) + 40

    def x_of(t):
        return left + width * (math.log10(t) - math.log10(lo)) / (math.log10(hi) - math.log10(lo))

    out = [
        (f'<svg xmlns="http://www.w3.org/2000/svg" width="{left + width + 90}" height="{height}" '
        'font-family="Segoe UI, Helvetica, Arial, sans-serif" font-size="13">'),
        '<rect width="100%" height="100%" fill="#ffffff"/>',
        '<text x="16" y="26" font-size="16" font-weight="600" fill="#1b1b1b">Seconds (shorter is faster, log scale)</text>',
    ]
    for k, (label, _, color) in enumerate(langs):
        lx = left + k * 130
        out.append(f'<rect x="{lx}" y="38" width="12" height="12" fill="{color}"/>')
        out.append(f'<text x="{lx + 18}" y="49" fill="#1b1b1b">{label}</text>')
    decade = lo
    while decade <= hi * 1.0001:
        x = x_of(decade)
        out.append(f'<line x1="{x:.1f}" y1="60" x2="{x:.1f}" y2="{height - 34}" stroke="#d0d4d8"/>')
        out.append(f'<text x="{x:.1f}" y="{height - 16}" text-anchor="middle" fill="#4a4f55">{decade:g}</text>')
        decade *= 10
    y = 66
    for name, *times in rows:
        out.append(f'<text x="{left - 12}" y="{y + (len(langs) * (bar + gap)) / 2 + 4:.0f}" text-anchor="end" fill="#1b1b1b">{name}</text>')
        for _, i, color in langs:
            t = times[i - 1]
            if t is not None:
                w = max(2.0, x_of(t) - left)
                out.append(f'<rect x="{left}" y="{y}" width="{w:.1f}" height="{bar}" rx="2" fill="{color}"/>')
                out.append(f'<text x="{left + w + 6:.1f}" y="{y + bar - 3}" fill="#1b1b1b">{t:.2f}</text>')
            y += bar + gap
        y += 20
    out.append("</svg>")
    (HERE / "chart.svg").write_text("\n".join(out) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
