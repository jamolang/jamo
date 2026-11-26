"""Jamo Playground: type Jamo code and press Run to see what it prints, with the examples one click away.

Needs Python 3 (tkinter comes with it) and jamo.exe, found beside this folder, on the PATH, or picked on first run.
Programs are built in a temporary folder, so the playground never leaves files beside your code.
Ctrl+Enter runs, Ctrl+S saves, Ctrl+O opens. Click an error to jump to its line.
"""
import json
import os
import queue
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, font

# --- Look: a warm paper notebook, with ink-colored code ---
PAPER = "#f6f1e7"
PAPER_DARK = "#ece4d4"
INK = "#2b2620"
INK_SOFT = "#6b6156"
RULE = "#d9cfbd"
ACCENT = "#c2410c"          # Jamo orange, 4.9:1 on PAPER
OUT_BG = "#221f1b"
OUT_FG = "#efe8dc"
OUT_ERR = "#ff9d7a"
OUT_DIM = "#a89f92"
CODE_COLORS = {
    "keyword": "#9a3412",
    "string": "#166534",
    "comment": "#7c7266",
    "number": "#1d4ed8",
    "type": "#7e22ce",
    "func": "#0f5d75",
}
PAD = 8
FONT_UI = ("Bahnschrift", 11)
FONT_UI_BOLD = ("Bahnschrift SemiBold", 11)
FONT_TITLE = ("Bahnschrift SemiBold", 15)

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
SETTINGS = Path(os.environ.get("APPDATA", str(Path.home()))) / "JamoPlayground" / "settings.json"
KEYWORDS = ["func", "end", "var", "const", "if", "elif", "else", "while", "for", "in", "break", "continue", "return",
            "and", "or", "not", "struct", "enum", "match", "case", "use", "extern", "try", "catch", "true", "false"]
TYPES = ["int", "float", "string", "bool", "i32", "f32"]
ERROR_LINE = re.compile(r"^(.+?\.jamo) line (\d+):")
STARTER = '''# Welcome to the Jamo Playground. Press Run (or Ctrl+Enter).

func main()
    var name = "Jamo"
    print(f"Hello from {name}!")
    for i in 1..6
        print(f"{i} squared is {i * i}")
    end
end
'''


def load_settings():
    try:
        return json.loads(SETTINGS.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}


def save_settings(data):
    SETTINGS.parent.mkdir(parents=True, exist_ok=True)
    SETTINGS.write_text(json.dumps(data, indent=2), encoding="utf-8")


def find_jamo(settings):
    for candidate in (settings.get("jamo"), ROOT / "jamo.exe", shutil.which("jamo")):
        if candidate and Path(candidate).is_file():
            return str(candidate)
    return None


class Playground:
    def __init__(self, root):
        self.root = root
        self.settings = load_settings()
        self.jamo = find_jamo(self.settings)
        self.process = None
        self.lines = queue.Queue()
        self.work = Path(tempfile.mkdtemp(prefix="jamo playground "))
        self.file = None
        root.title("Jamo Playground")
        root.configure(bg=PAPER)
        root.geometry(self.settings.get("geometry", "1180x720"))
        root.minsize(760, 460)
        self.code_font = font.Font(family="Cascadia Mono" if "Cascadia Mono" in font.families() else "Consolas", size=self.settings.get("size", 12))
        self.build()
        self.code.insert("1.0", self.settings.get("code", STARTER))
        self.color_code()
        self.update_lines()
        root.protocol("WM_DELETE_WINDOW", self.close)
        root.after(50, self.pump)
        if not self.jamo:
            self.say("Can't find jamo.exe. Click Find Jamo to pick it.\n", "dim")

    # --- Layout ---
    def build(self):
        top = tk.Frame(self.root, bg=PAPER, padx=16, pady=PAD)
        top.pack(fill="x")
        tk.Label(top, text="Jamo", font=FONT_TITLE, fg=ACCENT, bg=PAPER).pack(side="left")
        tk.Label(top, text="Playground", font=FONT_TITLE, fg=INK, bg=PAPER).pack(side="left", padx=(6, 24))
        self.run_button = self.button(top, "Run", self.run, primary=True)
        self.stop_button = self.button(top, "Stop", self.stop)
        self.stop_button.configure(state="disabled")
        self.button(top, "Examples", self.show_examples)
        self.button(top, "Open", self.open_file)
        self.button(top, "Save", self.save_file)
        self.button(top, "New", self.new_code)
        if not self.jamo:
            self.button(top, "Find Jamo", self.pick_jamo)
        self.button(top, "A+", lambda: self.zoom(1)).pack_configure(side="right")
        self.button(top, "A-", lambda: self.zoom(-1)).pack_configure(side="right")
        self.status = tk.Label(top, text="", font=FONT_UI, fg=INK_SOFT, bg=PAPER)
        self.status.pack(side="right", padx=16)

        tk.Frame(self.root, bg=RULE, height=1).pack(fill="x")
        body = tk.PanedWindow(self.root, orient="horizontal", bg=RULE, sashwidth=6, bd=0)
        body.pack(fill="both", expand=True)

        left = tk.Frame(body, bg=PAPER)
        self.numbers = tk.Text(left, width=4, font=self.code_font, bg=PAPER_DARK, fg=INK_SOFT, bd=0, padx=PAD, pady=PAD,
                               state="disabled", cursor="arrow", takefocus=0)
        self.numbers.pack(side="left", fill="y")
        self.code = tk.Text(left, font=self.code_font, bg=PAPER, fg=INK, insertbackground=ACCENT, bd=0, padx=16, pady=PAD,
                            undo=True, wrap="none", selectbackground="#f3c7a6", tabs=self.code_font.measure("    "))
        scroll = tk.Scrollbar(left, command=self.scroll_both)
        scroll.pack(side="right", fill="y")
        self.code.configure(yscrollcommand=lambda a, b: (scroll.set(a, b), self.numbers.yview_moveto(a)))
        self.code.pack(side="left", fill="both", expand=True)
        for name, color in CODE_COLORS.items():
            self.code.tag_configure(name, foreground=color)
        self.code.tag_configure("error_line", background="#fde2d4")
        body.add(left, stretch="always", minsize=360)

        right = tk.Frame(body, bg=OUT_BG)
        head = tk.Frame(right, bg=OUT_BG, padx=16, pady=PAD)
        head.pack(fill="x")
        tk.Label(head, text="Output", font=FONT_UI_BOLD, fg=OUT_FG, bg=OUT_BG).pack(side="left")
        clear = tk.Label(head, text="Clear", font=FONT_UI, fg=OUT_DIM, bg=OUT_BG, cursor="hand2")
        clear.pack(side="right")
        clear.bind("<Button-1>", lambda e: self.clear_output())
        self.output = tk.Text(right, font=self.code_font, bg=OUT_BG, fg=OUT_FG, bd=0, padx=16, pady=PAD, wrap="word",
                              state="disabled", insertbackground=OUT_FG)
        self.output.pack(fill="both", expand=True)
        self.output.tag_configure("err", foreground=OUT_ERR)
        self.output.tag_configure("dim", foreground=OUT_DIM)
        self.output.tag_configure("link", underline=True)
        self.output.tag_bind("link", "<Button-1>", self.jump_to_error)
        self.output.tag_bind("link", "<Enter>", lambda e: self.output.configure(cursor="hand2"))
        self.output.tag_bind("link", "<Leave>", lambda e: self.output.configure(cursor=""))
        typed = tk.Frame(right, bg=OUT_BG, padx=16, pady=PAD)
        typed.pack(fill="x")
        tk.Label(typed, text="Type here when the program asks for input, then press Enter", font=("Bahnschrift", 9),
                 fg=OUT_DIM, bg=OUT_BG).pack(anchor="w")
        self.input = tk.Entry(typed, font=self.code_font, bg="#2f2b26", fg=OUT_FG, insertbackground=OUT_FG, bd=0,
                              relief="flat", highlightthickness=1, highlightbackground="#4a443c", highlightcolor=ACCENT)
        self.input.pack(fill="x", ipady=4, pady=(4, 0))
        self.input.bind("<Return>", self.send_input)
        body.add(right, stretch="always", minsize=300)

        self.code.bind("<<Modified>>", self.on_change)
        self.code.bind("<KeyRelease>", lambda e: self.update_lines())
        self.code.bind("<Tab>", self.indent)
        self.code.bind("<Return>", self.new_line)
        for key in ("<Control-Return>", "<Control-r>"):
            self.root.bind(key, lambda e: (self.run(), "break")[1])
        self.root.bind("<Control-s>", lambda e: self.save_file())
        self.root.bind("<Control-o>", lambda e: self.open_file())
        self.root.bind("<Escape>", lambda e: self.stop())

    def button(self, parent, text, command, primary=False):
        bg, fg = (ACCENT, "#ffffff") if primary else (PAPER_DARK, INK)
        b = tk.Button(parent, text=text, command=command, font=FONT_UI_BOLD if primary else FONT_UI, bg=bg, fg=fg,
                      activebackground="#9a3412" if primary else RULE, activeforeground=fg, bd=0, padx=16, pady=4,
                      cursor="hand2", relief="flat")
        b.pack(side="left", padx=(0, PAD))
        return b

    def scroll_both(self, *args):
        self.code.yview(*args)
        self.numbers.yview(*args)

    def zoom(self, step):
        size = max(8, min(28, self.code_font.cget("size") + step))
        self.code_font.configure(size=size)
        self.settings["size"] = size

    # --- Editing ---
    def indent(self, _event):
        self.code.insert("insert", "    ")
        return "break"

    def new_line(self, _event):
        line = self.code.get("insert linestart", "insert")
        spaces = len(line) - len(line.lstrip(" "))
        stripped = line.strip()
        if re.match(r"^(func|if|elif|else|while|for|struct|enum|match|case|try|catch)\b", stripped):
            spaces += 4
        self.code.insert("insert", "\n" + " " * spaces)
        self.code.see("insert")
        self.update_lines()
        return "break"

    def on_change(self, _event=None):
        if self.code.edit_modified():
            self.color_code()
            self.update_lines()
            self.code.tag_remove("error_line", "1.0", "end")
            self.code.edit_modified(False)

    def update_lines(self):
        count = int(self.code.index("end-1c").split(".")[0])
        self.numbers.configure(state="normal")
        self.numbers.delete("1.0", "end")
        self.numbers.insert("1.0", "\n".join(str(i) for i in range(1, count + 1)))
        self.numbers.configure(state="disabled", width=max(3, len(str(count))))
        self.numbers.yview_moveto(self.code.yview()[0])

    def color_code(self):
        text = self.code.get("1.0", "end-1c")
        for name in CODE_COLORS:
            self.code.tag_remove(name, "1.0", "end")
        patterns = [
            ("comment", r"#[^\n]*"),
            ("string", r'f?"(?:\\.|[^"\\\n])*"?'),
            ("number", r"\b\d+(?:\.\d+)?\b"),
            ("keyword", r"\b(?:" + "|".join(KEYWORDS) + r")\b"),
            ("type", r"\b(?:" + "|".join(TYPES) + r")\b|\b[A-Z][A-Za-z0-9_]*\b"),
            ("func", r"\b[a-z_][A-Za-z0-9_]*(?=\()"),
        ]
        taken = []
        for name, pattern in patterns:
            for m in re.finditer(pattern, text):
                if any(a <= m.start() < b for a, b in taken):
                    continue
                if name in ("comment", "string"):
                    taken.append((m.start(), m.end()))
                self.code.tag_add(name, f"1.0+{m.start()}c", f"1.0+{m.end()}c")

    # --- Files and examples ---
    def new_code(self):
        self.code.delete("1.0", "end")
        self.code.insert("1.0", STARTER)
        self.file = None
        self.status.configure(text="")

    def open_file(self):
        path = filedialog.askopenfilename(filetypes=[("Jamo code", "*.jamo"), ("All files", "*.*")])
        if path:
            self.load(Path(path))
        return "break"

    def load(self, path):
        self.code.delete("1.0", "end")
        self.code.insert("1.0", path.read_text(encoding="utf-8"))
        self.file = path
        self.status.configure(text=path.name)
        self.code.mark_set("insert", "1.0")
        self.code.see("1.0")

    def save_file(self):
        path = self.file
        if path is None or path.is_relative_to(ROOT / "examples"):
            chosen = filedialog.asksaveasfilename(defaultextension=".jamo", filetypes=[("Jamo code", "*.jamo")])
            if not chosen:
                return "break"
            path = Path(chosen)
        path.write_text(self.code.get("1.0", "end-1c") + "\n", encoding="utf-8")
        self.file = path
        self.status.configure(text=f"Saved {path.name}")
        return "break"

    def show_examples(self):
        menu = tk.Menu(self.root, tearoff=0, font=FONT_UI, bg=PAPER, fg=INK, activebackground=ACCENT, activeforeground="#ffffff")
        found = sorted((ROOT / "examples").glob("*.jamo"))
        for path in found:
            first = path.read_text(encoding="utf-8").split("\n", 1)[0].lstrip("# ")
            label = first.split(":")[0] if ":" in first[:40] else path.stem.replace("_", " ").capitalize()
            menu.add_command(label=label, command=lambda p=path: self.load(p))
        if not found:
            menu.add_command(label="No examples folder found", state="disabled")
        x = self.root.winfo_pointerx()
        y = self.root.winfo_pointery()
        menu.tk_popup(x, y)

    def pick_jamo(self):
        path = filedialog.askopenfilename(title="Where is jamo.exe?", filetypes=[("jamo.exe", "jamo.exe")])
        if path:
            self.jamo = path
            self.settings["jamo"] = path
            self.say(f"Using {path}\n", "dim")

    # --- Running ---
    def run(self):
        if not self.jamo:
            self.pick_jamo()
            if not self.jamo:
                return
        self.stop()
        self.clear_output()
        self.code.tag_remove("error_line", "1.0", "end")
        source = self.code.get("1.0", "end-1c") + "\n"
        name = self.file.name if self.file else "playground.jamo"
        if self.file:
            # Files beside the program (other .jamo files it uses, data it reads) are copied along, so they're found
            for other in self.file.parent.iterdir():
                if other.is_file() and other.name != name and other.suffix.lower() in (".jamo", ".txt", ".json", ".wav", ".png", ".csv"):
                    shutil.copy(other, self.work / other.name)
        target = self.work / name
        target.write_text(source, encoding="utf-8")
        self.say(f"Running {name}...\n", "dim")
        self.run_button.configure(state="disabled")
        self.stop_button.configure(state="normal")
        threading.Thread(target=self.run_in_background, args=(target,), daemon=True).start()

    def run_in_background(self, target):
        built = subprocess.run([self.jamo, "build", str(target)], check=False, capture_output=True, text=True, cwd=self.work,
                               creationflags=subprocess.CREATE_NO_WINDOW)
        if built.stdout:
            self.lines.put(("out", built.stdout))
        if built.returncode != 0:
            self.lines.put(("err", built.stderr))
            self.lines.put(("done", None))
            return
        if built.stderr:
            self.lines.put(("err", built.stderr))
        exe = target.with_suffix(".exe")
        self.process = subprocess.Popen([str(exe)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                        cwd=self.work, text=True, encoding="utf-8", errors="replace", bufsize=1,
                                        creationflags=subprocess.CREATE_NO_WINDOW)
        errors = threading.Thread(target=self.read_stream, args=(self.process.stderr, "err"), daemon=True)
        errors.start()
        self.read_stream(self.process.stdout, "out")
        code = self.process.wait()
        errors.join(timeout=1)
        self.lines.put(("done", code))

    def read_stream(self, stream, kind):
        for line in iter(stream.readline, ""):
            self.lines.put((kind, line))

    def pump(self):
        try:
            while True:
                kind, text = self.lines.get_nowait()
                if kind == "done":
                    self.process = None
                    self.run_button.configure(state="normal")
                    self.stop_button.configure(state="disabled")
                    if text is not None:
                        self.say(f"\nFinished{'' if text == 0 else f' with code {text}'}.\n", "dim")
                elif kind == "err":
                    self.say_error(text)
                else:
                    self.say(text)
        except queue.Empty:
            pass
        self.root.after(30, self.pump)

    def send_input(self, _event):
        text = self.input.get()
        self.input.delete(0, "end")
        if self.process and self.process.stdin:
            self.say(text + "\n", "dim")
            try:
                self.process.stdin.write(text + "\n")
                self.process.stdin.flush()
            except OSError:
                pass

    def stop(self):
        if self.process and self.process.poll() is None:
            self.process.kill()
            self.say("\nStopped.\n", "dim")
        return "break"

    # --- Output ---
    def say(self, text, tag=None):
        self.output.configure(state="normal")
        self.output.insert("end", text, tag or ())
        self.output.see("end")
        self.output.configure(state="disabled")

    def say_error(self, text):
        for line in text.splitlines(keepends=True):
            m = ERROR_LINE.match(line)
            if m:
                self.say(line, ("err", "link"))
                self.mark_error(int(m.group(2)))
            else:
                self.say(line, "err")

    def mark_error(self, line):
        self.code.tag_add("error_line", f"{line}.0", f"{line}.end+1c")

    def jump_to_error(self, event):
        index = self.output.index(f"@{event.x},{event.y}")
        line_text = self.output.get(f"{index} linestart", f"{index} lineend")
        m = ERROR_LINE.match(line_text)
        if m:
            line = int(m.group(2))
            self.code.mark_set("insert", f"{line}.0")
            self.code.see(f"{line}.0")
            self.code.focus_set()

    def clear_output(self):
        self.output.configure(state="normal")
        self.output.delete("1.0", "end")
        self.output.configure(state="disabled")

    def close(self):
        self.stop()
        self.settings["geometry"] = self.root.geometry()
        self.settings["code"] = self.code.get("1.0", "end-1c")
        save_settings(self.settings)
        shutil.rmtree(self.work, ignore_errors=True)
        self.root.destroy()


def main():
    if sys.platform == "win32":
        try:
            import ctypes
            ctypes.windll.shcore.SetProcessDpiAwareness(1)
        except (AttributeError, OSError):
            pass
    root = tk.Tk()
    Playground(root)
    root.mainloop()


if __name__ == "__main__":
    main()
