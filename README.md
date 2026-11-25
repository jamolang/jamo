<p align="center"><img src="assets/jamo_logo_github.png" alt="Jamo" width="160"></p>

# Jamo

A small programming language for Windows. Its compiler writes real x86-64 `.exe` files by itself, with no interpreter, virtual machine, assembler or linker. Programs need nothing else to run, and memory is cleaned up for you.

**Jamo is in early development.** It has ints, floats, bools, strings, arrays, structs with methods, enums, generics, variables shared by every function, modules, files and typed input, and a `Map` that looks things up by any key. It can open windows, draw 2D graphics, play sound, show buttons, sliders and text boxes, draw 3D scenes and make web requests. It only runs on Windows. The end of the [guide](GUIDE.md) lists what it can't do.

## Install

Jamo comes as a small zip (about 210 KB) with `jamo.exe`, the guide and the examples. Get it from the Releases page.

1. Unzip the whole folder anywhere.
2. Double-click `install.cmd`. It copies `jamo.exe` into a folder of its own and adds that folder to your PATH, for your Windows user only, with no administrator needed.
3. Open a new terminal and try `jamo version`.

`uninstall.cmd` takes it all away again. Jamo is one file with nothing else to install, so you can also put `jamo.exe` in any folder on your PATH yourself.

`python release/make_release.py` makes the zip from a built `jamo.exe`. To build `jamo.exe` itself, see "Building Jamo itself" below.

## A first program in 5 minutes

Save this as `hello.jamo`:

```jamo
func add(a: int, b: int): int
    return a + b
end

func main()
    var total = add(2, 3)
    print(f"The total is {total}")
end
```

Then run it from a terminal in the same folder:

```
jamo run hello.jamo
```

```output
The total is 5
```

From there, [GUIDE.md](GUIDE.md) teaches the whole language in small steps with exercises, and [examples](examples) has twelve small programs to read and change, from a guessing game to Breakout and a spinning 3D scene.

## Commands

- `jamo run file.jamo` builds it and runs it. Anything after the file goes to the program.
- `jamo build file.jamo` writes `file.exe` beside the source.
- `jamo build --window file.jamo` writes a window program, with no console window behind it. That's the one to share for a game. If it stops with an error, the error shows in a message box.
- `jamo check file.jamo` finds mistakes without writing an `.exe`.
- `jamo new name` makes a folder with a program to start from, and `jamo new --game name` starts it from a small window game instead.
- `jamo version` and `jamo help` do what they say.

## VS Code

The [vscode](vscode) folder holds an extension with colors and red underlines. `python vscode/make_vsix.py` packs it into one file, and `code --install-extension vscode/jamo.vsix` installs it.

## Building Jamo itself

`jamo.exe` is written in Jamo, in [compiler](compiler), and it compiles itself. To build it from scratch you need g++ with C++20 support, since the first step uses the C++ compiler in [src](src), which is kept for exactly this.

```
build.bat
```

It builds three things in turn:

1. `jamo_cpp.exe`, the C++ compiler, from `src`
2. `jamo.exe`, the compiler written in Jamo, built by `jamo_cpp.exe`
3. `jamo.exe` again, built by itself, which must come out the same, byte for byte

Once you have a `jamo.exe`, it can build a new one by itself: `jamo build compiler/jamo.jamo` writes `compiler/jamo.exe`.

## Testing

You also need Python.

```
python tests/run_tests.py
```

## More

- [GUIDE.md](GUIDE.md): learn Jamo from zero
- [LANGUAGE.md](LANGUAGE.md): the exact rules of the language
- [DECISIONS.md](DECISIONS.md): why things are the way they are
- [CHANGELOG.md](CHANGELOG.md): what changed
- [runtime](runtime): the runtime's source. `python runtime/make_runtime.py` checks every instruction with GNU as and rewrites `src/runtime.cpp` and `compiler/runtime.jamo`
- [LICENSE](LICENSE): MIT
