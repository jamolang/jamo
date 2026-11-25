# Changelog

What changed in each version of Jamo.

## Not released yet

- A mistake inside a generic copy also points at the line that asked for it, like `Map<float, int>.set doesn't work with these types, because of the mistake above.`, so a mistake inside a library says which line of the program caused it.
- A generic call given an input that gives back nothing says so, instead of saying it can't work out the type.
- `math.jamo` and `time.jamo` keep their state in variables outside functions, instead of a trick with the `.exe`'s header. Nothing changes for programs.
- Review, second pass: 3,000 random programs checked against Python and 3,000 broken programs, with no crashes and the two compilers always agreeing. See REVIEW.md.

## 0.2.0

- `jamo watch file.jamo` runs a program, then builds and runs it again each time one of its files is saved. A mistake shows straight away, and the last version that worked keeps running until it's fixed.
- Safe versions of things that stop a program: `read_file_or` and `try_write_file` in `files.jamo`, and `int_or` and `float_or` in `text.jamo`. `read_file_or` reads big files in parts, and gives the fallback for a missing file, a folder or a file in use.
- List tools for every kind of array: `has_item`, `find_item`, `count_item`, `reversed`, `swap_items` and `last_item` in `list.jamo`, and `pick` and `shuffle` in `math.jamo`.
- A Snake example, using `grid`, `timer` and `pick`.
- The prime finder says "Found 1 prime." instead of "1 primes".
- The VS Code extension comes as `jamo.vsix` on the Releases page.

## 0.1.0

### Game helpers

- `vec.jamo`: `Vec2` for places, speeds and directions, with `plus`, `minus`, `times`, `dot`, `length`, `distance`, `normalized`, `angle`, `rotated`, `lerp_to` and `limited`, and `vec2_from_angle`.
- `grid.jamo`: `Grid<T>`, a board of any type, with `get`, `set`, `inside`, `get_or`, `fill`, `count`, `copy` and `neighbours`. Going off the edge stops with an error that says the grid's size.
- `timer.jamo`: `Timer`, which fires every so many seconds, and `Cooldown`, which needs a rest between uses. Each has an `_at` version for a game's own clock.
- The guide has a new section 27 on them, and the README is rewritten for people new to programming.

### Variables outside functions

- `var score = 0` at the top level of a file makes a variable every function in the program can use and change. Any value works, including function calls.
- They're set up before `main`: each file's after the files it uses, then top to bottom. Using one before it's set up, directly or through a function, is a compile error that says which one to move.
- The memory cleaner keeps everything they hold, in every cleaning mode.
- A run error while they're being set up says "called from setting up the variables outside functions".
- 12 new error tests, a test with 20,000 rounds of garbage around them, and a three-file test of the order.

### Generics

- Functions and structs can take types: `func first<T>(items: [T]): T`, `struct Pair<A, B>`, and methods like `func Pair<A, B>.swap(): Pair<B, A>`.
- A call works out the types from its inputs, and they can be given in `< >` when it can't: `new_map<string, int>()`.
- Each set of types gets its own copy, checked like normal code, so generics run as fast as functions written by hand, and the types are still checked.
- `map.jamo` has `Map<K, V>`, which maps ints, strings, bools or enums to anything, with `set`, `get`, `get_or`, `has`, `remove`, `size`, `key_list` and `value_list`. It's checked against Python on 3,000 random steps.
- `hash_of(x)` gives a number from an int, string, bool or enum, never below 0, for hash tables.
- 25 new error messages, each tested, like "Jamo can't work out T for make from its inputs" with a hint to give the types.
- The guide has a new section 26 on generics and `Map`.

### Faster loops

- Variables held in registers are changed right where they are: `total += i` is one `add`, `i += 1` is one `inc`, and `var x = 0` puts the number straight in the register, instead of each going through `rax` and back.
- `if` and `while` compare a register variable straight against another one, a number or a simple value.
- `for` and `while` loops check their condition at the bottom, so each round takes one jump instead of two. `continue` still goes through the count and the check.
- An `if` with nothing after its last part no longer jumps to the very next instruction.
- `x % 3` multiplies by 3 directly instead of loading it into a register first.
- Benchmarks: loops 0.084 s to 0.067 s, array math 0.043 s to 0.036 s. Recursion and the particles stay about the same. `--no-optimize` keeps the old code.

### Faster game code

- Float math stays in float registers through a whole expression, instead of moving each number through a normal register for every operation. `+=` and the others on a float field work straight on the field in memory. A simple left side, like a variable, is read after the right side only when the right side calls nothing, so nothing can change it. The particle benchmark went from 0.038 s to 0.033 s, and the `min`/`max` version gains about as much.
- Small functions and methods that can never stop with an error are written into their callers instead of called, including ones that call other such functions. A run error lists the calls that led to it, so only functions that can't stop are written in place, and every error message stays the same. The particle benchmark using `min` and `max` went from 0.037 s to 0.030 s.
- Changing a field of a struct held by a variable, like `self.x += self.vx`, keeps the object in a register, with nothing pushed on the stack. When the value calls a function, the old order is kept, so nothing can change what it gives.
- Reading such a field next to an operator loads it straight into a register.
- `if` and `while` on float comparisons jump straight from the compare, giving the same answers for NaN as before.
- A game-like benchmark (5,000 particles, 1,000 frames, structs, methods and floats) is in the suite: 0.041 s before, 0.037 s now, against 1.0 s in Python and 0.011 s in C++ at -O2.

### The standard library, filled in

- `list.jamo`: `contains`, `index_of`, `insert` and `remove`, for int and string arrays (`contains_int`, `insert_string` and so on).
- `files.jamo`: folders, paths, copying, moving, deleting, file sizes and listing a folder, with names in any language.
- `json.jamo`: reads and writes JSON, with pretty output for files people may read. A mistake gives an error with its line instead of stopping. Checked against Python's own JSON reader on 544 texts.
- `term.jamo`: colors, bold, clearing the screen, padding, tables, progress bars and numbered menus.
- `log.jamo`: messages with the time and a level, to the screen and a file that's added to.
- `test.jamo`: checks for your own code, with a summary, ending with exit code 1 when any fail.
- `lerp` in `math.jamo`, and `sleep_ms` and `time_ms` in `time.jamo`.
- The guide's section 25 shows them all.

### Review phase, first pass

- A download: `release/make_release.py` makes a 210 KB zip with `jamo.exe`, the guide, the language rules, the examples, and `install.cmd` and `uninstall.cmd`, which put `jamo.exe` on your PATH and take it off again, for your Windows user only.
- Fixed: since the switch-over, the test suite had been testing the C++ compiler by default instead of `jamo.exe`. It tests `jamo.exe` again, and all of it passes.
- The guide has two new sections: "Making a whole game", with a complete game, the ideas that hold a game together, and a table of every library type to use in your own functions, and "Sharing what you make", with `--window` and what Windows shows for a downloaded `.exe`. It also says plainly that there are no variables outside functions, and which samples are run and which are only compiled.
- `jamo new --game name` makes a folder with a small window game that already works: a square moved with the arrow keys, catching coins. `jamo new` with a name starting with `--` shows how to use it, instead of making a folder by that name.
- `jamo build --window game.jamo` makes a window program, with no console window behind it, for sharing games. With no console to print to, a run error shows in a message box, with the same words as on the console. Only window programs import `LoadLibraryA` and `GetProcAddress`, which they use to find the message box.
- `is_int(text)` and `is_float(text)` in `text.jamo` say whether `int(text)` and `float(text)` would work, so a program can check what someone typed instead of stopping. They match the conversions exactly, up to the edges of 64 bits and of floats.
- Error messages put "a" or "an" before a type: "this value is a string", "The input x must be an int".
- The links leak test allows the same small handle wobble as the web tests. Windows' own networking threads move it by up to 4, the same after 1,500 links as after 300.

### Phase 6

- Phase 6 is done. On a brand-new Windows user with nothing but Windows on the PATH, the Jamo-written `jamo.exe` ran every console sample in the guide, built the graphics, sound and internet samples and every example into the same files as on the main account, and built itself twice into the same file.
- The compiler compiles itself under `--gc-test`, cleaning at every allocation: 117 minutes, and the same compiler as the normal build, byte for byte.
- Faster programs, with the language unchanged (in both compilers, so they still make the same bytes):
  - `items[i]` and `items[i] = x` check the index and reach the item right in the program's code, calling the runtime only to stop with the error.
  - `items.add(x)` puts the item straight in when there's room, and only calls the runtime to grow.
  - A chain of `+` on text, like `"item " + string(i) + " done"`, and f-strings with 2 or more parts are made in one go, instead of a new string at every step.
  - Comparing text answers at once when both sides are the same string.
  - Comparisons, `+`, `-`, `+=` and `-=` with an int that fits in 32 bits use it straight from the instruction.
  - The cleaner keeps its table of blocks between cleans while it's 2 MB or less, so a program that cleans often doesn't ask Windows for new memory each time.
  - Jumps and returns that can never run are left out, and small stack moves use the short instruction.
  - The benchmarks: array math 0.122 s to 0.038 s, string joining 0.281 s to 0.143 s, loops 0.093 s to 0.081 s.
- The compiler written in Jamo only cuts a file into lines when an error shows one, and a whole build of it went from 0.134 s to 0.092 s.
- `jamo build` tries again for up to a second when Windows holds the old `.exe` for a moment, like while scanning it for viruses, before saying it can't write it.
- The speaker leak test opens and closes the speakers 2,000 times and checks that memory's low points stay level, since memory goes up and down as the cleaner runs. A planted real leak still fails it.
- Switched over: `jamo.exe` is now the compiler written in Jamo. `build.bat` builds the C++ compiler as `jamo_cpp.exe`, uses it to build `jamo.exe`, then has `jamo.exe` build itself and checks the two are the same file. The C++ compiler stays in `src`, so Jamo can always be rebuilt from scratch.
- The web tests allow the handle count to move by 4 instead of 2. Windows' own pool moves it by about 3 either way, the same after 2,000 requests as after 8,000, so it was never a leak.
- Step 1: the lexer written in Jamo, in `compiler/lexer.jamo`, gives exactly the C++ lexer's tokens and errors.
- `jamo tokens file.jamo` prints the tokens, for checking the Jamo-written lexer. It's for developing Jamo.
- Step 2: the parser written in Jamo, in `compiler/parser.jamo` and `compiler/ast.jamo`, gives exactly the C++ parser's tree and errors. `jamo ast file.jamo` prints that tree.
- Step 3: the checker written in Jamo, in `compiler/checker.jamo`, with `compiler/modules.jamo` for loading every file a program uses and `compiler/errors.jamo` for error messages, gives exactly the C++ checker's errors, warnings and checked tree. `jamo checked file.jamo` prints them.
- Step 4: the code generator written in Jamo, in `compiler/codegen.jamo`, `compiler/assembler.jamo` and `compiler/runtime.jamo` (made from the runtime's source by `make_runtime.py`), makes exactly the C++ one's machine code, strings and imports, byte for byte. `jamo code flags file.jamo` prints them.
- Step 5: the PE writer written in Jamo, in `compiler/pe.jamo`, makes exactly the C++ one's `.exe` files, byte for byte. `compiler/exe.jamo` builds a program with the whole compiler written in Jamo, and it can build itself: the C++ build of it, its own build and that build's build are the same file.
- Step 6: the `jamo` command written in Jamo, in `compiler/jamo.jamo`, with `build`, `run`, `check`, `new`, `version` and `help`. It prints exactly what the C++ `jamo.exe` prints, and every test passes with it. The self-compile test passes: stage 2 and stage 3 are the same file, byte for byte, and so is stage 1 from the C++ compiler.
- Programs that make lots of short-lived text or arrays run faster: `text[i]` no longer makes a new string, empty arrays use one block instead of two, and memory is cleaned less often. The Jamo-written checker went from 0.24 s to 0.14 s on its own source.

### After Phase 5

- `const NAME = value` at the top level, for values known before the program runs.
- `+=` joins text, like `word += "s"`.
- Dividing by a constant of 2 or more uses a multiply instead of a slow division, and the line number is only noted when a division by zero actually happens. The loop benchmark got about 1.7x faster.
- `dict.jamo` (Dict and TextDict), `builder.jamo` (TextBuilder) and `bytes.jamo` (Bytes), with the self-hosted compiler in mind.
- Each function keeps its busiest variables in registers, a few percent faster. `--no-optimize` turns it off.

### Phase 5

- Five new examples: bouncing balls with a sound on each bounce, Breakout, a sign-up form, a spinning 3D scene, and a program that fetches a web page.
- ui: moving the keyboard onto a list no longer chooses its first item, and sliders show whole numbers without .0.
- net: links between programs for multiplayer, with `listen`, `listen_here`, `connect`, `new_links()`, `send()` and `messages()`. Each message arrives whole.
- net: background requests with `start_get` and `start_post`, checked each frame with `done()`, and `cancel()`.
- `text.jamo`: `split`, `find`, `contains`, `starts_with`, `ends_with`, `slice`, `replace`, `trim`, `upper`, `lower`, `join` and `repeat`, plus `sort_ints`, `sort_strings`, `reverse_ints` and `reverse_strings`.
- `time.jamo`: `seconds_since_start()`, `sleep(seconds)`, and `now()` for the date and time of day.
- `math.jamo`: square roots, angles, powers, `floor`, `ceil` and `round`, `min`, `max` and `clamp` for floats and ints, `pi()`, and random numbers with `random_int`, `random_float` and `random_seed`.
- An `extern` can be declared again with exactly the same DLL, inputs and answer, so a program can declare `sqrt` or `Sleep` while using a library that has it. Ones that differ give an error that says how.
- `net.jamo`: web requests with `get` and `post` over HTTP and HTTPS, with status codes, timeouts, and clear errors that don't stop the program.
- `memory_text(address, bytes)` makes text from bytes in memory, for libraries.
- `g3d.jamo`: 3D with a camera, depth, textures, sunlight, 4 lamps, fog, cubes, spheres, planes, your own meshes and OBJ model files. 2D drawing goes on top.
- `ui.jamo`: labels, panels, buttons, checkboxes, sliders, lists and text boxes, with Tab between them, a light and a dark theme, and layouts that follow the window's size.
- window: `typed()`, `key_repeated`, `mouse_pressed`, and the delete, home and end keys.
- `sound.jamo`: plays WAV files and made-up tones, up to 64 at once, with volume for each sound and for everything.
- gfx: small text no longer shows stray marks from the letter below.
- `gfx.jamo`: 2D drawing with OpenGL 3.3, with rectangles, circles, lines, text, PNG pictures and pixel reading. `png.jamo` reads PNG files by itself.
- `extern "OpenGL"` finds OpenGL's functions the first time they're called, and `f32` passes 32-bit floats. `byte(text, i)` gives a byte's number.
- `window.jamo`, built into jamo.exe: open a window, read the keyboard and mouse, and keep a steady frame rate.
- `fail(message)` stops with a run error, for libraries.
- `extern` calls Windows functions from any DLL, with `callback(f)`, `wide(text)`, `memory(bytes)` and `peek_`/`poke_` for libraries.

### Phase 4

- The `\r` escape, the character at the end of a Windows line.
- `jamo check`, `jamo new`, `jamo version` and `jamo help`.
- Several syntax mistakes show in one run, with warnings for unused variables and code that can never run.
- Run errors show the function calls that led there.
- Programs work out constants, leave out code that can't run, and jump straight on comparisons. `--no-optimize` turns this off, for testing.
- A VS Code extension, seven tested examples including a Lunar Lander game, and a guide where every sample is run by the tests.

### Memory Cleaner

- Programs free unused strings, arrays and structs by themselves, and stay at a steady memory size.
- `--gc-off`, `--gc-test` and `--gc-check` test the cleaner. They aren't for normal programs.
- Making small strings, arrays and structs is much faster, because Jamo now hands out blocks of up to 512 bytes itself instead of asking Windows for each one.

### Phase 3

- `text[i]`, and lines that carry on after an operator, a comma or an open bracket.
- Arrays: `[1, 2]`, `add`, `pop`, `len`, `for item in items`, and clear errors for going outside them.
- Structs with fields and methods. Arrays and structs are shared when assigned.
- Enums and `match`.
- Modules with `use`.
- `input()`, `args()`, `read_file`, `write_file`, `exists`, `int(text)` and `float(text)`.

### Phase 2

- Strings: `+`, `==`, `!=`, `len`, `string(x)` and escapes. UTF-8 passes through unchanged.
- F-strings, like `f"Score: {score}"`.
- Floats, with `int(x)` and `float(x)`. Printing rounds exactly, and mixing ints and floats is a compile error.
- `and`, `or`, `not` and `elif`.
- `for i in 0..10`, `break`, `continue`, `%`, `+=`, `-=`, `*=` and `/=`.
- Run errors now show the file and line. Running out of memory stops with a clear error.

### Phase 1

- `jamo build` and `jamo run`.
- `int` (64-bit) and `bool`, variables, functions, recursion, `if`/`else`, `while`, math, comparisons, `print` and comments.
- Clear errors with the line, a marker and suggestions like "Did you mean score?".
- Real x86-64 machine code in real Windows `.exe` files, written with no assembler or linker.
