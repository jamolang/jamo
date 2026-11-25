# Decisions

Each important choice about Jamo, and why it was made. Newest first.

## 2026-11-25

- **math.jamo and time.jamo keep their state in variables outside functions now,** not in the `.exe`'s DOS header (the 2026-11-02 decision below). The header trick needed `VirtualProtect` and broke if the compiler ever wrote those bytes. The random numbers for a seed are the same as before, and asking the time still needs no new memory, since the counter reads into one block made at the start.
- **A mistake inside a copy of a generic also points at the program's own line that asked for it,** like `Map<float, int>.set doesn't work with these types, because of the mistake above.` The first mistake is often inside a library, where it says nothing about which line of the program caused it. A copy made by another copy follows that one back to code that isn't a copy, and each copy is pointed at once.
- **Review, second pass: fuzzing.** `tests/fuzz/fuzz_checker.py` breaks real test programs at random, and `fuzz_programs.py` writes random programs with a Python twin (globals, generics, loops, calls that change globals). 3,000 broken programs: no crashes, and the two checkers always agreed.

- **Variables outside functions are plain `var` lines at the top level,** as the user picked, with any value allowed. This undoes the spec's "only functions at the top level", on the user's request.
- **They live on the stack, in a block the startup reserves right after the cleaner notes where the stack starts,** so the cleaner looks at them like any other value in use, with no change to the runtime. A data slot at the end of the `.exe`'s data (`DataRef::Globals`) holds where the block is, and each use loads it into r11 first. The cleaner notes its start inside its own call, a word lower than the startup's stack, so the block has a spare word on top. Without it the last variable was outside what the cleaner looked at, and its value got freed: a strict-mode test caught it.
- **Their values are set up by a function the checker makes, `(setup)`,** run before `main`. It's checked and made into code like any function, so inlining, error lines and call lists all work there.
- **They get slots from -2 down,** so every check for `slot >= 0` treats them as not local, which keeps them out of registers and out of inlining's slot shifting with no other changes.
- **A value can't use a variable that isn't set up yet, even through functions it calls.** Jamo has no null, so an unset array or struct would crash. The checker notes every call and every read of these variables in each function, then follows the calls from each value. Writing to one early isn't an error, since its own line sets it again.
- **Each file's are set up after those of the files it uses** (walking the use lines from the main file, each file after its own uses), so a library's are ready before a program uses them. The plain load order would have put the main file's first.
- **The register rule for `+=` still holds:** only local variables go in registers, and nothing else can change a local while its value runs. A variable outside functions is read before the value, like `-` and `/` already did, so `score += bump()` gives the same answer as Python when `bump` changes `score`.

- **Generics are copies, like C++ templates.** The checker makes a copy of a generic function or struct for each set of real types, with the types swapped in, and checks the copy like normal code. The code generator only ever sees normal functions and structs, so it barely changed, and generic code is as fast as code written by hand. Erasing types (one copy for all, like Java) would need every value boxed or a type tag, and Jamo's code generator relies on knowing each type.
- **The syntax is the spec's: `func first<T>(items: [T]): T`, `struct Pair<A, B>`, `func Pair<A, B>.swap()`.** The user picked functions and structs, and plain `<T>`. Methods can't add types of their own, which keeps names like `Pair<string, int>.swap` simple.
- **`<` after a name starts types only when its matching `>` is followed by `(` or `{`,** found by looking ahead through tokens that can be in a type. This is C#'s rule. It can't break a valid old program except `f(a < b, c > (d))`, which now reads as types, so the error for that gives a hint to drop the brackets.
- **A line ending in `>` still carries on to the next, so the parser accepts a line end where the next word starts on a later line right after a `>`.** Changing the lexer would have changed how `a >` then `b` on the next line reads.
- **A generic's types are found from its inputs only, never from where the answer goes.** `make<int>()` has to say its type. This keeps checking one direction, like the rest of Jamo.
- **Generics that are never used aren't checked,** the same as C++ templates. Checking them would need a type that stands for "any type", with rules for what it allows.
- **Each copy's warnings show only once,** for the first copy of each generic, since every copy would give the same ones. Errors can repeat, one for each copy, but they point at the same line.
- **A program can make up to 500 copies.** `struct Node<T>` holding `[Node<[T]>]` would otherwise need copies forever.
- **The new key-to-anything table is `Map<K, V>`, not `Dict<K, V>`,** because `dict.jamo` already has a `Dict` for text to ints, and the user wanted the old one to keep working. Keys must work with `==` and `hash_of`: ints, strings, bools and enums. Entries are packed in two arrays, and the hash table holds their places, since a generic can't make an empty `K` to fill unused slots. Removing moves the last entry into the gap.
- **`hash_of` is built into the code generator:** FNV-1a over a string's bytes, then splitmix64's last steps for every kind, halved so it's never below 0. Jamo has no bit operators, so it couldn't be written in Jamo itself.
- **The C++ checker copies a struct's fields before checking a struct literal's values,** since checking a value can add a type, which can move the type table in memory. The old code held a reference into the table there. It never failed, but generics make new types much more often.

- **Register variables are changed in their register,** but only for `=` with a small int constant and `+=`/`-=` on ints. Ints wrap, and nothing can change a local variable while the value on the right runs (no globals or closures yet), so working the value out first and then adding gives the same answer. **When globals or closures arrive, check this still holds:** a function called on the right side must not be able to change the variable.
- **Loops check at the bottom when optimizing:** jump to the check once, then the body, the count, and a backward jump while the condition holds. `continue` goes to the count (for) or the check (while). A planted bug sending `continue` to the body was only caught after a test with `continue` on the last round was added (`tests/good/register_math`).
- **The line-number and divide-by-constant speedups listed as "next" in older notes were already done.** The line is noted only on the way to an error, and dividing by a constant uses a multiply. What was left in loops was moving values through `rax`.
- **Jamo against C at -O2, same PC:** loops 0.074 s against 0.057 s, fib(35) 0.044 s against 0.013 s, particles about 3x. gcc keeps everything in registers and turns part of fib into a loop. Closing that gap needs a real register allocator, a much bigger change.

## 2026-11-03

- **The Jamo parser stands in for exceptions with a stopped flag.** The C++ parser throws on a mistake and catches it at the next statement or top-level item. In Jamo, a mistake sets `stopped`, every function hands control straight back while it's set, and the same catch points check it and carry on. It matches the C++ parser on every recovery case tried, including 1,500 garbage files twice.
- **Jamo's tree keeps every expression and statement in one list each, pointing at each other by place,** since a Jamo struct can't hold itself. Spots are plain ints, never a shared struct, because structs are passed by reference.
- **Phase 6 lives in `compiler/`, one Jamo file for each part of the C++ compiler.** Step 1 is `compiler/lexer.jamo`, checked against `jamo tokens`, a developer command in the C++ compiler that prints every token and lexer error in a fixed form.
- **The Jamo lexer checks a decimal is in range before `float()` turns it,** since a Jamo program can't catch the run error. It compares the digits with the exact decimal forms of 2^1024 - 2^970 and 2^-1075, where rounding goes to endless or to 0, so it reports exactly what the C++ lexer reports.
- **The Jamo lexer is about 8 times slower than the C++ one** on a 245 KB file (0.164 s against 0.021 s). Speed waits until the whole compiler works.
- **The Jamo checker copies std::map's sorted order** wherever it shows: which name "Did you mean" suggests and the order of "never used" warnings. Its Dicts don't keep any order, so it sorts their names with `sort_strings`, which compares bytes like std::string does.
- **The Jamo loader finds files with GetFullPathNameW and GetFileAttributesW** and tidies paths by their text, like std::filesystem's `canonical` and `lexically_relative`, so a path keeps the capitals it was written with, and two paths are the same file when they match ignoring capitals.
- **The Jamo code generator makes the same machine code byte for byte,** though the plan only asks for the same behavior. Matching bytes is the strongest check there is, and it also proves the same behavior. It's compared through `jamo code`, which prints the code, its gaps for the .exe writer, the call sites, the imports and the strings.
- **The runtime's Jamo copy is made from `runtime.dsl` too.** `make_runtime.py` writes `compiler/runtime.jamo` beside `src/runtime.cpp`, with each instruction as hex text, so the two can never drift apart.
- **Jamo has no unsigned ints or bit operators,** so the Jamo code generator does the unsigned math for dividing by a constant with small helpers (adding the sign bit turns unsigned order into signed order), and gets a float's bits through memory.
- **The Jamo PE writer is checked against the real `.exe` files `jamo build` writes,** since the C++ writer leaves out the timestamp and the same source always gives the same file.
- **The installer is two `.cmd` files, not a setup program.** They need no administrator, change only the user's own PATH, and can be read in a minute. The folder passes to PowerShell through an environment variable, so a user name with an apostrophe can't break it. They only wait for a key when they were double-clicked, which is when they get no inputs.
- **A window program shows its run error in a message box.** Its error stream has nowhere to go, so what's written to it is kept, up to 4,095 bytes, and `exit` shows it with MessageBoxA, found with LoadLibraryA and GetProcAddress. Only window programs import those two, since loading functions by name is something antivirus programs look at, and other programs never need it.
- **The window flag rides in the cleaner's mode, as 16,** since that value already reaches the runtime at startup, and every use of the mode tests single bits.
- **Only functions that can never stop with an error are inlined.** A run error shows each call that led to it, so inlining a function that could stop would drop a line from that list. A function qualifies when it's small (64 statements and expressions at most) and uses only things that can't fail: no indexing, dividing by a variable, joining text, making structs or arrays, `float` to `int`, loops or `print`, and calls only to Windows or to other functions that qualify. `clamp` doesn't, since it stops when low is above high.
- **An inlined function's inputs and variables live in spare slots of the caller's frame,** counted before the function starts, with each call's inner calls using slots past its own.
- **Speedups go into both compilers at once,** so the C++ one and the Jamo one keep making the same bytes, and `build.bat`'s check that the Jamo compiler builds itself into the same file keeps meaning something. Each one only happens with optimizing on, so `--no-optimize` keeps the plain code to compare against.
- **The cleaner's kept table stops at 2 MB.** Keeping a bigger one holds memory a large block may need, and a program with that much in use cleans rarely, so asking Windows for a new table costs it little.
- **Leak checks look at memory's low points, not two moments.** Memory rises until the cleaner runs, then drops, so comparing two moments depends on where the cleans fall. Less garbage per round moved them and made the old check fail without any leak.
- **`jamo.exe` is the compiler written in Jamo, and `jamo_cpp.exe` is the C++ one, kept as the way back.** `build.bat` makes both, and it checks every time that the Jamo compiler builds itself into the same file the C++ compiler made. The C++ one is only needed to build from scratch and for the developer dumps.
- **The developer dumps (`jamo tokens`, `ast`, `checked` and `code`) stay in the C++ compiler only.** They exist to compare the Jamo-written parts with the C++ ones, so the C++ compiler, which is kept, is the one that needs them.
- **`JAMO_COMPILER` makes the test suite use another compiler,** like the one written in Jamo. The Phase 6 comparisons still take their dumps from the C++ `jamo.exe`.
- **The Jamo `jamo` command writes its messages with \r\n,** since the C++ one writes through C's text mode, which turns each new line into \r\n. That way the two print exactly the same bytes.
- **Compiling the compiler under `--gc-test` takes hours,** since it cleans at every allocation and each clean walks everything in use. The suite normally has the `--gc-test` compiler build the lexer program instead (about 30 s). `JAMO_LONG_TESTS=1` runs the whole self-compile. Run once on 2026-11-04, it took 117 minutes and gave exactly the same compiler as the normal build.
- **A whole build with the compiler written in Jamo is about 2 times slower than with the C++ one** (0.083 s against 0.041 s for the compiler's own source with every file it uses).
- **The Jamo code generator is about 3 times slower than the C++ one** on its own source with every file it uses (0.14 s against 0.05 s, including printing the dump).
- **The Jamo checker is about 5 times slower than the C++ one** on its own source (0.24 s against 0.05 s, with every file it uses).
- **Constants are swapped for a copy of their value where they're used.** The checker checks each constant once, then each use becomes the literal value, so the code generator never sees a constant's name. Values can only use literals, other constants and operators, so they're known before the program runs.
- **Dividing by a constant of 2 or more uses a multiply and a shift (Hacker's Delight).** It can't divide by zero, so it needs no checks. 10,875 divisions and remainders, including the smallest and largest ints, match Python exactly.
- **The line number for a division is noted only on the way to the error,** not before every division.
- **dict, builder and bytes are libraries, not language features,** since Jamo has no generics: Dict holds ints and TextDict holds text.
- **The strict cleaner check only reports pointers to blocks freed in the last 2 cleans.** Jamo's memory sits at low addresses, like 14 to 40 million, so a plain number such as a buffer size can equal the address of a block freed long ago, and the check reported it as a bug. A program that hid an address and stored it back as a number proved this. When the cleaner really frees something still in use, the leftover pointer shows at the very next clean, so real bugs are still caught. This was the cause of the rare net and links failures in strict mode.
- **Each function keeps its 4 busiest variables in r12 to r15,** counting uses inside loops 10 times per loop. Functions save and restore the registers they use, Windows keeps them across calls, and the runtime routines that use them now save them too. It made the benchmarks only 3 to 8% faster: on this CPU, reading a variable from the stack was already nearly as fast. The bigger costs in loops are division and noting the line number before each check.
- **Phase 5 is done.** On a temporary Windows user with no developer tools on PATH, all 12 examples built, the console ones and the page fetch worked, and the 4 window samples opened, drew and played (Breakout's test bot won). The user and its files were removed afterward.
- **Text handed to WinHTTP lives outside Jamo's memory until the request has closed.** The cleaner's check mode once found a pointer to freed memory during a background request. Only WinHTTP had held one: it can read the server name, path or headers after the call that took them returns. Now they're copied into the request's own block, which is only given back once WinHTTP says the handle has closed.
- **Each web request keeps its own deadline, which restarts whenever data arrives.** WinHTTP's time limits only get checked when something arrives: a 1-second limit fired at 4 seconds, when the slow server finally answered, and half the time the late answer won. The early timeout test only passed by luck. Waiting `get` and `post` now run through the same background code, waiting 1 ms at a time.
- **Links send at most 64 KB per call and keep a mark of how far they've got,** instead of moving the rest of the buffer after each send.
- **Links send whole messages, not a stream of bytes.** Each message goes out with its length in front as 4 bytes, so a game gets exactly what was sent however TCP splits or joins it. Messages can be up to 16 MB, and a bigger length closes the link instead of using up memory.
- **Links never wait.** Sockets are set to return at once, and `messages()` sends and reads whatever it can each frame, so a slow connection never freezes a game.
- **`listen_here` only takes programs on the same PC,** so tests and local games never open the firewall. `listen` takes other PCs too, and Windows may ask the player first.
- **Links only connect to number addresses and `localhost` for now,** so connecting never waits on a name lookup.
- **Background requests use WinHTTP's own threads, but Jamo code there only writes numbers.** WinHTTP reports progress by calling `net_signal` on its threads. That function never makes text, arrays or structs, because the memory cleaner runs on the program's thread. `done()`, on the program's thread, takes every next step.
- **What WinHTTP writes into lives in memory from LocalAlloc,** which the cleaner never sees, and is given back only after WinHTTP says the request's handle has closed. A request dropped without `cancel()` keeps its memory instead of risking it being reused while WinHTTP still writes there.
- **WinHTTP opens only a few connections to one server at once,** so 20 requests that each take 0.5 s finished in about 3 s, not 0.5 s. That's WinHTTP's default, kept to be kind to servers.
- **Random numbers and the start time live in the `.exe`'s own DOS header in memory.** Jamo has no program-wide variables, and adding them would change how Jamo looks. Jamo writes bytes 2 to 59 of that header as zeros and Windows never reads them once the program is loaded, so math uses bytes 8 to 23 and time 24 to 47. The page is read-only at first, so the first write makes it writable with `VirtualProtect`. If the compiler ever puts something in those bytes, these libraries must move.
- **The random generator is a 64-bit LCG with Knuth's MMIX numbers, using only the top 32 bits of each step.** Jamo has no bit operators, so xorshift would need slow sums for every xor. Plain multiplying and adding wraps exactly the way an LCG needs. Its low bits repeat quickly, so they're thrown away, and `random_int` turns away draws that would make some numbers more likely than others. It isn't safe for secrets.
- **Without `random_seed`, the first random number seeds from the performance counter and the process id,** so two programs started at once still differ.
- **`floor`, `ceil` and `round` give ints, and `round` takes halves away from zero,** the way people round by hand. Python and C# round halves to even, which surprises beginners. A float too big for an int, or nan, stops with a message naming the function.
- **Jamo has no overloading, so the int versions get `_int` on the end:** `abs_int`, `min_int`, `max_int` and `clamp_int`. Floats get the short names, because most math in games is on floats.
- **`seconds_since_start` counts from when Windows made the process, not from the first call.** The first call works out where the performance counter was then, from the process's start time, and later calls only read the counter, with no memory, so it's quick enough to call many times a frame.
- **`now()` gives a `DateTime` struct, with `weekday` from 0 for Sunday,** the way Windows gives it.
- **The text tools build their result in memory and turn it into text once,** with `memory_text`. Joining 200,000 pieces a letter at a time would copy the text over and over.
- **`sort_ints`, `sort_strings` and the reverses change the array in place,** since Jamo arrays are shared and most programs want the array itself sorted. The sort is a merge sort, so its time never grows faster than n log n.
- **Text compares by bytes when sorting,** so capitals come first. That's the same order as Unicode for UTF-8 text, and needs no language rules.
- **`trim` takes off spaces, tabs and line endings only,** which covers text from files and typing.
- **`split` and `replace` with empty text to look for stop the program,** since there's no single right answer for them.
- **An extern may be declared again if it matches exactly: the same DLL, the same input and answer types, and the same `i32` and `f32` choices.** It's the same Windows function, imported once, so a program can declare `sqrt` or `QueryPerformanceCounter` while using a library that has it. This lifts the known limit below. Input names don't have to match, since they don't change the call. Anything else that differs is an error saying what differs, because the two would call the function in different ways.
- **When a program's extern differs from a built-in library's, the error points at the program's line,** and the library keeps its own, so the library's calls don't give extra errors.
- **The DLL name has to match exactly, capitals too.** Windows would treat `KERNEL32.dll` as the same DLL, but asking for one spelling keeps the rule simple to explain.
- **A failed web request gives back an error instead of stopping the program.** Networks fail all the time and Jamo has no way to catch errors, so `response.error` says what went wrong. Only program mistakes, like a web address without `http://`, stop it.
- **`memory_text(address, bytes)` was added for libraries,** so net can turn downloaded bytes into text. Jamo had no other way to make text from memory.
- **net is tested only against a Python server on 127.0.0.1.** HTTPS uses the same code with one flag, but isn't tested end to end, since a local HTTPS server needs a certificate Windows trusts.
- **WinHTTP reports a connection closed without an answer the same way as a bad answer,** so both get one message that names both.
- **g3d draws into gfx's own picture, which now has a depth buffer.** That way 2D text and shapes can go on top of a 3D scene in the same frame. Each library switches OpenGL to its own settings when it starts drawing.
- **Each `draw` call sends one mesh to the graphics card right away,** instead of collecting them. 2,500 shapes still ran at about 1,200 frames a second in a small window.
- **Looking straight up or down, the top of the screen points along -z or +z,** so the camera works from any spot without flipping or blanking.
- **The OBJ reader reads its own numbers,** including `+1.5` and `1.0E+0`, so a bad number gives the file's line instead of a general error. It ignores materials and groups.
- **Faces light from both sides,** so OBJ files with backward faces still look right.
- **A known limit: 3D edges aren't smoothed.** There's no anti-aliasing yet.
- **The UI is drawn fresh each frame, and each control call gives back its value** (`name = ui.text_box(name)`). Jamo has no closures or events to hang code on, and this way nothing has to be kept in step between the program and the controls.
- **A control's keyboard focus is its place in the drawing order.** Tab moves to the next place. A program that draws different controls from frame to frame can see focus land somewhere new.
- **Controls stack down the window at its full width by default,** so a form follows resizing with no layout code. `panel`, `columns` and `place` cover the rest.
- **Corners are rounded with four of gfx's soft circles,** so the default look stays smooth without new drawing code.
- **Text boxes and lists only show English letters and symbols,** like gfx text. Typed letters outside that are dropped.
- **Sound's buffers live in memory from VirtualAlloc, not Jamo memory.** Windows reads them and writes their headers while they play, so the cleaner must never see or free them. Only `close()` gives them back. If a program drops its player without closing it, the buffers leak instead of being freed while in use.
- **The program mixes sound by calling `update()` once a frame,** not a background thread. Jamo's memory cleaner isn't safe across threads. Six buffers of 512 frames cover about 70 ms, so a frame can run long without a gap. Mixing 64 sounds takes about 1% of one core.
- **Everything plays at 44100 frames a second in 16-bit stereo.** WAV files at other rates are converted once, when they load.
- **A PC without a sound device prints a note and runs on silently,** so a game still works there. `has_device()` tells a program which happened.
- **Sound tests play at no volume, and check the volume really is 0 before any sound starts.** A planted bug that skipped the muting once played test tones out loud.
- **gfx draws into its own picture, then copies it to the window.** Windows doesn't keep pixels for parts of a window that are off the screen, so reading pixels back, for tests and `read_pixel`, needs a picture gfx owns.
- **Every shape and letter comes from one texture,** so a whole frame of shapes and text goes to the graphics card in one batch. 10,000 shapes ran at about 940 frames a second.
- **Text is drawn by Windows once, in Segoe UI, into that texture.** That gives smooth letters with nothing to ship. Only English letters and symbols show for now.
- **PNG files are read by Jamo's own deflate reader, written in Jamo.** It handles 8-bit color in every PNG color type. 16-bit and interlaced files stop with a clear message.
- **OpenGL memory is measured over time, not capped.** The graphics driver uses about 200 MB inside the program, so a hard memory limit stops the driver, not Jamo.
- **A known limit: a library's extern names are shared with the program,** so a program can't declare its own `QueryPerformanceCounter` while using `window.jamo`. It can call the library's one instead.

## 2026-11-02

- **Built-in libraries like `window.jamo` live in `libraries` as plain Jamo, packed into jamo.exe by `libraries/embed.py`.** `use "window.jamo"` always means the built-in one, even if the program's folder has a file with that name, so a library can't be hidden by accident.
- **A window collects input between frames, and each `is_open()` hands it over as that frame's.** Otherwise a resize or key press that arrives between frames would be lost.
- **`quiet()` opens a window without taking the keyboard.** Tests need it so test windows never steal typing from the user, and it's useful for small helper windows too.
- **Using a window after closing it, or closing it twice, stops with a run error naming the library line and the caller's line.**
- **Warnings never show for the built-in libraries.** They're tested already.

- **Added `\r`, though the spec listed no new features for Phase 4.** The user chose this after the clean PC test showed programs couldn't handle Windows line endings in files.
- **The version was 0.1.0 until the first release.** 0.2.0 (2026-11-25) added jamo watch, the _or functions, list tools for every type and Snake.
- **Warnings don't stop a build, and they only show when there are no errors.** Errors matter first, and warnings about half-broken code are often wrong.
- **Only a `var` gets the unused warning.** Inputs and loop variables are often left unused on purpose.
- **After a syntax mistake, the compiler carries on from the next line, but only checks types if the structure was clean.** A broken structure would cause a flood of follow-on errors.
- **A statement word like `end` at the start of a line closes any forgotten brackets.** Otherwise one missing `)` would swallow the rest of the file.
- **Keeping local variables in registers is left out for now.** The spec says only where it's clearly safe, and the runtime uses those registers freely. Half-finished sums use `rcx` instead of the stack when the right side is simple. On this PC that gained little, since the processor already handles stack values very quickly.
- **Unused code in the built-in runtime stays in.** Only the program's own functions get left out, so every program is still about 10 KB.
- **Benchmarks throw away a program's first run.** A fresh `.exe`'s first run took about 0.2 s to 0.5 s longer, which I suspect is Windows Defender scanning it, though that isn't proven. It's the reason earlier fib(35) numbers looked like 0.25 s when it really takes 0.045 s.

- **Blocks of up to 512 bytes come from Jamo's own allocator.** It splits 256 KB chunks from HeapAlloc into 32 size classes, 16 bytes apart, with a free list for each. Larger blocks and blocks of size 0 still use HeapAlloc and HeapFree. Calling Windows for each small block was most of the cleaner's cost.
- **Every small block is zeroed when handed out, even text.** Only blocks that hold references need it, but code may lean on the old behavior, and it costs little. If unsure, keep it.
- **A free-list block has type 3 and isn't in the cleaner's table.** So an old number that matches its address finds nothing, the same as memory given back to Windows. Taking a block whose type isn't 3 stops with the damaged header error.
- **Chunks with nothing in use are taken back whole, and can then serve any size.** Without this, a program whose sizes change over time would keep memory in every size class it ever used. A block's header stores where it sits in its chunk, so a sweep can count what's in use in each chunk.
- **Empty chunks are only taken back when they're at least 1/8 of all chunks.** Taking them back means walking every free list, which made one test 3x slower when it happened at every clean.
- **Empty chunks beyond the next clean's allowance, plus 512 KB, go back to Windows.** That also lets a large block use memory that small blocks no longer need. The kept ones save a program that makes and drops lots of blocks from going back to Windows each time.
- **With `--gc-check`, blocks are never reused or taken back.** They're overwritten with 0xDD and kept, as before.
- **Before getting a new chunk, the allocator checks there's room for that chunk plus the cleaner's own table, and cleans first if not.** Otherwise memory would run out with no room left for the table, the clean would be skipped, and the retry would do nothing. A large block that can't be had can still hit this when memory is very tight, as before.
- **Jamo code can't read a block before it's filled, so the zeroing test watches memory instead.** Leftover references in reused blocks would keep large text in use until the memory limit stops the program.

- **The cleaner treats any word on the stack or in a block as a possible reference, if it exactly matches where a block starts.** It never needs to know types, so it can't miss a value. A number that happens to match only keeps something a little longer, which is the safe mistake.
- **Runtime routines never hold a pointer into the middle of a block across an allocation.** That's what makes exact matching safe. `text[i]` copies its byte out first for this reason.
- **Every saved register is pushed onto the stack before cleaning,** so half-finished values in registers are found the same way as everything else.
- **New blocks are zeroed.** A half-built array or struct can be looked through at any time, and leftover memory can't look like a reference.
- **Cleaning starts once the bytes allocated since the last clean reach the larger of 4 MB and twice the bytes still in use.** Memory stays within about three times what's in use, plus 4 MB. It was once what's in use, but each clean walks every block, so a program with lots in use cleaned too often: the checker written in Jamo spent most of its time cleaning. Empty chunks kept for reuse still follow what's in use, not the doubled amount, so a large block can still find room.
- **`text[i]` gives one of 256 shared one-letter strings,** made once in the program's data, since text never changes. It used to make a new string each time, which was most of the garbage in code that reads text letter by letter, like a lexer.
- **An empty array gets room for its items on its first `add`.** Most of the arrays in a program's tree never get any, and each one was a second block for the cleaner to walk.
- **If memory runs out, the cleaner runs once and the allocation tries again** before giving the out of memory error.
- **The text builder and input buffer aren't cleaned.** They last for the whole program and never hold references.
- **If the cleaner can't get memory for its own table, it skips that clean and frees nothing.** If unsure, keep it.
- **The stress tests got smaller.** Cleaning at every allocation takes far longer as programs grow, so the big sizes moved to a separate test that runs with the cleaner in its normal mode.

- **Arrays are a length, a room size and a pointer to their items.** Growing replaces only the pointer, so everything sharing the array still sees it.
- **`items[i] = f()` works out the item's place after `f()`.** `f` could grow the array and move its items, so the place must be found last.
- **Strings show in quotes inside arrays and structs, with no escapes.** The spec only shows numbers. `["a", "b"]` reads like the code that made it, and tells `"1"` from `1`.
- **A value that holds itself shows `...`.** An array of structs can hold its own struct, and printing it would otherwise never end.
- **A `for` over an array can't assign to its variable, as with ranges.** It wouldn't change the array, so it's most likely a mistake. Fields can still be set: `item.health = 1`.
- **`self` can't be assigned to.** It would only change the local name, not the struct, which would be a surprise.
- **A `case` needs a fixed value.** That lets the compiler catch repeats and missing enum members.
- **`input()` gives `""` when input runs out.** The spec doesn't cover this. A loop like `while line != ""` then finishes on its own, and a stop with an error would leave no way to read until the end.
- **`int(text)` and `float(text)` don't allow spaces.** Any leftover would be a silent guess at what was meant, and the error says exactly which text failed.
- **`float(text)` rounds exactly, with whole-number math.** It gives the same float as writing the number in code, and the same as Python on every random test.
- **`use` paths can go into folders, as long as they stay in the main file's folder.** The spec's rule is that paths can't leave the program's folder.
- **`input`, `args` and the other new functions aren't reserved words.** A variable called `input` is common and harmless, so only function, struct and enum names are blocked.
- **Every built program carries the whole runtime.** It's about 7.7 KB now. Leaving out unused parts can wait for Phase 4.
- **The planned Windows list leaves out changing the console's code page,** so non-English text can show garbled in a console window, though files and pipes get the exact bytes.

- **Strings keep a hidden new line past their end.** `print` can then send a string in one write. It isn't part of the string or its length.
- **All values stay 8 bytes, including floats.** A float travels through the same register and stack as an int until it needs math, which keeps the compiler simple. Speed comes in Phase 4.
- **The line for a run error is saved in memory just before anything that can fail.** It costs one instruction pair there and nothing elsewhere.
- **Floats round with whole-number math, never float math.** Otherwise a value like 0.0078125, an exact tie, could round the wrong way. Results match Python's on every random test.
- **Scientific form starts at 1.0e+16.** The spec asks for very large values to stay readable, and printing all 309 digits isn't. Above 2^53 every float is a whole number, so the exact digits are still used for rounding.
- **`int(x)` of a float that doesn't fit is a run error.** The spec doesn't cover this, and the processor's answer, the smallest int, would be a silent wrong result.
- **Float division by zero isn't an error.** The spec requires `inf` and `nan` to print, and this is how a program gets them. Int division by zero stays an error.
- **F-strings show braces with `{{` and `}}`.** The spec doesn't say how, and this is what Python and C# do.
- **`and`/`or`/`not` follow Python's order,** so `not a == b` means `not (a == b)`.
- **`int(5)` and `string("a")` are errors.** Converting something to its own type is usually a mistake.
- **The runtime's instructions were first checked with GNU as,** then written into `src/runtime.cpp` as bytes with each instruction beside them. Neither Jamo nor its build needs an assembler.

- **Simple code generation first.** Every expression leaves its answer in `rax`, and half-finished sums wait on the stack. It isn't the fastest way, but it's easy to get right, and speed comes in Phase 4.
- **Jamo functions pass inputs on the stack.** Only the generated runtime follows Windows' rules, because it's the only code that talks to Windows.
- **8 MB of stack, not Windows' usual 1 MB.** Recursion is common in beginner programs, and 100,000 levels should work.
- **Big frames touch each page of stack.** Otherwise a function with thousands of variables could jump past the stack guard and hit other memory.
- **No timestamp in the .exe.** The same source always gives exactly the same file.
- **Tests compare with hand-written answers.** Expected outputs never come from the compiler itself, and a copy of the compiler with planted bugs was used to prove the tests catch them.

## 2026-11-01

- **The logo is a cut gem shaped like a jam drop, with a j inside.** Language logos work best as one simple flat shape, and this one suggests jam while standing out. Small sizes drop the facets so the j stays clear. It still needs a trademark check before public release.
- **Named Jamo.** Other programming languages already use Jam and Jamlang. A quick GitHub search found no language called Jamo. A full name and trademark check is still due before public release.
- **Commands and files.** `jamo build`, `jamo run` and `.jamo` files.
- **No fake computer.** The old Jam ran on a made-up CPU called JAM-32. Jamo compiles straight to real x86-64 and writes real `.exe` files, with no interpreter, virtual machine, external assembler or external linker. The old code is in Git at commit `22a6095`.
- **First compiler in C++20.** Jamo doesn't exist yet to write its own compiler, and g++ is already installed. It gets rewritten in Jamo in Phase 6.
- **Windows x86-64 first.** The user's friends all use Windows.
- **A small Phase 1.** Only `int`, `bool`, functions, `if`/`else`, `while`, basic operators and `print`, so a real compiler works early. Everything else comes one step at a time.
- **Types on function inputs and outputs.** They're required there, and `var` can work out its own type. This keeps the type checker simple.
- **`int` is 64-bit, and overflow wraps around.**
- **Three Windows functions in Phase 1.** `GetStdHandle`, `WriteFile` and `ExitProcess`, only in generated startup and print code. Windows programs can't print or exit cleanly without them, and Jamo code can't call Windows itself.
- **Friends and public release come last.** Nothing is shared until the user approves it at each step.
