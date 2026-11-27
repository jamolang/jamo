# The Jamo Language

The exact rules of Jamo. Update this with every feature.

**Status: Phase 3.** Everything here is built and tested. New features get added one phase at a time.

## Types
- `int`: a 64-bit signed whole number. Overflow wraps around
- `float`: a 64-bit decimal number, written like `1.5`. It needs a digit on both sides of its dot
- `bool`: `true` or `false`
- `string`: text, written like `"hi"`. It can't be changed in place
- `[int]`: an array, here of ints. See Arrays
- A struct or enum, by its name. See Structs and Enums

## Strings
- The escapes are `\n \t \r \" \\`. A string must close on the line it starts on
- Any UTF-8 text passes through unchanged
- `+` joins two strings. `==` and `!=` compare them, and nothing else does
- `len(text)` counts bytes, not letters
- `text[i]` gives a string holding the one byte at that place, counting from 0. A letter that isn't English can take several bytes
- `string(x)` converts an int, float or bool

## F-strings
```
print(f"Score: {score + 1}")
```
- Anything printable can go inside `{ }`, including another f-string
- `{{` and `}}` show a single brace
- A `{ }` must close on the line it starts on

## Arrays
```
var items = [1, 2, 3]
var empty: [int] = []
items[0] = 5
items.add(4)
print(items.pop())
print(len(items))
for item in items
    print(item)
end
```
- All the items share one type, and arrays can hold anything, including other arrays
- `[]` needs to know its type from where it's going
- Assigning an array shares it and doesn't copy it, and so does passing it to a function
- Going outside the array, or popping an empty one, stops with a run error
- A `for` over an array looks at its length every round, so items added during the loop are reached too
- `print` shows `[1, 2, 3]`, with strings in quotes: `["a", "b"]`. A value that holds itself shows `...` at that point
- Arrays can't be compared with `==` or `!=`

## Structs
```
struct Enemy
    health: int
    speed: float
end

func Enemy.hit(damage: int)
    self.health -= damage
end

func main()
    var e = Enemy{health: 3, speed: 1.5}
    e.health = 2
    e.hit(1)
    print(e)
end
```
- Every field must be given, in any order
- Assigning a struct shares it and doesn't copy it, and so does passing it to a function
- `print` shows `Enemy{health: 1, speed: 1.5}`
- There's no null. A struct can't contain itself, directly or through other structs, except through an array
- A method's `self` shares the struct, so changes stay after it returns. `self` itself can't be assigned to
- Structs can't be compared with `==` or `!=`

## Enums
```
enum Mood
    CALM
    ANGRY
end

match mood
    case Mood.CALM
        print("calm")
    else
        print("not calm")
end
```
- Use one as `Mood.CALM`, and compare with `==` and `!=`
- `print` shows `Mood.CALM`
- `match` works on enums, ints and strings. Only the first matching case runs
- A `case` needs a fixed value, and can't repeat one
- A `match` on an enum must cover every member or have an `else`

## Function values
```
func double(n: int): int
    return n * 2
end

var change: func(int): int = double
print(change(4))
```
- A function's name without `( )` is a value of type `func(...)`: the inputs' types, then `:` and the answer's type, or nothing after `)` when it gives back nothing, like `func(string)`
- A variable, input, struct field or array item of a function type is called with `( )`, like `change(4)` or `button.on_click(4)`. A field holding a function is called like a method
- Methods, generic functions and `extern` functions can't be values
- Two function types are the same when their inputs and answers are. Function values can't be compared with `==` or `!=`, and print as their type
- `func(x: int, y: int): int => x * y` is a function written in place. The answer's type can be left out, and comes from the expression after `=>`, which is all it does
- One written in place can use the variables and inputs around it. It keeps a copy of each one's value from when it was made (arrays and structs are shared, as always). Variables outside functions are used directly, not copied
- `( )` after any value holding a function calls it, like `steps[0](3)` or `make_adder(5)(1)`
- Without `=>`, a function written in place has lines after it and then `end`, like any function's body
- `func name(x: int): int` inside a function, with `=> expression` or lines and `end`, is the same as `var name = func(x: int): int ...`. It can't call itself, and it can't change the variables it captured, which are copies

## Generics
```
func first<T>(items: [T]): T
    return items[0]
end

struct Pair<A, B>
    left: A
    right: B
end

func Pair<A, B>.swap(): Pair<B, A>
    return Pair<B, A>{left: self.right, right: self.left}
end

var p = Pair<string, int>{left: "hi", right: 5}
var m = new_map<string, int>()
```
- A function or struct can take types in `< >` after its name. A method of a generic struct writes the same types after the struct's name, and can't have types of its own
- A generic's types can't share a name with a real type, or with each other
- A call works out a function's types from its inputs. When it can't, the types go in `< >` after the name, and they can always be given that way
- A generic struct always needs its types, in both types and struct literals: `Pair<string, int>`
- Each different set of types gets its own copy, which is checked like normal code, so a mistake shows on the generic's own line. A generic that's never used isn't checked. Each copy's warnings show only once
- A program can make up to 500 copies, so a generic that keeps needing bigger types of itself is an error
- `print` shows a copy's full name: `Pair<string, int>{left: "hi", right: 5}`
- After a name, a `<` whose matching `>` comes straight before `(` or `{` starts types, the way C# decides. So `f(a < b, c > (d))` reads as types: write `c > d` to compare
- `hash_of(x)` gives a number from an int, string, bool or enum that's never below 0 and is always the same for the same value, for hash tables like `Map` in `map.jamo`
- A line that ends with `>` carries on to the next, except where a `>` closes types at the end of a line, like `func make<T>(): Box<T>`

## Modules
```
use "enemy.jamo"
```
- This loads a file, looking from the folder of the file using it. Each file loads once, so files can use each other
- `use` goes at the top level. Paths can go into folders, but can't leave the main file's folder
- Everything in a program shares one set of names, so using a name twice is a compile error, naming both files. The one exception is an `extern` declared again exactly the same. See Calling Windows

## Input and files
- `input()` reads a line, without its line ending. When there's no more input, it gives `""`
- `args()` gives the command line's inputs as `[string]`, leaving out the program's own name. `jamo run game.jamo a b` passes `a` and `b` along
- `read_file(path)` gives a file's text, and `write_file(path, text)` replaces a file. Each stops with a run error if it fails
- `exists(path)` gives a bool, and is true for folders too
- `int(text)` takes digits with an optional minus sign. `float(text)` takes the same, with optionally a dot and more digits. Spaces or anything else stop with a run error
- `input`, `args`, `read_file`, `write_file`, `exists` and `hash_of` can't be function, struct or enum names

## Floats and ints
- Mixing them is always a compile error, so `1 + 1.5` and `1 < 1.5` won't compile. Use `float(x)` or `int(x)`
- `int(x)` and `float(x)` also take text. See Input and files
- `int(x)` drops the decimals toward zero. If the float doesn't fit in an int, or is `nan` or infinity, the program stops with a run error
- `float(x)` gives the closest float
- Dividing a float by zero gives `inf`, `-inf` or `nan`. It isn't an error
- `nan` compares false with everything, except that `!=` is always true

## Comments and lines
- `#` starts a comment that runs to the end of the line
- Each statement goes on its own line
- A line carries on to the next if it ends with an operator or comma, or if a `(`, `[` or `{` is still open

## Names
Names use letters, numbers and `_`, and can't start with a number. Keywords can't be used as names.

## Constants
```
const MAX_PLAYERS = 4
const TITLE = "Space Race"
const HALF = MAX_PLAYERS / 2
```
- `const` goes at the top level of a file, and every file in the program can use it
- Its value must be known before the program runs: numbers, text, `true` or `false`, other constants, and operators. It can't call functions
- A constant can't be changed, and no variable can have its name
- Constants can use each other in any order, but not in a circle

## Functions
```
func add(a: int, b: int): int
    return a + b
end
```
- Input and return types are required
- The return type is left out if nothing is returned
- Functions can be called before they're defined, and can recurse
- A program needs `func main()`, with no inputs and no return value

## Variables
```
var x: int = 1
var y = 2
x = 3
```
- A value is always required
- The type can be worked out from the value
- Variables last until their block ends
- Reusing a name in the same block is an error

### Variables outside functions
```
var score = 0
var names: [string] = []

func add_point()
    score += 1
end
```
- A `var` at the top level of a file can be used and changed by every function, in every file of the program
- They're set up before `main` runs: each file's after those of the files it uses, then from top to bottom within a file
- A value can use the ones set up before it, and call any function, but not use one that isn't set up yet, either itself or through a function it calls. Each of these is a compile error that names the one to move
- Their names share one set with functions, structs, enums and constants. A variable inside a function can hide one until its block ends
- A run error while they're being set up lists "setting up the variables outside functions" among its calls

## Control flow
```
if total > 4
    print(total)
else
    print(0)
end

while total > 0
    total = total - 1
end
```
Conditions must be `bool`.

```
if score > 90
    print("A")
elif score > 80
    print("B")
else
    print("C")
end

for i in 0..10
    print(i)
end
```
- `for` starts at the first number and stops before the second, so `0..10` goes from 0 to 9, and `5..5` doesn't run at all
- Both numbers must be ints, and each is worked out once, before the loop starts
- A `for` variable lasts until the loop ends and can't be assigned to
- `break` leaves the innermost loop, and `continue` goes on to its next round. Both only work inside a loop

```
try
    var n = int(text)
catch problem
    print(problem)
end
```
- A run error or `fail` inside the `try` part, at any depth of calls, jumps to the `catch` part. The rest of the `try` part is skipped
- The name after `catch` is optional. It's a `string` holding the error's words, without the file and line, and lasts until the `end`
- The nearest open `try` catches. An error inside a `catch` goes to the `try` around it, if there is one
- `return`, `break` and `continue` may leave a `try` or `catch` part
- Running out of memory isn't caught. Neither is an error inside a function Windows calls back through `callback`, since a catch can't be reached across Windows' own code: those stop the program

## Operators
`+ - * / % == != < > <= >=`, unary `-`, `and`, `or`, `not`, and parentheses. From loosest to tightest: `or`, `and`, `not`, `== !=`, `< > <= >=`, `+ -`, `* / %`, unary `-`.
- `+ - * / %` and `< > <= >=` need two ints or two floats
- `and`, `or` and `not` need bools. `and` and `or` skip their right side when the left side decides the answer
- `x += y`, `x -= y`, `x *= y` and `x /= y` work on numbers, and `+=` also joins text. There's no `%=`
- `%` keeps the sign of its left side, so `-7 % 3` is `-1`. It works on floats too
- `==` and `!=` need both sides to have the same type
- `/` drops the remainder toward zero
- An int divided by a literal zero is a compile error, with `/`, `%` or `/=`
- Dividing an int by zero while running stops the program with a run error

## More rules
- Only functions, structs, enums, constants, variables, `extern` and `use` lines can go at the top level, and functions can't go inside functions
- A function with a return type must return on every path. An `if` counts only if every branch returns and it has an `else`, a `match` counts only if every case returns and it covers everything, and loops never count
- `return` with no value is allowed only in functions with no return type
- A function call can be its own statement, even if it returns a value. A function with no return value can't be used as a value
- An inner block may reuse a name from an outer block, hiding it until the block ends
- `var x = x + 1` in an inner block uses the outer `x` for its value
- `a < b < c` is an error, since `a < b` is a bool
- A number too large for 64 bits is a compile error, but `-9223372036854775808` works
- The smallest int divided by -1 wraps to itself
- `print`, `len`, `int`, `float`, `bool`, `string`, `true`, `false`, `func`, `end`, `var`, `if`, `elif`, `else`, `while`, `for`, `in`, `break`, `continue`, `return`, `and`, `or`, `not`, `struct`, `enum`, `const`, `match`, `case`, `use`, `try` and `catch` can't be names
- Files may use Windows or Unix line endings, and tabs or spaces. Non-English text is allowed only in comments and quoted text
- A program exits with code 0 when it finishes normally, and running out of stack ends it with a non-zero exit code
- Compile errors stop at the first mistake in a program's structure. Mistakes in types and names are all reported together, up to 20

## Printing
`print(x)` takes an `int`, `float`, `bool` or `string`, then starts a new line. `print(true)` shows `true`. Text is UTF-8: in a console window it shows, and can be typed, in any language, and files and pipes get the bytes unchanged.

Floats show up to 6 decimals, rounded exactly, with extra zeros removed: `2.0`, `1234567.5`, `0.123457`. From `1.0e+16` up, they show in scientific form with up to 7 digits, like `1.234568e+29`. The special values show as `inf`, `-inf` and `nan`.

## Run errors
A run error shows the file and line, then stops the program with exit code 1:
```
game.jamo line 8: Error: division by zero
```
The others are running out of memory, a float that doesn't fit in an int, going outside a string or array, popping an empty array, a failed file read or write, and text that isn't a number. Any of them but running out of memory can be caught with `try`, so the program carries on.

## Memory
Jamo frees strings, arrays and structs by itself once nothing can reach them. Programs never free anything by hand.

## Calling Windows
For libraries that need Windows itself. Normal programs rarely need this.
```
extern "user32.dll" func MessageBoxW(window: int, text: int, title: int, kind: i32): i32
```
- Inputs can be `int`, `i32`, `float`, `f32` or `bool`, up to 16. The answer can be any of those, or nothing. `i32` is a 32-bit int and `f32` a 32-bit float, which many Windows and OpenGL functions use
- `extern "OpenGL"` finds an OpenGL function the first time it's called, once a window has OpenGL set up
- `byte(text, i)` gives the number of the byte at that place, from 0 to 255
- Calls follow the Windows x64 rules, and the DLL is added to the `.exe`'s imports
- An extern can be declared more than once, in any files, if every declaration has the same DLL, inputs and answer, with the same `i32` and `f32` choices. They're one function, imported once. Input names may differ. Declarations that differ are a compile error that says how
- `extern` is the one exception to names being used once, so a program can declare `sqrt` or `Sleep` while using a library that declares it too
- `callback(f)` gives Windows the address of a Jamo function to call back, like a window's message handler. It can take up to 4 int or bool inputs
- `wide(text)` gives the address of a copy of the text in the form Windows' `W` functions take
- `memory(bytes)` gives the address of zeroed memory, kept as long as the program holds that address
- `memory_text(address, bytes)` copies that many bytes from memory into new text. Below 0 bytes gives empty text
- `peek_i64`, `peek_i32`, `peek_u16`, `peek_u8`, `peek_f64` and `peek_f32` read memory at an address plus an offset, and `poke_` with the same endings writes it
- These reach outside Jamo's safety: a wrong address or a wrong signature can crash the program
- `fail(message)` stops the program with a run error, like the built-in ones. Libraries use it for misuse

## Built-in libraries
`use "window.jamo"` loads a library that comes inside jamo.exe. These always win over a file with the same name in the program's folder. The guide shows how to use each one
