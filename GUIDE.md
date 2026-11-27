# Learn Jamo

This guide teaches Jamo from zero, one small step at a time. You don't need to know any other language. Jamo's tests check every program here: the ones that print are run and their output compared, and the ones that open a window, play sound or use the internet are compiled. So what you see is what you'll get.

Each step ends with something to try. Trying them is the fastest way to learn.

## 1. Your first program

Make a file called `hello.jamo` with this in it:

```jamo
func main()
    print("Hello!")
end
```

Then run it from a terminal, in the same folder:

```
jamo run hello.jamo
```

```output
Hello!
```

Every program starts at `main`. `print` shows a value and starts a new line. `end` closes what `func` opened.

While you're working on a program, `jamo watch hello.jamo` runs it, then builds and runs it again every time you save. A mistake shows up straight away, and the last version that worked keeps running until it's fixed. Press Ctrl+C to stop watching.

When a program does something you didn't expect, `jamo run --trace hello.jamo` shows each of its own functions as it starts, two spaces in for each call it's inside, so you can follow what it did.

`jamo fmt hello.jamo` tidies a program's indentation: 4 spaces for each block, and the same for each line that carries on. It never changes what the program does, so it's safe to run any time.

**Try it:** make it print your name on a second line.

## 2. Values and variables

`var` makes a variable. Jamo works out its type from its value, and you can change it with `=`.

```jamo
func main()
    var score = 10
    var speed = 2.5
    var name = "Sam"
    var alive = true
    score = score + 5
    print(score)
    print(speed)
    print(name)
    print(alive)
end
```

```output
15
2.5
Sam
true
```

Those are Jamo's four simple types: `int` for whole numbers, `float` for decimals, `string` for text and `bool` for true or false.

A variable always keeps its type, and Jamo tells you if you get it wrong before the program runs:

```jamo
func main()
    var score = 10
    score = "lots"
    print(score)
end
```

```error
main.jamo line 3: score holds an int, but this value is a string.
        score = "lots"
                ^^^^^^
```

**Try it:** make a variable for your age, then add 1 to it and print it.

A constant is a name for a value that never changes. It goes at the top of the file, outside any function, and every function can use it:

```jamo
const LIVES = 3
const NAME = "Jamo"

func main()
    print(f"{NAME} starts with {LIVES} lives")
end
```

```output
Jamo starts with 3 lives
```

## 3. Math

```jamo
func main()
    print(2 + 3 * 4)
    print((2 + 3) * 4)
    print(7 / 2)
    print(7 % 2)
    print(7.0 / 2.0)
    var coins = 5
    coins += 10
    coins *= 2
    print(coins)
end
```

```output
14
20
3
1
3.5
30
```

Dividing two ints drops the remainder, and `%` gives you the remainder. Jamo won't mix ints and floats by itself, so convert one with `float(x)` or `int(x)`:

```jamo
func main()
    var count = 3
    print(float(count) * 1.5)
    print(int(4.9))
end
```

```output
4.5
4
```

**Try it:** work out how many minutes are in a week.

## 4. Text

`+` joins strings. An f-string, which starts with `f"`, puts values right inside the text.

```jamo
func main()
    var name = "Sam"
    var score = 12
    print("Hi " + name)
    print(f"{name} has {score + 1} points")
    print(len(name))
    print(name[0])
    print("one\ntwo")
    print(string(score) + "!")
end
```

```output
Hi Sam
Sam has 13 points
3
S
one
two
12!
```

`len` counts bytes, and `name[0]` gives the first byte as a string. Counting starts at 0. English letters are one byte each, but other letters can be more.

**Try it:** print a sentence with your name and age in it, using an f-string.

## 5. Making choices

`if` runs code only when something is true. `elif` and `else` are optional.

```jamo
func main()
    var score = 75
    if score >= 90
        print("Amazing")
    elif score >= 50
        print("Good")
    else
        print("Keep trying")
    end
    if score > 50 and score < 100
        print("In the middle")
    end
    if not (score == 0)
        print("Not zero")
    end
end
```

```output
Good
In the middle
Not zero
```

You can compare with `==`, `!=`, `<`, `>`, `<=` and `>=`, and combine those with `and`, `or` and `not`.

**Try it:** print whether a number is even or odd. `%` will help.

## 6. Loops

`for` counts, and it stops before the last number. `while` keeps going as long as something is true.

```jamo
func main()
    for i in 0..3
        print(i)
    end
    var left = 3
    while left > 0
        print(f"{left} left")
        left -= 1
    end
    for i in 0..10
        if i == 1
            continue
        end
        if i == 3
            break
        end
        print(i * 100)
    end
end
```

```output
0
1
2
3 left
2 left
1 left
0
200
```

`continue` skips to the next round, and `break` leaves the loop.

**Try it:** add up all the numbers from 1 to 100. You should get 5050.

## 7. Functions

A function has a name, and can take inputs and give back a value. Inputs and the answer need their types written out.

```jamo
func add(a: int, b: int): int
    return a + b
end

func greet(name: string)
    print(f"Hello, {name}!")
end

func main()
    print(add(2, 3))
    greet("Sam")
end
```

```output
5
Hello, Sam!
```

Functions can be written in any order, and can call themselves.

If a program stops with a problem while it's running, Jamo shows where, and which calls led there:

```jamo
func half(n: int, by: int): int
    return n / by
end

func main()
    print(half(4, 0))
end
```

```error
main.jamo line 2: Error: division by zero
    called from main, main.jamo line 6
```

A variable made outside every function can be used and changed by all of them:

```jamo
var score = 0

func add_points(points: int)
    score += points
end

func main()
    add_points(5)
    add_points(3)
    print(score)
end
```

```output
8
```

These are set up before `main` starts, from top to bottom, so each one's value can only use the ones above it. Keep them for things the whole program shares, like a score. Values a function needs are clearer as its inputs.

**Try it:** write a function that gives back the bigger of two numbers.

## 8. Arrays

An array is a list of values that all share one type.

```jamo
func total(items: [int]): int
    var sum = 0
    for item in items
        sum += item
    end
    return sum
end

func main()
    var items = [1, 2, 3]
    items.add(4)
    items[0] = 10
    print(items)
    print(len(items))
    print(total(items))
    print(items.pop())
    print(items)
    var names: [string] = []
    names.add("Sam")
    print(names)
end
```

```output
[10, 2, 3, 4]
4
19
4
[10, 2, 3]
["Sam"]
```

An empty `[]` needs its type written out, since Jamo has nothing to work it out from. Giving an array to another variable or a function shares it and doesn't copy it, so a change through one name shows through the other.

**Try it:** make an array of numbers, then print the largest one.

## 9. Structs

A struct groups values together. A method is a function that belongs to a struct, and `self` is the one it was called on.

```jamo
struct Enemy
    name: string
    health: int
end

func Enemy.hit(damage: int)
    self.health -= damage
end

func Enemy.is_alive(): bool
    return self.health > 0
end

func main()
    var imp = Enemy{name: "imp", health: 3}
    imp.hit(1)
    print(imp)
    print(imp.is_alive())
    imp.health = 0
    print(imp.is_alive())
    var enemies = [imp, Enemy{name: "bat", health: 1}]
    for enemy in enemies
        enemy.hit(1)
    end
    print(enemies[1].health)
end
```

```output
Enemy{name: "imp", health: 2}
true
false
0
```

Every field must be given when you make one. Structs are shared just like arrays.

**Try it:** make a `Player` struct with a score, and a method that adds to it.

## 10. Enums and match

An enum is a type with a fixed list of values. `match` picks one case to run.

```jamo
enum Mood
    CALM
    ANGRY
end

func describe(mood: Mood): string
    match mood
        case Mood.CALM
            return "relaxed"
        case Mood.ANGRY
            return "furious"
    end
end

func main()
    var mood = Mood.ANGRY
    print(describe(mood))
    print(mood == Mood.CALM)
    match 2 + 2
        case 4
            print("four")
        else
            print("something else")
    end
end
```

```output
furious
false
four
```

A `match` on an enum has to cover every value or have an `else`, so you can't forget one. `match` also works on ints and strings.

**Try it:** add a `SLEEPY` mood, and see what Jamo says before you fix `describe`.

## 11. Typing and files

`input()` reads a line that someone types. `int(text)` and `float(text)` turn text into numbers.

```jamo
func main()
    print("What's your name?")
    var name = input()
    print("How old are you?")
    var age = int(input())
    print(f"{name} will be {age + 1} next year.")
end
```

```input
Sam
12
```

```output
What's your name?
How old are you?
Sam will be 13 next year.
```

Files are just as short. `write_file` replaces the whole file:

```jamo
func main()
    write_file("score.txt", "42")
    if exists("score.txt")
        print(read_file("score.txt"))
    end
end
```

```output
42
```

If a program can't read a file, or the text isn't a number, it stops with a clear error. So check first: `exists(path)` says whether a file is there, and `is_int(text)` and `is_float(text)` from `text.jamo` say whether `int` and `float` will work:

```jamo
use "text.jamo"

func main()
    print("How many lives?")
    var typed = input()
    if is_int(typed)
        print(f"Starting with {int(typed)} lives")
    else
        print(f"{typed} isn't a whole number")
    end
end
```

```input
lots
```

```output
How many lives?
lots isn't a whole number
```

Or, when a fallback is all you need, the `_or` versions never stop the program. `int_or` and `float_or` are in `text.jamo`, and `read_file_or` and `try_write_file` in `files.jamo`:

```jamo
use "text.jamo"
use "files.jamo"

func main()
    var lives = int_or("lots", 3)
    print(lives)
    var saved = read_file_or("no save yet.txt", "new game")
    print(saved)
    print(try_write_file("no such folder\\save.txt", "level 2"))
end
```

```output
3
new game
false
```

`read_file_or` gives the fallback when the file is missing, is a folder or is in use, and `try_write_file` answers `false` instead of stopping.

`args()` gives you anything typed after the program's name, as an array of strings.

**Try it:** save someone's name to a file, and greet them by it the next time the program runs.

## 12. More than one file

`use` loads another file from the same folder. Save this one as `enemy.jamo`:

```jamo file=enemy.jamo
func make_noise(): string
    return "grr"
end
```

Then use it from your main file:

```jamo
use "enemy.jamo"

func main()
    print(make_noise())
end
```

```output
grr
```

All the names in a program share one space, so two files can't both have a function with the same name.

As a program grows, a file often has helpers that only it should use. Put `private` in front of them, and Jamo stops any other file from using them, so nobody leans on something you might change later:

```jamo file=bank.jamo
private var balance = 0

private func check_amount(amount: int)
    if amount <= 0
        fail("The amount has to be more than 0.")
    end
end

func deposit(amount: int)
    check_amount(amount)
    balance += amount
end
```

If your main file tries `balance = 1000000`, Jamo won't build it:

```output
main.jamo line 4: balance is private to bank.jamo, so only that file can use it.
```

`private` works on functions, methods, constants and variables outside functions.

## 13. A window

`use "window.jamo"` opens a real window, and reads the keyboard and mouse. It comes with Jamo, so there's nothing to download.

```jamo check-only
use "window.jamo"

func main()
    var window = open_window("My game", 640, 360)
    var x = 0
    while window.is_open()
        if window.key_down("right")
            x += 1
        end
        if window.key_pressed("space")
            print(f"Jump at {x}!")
        end
        if window.key_down("escape")
            window.close()
        end
        window.wait_frame(60)
    end
end
```

A program built the usual way also has a console window, where `print` shows up, which is handy while you're making something. When it's ready to share, `jamo build --window game.jamo` makes a game with no console behind it. It has nowhere to `print` to, so if it stops with an error, the error shows in a message box instead.

`is_open()` handles everything Windows sends, so call it once a frame. `wait_frame(60)` keeps it at 60 frames a second. A window also knows `key_released`, `key_repeated` (true again while a key is held), `typed()` (the letters typed this frame), `mouse_x`, `mouse_y`, `mouse_down("left")`, `mouse_pressed("left")`, `wheel`, `width`, `height`, `frame_time`, `set_title`, `move` and `resize`. Keys are named like `"a"`, `"5"`, `"left"`, `"space"` and `"enter"`.

## 14. Drawing

`use "gfx.jamo"` draws shapes, text and PNG pictures in a window. It loads `window.jamo` for you.

```jamo check-only
use "gfx.jamo"

func main()
    var window = open_window("Drawing", 640, 360)
    var screen = start_gfx(window)
    var x = 40.0
    while window.is_open()
        x += 2.0
        if x > 600.0
            x = 40.0
        end
        screen.clear(rgb(20, 24, 40))
        screen.rect(20.0, 300.0, 600.0, 20.0, rgb(80, 200, 120))
        screen.circle(x, 270.0, 30.0, rgb(255, 80, 60))
        screen.line(20.0, 20.0, 620.0, 60.0, 3.0, rgb(255, 210, 60))
        screen.text(f"x is {int(x)}", 20.0, 80.0, 24.0, rgb(255, 255, 255))
        screen.show()
        window.wait_frame(60)
    end
end
```

Positions are floats, counting pixels from the window's top left. Colors use `rgb(red, green, blue)` from 0 to 255, or `rgba` with a fourth number for how see-through it is. `screen.load_image("ship.png")` loads a PNG picture to draw with `screen.image(picture, x, y)` or `image_sized`. Call `show()` once a frame to put what you drew on the window.

### Sprites and animation

`use "sprite.jamo"` draws characters that move. A sprite sheet is one picture cut into frames of the same size, counted from 0, left to right and then down. You can load one with `screen.load_pixel_image("hero.png")`, which keeps pixel art sharp, or write it as text right in your code: each letter picks a color from a list, `0` to `9` and then `a` to `z`, and `.` is see-through.

```jamo check-only
use "sprite.jamo"

func main()
    var window = open_window("Blinking face", 320, 240)
    var screen = start_gfx(window)
    var yellow = rgb(255, 210, 60)
    var dark = rgb(40, 30, 20)
    # Two 6 by 6 frames side by side: eyes open, then eyes shut
    var art = screen.image_from_text([".0000..0000.", "010010000000", "000000011110", "010010000000", "001100001100", ".0000..0000."], [yellow, dark])
    var face = sheet(art, 6, 6)
    var blink = animation([0, 0, 0, 0, 1], 4.0, true)
    var x = 120.0
    var facing_left = false
    while window.is_open()
        if window.key_down("left")
            x -= 3.0
            facing_left = true
        end
        if window.key_down("right")
            x += 3.0
            facing_left = false
        end
        blink.update(window.frame_time())
        screen.clear(rgb(30, 60, 90))
        screen.sprite(face, blink.frame(), x, 90.0, 10.0, facing_left)
        screen.show()
        window.wait_frame(60)
    end
end
```

- `screen.sprite(sheet, frame, x, y, scale, flipped)` draws a frame with its top left at x, y, `scale` times its size, mirrored when `flipped` is true. `sprite_turned` draws it around its middle, turned by an angle in radians.
- `animation(frames, frames_a_second, looping)` plays a list of frame numbers. Call `update(seconds)` once a frame and draw `frame()`. One that doesn't loop stops on its last frame, and `done()` says when it has finished. `restart()` starts it over.
- Without a sheet, `image_part` draws part of any image, and `image_turned` turns and mirrors a whole one.

### Particles

`use "particles.jamo"` adds sparks, explosions and smoke. Keep one `Particles` for the game, add to it when something happens, and update and draw it every frame:

```jamo check-only
use "particles.jamo"

func main()
    var window = open_window("Sparks", 640, 480)
    var screen = start_gfx(window)
    var sparks = new_particles()
    sparks.gravity = 500.0
    while window.is_open()
        if window.mouse_pressed("left")
            sparks.burst(float(window.mouse_x()), float(window.mouse_y()), 40, rgb(255, 200, 60), 300.0, 0.8)
        end
        sparks.update(window.frame_time())
        screen.clear(rgb(20, 20, 30))
        sparks.draw(screen)
        screen.show()
        window.wait_frame(60)
    end
end
```

### Controllers

`use "gamepad.jamo"` reads Xbox-style controllers, for up to 4 players. Make one with `gamepad(0)` for player 1, call `update()` once a frame, then ask it things:

```jamo check-only
use "gfx.jamo"
use "gamepad.jamo"

func main()
    var window = open_window("Pad", 640, 480)
    var screen = start_gfx(window)
    var pad = gamepad(0)
    var x = 320.0
    var y = 240.0
    while window.is_open()
        pad.update()
        x += pad.left_x() * 5.0
        y += pad.left_y() * 5.0
        if pad.pressed("a")
            pad.rumble(0.5, 0.5)
        end
        if pad.released("a")
            pad.rumble(0.0, 0.0)
        end
        screen.clear(rgb(20, 20, 30))
        screen.circle(x, y, 20.0, rgb(90, 200, 255))
        screen.show()
        window.wait_frame(60)
    end
end
```

`down(button)` is true while a button is held, `pressed` and `released` only on the frame it changes. The buttons are `a`, `b`, `x`, `y`, `up`, `down`, `left`, `right`, `start`, `back`, `lb`, `rb`, `left_stick` and `right_stick`. `left_x()`, `left_y()`, `right_x()` and `right_y()` go from -1.0 to 1.0, with up being negative like the screen, and `left_trigger()` and `right_trigger()` from 0.0 to 1.0. A controller that isn't plugged in reads as nothing pressed, so the keyboard can always work too.

`burst(x, y, count, color, speed, life)` sends particles every way, and `spray(x, y, angle, spread, ...)` sends them one way, like a jet or a trail. `gravity` pulls them down (below 0 floats them up, like smoke), `drag` slows them, and `size` sets how big new ones are. Each one fades and shrinks as it gets old.

## 15. Sound

`use "sound.jamo"` plays WAV files and made-up tones, many at once.

```jamo check-only
use "gfx.jamo"
use "sound.jamo"

func main()
    var window = open_window("Sound", 400, 200)
    var screen = start_gfx(window)
    var speakers = start_sound()
    var beep = tone(660.0, 0.15, "square")
    var hum = tone(110.0, 2.0, "sine")
    var music = speakers.play_loop(hum)
    music.set_volume(0.3)
    while window.is_open()
        if window.key_pressed("space")
            speakers.play(beep)
        end
        speakers.update()
        screen.clear(rgb(30, 30, 50))
        screen.text("Press space to beep", 20.0, 80.0, 24.0, rgb(255, 255, 255))
        screen.show()
        window.wait_frame(60)
    end
    speakers.close()
end
```

`tone(frequency, seconds, wave)` makes a sound from a `"sine"`, `"square"`, `"saw"`, `"triangle"` or `"noise"` wave. `load_sound("boom.wav")` reads a WAV file. `play` gives back the playing sound, which you can `stop()` or `set_volume(0.5)`, and `set_volume` on the speakers changes everything. Call `update()` once a frame, or the sound runs out and stops for a moment. Up to 64 sounds play at once. A PC with no speakers shows a note and the program carries on without sound.

To make music in code, start with `silence(seconds)` and mix clips into it with `song.mix(clip, at, volume)`, where `at` is the time in seconds. Each note lands on the exact sample, so a song played as one clip keeps perfect time, and `voice.place` tells how far the mixer has got through it, in 44,100ths of a second. The Beat Run example makes its whole song this way.

## 16. Buttons, sliders and text boxes

`use "ui.jamo"` draws controls with gfx. Each control is one call, made every frame, that gives back its value.

```jamo check-only
use "ui.jamo"

func main()
    var window = open_window("Form", 420, 420)
    var screen = start_gfx(window)
    var ui = start_ui(screen)
    var name = ""
    var sound = true
    var volume = 0.5
    var colors = ["Red", "Green", "Blue"]
    var color = 0
    while window.is_open()
        screen.clear(ui.theme.background)
        ui.begin()
        ui.title("New player")
        ui.label("Name")
        name = ui.text_box(name)
        sound = ui.checkbox("Sound", sound)
        volume = ui.slider("Volume", volume, 0.0, 1.0)
        color = ui.list(colors, color, 3)
        if ui.button("Start")
            print(f"{name} picked {colors[color]}")
        end
        ui.finish()
        screen.show()
        window.wait_frame(60)
    end
end
```

Controls stack down the window between `begin()` and `finish()`, as wide as the window, so they follow it when it's resized. `panel(x, y, width, height)` draws a panel and puts the next controls inside it, `columns(2)` puts the next two side by side, and `place(x, y, width)` moves them anywhere. There's also `note(text)` for softer text and `space(height)`.

Click a control, or press Tab and Shift+Tab, to give it the keyboard. Space and Enter press buttons, Space ticks checkboxes, the arrows move sliders and lists, and text boxes take typing with Backspace, Delete, the arrows, Home and End. Change the look with `ui.theme = dark_theme()`, or set single colors like `ui.theme.accent = rgb(230, 120, 40)`.

## 17. 3D

`use "g3d.jamo"` draws 3D scenes with a camera, lights, fog, textures and models. It draws into gfx's picture, so text and shapes can go on top.

```jamo check-only
use "g3d.jamo"

func main()
    var window = open_window("3D", 800, 500)
    var screen = start_gfx(window)
    var world = start_3d(screen)
    var cube = world.cube()
    var ball = world.sphere(24)
    var ground = world.plane(1.0)
    var sky = rgb(150, 190, 230)
    world.fog(sky, 20.0, 60.0)
    var angle = 0.0
    while window.is_open()
        angle += 1.0
        screen.clear(sky)
        world.camera(vec3(6.0, 4.0, 9.0), vec3(0.0, 0.5, 0.0))
        world.draw(ground, vec3(0.0, 0.0, 0.0), vec3(0.0, 0.0, 0.0), vec3(100.0, 1.0, 100.0), rgb(90, 140, 80))
        world.draw(cube, vec3(0.0, 0.5, 0.0), vec3(0.0, angle, 0.0), vec3(1.0, 1.0, 1.0), rgb(220, 70, 60))
        world.draw(ball, vec3(2.0, 0.7, 0.0), vec3(0.0, 0.0, 0.0), vec3(1.4, 1.4, 1.4), rgb(240, 240, 240))
        screen.text("Spinning", 16.0, 16.0, 20.0, rgb(20, 30, 40))
        screen.show()
        window.wait_frame(60)
    end
end
```

`draw(mesh, place, turn, size, color)` puts a mesh at a place, turned by degrees around x, y and z, and stretched by size. y points up. `camera(eye, target)` moves the camera, and `lens(degrees, near, far)` changes how wide it sees.

The meshes are `cube()`, `sphere(detail)`, `plane(tiles)`, `load_obj("ship.obj")` for model files, and `mesh(points)` for your own triangles, with 8 floats for each corner: place, normal, then texture spot. `load_texture("crate.png")` loads a picture for `draw_textured(mesh, texture, place, turn, size)`. For light there's `sun(direction, color)`, `ambient(color)`, and up to 4 lamps with `lamp(number, place, color, reach)`. `fog(color, start, end)` fades things into the distance.

## 18. The internet

`use "net.jamo"` fetches web pages and sends text to web servers, over HTTP or HTTPS.

```jamo check-only
use "net.jamo"

func main()
    var net = start_net()
    var page = net.get("https://example.com/")
    if page.error != ""
        print(f"That didn't work: {page.error}")
    else
        print(f"Status {page.status}, {len(page.text)} bytes")
    end
    var sent = net.post("https://example.com/scores", "name=Sam&score=120")
    print(sent.status)
end
```

A request waits until the whole answer has come. `status` is the server's number, like 200 for fine or 404 for not found, and `text` is what it sent back. When something goes wrong on the way, like a server that can't be reached or takes too long, the program keeps going: `error` says what happened and `status` is 0. `set_timeout(10.0)` changes how many seconds a request may wait, 30 at first.

A game shouldn't freeze while it waits, so `start_get` and `start_post` run a request in the background. Check `done()` once a frame, and read `response()` when it says true:

```jamo check-only
use "gfx.jamo"
use "net.jamo"

func main()
    var window = open_window("Scores", 400, 200)
    var screen = start_gfx(window)
    var net = start_net()
    var scores = net.start_get("https://example.com/scores")
    var shown = "Loading..."
    while window.is_open()
        if scores.done()
            var answer = scores.response()
            if answer.error == ""
                shown = answer.text
            else
                shown = answer.error
            end
        end
        screen.clear(rgb(20, 24, 40))
        screen.text(shown, 20.0, 80.0, 20.0, rgb(255, 255, 255))
        screen.show()
        window.wait_frame(60)
    end
end
```

Many requests can run at once. `cancel()` stops one you don't need any more.

For multiplayer games, programs can talk to each other directly with links. One program listens on a port, and the others connect to it:

```jamo check-only
use "net.jamo"
extern "kernel32.dll" func Sleep(milliseconds: i32)

func main()
    var net = start_net()
    var host = net.listen_here(5000)
    var player = net.connect("localhost", 5000)
    player.send("hello from player 1")
    var players: [Link] = []
    var waiting = true
    while waiting
        for link in host.new_links()
            players.add(link)
        end
        player.messages()
        for link in players
            for message in link.messages()
                print(f"The host got: {message}")
                waiting = false
            end
        end
        Sleep(16)
    end
end
```

Each `send(text)` arrives as one whole message, in order, from `messages()`, which you call once a frame. `listen(port)` lets other PCs connect, and Windows may ask whether to let the program through its firewall. `listen_here(port)` only takes programs on the same PC. When the other side goes away, `is_open()` turns false and `problem()` says why.

**Jamo programs have the same power as any other Windows program.** They can read and change your files and reach the internet. Only run programs you trust, the same as with any `.exe`.

## 19. Math and random numbers

`use "math.jamo"` has square roots, angles, rounding and random numbers.

```jamo
use "math.jamo"

func main()
    print(sqrt(16.0))
    print(round(2.5))
    print(floor(-1.5))
    print(clamp_int(150, 0, 100))
    print(max(1.5, 4.0))
    random_seed(7)
    var roll = random_int(1, 6)
    random_seed(7)
    print(random_int(1, 6) == roll)
    var x = random_float()
    print(x >= 0.0 and x < 1.0)
end
```

```output
4.0
3
-2
100
4.0
true
true
```

The float tools are `sqrt`, `sin`, `cos` and `tan` (in radians), `atan2(y, x)`, `pow(base, power)`, `abs`, `pi()`, `min`, `max`, `clamp(value, low, high)` and `lerp(a, b, part)`, which goes `part` of the way from `a` to `b`. Ints have their own: `abs_int`, `min_int`, `max_int` and `clamp_int`. `floor`, `ceil` and `round` turn a float into an int, and `round` takes halves away from zero, so `round(-2.5)` is `-3`.

`random_int(low, high)` gives a whole number with both ends included, so `random_int(1, 6)` rolls a die. `random_float()` gives a float from 0.0 up to, but never reaching, 1.0. Each run starts from the clock, so the numbers change every time. `random_seed(n)` starts them over, and the same seed always gives the same numbers, which helps when testing a game. The numbers are fine for games, but not safe for passwords or anything secret.

## 20. Time

`use "time.jamo"` tells the time, measures it, and waits.

```jamo check-only
use "time.jamo"

func main()
    var today = now()
    print(f"Today is {today.weekday_name()}, {today.date_text()}, and it's {today.time_text()}.")
    var start = seconds_since_start()
    sleep(0.5)
    print(f"That took {seconds_since_start() - start} seconds.")
end
```

`seconds_since_start()` counts from when the program started, as a float with better than a microsecond's detail, and it never goes backward. `sleep(seconds)` waits, to about a millisecond, and `sleep_ms` and `time_ms()` do the same in whole milliseconds. `now()` gives a `DateTime` with `year`, `month`, `day`, `hour`, `minute`, `second`, `millisecond` and `weekday`, which is 0 for Sunday up to 6 for Saturday. Its `date_text()` looks like `2026-11-03`, `time_text()` like `09:05:30`, and `weekday_name()` like `Tuesday`.

## 21. Text tools

`use "text.jamo"` splits, searches and changes text, and sorts arrays.

```jamo
use "text.jamo"

func main()
    var fruit = split(trim("  apples, pears,plums  "), ",")
    for i in 0..len(fruit)
        fruit[i] = trim(fruit[i])
    end
    sort_strings(fruit)
    print(join(fruit, " + "))
    print(find("banana", "nan"))
    print(contains("banana", "x"))
    print(replace("a-b-c", "-", "/"))
    print(upper("Shout"))
    print(slice("hello", 1, 4))
    print(repeat("ab", 3))
    var scores = [30, 10, 20]
    sort_ints(scores)
    reverse_ints(scores)
    print(scores)
end
```

```output
apples + pears + plums
2
false
a/b/c
SHOUT
ell
ababab
[30, 20, 10]
```

The text tools are `split(text, by)`, `find(text, part)`, which gives -1 when it isn't there, `contains`, `starts_with`, `ends_with`, `slice(text, from, to)`, which goes up to but not including `to`, `replace(text, old, new)`, `trim`, which takes spaces, tabs and line endings off both ends, `upper` and `lower`, which change English, accented, Greek and Cyrillic letters, `letter_count(text)` and `letters(text)`, which count and split whole letters instead of bytes, `reverse_letters`, `join(parts, between)`, `repeat(text, times)`, `fixed(number, places)`, which writes a float with that many decimals, like `fixed(3.14159, 2)` for `3.14` (halves round up, the way you'd do it by hand), `commas(1234567)` for `1,234,567`, and `is_int(text)` and `is_float(text)`, which say whether `int(text)` and `float(text)` would work. Places count bytes from 0, like `text[i]`. Each gives back new text, and a place outside the text stops the program with a message saying why.

`sort_ints`, `sort_strings`, `reverse_ints` and `reverse_strings` change the array you give them, instead of making a new one. Text sorts by its bytes, so capitals come before small letters: `"Zoo"` before `"apple"`.

## 22. Dictionaries, text builders and bytes

`use "dict.jamo"` looks things up by name. A `Dict` maps text to ints, and a `TextDict` maps text to text:

```jamo
use "dict.jamo"

func main()
    var scores = new_dict()
    scores.set("Sam", 120)
    scores.set("Ava", 95)
    scores.set("Sam", 130)
    print(scores.get("Sam"))
    print(scores.has("Max"))
    print(scores.get_or("Max", 0))
    print(scores.size())
end
```

```output
130
false
0
2
```

`get` stops with an error when the key is missing, so use `has` or `get_or` when it might be. `remove` takes a key out, and `key_list()` gives every key.

`use "builder.jamo"` builds big text quickly. Joining with `+` copies everything each time, but a `TextBuilder` adds to one buffer:

```jamo
use "builder.jamo"

func main()
    var page = new_builder()
    for i in 1..4
        page.line(f"Line {i}")
    end
    page.add("The end")
    print(page.text())
end
```

```output
Line 1
Line 2
Line 3
The end
```

`use "bytes.jamo"` builds binary data, like a file format. Numbers go in little end first, and `set_u32` and the others change bytes already added, for sizes only known later. `save(path)` writes the file and `load_bytes(path)` reads one back.

## 23. Making a whole game

The quickest start is `jamo new --game my_game`, which makes a folder with a small game already working: a square you move with the arrow keys, catching coins. Change it bit by bit from there.

Here's a bigger one, made from the parts you've seen. Stars fall, you catch them, and three misses ends the game:

```jamo check-only
use "gfx.jamo"
use "sound.jamo"
use "math.jamo"

const WIDTH = 480
const HEIGHT = 640

enum Mode
    PLAYING
    OVER
end

struct Star
    x: float
    y: float
    speed: float
end

struct Game
    mode: Mode
    player_x: float
    stars: [Star]
    score: int
    missed: int
    next_star: float
end

func new_game(): Game
    return Game{mode: Mode.PLAYING, player_x: 220.0, stars: [], score: 0, missed: 0, next_star: 0.0}
end

# True when two rectangles overlap
func overlap(ax: float, ay: float, aw: float, ah: float, bx: float, by: float, bw: float, bh: float): bool
    return ax < bx + bw and bx < ax + aw and ay < by + bh and by < ay + ah
end

# Moves everything on by one frame. Answers true when a star was caught, so main can play a sound
func Game.update(window: Window): bool
    if window.key_down("left")
        self.player_x -= 6.0
    end
    if window.key_down("right")
        self.player_x += 6.0
    end
    self.player_x = clamp(self.player_x, 0.0, float(WIDTH) - 40.0)
    self.next_star -= window.frame_time()
    if self.next_star <= 0.0
        self.stars.add(Star{x: random_float() * float(WIDTH - 20), y: -20.0, speed: 2.0 + random_float() * 3.0})
        self.next_star = 0.6
    end
    var caught = false
    var still_falling: [Star] = []
    for star in self.stars
        star.y += star.speed
        if overlap(star.x, star.y, 20.0, 20.0, self.player_x, float(HEIGHT) - 50.0, 40.0, 20.0)
            self.score += 1
            caught = true
        elif star.y > float(HEIGHT)
            self.missed += 1
        else
            still_falling.add(star)
        end
    end
    self.stars = still_falling
    if self.missed >= 3
        self.mode = Mode.OVER
    end
    return caught
end

func Game.draw(screen: Gfx)
    screen.clear(rgb(14, 16, 32))
    for star in self.stars
        screen.rect(star.x, star.y, 20.0, 20.0, rgb(255, 220, 90))
    end
    screen.rect(self.player_x, float(HEIGHT) - 50.0, 40.0, 20.0, rgb(90, 200, 255))
    screen.text(f"Stars: {self.score}   Missed: {self.missed} of 3", 12.0, 12.0, 20.0, rgb(255, 255, 255))
    if self.mode == Mode.OVER
        screen.text("Game over! Space to play again", 60.0, 300.0, 24.0, rgb(255, 120, 100))
    end
end

func main()
    var window = open_window("Catch the stars", WIDTH, HEIGHT)
    var screen = start_gfx(window)
    var speakers = start_sound()
    var ding = tone(880.0, 0.08, "triangle")
    var game = new_game()
    while window.is_open()
        match game.mode
            case Mode.PLAYING
                if game.update(window)
                    speakers.play(ding)
                end
            case Mode.OVER
                if window.key_pressed("space")
                    game = new_game()
                end
        end
        speakers.update()
        game.draw(screen)
        screen.show()
        window.wait_frame(60)
    end
    speakers.close()
end
```

A few ideas hold it together, and they work for most small games:

- **Everything the game keeps track of lives in one struct,** here `Game`. `main` makes the struct and hands it to the functions that need it, so it's always clear what a function changes, and starting over is just `game = new_game()`. A variable outside functions (section 7) works too, for small things like a high score.
- **An enum says what the game is doing,** like `PLAYING` and `OVER`, and a `match` in the main loop does the right thing for each.
- **Each frame updates, then draws.** `update` moves things and says what happened, and `draw` only draws. Keeping them apart makes both easier to change.
- **Things that come and go live in an array of structs.** To take some away, build a new array of the ones that stay, like `still_falling`.
- **`frame_time()` gives the seconds since the last frame,** which makes timers that count down in real time, like `next_star`.
- **`overlap` is the usual way to tell if two things touch,** by checking their rectangles.

To pass the window, the screen or the sound to your own functions, you need their types. These are the ones the libraries give you:

| You get it from | Its type |
|---|---|
| `open_window` | `Window` |
| `start_gfx` | `Gfx` |
| `rgb` and `rgba` | `Color` |
| `screen.load_image` | `Image` |
| `start_sound` | `Sound` |
| `tone` and `load_sound` | `Clip` |
| `speakers.play` and `play_loop` | `Voice` |
| `start_ui` | `Ui` |
| `start_3d` | `G3d` |
| `vec3` | `Vec3` |
| `world.cube`, `sphere`, `plane`, `mesh` and `load_obj` | `Mesh` |
| `world.load_texture` | `Texture` |
| `start_net` | `Net` |
| `net.get` and `post`, and a request's `response()` | `Response` |
| `net.start_get` and `start_post` | `Request` |
| `net.listen` and `listen_here` | `Listener` |
| `net.connect`, and a listener's `new_links()` | `Link`, and `[Link]` |
| `now` | `DateTime` |
| `new_dict`, `new_text_dict`, `new_builder` and `new_bytes` | `Dict`, `TextDict`, `TextBuilder` and `Bytes` |
| `new_map<K, V>` | `Map<K, V>` |
| `vec2` and `vec2_from_angle` | `Vec2` |
| `new_grid<T>`, and a grid's `neighbours` | `Grid<T>`, and `[Cell]` |
| `new_timer` and `new_cooldown` | `Timer` and `Cooldown` |

**Try it:** give the player three lives and show them as small squares in a corner.

## 24. Sharing what you make

A Jamo program is one `.exe` that runs on any 64-bit Windows 10 or 11 PC, with nothing to install. To share a game:

1. Build it with `jamo build --window my_game/main.jamo`. That makes a window program with no console window behind it.
2. Send the `.exe`, along with any PNG, WAV or other files it loads, in the same folder. Zipping the folder keeps them together.

When someone downloads an `.exe` from the internet, Windows may show "Windows protected your PC", because it doesn't know who made it. That happens to any program that isn't signed with a paid certificate, not just Jamo ones. They can click "More info", then "Run anyway". It's worth telling friends that up front, so it doesn't worry them.

### Sharing a library, and using other people's

`jamo get someone/project` downloads a library from a public GitHub project into a `packages` folder beside where you run it, and says the `use` line to write:

```
jamo get sam/shapes
Got 2 files from sam/shapes into packages/shapes
Use it with: use "packages/shapes/shapes.jamo"
```

To share a library of your own, put its `.jamo` files in a public GitHub project and add a file called `jamo-package.txt` that lists them, one name per line (lines starting with `#` are notes). The first file listed is the one people `use`. A project with just one file can skip the list if the file has the project's name, like `shapes.jamo` in `shapes`. The files can `use` each other by name, since they land in the same folder.

`jamo get` only saves plain `.jamo` file names into the package's own folder, and saves nothing unless every file downloads. Read code before you run it, the same as any code from the internet.

## 25. Lists, files, JSON, the console, logs and tests

These come with Jamo too. Each is one `use` line away.

`list.jamo` finds, puts in and takes out items. These came first, so they come in an int and a string version:

```jamo
use "list.jamo"

func main()
    var scores = [30, 10, 20]
    print(contains_int(scores, 10))
    print(index_of_int(scores, 20))
    insert_int(scores, 0, 99)
    print(remove_int(scores, 1))
    print(scores)
end
```

```output
true
2
30
[99, 10, 20]
```

And these work on every kind of array, since they're generics (section 26). `pick` and `shuffle` are in `math.jamo`, next to the other random things:

```jamo
use "list.jamo"
use "math.jamo"

enum Fruit
    APPLE
    PEAR
end

func main()
    var basket = [Fruit.PEAR, Fruit.APPLE, Fruit.PEAR]
    print(has_item(basket, Fruit.APPLE))
    print(find_item(basket, Fruit.APPLE))
    print(count_item(basket, Fruit.PEAR))
    print(reversed([1.5, 2.5, 3.5]))
    print(last_item(["a", "b", "c"]))
    var deck = [1, 2, 3, 4]
    shuffle(deck)
    print(len(deck))
    print(has_item(deck, pick(deck)))
end
```

```output
true
1
2
[3.5, 2.5, 1.5]
c
4
true
```

`swap_items(items, a, b)` swaps two places too.

To sort an array of anything, give each item a key: a second array the same length. `sort_by` sorts both arrays by int keys, `sort_by_float` by float keys and `sort_by_text` by text keys, from smallest to largest. Items with equal keys stay in the order they were in. `sort_floats` sorts an array of floats.

```jamo
use "list.jamo"

struct Racer
    name: string
    time: float
end

func main()
    var racers = [Racer{name: "Mo", time: 12.5}, Racer{name: "Liv", time: 11.25}, Racer{name: "Sam", time: 12.5}]
    var times: [float] = []
    for r in racers
        times.add(r.time)
    end
    sort_by_float(racers, times)
    for r in racers
        print(f"{r.name} {r.time}")
    end
end
```

```output
Liv 11.25
Mo 12.5
Sam 12.5
```

`files.jamo` works with folders: `make_folder`, `list_files`, `is_folder`, `file_size`, `copy_file`, `move_file`, `delete_file` and `delete_folder`, and paths: `path_join`, `path_name`, `path_folder`, `path_ending` and `current_folder`. The ones that change things answer `true` when they worked, so a program can carry on when one doesn't:

```jamo
use "files.jamo"

func main()
    make_folder("saves")
    write_file(path_join("saves", "slot1.txt"), "level 3")
    print(list_files("saves"))
    print(file_size("saves\\slot1.txt"))
    print(path_ending("saves\\slot1.txt"))
    print(delete_file("saves\\nothing.txt"))
end
```

```output
["slot1.txt"]
7
.txt
false
```

`json.jamo` reads and writes JSON, which is handy for save files. `parse_json` never stops the program: when the text isn't JSON, its `error` says what's wrong and on which line:

```jamo
use "json.jamo"

func main()
    var save = json_object()
    save.set("name", json_text("Sam"))
    save.set("level", json_int(3))
    var text = to_json(save)
    print(text)
    var loaded = parse_json(text)
    print(loaded.get("level").as_int() + 1)
    print(parse_json("{oops}").error)
end
```

```output
{"name":"Sam","level":3}
4
line 1: an object's keys must be text in double quotes.
```

A value's `kind` says what it is: `JsonKind.NULL`, `BOOL`, `NUMBER`, `TEXT`, `LIST` or `OBJECT`. Make values with `json_null`, `json_bool`, `json_number`, `json_int`, `json_text`, `json_list` and `json_object`, read them with `get(key)`, `at(place)`, `size()`, `has(key)` and `as_int()`, or the fields `truth`, `number` and `text`. `to_json_pretty` writes one item a line, for files people may read.

`term.jamo` dresses up console programs: `colored(text, "green")`, `bold(text)`, `clear_screen()`, `pad_left`, `pad_right`, `table(rows)`, `progress_bar(done, total, width)` and `menu(title, options)`, which asks until a number is picked:

```jamo
use "term.jamo"

func main()
    print(table([["Name", "Score"], ["Sam", "120"], ["Ava", "95"]]))
    print(progress_bar(3, 4, 8))
end
```

```output
Name  Score
----  -----
Sam   120
Ava   95
[######--] 75%
```

`log.jamo` writes messages with the time and a level, to the screen and to a file that's added to, never replaced. Only the levels from `set_level` up are kept, `info` at first:

```jamo check-only
use "log.jamo"

func main()
    var log = new_log("game.log")
    log.info("Level 2 started")
    log.debug("Only shown after set_level(\"debug\")")
    log.warn("Low on health")
end
```

`test.jamo` checks your own code. `finish` shows a summary, and ends with exit code 1 if anything failed:

```jamo
use "test.jamo"

func add(a: int, b: int): int
    return a + b
end

func main()
    var t = new_tests()
    t.equal_int("adding", add(2, 3), 5)
    t.check("adding zero", add(4, 0) == 4)
    t.finish()
end
```

```output
All 2 checks passed.
```

## 26. Generics: one function for many types

A function that works the same for any type can say so with a name in `< >`, usually `T`. Jamo works out what `T` is from the inputs:

```jamo
func first<T>(items: [T]): T
    return items[0]
end

func main()
    print(first([3, 1, 2]))
    print(first(["apple", "pear"]))
end
```

```output
3
apple
```

Behind the scenes Jamo makes a copy of `first` for ints and another for strings, so generics run as fast as functions written out by hand. The types are still checked: `first(5)` is an error, since 5 isn't an array.

Structs can have types too. Their methods name the same types after the struct's name:

```jamo
struct Pair<A, B>
    left: A
    right: B
end

func Pair<A, B>.swap(): Pair<B, A>
    return Pair<B, A>{left: self.right, right: self.left}
end

func main()
    var p = Pair<string, int>{left: "score", right: 120}
    print(p)
    print(p.swap().left)
end
```

```output
Pair<string, int>{left: "score", right: 120}
120
```

When the inputs don't show a type, give it in `< >` after the name, like `new_map<string, int>()` below.

`use "map.jamo"` gives `Map<K, V>`, which looks things up by any key: ints, strings, bools or enums. The values can be anything:

```jamo
use "map.jamo"

struct Player
    name: string
    lives: int
end

func main()
    var players = new_map<int, Player>()
    players.set(7, Player{name: "Sam", lives: 3})
    players.set(9, Player{name: "Ava", lives: 2})
    players.get(7).lives -= 1
    print(players.get(7))
    print(players.has(8))
    print(players.size())
    var counts = new_map<string, int>()
    for word in ["a", "b", "a"]
        counts.set(word, counts.get_or(word, 0) + 1)
    end
    print(counts.get("a"))
    print(counts.key_list())
end
```

```output
Player{name: "Sam", lives: 2}
false
2
2
["a", "b"]
```

A `Map` has the same `get`, `get_or`, `has`, `remove`, `size` and `key_list` as a `Dict`, and `value_list` too. `hash_of(key)` gives the number a `Map` uses to place a key, if you build a table of your own.

One thing to watch: after a name, a `<` whose matching `>` comes straight before `(` reads as types. So `both(a < b, c > (d))` reads as a call to `a<b, c>`. Write `c > d` without the brackets, and it's a comparison again.

Try it: write `func last_or<T>(items: [T], fallback: T): T` that gives the last item, or `fallback` when the array is empty, and call it with ints and with strings.

## 27. Game helpers: points, grids and timers

`use "vec.jamo"` gives `Vec2`, an x and a y, for places, speeds and directions. Each method gives back a new one:

```jamo
use "vec.jamo"

func main()
    var place = vec2(10.0, 20.0)
    var speed = vec2(3.0, 4.0)
    place = place.plus(speed.times(2.0))
    print(place)
    print(speed.length())
    print(speed.normalized())
    print(place.distance(vec2(10.0, 20.0)))
end
```

```output
Vec2{x: 16.0, y: 28.0}
5.0
Vec2{x: 0.6, y: 0.8}
10.0
```

`minus`, `dot`, `angle`, `rotated`, `lerp_to` and `limited` (for a top speed) are there too, and `vec2_from_angle(angle, length)` makes a direction from an angle.

`use "grid.jamo"` gives `Grid<T>`, a board of any type, for tile maps, mazes and puzzles. Places count from 0, with x going right and y going down:

```jamo
use "grid.jamo"

enum Tile
    FLOOR
    WALL
end

func main()
    var map = new_grid<Tile>(5, 3, Tile.FLOOR)
    for x in 0..5
        map.set(x, 0, Tile.WALL)
    end
    print(map.get(2, 0))
    print(map.count(Tile.WALL))
    print(map.inside(5, 1))
    print(map.get_or(9, 9, Tile.WALL))
    print(len(map.neighbours(0, 2, false)))
end
```

```output
Tile.WALL
5
false
Tile.WALL
2
```

`get` stops with an error that says the grid's size when a place is off the edge, so use `inside` or `get_or` when it might be. `neighbours(x, y, diagonals)` gives the `Cell`s next to a place, `fill` sets every cell, and `copy` makes a new grid.

`use "timer.jamo"` gives a `Timer`, which fires every so many seconds, and a `Cooldown`, which needs a rest between uses:

```jamo
use "timer.jamo"

func main()
    var gun = new_cooldown(0.3)
    print(gun.try_use_at(1.0))
    print(gun.try_use_at(1.1))
    print(gun.try_use_at(1.4))
    var spawn = new_timer_at(2.0, 0.0)
    print(spawn.ready_at(1.0))
    print(spawn.ready_at(2.0))
    print(spawn.ready_at(2.5))
end
```

```output
true
false
true
false
true
false
```

The `_at` versions take the time, for a game with its own clock that can pause. Without `_at`, like `gun.try_use()` and `spawn.ready()`, they use the real time from `seconds_since_start()`.

Try it: make a 10 by 10 `Grid<bool>`, set a few cells to `true`, and print how many of a cell's 8 neighbours are `true`, the first step of a Minesweeper.

## 28. When things go wrong: try and catch

A run error, like a bad number, a missing file or going past the end of an array, stops the program. Put the code that might go wrong inside `try`, and say what to do instead after `catch`:

```jamo
func main()
    var typed = ["12", "twelve", "7"]
    var total = 0
    for text in typed
        try
            total += int(text)
        catch problem
            print("Skipped: " + problem)
        end
    end
    print(total)
end
```

```output
Skipped: can't turn "twelve" into an int
19
```

- The name after `catch` holds the error's words as a string. Leave it out, with just `catch`, when you don't need them.
- An error anywhere inside the `try`, even deep inside functions it calls, jumps straight to the `catch`. The rest of the `try` is skipped. With no error, the `catch` is skipped.
- `fail("words")` makes your own error, and a `try` catches it like any other. That's how a function says "I can't do this" and lets the code that called it decide what to do.
- `try` can go inside `try`. An error goes to the nearest `catch`, and an error inside a `catch` goes to the next one out.
- `return`, `break` and `continue` work inside both parts.
- Running out of memory can't be caught, and neither can an error inside a function Windows calls back, like a window's message handler. Those still stop the program.

```jamo
func safe_divide(a: int, b: int): int
    try
        return a / b
    catch
        return 0
    end
end

func check_age(age: int): int
    if age < 0
        fail(f"an age can't be {age}")
    end
    return age
end

func main()
    print(safe_divide(10, 2))
    print(safe_divide(10, 0))
    try
        print(check_age(30))
        print(check_age(-4))
    catch problem
        print(problem)
    end
end
```

```output
5
0
30
an age can't be -4
```

Try it: ask for numbers with `input()` until one is typed correctly, using `try` around `int(input())`.

## 29. Functions as values

A function's name without `( )` is the function itself, as a value. It can go in a variable, an array or a struct, and be passed to another function, which calls it like any function. Its type is written `func(int, int): bool`: the inputs' types in `( )`, then `:` and the answer's type, or nothing after the `)` when it gives back nothing.

```jamo
use "list.jamo"

struct Player
    name: string
    score: int
end

func by_score(a: Player, b: Player): bool
    return a.score > b.score
end

func double(n: int): int
    return n * 2
end

func apply(change: func(int): int, x: int): int
    return change(x)
end

func main()
    var players = [Player{name: "Ana", score: 30}, Player{name: "Bo", score: 70}, Player{name: "Cy", score: 50}]
    sort_with(players, by_score)
    for p in players
        print(p.name)
    end
    print(apply(double, 21))
    var steps: [func(int): int] = [double, double]
    var n = 5
    for step in steps
        n = step(n)
    end
    print(n)
end
```

```output
Bo
Cy
Ana
42
20
```

- `sort_with(items, before)` sorts any array with your own rule: `before(a, b)` says whether `a` comes first. `filtered(items, keep)` keeps the items where `keep` is true, and `mapped(items, change)` gives a new array of `change(item)` for each. All three are in `list.jamo`.
- A struct can hold a function, and calling it looks like calling a method: with `on_click: func(int): int` in a `Button`, `button.on_click(3)` calls whatever function is in it. That's handy for buttons and game events.
- A generic function can take functions too, like `func best<T>(items: [T], better: func(T, T): bool): T`, and its types are worked out from them.
- Only named Jamo functions can be values. Methods, generic functions and `extern` Windows functions can't, so write a small function that calls one, and use that. Function values can't be compared with `==`, and printing one shows its type.

### Functions written in place

A small function can be written right where it's needed: `func`, its inputs, then `=>` and what it gives back. It can use the variables around it, and keeps their values from when it was made:

```jamo
use "list.jamo"

func make_adder(k: int): func(int): int
    return func(x: int): int => x + k
end

func main()
    var limit = 50
    var prices = [10, 80, 45, 51, 3]
    print(filtered(prices, func(p: int): bool => p < limit))
    var add5 = make_adder(5)
    print(add5(1))
    var steps = [make_adder(1), make_adder(10)]
    print(steps[1](2))
    var say = func(name: string) => print_hello(name)
    say("Sam")
end

func print_hello(name: string)
    print("Hello, " + name)
end
```

```output
[10, 45, 3]
6
12
Hello, Sam
```

- The answer's type after the `)` can be left out, and Jamo works it out from what's after `=>`.
- It keeps a copy of each variable it uses, made when the function is. Changing the variable afterwards doesn't change the function. Arrays and structs are shared as always, so adding to an array it uses works.
- Any value holding a function can be called straight away, like `steps[1](2)` or `make_adder(5)(1)`.

For more than one expression, leave out `=>` and write lines, then `end`. A function can also go inside another one, by name. Both keep copies of the variables around them, the same way:

```jamo
use "list.jamo"

func main()
    var limit = 50
    func cheap(price: int): bool
        if price < 0
            return false
        end
        return price < limit
    end
    print(filtered([10, 80, -5, 45], cheap))
    func double(n: int): int => n * 2
    print(double(21))
    var shout = func(word: string): string
        var loud = word + "!"
        return loud + loud
    end
    print(shout("hey"))
end
```

```output
[10, 45]
42
hey!hey!
```

- A function inside a function is made where it's written, like a variable, so it can't call itself. For that, write it outside.
- It can't change the variables it copied. To share a value that changes, keep it in an array or a struct: adding to an array it uses works.

Try it: make an array of three functions that each change a number, and run a number through all of them in a loop.

## 30. Maybe there, maybe not, and giving back two things

`use "maybe.jamo"` helps when a function might not find anything, or needs to give back more than one value.

```jamo
use "maybe.jamo"

func divide(a: int, b: int): Maybe<int>
    if b == 0
        return none<int>()
    end
    return some(a / b)
end

func min_and_max(items: [int]): Pair<int, int>
    var low = items[0]
    var high = items[0]
    for n in items
        if n < low
            low = n
        end
        if n > high
            high = n
        end
    end
    return pair(low, high)
end

func main()
    var answer = divide(10, 0)
    if answer.has()
        print(answer.get())
    else
        print("can't divide by 0")
    end
    print(divide(9, 3).or_else(0))
    var spread = min_and_max([4, -2, 9])
    print(f"from {spread.first} to {spread.second}")
    print(find_first([3, 8, 11], func(n: int): bool => n > 5).or_else(-1))
end
```

```output
can't divide by 0
3
from -2 to 9
8
```

`some(value)` holds a value and `none<int>()` holds nothing (its type has to be given). `has()` says which, `get()` gives the value (and stops with a clear error when there isn't one), and `or_else(fallback)` gives the value or the fallback. `find_first(items, keep)` gives the first item `keep` says yes to. `pair(a, b)` and `triple(a, b, c)` bundle values into a `Pair` or `Triple`, read with `.first`, `.second` and `.third`.

## 31. Testing with jamo test

As a program grows, a change in one place can break something somewhere else. Tests catch that. Put them in a file whose name ends in `_test.jamo`, next to the code they check. Each test is a function whose name starts with `test_`, and it has no `main`:

```jamo file=score_test.jamo
use "test.jamo"

func points_for(hits: int): int
    return hits * 100
end

func test_points()
    expect_equal(points_for(3), 300)
end

func test_no_hits()
    expect_equal(points_for(0), 0)
end
```

Then type `jamo test`. It finds every `_test.jamo` file in the folder and the folders inside it, and runs each test as its own little program, so one that crashes can't stop the rest:

```output
score_test.jamo
  ok    test_points
  ok    test_no_hits

2 tests passed.
```

When a test fails, you see what went wrong and the line it happened on, along with anything the test printed:

```output
  FAIL  test_points
        Error: got 200, wanted 300
            called from test_points, score_test.jamo line 8
```

The checks are `expect(ok)`, `expect_equal(got, want)` for whole numbers, `expect_text(got, want)` and `expect_near(got, want, close)` for floats. `jamo test some_folder` tests just that folder, and `jamo test score_test.jamo` just that file. Each test runs in its own file's folder, so it can read files that sit beside it.

## What Jamo can't do yet

Jamo is young, so it's better to know this up front:

- It only runs on Windows.
- `len` and `text[i]` count bytes. `letter_count` and `letters` count whole letters, and `upper` and `lower` change English, accented, Greek and Cyrillic letters, but not every language's.

## Where to go next

The `examples` folder has sixteen small programs to read and change. In the console: a guessing game, a calculator, a to-do list, a text adventure, a word counter, a prime finder and a lunar lander game. In a window: Sky Hopper (a platformer), Beat Run (a rhythm game with music made in code), Star Guard (an arena shooter that also works with a controller), bouncing balls with sound, Breakout, Snake, a sign-up form and a spinning 3D scene. And `fetch_page.jamo` downloads a web page. `LANGUAGE.md` has the exact rules of the language.
