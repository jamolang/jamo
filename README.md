# Jamo

Jamo is a small programming language for Windows, built for making real things.
Its goal is simple: one download, code that's easy to read, and real `.exe` files.

**Jamo is in early development and isn't ready to use yet.**

## What it will look like

    func add(a: int, b: int): int
        return a + b
    end

    func main()
        var total = add(2, 3)
        print(total)
    end

This example shows the planned language. It doesn't run yet.

## The plan

- A compiler that writes real x86-64 Windows programs by itself
- Strings, floats, arrays, structs, modules and files
- Automatic memory cleanup
- Clear error messages, a VS Code extension and a guide
- Graphics, UI, sound and internet
- A compiler written in Jamo itself
