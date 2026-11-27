# Jamo review, first pass

An honest look at why people would or wouldn't use Jamo, before friends try it. Written on 2026-11-04.

**How this was done, and its limits.** This review was done by Claude, who also wrote most of Jamo. So it is not a real new user's experience: I already knew the language, and I can't feel confusion the way a beginner does. Everything below is marked as one of:

- **Measured:** timed or counted on this PC (Windows 11, the Jamo-written `jamo.exe`).
- **Published:** a figure from the other projects' own downloads and docs, not measured here, and only roughly right.
- **Opinion:** my judgment, not evidence.

Real user opinions come from the Friends phase. None are made up here.

## 1. Trying Jamo as a new user

### Install

- **Measured:** there is no download yet. The README says to build Jamo yourself, which needs g++ (WinLibs) installed first. That's hundreds of MB and an unfamiliar tool, before a beginner writes one line.
- **Measured:** once someone has `jamo.exe`, it's one 888 KB file that needs nothing else. The clean-account run proved that: a brand-new Windows user with nothing on the PATH built and ran every guide sample.
- **Opinion:** with a ready-made download, install would take a couple of minutes: download one file, put it in a folder, add that folder to the PATH. Adding to the PATH is still a step many beginners don't know how to do.

### First program

- **Measured:** `jamo run hello.jamo` takes 41 ms from typing to "Hello!". Building takes 9 ms.
- **Measured:** the guide's first program worked exactly as written.

### Three new programs, written from the guide alone

I wrote three programs without starting from the examples, noting every snag:

| Program | Result | Snags |
|---|---|---|
| Hangman (console) | Worked first try. 21 KB `.exe`, built in 30 ms | When input runs out, `input()` gives `""`, the same as an empty line, so the game loops forever instead of ending |
| Dodge (window game, with sound) | Built first try. 66 KB `.exe`, built in 20 ms | The guide never names the types `Window` and `Gfx`, so a beginner can't write a function that takes the screen without reading the library source. Collisions had to be written by hand. Every window game also opens a black console window behind it |
| Top words (file tool) | Worked first try, 23 KB, read the whole guide in 41 ms | Only arrays of ints and strings can be sorted, so sorting the counts took a hand-written sort (9 lines) |

### Confusing moments, bad errors and missing tools

- **Install needs a C++ compiler.** The biggest barrier by far (measured above).
- **Every window program shows a console window too.** Jamo only makes console programs. That's fine for learning, but a shared game looks unfinished.
- **The guide never names the library types** (`Window`, `Gfx`, `Sound`, `Ui`, `World`...), though you need them to pass things to your own functions.
- **No global variables.** Only constants can go outside functions, and the guide doesn't say so. Game state has to be passed around in a struct, which a beginner has to work out alone.
- **A bad number stops the program.** `int(input())` on "abc" ends the program, and there's no way to check the text first. The guide says so honestly, but a guessing game can't guard against a typo.
- **An error message is missing a word:** "score holds an int, but this value is string." should say "a string".
- **The end of input looks like an empty line,** so a program can't tell that input has run out.
- **No guide section on sharing a game,** or on the warning Windows shows for programs downloaded from the internet.
- **A small mismatch in the guide:** the multiplayer example declares `Sleep` itself, though `time.jamo` already has `sleep()`.
- **What went well (measured):** every guide sample I tried works as written, errors point at the exact spot, and the three programs worked or built on the first try.

## 2. Compared with other options

### Measured on this PC

A program that prints "Hello!", and `fib(35)` (about 30 million function calls):

| | Build "Hello!" | What you share | "Hello!" starts in | fib(35) |
|---|---|---|---|---|
| **Jamo** | 9 ms | 15 KB `.exe`, runs alone | 7 ms | 0.05 s |
| **Go** 1.27 | 114 ms (cached) | 2,434 KB `.exe`, runs alone | 9 ms | 0.04 s |
| **C#** (.NET 10) | 2.2 s | 70 MB standalone `.exe` (10 s to make), or needs .NET installed | 47 ms | 0.09 s |
| **JavaScript** (Node 24) | no build | needs Node installed | 41 ms | 0.11 s |
| **Lua** 5.4 | no build | needs Lua installed | 8 ms | 0.39 s |
| **Python** 3.13 | 9.8 s to pack an `.exe` with PyInstaller | 8.4 MB packed `.exe`, or needs Python | 23 ms (466 ms packed) | 0.90 s |

Jamo's own benchmarks against Python (measured, from the test suite): loops 75x faster, array math 66x, recursion 18x, text joining 4.3x.

### Not measured here

- **Lua with LÖVE** (published): a small download (a few MB) and one of the easiest ways to make 2D games, with years of tutorials. Games are shared as a `.love` file, which needs LÖVE, or packed with LÖVE's own `.exe`.
- **C# with Unity** (published): a download of several GB, a big editor, and the most game tools, tutorials and jobs of anything here. A small build is tens of MB.

### By topic (opinions, unless marked)

| | Jamo | Python | Lua + LÖVE | C# + Unity | Go | JavaScript |
|---|---|---|---|---|---|---|
| Install | Hard today: build it yourself. One small file once there's a download | Easy installer | Easy | Very large | Easy installer | Easy installer |
| Learning | Small and readable, with a guide that checks every sample. Static types catch mistakes early | Very easy, huge amount of help | Easy | Hard: language plus a big editor | Medium | Easy to start, many traps |
| Speed (measured) | Near Go on these tests | Slowest here | Middle | Fast | Fastest here | Fast |
| Libraries | Its own: window, 2D, 3D, sound, UI, web, links. No packages | Huge | Game-focused | Huge | Large | Huge |
| Game making | Possible, all built in, but few helpers: no sprite sheets, collisions or scenes | Needs pygame or similar | Made for it | Made for it | Needs a library | Great in a browser |
| Sharing programs (measured where shown) | Best here: a tiny `.exe` that runs alone. But a console window shows, and Windows warns about unknown `.exe` files | Hard (8 MB+, slow start) | Medium | Large builds | Good (2 MB) | Easy on the web |
| Community help | None yet | Huge | Good | Huge | Large | Huge |

## 3. The verdict

### What's good, and why it matters to users

- **Tiny programs that run anywhere on Windows** (measured: 15 to 66 KB, start in 7 ms, need nothing installed). Sending a friend a game is just sending one small file.
- **Fast** (measured: near Go, 18 to 75x faster than Python on the suite's benchmarks), so a beginner's slow code is still fast enough for games.
- **Games, sound, UI, 3D and the web with nothing to install.** Every library comes inside `jamo.exe`, so there's no package setup to get wrong.
- **Mistakes are caught before running,** with the exact spot marked, and a run error says which calls led there.
- **Small and consistent,** with a guide where every sample is tested.
- **Trustworthy under the hood** (measured): 521 tests, a compiler that builds itself identically, and a clean-account run.

### What's bad, and why it would make users leave

- **No download.** Almost everyone would stop at "install g++ first".
- **Window games open a console window too.** It makes shared games look unfinished.
- **No community and no packages.** When a beginner gets stuck, there's nobody to ask and no answers to search.
- **Missing everyday tools:** no globals, no way to handle bad input or missing files yourself, no sorting your own data, and no game helpers like collisions.
- **Windows only,** and shared `.exe` files from an unknown maker get a SmartScreen warning when downloaded (published Windows behavior, not tested here).

### Who Jamo is really for

- **For:** beginners and hobbyists on Windows who want to make small games and tools, and share them as a tiny `.exe` with friends. Also anyone curious how a language and compiler work, since the whole thing is small and readable.
- **Should use something else:** anyone who needs Mac, Linux, phones or the web (use JavaScript or Unity). Big or commercial games (Unity). Anyone who needs lots of libraries or help from others (Python). Fast 2D games with lots of tutorials (LÖVE).

### Why pick Jamo, in two sentences

Jamo turns a short, readable program into a tiny Windows `.exe` that starts instantly and runs anywhere, with windows, sound, 3D and the web built in and nothing else to install. It's for making small games and tools you can hand to a friend as one file.

## 4. Improvements to choose from

Ranked by how much users would notice them. Work and risk are my estimates (opinion).

| # | Idea | Users notice | Work | Risk | Changes the language? |
|---|---|---|---|---|---|
| 1 | **A ready-made download:** a zip with `jamo.exe` and a small `install` script that copies it and adds it to the PATH | Hugely | Small | Low | No |
| 2 | **Window programs without the console:** for example `jamo build --window game.jamo` | A lot, when sharing | Small | Low | Adds a build option |
| 3 | **A "Make a game" guide chapter:** a whole small game step by step, the library types named (`Window`, `Gfx`...), how to keep game state in a struct, and sharing your game | A lot | Medium | Low | No |
| 4 | **`jamo new name --game`:** starts from a window game that already moves a player | A lot | Small | Low | Adds a command option |
| 5 | **Checking text before turning it into a number:** `is_int(text)` and `is_float(text)` in `text.jamo` | Some | Small | Low | No (a library) |
| 6 | **Game helpers:** a `game.jamo` with rectangle overlap, 2D points, timers and sprite sheets | Some | Medium | Low | No (a library) |
| 7 | **Error wording:** "this value is a string", and a check of every message for missing words | A little | Small | Low | No |
| 8 | **A ready-made VS Code extension** in the download | Some | Small | Low | No |
| 9 | **Telling the end of input apart from an empty line** | A little | Small | Low | Yes (a new built-in) |
| 10 | **Global variables** | Some | Large | Medium | Yes |
| 11 | **Handling errors yourself**, like a file that can't be read | Some | Large | High | Yes |
| 12 | **Sorting your own data** | A little | Large | Medium | Yes (needs functions as values) |

**My recommendation:** 1, 2, 3, 4, 5 and 7 now. They fix the biggest reasons someone would give up, without changing the language. 6 and 8 would make a good second round. 9 to 12 change the language, so they need your OK, and they're probably better left for after the Friends phase shows what people actually miss.

## 5. What could put people off

- **Antivirus** (measured): Windows Defender scans of built programs and `jamo.exe` come back clean in the test suite. SmartScreen warnings on downloaded `.exe` files from an unknown maker are normal Windows behavior (published). Avoiding them needs a paid code-signing certificate. Not tested here.
- **Slow start-up** (measured): none. 7 ms for a program, 41 ms for `jamo run`.
- **Large downloads** (measured): none. `jamo.exe` is 888 KB.
- **Crashes** (measured): the tests feed the compiler thousands of garbage files, and it never crashes.
- **Unclear limits:** the guide's "What Jamo can't do yet" is honest, but leaves out that there are no global variables, and that a window program always shows a console.
- **Claims the evidence doesn't support:** I checked the README's claims. "Programs need nothing else to run" is true (the clean-account run). "Memory is cleaned up for you" is true, and tested in every cleaner mode. The guide's "every program here is run by Jamo's tests" is true for the runnable samples, while window, sound and internet samples are only built and checked. That's worth saying in the guide.

## 6. What was done, and the new-user test again

The user chose improvements 1 to 6. All are done, each with tests, and the whole test suite passes with the Jamo-written `jamo.exe` (537 checks, on 2026-11-04).

| # | Improvement | Done as |
|---|---|---|
| 1 | A ready-made download | A 210 KB zip from `release/make_release.py`, with `install.cmd` and `uninstall.cmd`. They need no administrator and change only the user's own PATH |
| 2 | Window programs without the console | `jamo build --window`. A run error shows in a message box with the same words as on the console |
| 3 | A "Make a game" guide chapter | Guide sections 23 ("Making a whole game", with a complete game and a table of every library type) and 24 ("Sharing what you make") |
| 4 | Starting from a game | `jamo new --game name` |
| 5 | Checking text before turning it into a number | `is_int(text)` and `is_float(text)` in `text.jamo`, matching `int()` and `float()` exactly |
| 6 | Error wording | 35 messages now say "a" or "an" before a type |

Found along the way: since the switch-over to the Jamo-written compiler, the test suite had been testing the C++ compiler by default. It was fixed, and the whole suite passes with `jamo.exe`.

### The new-user test, repeated (measured, 2026-11-05)

On a brand-new Windows user with no `jamo`, g++ or Python on the PATH, following only the zip's README:

| Step | Before | After |
|---|---|---|
| Getting Jamo | Build it from source, which needs g++ installed first | Unzip (38 ms) and double-click `install.cmd` (1.8 s) |
| `jamo` works in a new terminal | (only after a manual PATH change) | Yes, found through the PATH, 58 ms |
| First program | 41 ms to build and run | `jamo new hello` and `jamo run`, 64 ms |
| Starting a game | Write it from a blank file | `jamo new --game`, a 61 KB game that checks with no warnings, 59 ms with the build |
| Sharing the game | A console window behind it | `jamo build --window` makes a window program |
| A typed "lots" where a number was wanted | Stops the program | `is_int` lets the program say "lots isn't a number" |
| Uninstalling | (nothing to uninstall) | `uninstall.cmd`, 1.8 s, leaving no folder and no PATH entry |

The machine times are real. How long a person takes, reading the README and clicking, wasn't measured, and should come from the Friends phase. My guess (opinion) is a couple of minutes from download to a running game, down from "can't start" for anyone without g++.

### Still open, for the Friends phase to judge

- No community and no packages.
- No variables outside functions, no handling errors yourself, and no sorting your own data. These are language changes, so they wait for the user's approval and for what friends actually miss.
- SmartScreen's warning on a downloaded `.exe` can only be avoided with a paid code-signing certificate.
- The end of input still looks like an empty line.

# Jamo review, second pass

A look at the compiler's own code, after a day that added generics, variables outside functions, `jamo watch` and new libraries. Written on 2026-11-25, while the user was away. The same caveat applies: Claude wrote most of this code, so this finds what tools and careful reading can find, not what a fresh pair of eyes would.

## How it was checked

- **Random programs with a Python twin** (measured): `tests/fuzz/fuzz_programs.py` writes programs that mix variables outside functions, a generic `Box<T>` and `pick<T>`, loops with `continue` and `break`, `+=` and `-=`, and calls that change a variable outside functions in the middle of a sum. Each one is built by both compilers and with `--no-optimize`. 3,000 programs: every answer matched Python, the two compilers made the same `.exe` every time, and `--no-optimize` always agreed.
- **Broken programs** (measured): `fuzz_checker.py` breaks the generics, globals and library tests at random (lines deleted, copied or swapped, types and names changed, brackets removed). 3,000 broken programs: neither compiler crashed, and the checker written in Jamo reported exactly what the C++ one did every time.
- **Reading the new code** for cases the tests don't reach.

## What was found and fixed

| Found | Fixed |
|---|---|
| A mistake inside a generic copy pointed only inside the copy. With a library generic, like `new_map<float, int>()` (floats can't be keys), the only error was in `map.jamo`, saying nothing about which line of the program caused it | Such a mistake now also points at the program's own line: `Map<float, int>.set doesn't work with these types, because of the mistake above.` A copy made by another copy follows the chain back to the program's code. A new test pins it |
| `math.jamo` and `time.jamo` kept their state in the `.exe`'s DOS header, unlocked with `VirtualProtect`, a trick that breaks if the compiler ever writes those bytes. It was there because Jamo had no variables outside functions | They use variables outside functions now. Same random numbers for a seed, and asking the time still needs no new memory |
| Two library comments said Jamo has no program-wide variables | Updated |
| A generic call given an input that gives back nothing, like `first(nothing())`, said it couldn't work out the type | It says the input gives back no value, like any other call. A new test pins it |
| The C++ check for a struct holding itself renamed its inputs for no reason | Tidied |
| (Found while building today) The C++ checker held a reference into the type table while checking a struct literal's values, which can add types and move the table | It copies the fields first |
| (Found while building today) The last variable outside functions sat one word outside what the memory cleaner looks at, so its value could be freed | Fixed with a spare word, and a strict-cleaner test with 20,000 rounds of garbage guards it |

## Still open

- Errors inside a generic copy can repeat once for each copy that has them, though they point at the same line.
- A generic that's never used isn't checked, so a typo in one only shows when something uses it, like in C++.
- `f(a < b, c > (d))` reads as a call with types. It's documented, and the error gives a hint, but it can surprise.


# Jamo review, third pass

Written on 2026-11-26, after a day of work aimed at what the first pass said held Jamo back for real use. The same honesty rules apply: Claude wrote this code too, so this isn't a new user's view.

## What the first passes asked for, and where it stands

| Gap | Now |
|---|---|
| No way to handle errors | `try` and `catch`, in both compilers. Errors at any depth of calls reach the catch, `return`, `break` and `continue` close the trys they leave, and a catch restores the registers that functions in between changed. 11 bugs planted in a copy of the compiler were all caught by the new tests, and the random-program and broken-program fuzzers found no problems (600 and 800 runs) |
| Only int and string arrays could be sorted | `sort_by`, `sort_by_float` and `sort_by_text` sort any array by keys, keeping ties in order, checked against Python's `sorted` |
| Text tools only knew English letters | `upper` and `lower` handle accented, Greek and Cyrillic letters, and `letter_count`, `letters` and `reverse_letters` count whole letters. Checked against Python's `str.upper` and `str.lower` |
| No packages | `jamo get someone/project` fetches a shared library from GitHub, refusing file names that could leave its folder and saving nothing unless every file arrives |
| Hard to see what Jamo can do | Two new examples with GIFs at the top of the README: Sky Hopper, a platformer, and Beat Run, a rhythm game whose music is mixed in code. Both were proven beatable by solvers, and Beat Run's timing windows measured: at least 50 ms either side of every jump, most 142 ms |
| Speed claims weren't easy to check | `bench/run_bench.py` times five programs in Jamo, C and Python, checks all three print the same, and draws the chart in the README. Jamo is 4 to 76 times faster than Python, and 1.2 to 4 times slower than C at number crunching |
| Trying Jamo meant setting up an editor | Jamo Playground, a small editor with Run, typed input, clickable errors and the examples one click away. The VS Code extension gained hover, go to definition (into the built-in libraries too), an outline and suggestions |
| Small wording | Index errors say "1 item" and "1 byte" |

## Still open

- Functions can be passed as values, and written in place with `=>`, keeping the values of the variables around them (closures, added the next day). One written in place holds a single expression.
- Printing to the console now goes through `WriteConsoleW`, and `input()` reads through `ReadConsoleW`, so printing and typing work in any language.
- Running out of memory, and errors inside functions Windows calls back, can't be caught. Both are on purpose, and documented.
- `jamo get` has no versions: it takes whatever is on the project's main branch.
- The repo is still private, so none of this has met a real new user. That's the next real test.
