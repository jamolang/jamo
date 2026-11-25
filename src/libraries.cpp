// Made by libraries/embed.py from the .jamo files in libraries. Change those, not this.
#include "libraries.hpp"

const char* builtin_library(const std::string& name) {
    if (name == "builder.jamo") return R"JAMOLIBRARY(# builder: builds big text quickly. use "builder.jamo"
#
# Joining text with + copies all of it each time, so building a big text piece by piece gets slow.
# A TextBuilder keeps the pieces in one buffer that doubles when it's full, and makes the text once
# at the end.

struct TextBuilder
    buffer: int
    capacity: int
    used: int
end

func new_builder(): TextBuilder
    return TextBuilder{buffer: memory(256), capacity: 256, used: 0}
end

func TextBuilder.room(extra: int)
    if self.used + extra <= self.capacity
        return
    end
    var size = self.capacity
    while self.used + extra > size
        size = size * 2
    end
    var bigger = memory(size)
    for i in 0..(self.used + 7) / 8
        poke_i64(bigger, i * 8, peek_i64(self.buffer, i * 8))
    end
    self.buffer = bigger
    self.capacity = size
end

func TextBuilder.add(text: string)
    var count = len(text)
    self.room(count)
    for i in 0..count
        poke_u8(self.buffer, self.used + i, byte(text, i))
    end
    self.used += count
end

# Adds the text, then a new line
func TextBuilder.line(text: string)
    self.add(text)
    self.room(1)
    poke_u8(self.buffer, self.used, 10)
    self.used += 1
end

func TextBuilder.add_int(value: int)
    self.add(string(value))
end

# How many bytes it holds so far
func TextBuilder.length(): int
    return self.used
end

# Everything added, as one text
func TextBuilder.text(): string
    return memory_text(self.buffer, self.used)
end

# Starts over, keeping its buffer
func TextBuilder.clear()
    self.used = 0
end
)JAMOLIBRARY";
    if (name == "bytes.jamo") return R"JAMOLIBRARY(# bytes: builds and reads binary data, like a file format. use "bytes.jamo"
#
# A Bytes holds bytes in one buffer that doubles when it's full. Numbers go in and out little end
# first, the way Windows files and x86-64 store them. set_ changes bytes already added, which helps
# when a size or a place is only known later.

struct Bytes
    buffer: int
    capacity: int
    used: int
end

func new_bytes(): Bytes
    return Bytes{buffer: memory(256), capacity: 256, used: 0}
end

# The bytes of a file
func load_bytes(path: string): Bytes
    var data = read_file(path)
    var b = new_bytes()
    b.add_text(data)
    return b
end

func Bytes.room(extra: int)
    if self.used + extra <= self.capacity
        return
    end
    var size = self.capacity
    while self.used + extra > size
        size = size * 2
    end
    var bigger = memory(size)
    for i in 0..(self.used + 7) / 8
        poke_i64(bigger, i * 8, peek_i64(self.buffer, i * 8))
    end
    self.buffer = bigger
    self.capacity = size
end

func bytes_fits(value: int, low: int, high: int, kind: string)
    if value < low or value > high
        fail(f"{value} doesn't fit in {kind}, which goes from {low} to {high}.")
    end
end

# A byte, from 0 to 255
func Bytes.add_u8(value: int)
    bytes_fits(value, 0, 255, "a byte")
    self.room(1)
    poke_u8(self.buffer, self.used, value)
    self.used += 1
end

# 2 bytes, from 0 to 65535
func Bytes.add_u16(value: int)
    bytes_fits(value, 0, 65535, "2 bytes")
    self.room(2)
    poke_u16(self.buffer, self.used, value)
    self.used += 2
end

# 4 bytes, for anything from -2147483648 to 4294967295
func Bytes.add_u32(value: int)
    bytes_fits(value, -2147483648, 4294967295, "4 bytes")
    self.room(4)
    poke_i32(self.buffer, self.used, value)
    self.used += 4
end

# 8 bytes: any int
func Bytes.add_u64(value: int)
    self.room(8)
    poke_i64(self.buffer, self.used, value)
    self.used += 8
end

# The text's bytes, with nothing added at the end
func Bytes.add_text(text: string)
    var count = len(text)
    self.room(count)
    for i in 0..count
        poke_u8(self.buffer, self.used + i, byte(text, i))
    end
    self.used += count
end

# Zero bytes until the length is a multiple of size, like 512 for a file section
func Bytes.pad_to(size: int)
    if size < 1
        fail(f"pad_to needs a size of at least 1, not {size}.")
    end
    while self.used % size != 0
        self.add_u8(0)
    end
end

func Bytes.length(): int
    return self.used
end

func Bytes.check_place(at: int, size: int)
    if at < 0 or at + size > self.used
        fail(f"There's no room for {size} bytes at {at}: this holds {self.used} bytes.")
    end
end

func Bytes.get_u8(at: int): int
    self.check_place(at, 1)
    return peek_u8(self.buffer, at)
end

func Bytes.get_u16(at: int): int
    self.check_place(at, 2)
    return peek_u16(self.buffer, at)
end

# 4 bytes read as a number from 0 to 4294967295
func Bytes.get_u32(at: int): int
    self.check_place(at, 4)
    var value = peek_i32(self.buffer, at)
    if value < 0
        value += 4294967296
    end
    return value
end

func Bytes.get_u64(at: int): int
    self.check_place(at, 8)
    return peek_i64(self.buffer, at)
end

func Bytes.set_u8(at: int, value: int)
    bytes_fits(value, 0, 255, "a byte")
    self.check_place(at, 1)
    poke_u8(self.buffer, at, value)
end

func Bytes.set_u16(at: int, value: int)
    bytes_fits(value, 0, 65535, "2 bytes")
    self.check_place(at, 2)
    poke_u16(self.buffer, at, value)
end

func Bytes.set_u32(at: int, value: int)
    bytes_fits(value, -2147483648, 4294967295, "4 bytes")
    self.check_place(at, 4)
    poke_i32(self.buffer, at, value)
end

func Bytes.set_u64(at: int, value: int)
    self.check_place(at, 8)
    poke_i64(self.buffer, at, value)
end

# All the bytes as text, which can hold any bytes
func Bytes.text(): string
    return memory_text(self.buffer, self.used)
end

func Bytes.save(path: string)
    write_file(path, self.text())
end
)JAMOLIBRARY";
    if (name == "dict.jamo") return R"JAMOLIBRARY(# dict: look things up by name. use "dict.jamo"
#
# Dict maps text to ints, and TextDict maps text to text. Both are hash tables: a key's hash picks a
# place, and if that place is taken, the next ones are tried. A removed key leaves a marker, so keys
# placed after it can still be found. The table doubles when it's two thirds full.

struct Dict
    keys: [string]
    values: [int]
    states: [int]
    count: int
    taken: int
end

struct TextDict
    keys: [string]
    values: [string]
    states: [int]
    count: int
    taken: int
end

# --- Hashing, shared by both ---

# FNV-1a over the key's bytes, kept between 0 and 2^62 so it's never below 0
func dict_hash(key: string): int
    var hash = 1469598103934665603
    for i in 0..len(key)
        hash = (hash + byte(key, i)) * 1099511628211
    end
    hash = hash % 4611686018427387904
    if hash < 0
        hash += 4611686018427387904
    end
    return hash
end

# Where a key is, or -1 - where it can go when it's missing. states: 0 empty, 1 in use, 2 removed
func dict_find(keys: [string], states: [int], key: string): int
    var size = len(states)
    var place = dict_hash(key) % size
    var free = -1
    while true
        var state = states[place]
        if state == 0
            if free >= 0
                return -1 - free
            end
            return -1 - place
        end
        if state == 1 and keys[place] == key
            return place
        end
        if state == 2 and free < 0
            free = place
        end
        place = (place + 1) % size
    end
    return -1
end

func dict_empty_states(size: int): [int]
    var states: [int] = []
    for i in 0..size
        states.add(0)
    end
    return states
end

# --- Dict: text to ints ---

func new_dict(): Dict
    var keys: [string] = []
    var values: [int] = []
    for i in 0..8
        keys.add("")
        values.add(0)
    end
    return Dict{keys: keys, values: values, states: dict_empty_states(8), count: 0, taken: 0}
end

func Dict.set(key: string, value: int)
    var place = dict_find(self.keys, self.states, key)
    if place >= 0
        self.values[place] = value
        return
    end
    place = -1 - place
    if self.states[place] == 0
        self.taken += 1
    end
    self.keys[place] = key
    self.values[place] = value
    self.states[place] = 1
    self.count += 1
    if self.taken * 3 >= len(self.states) * 2
        self.grow()
    end
end

func Dict.grow()
    var old_keys = self.keys
    var old_values = self.values
    var old_states = self.states
    var size = len(old_states) * 2
    if self.count * 3 < len(old_states)
        size = len(old_states)
    end
    self.keys = []
    self.values = []
    for i in 0..size
        self.keys.add("")
        self.values.add(0)
    end
    self.states = dict_empty_states(size)
    self.count = 0
    self.taken = 0
    for i in 0..len(old_states)
        if old_states[i] == 1
            self.set(old_keys[i], old_values[i])
        end
    end
end

# The value for a key. A missing key stops with an error, so check has() first when it may be missing
func Dict.get(key: string): int
    var place = dict_find(self.keys, self.states, key)
    if place < 0
        fail(f"This dict has no key {key}.")
    end
    return self.values[place]
end

# The value for a key, or the fallback when it's missing
func Dict.get_or(key: string, fallback: int): int
    var place = dict_find(self.keys, self.states, key)
    if place < 0
        return fallback
    end
    return self.values[place]
end

func Dict.has(key: string): bool
    return dict_find(self.keys, self.states, key) >= 0
end

# Takes a key out. Answers whether it was there
func Dict.remove(key: string): bool
    var place = dict_find(self.keys, self.states, key)
    if place < 0
        return false
    end
    self.states[place] = 2
    self.keys[place] = ""
    self.count -= 1
    return true
end

func Dict.size(): int
    return self.count
end

# Every key, in no particular order
func Dict.key_list(): [string]
    var found: [string] = []
    for i in 0..len(self.states)
        if self.states[i] == 1
            found.add(self.keys[i])
        end
    end
    return found
end

# --- TextDict: text to text ---

func new_text_dict(): TextDict
    var keys: [string] = []
    var values: [string] = []
    for i in 0..8
        keys.add("")
        values.add("")
    end
    return TextDict{keys: keys, values: values, states: dict_empty_states(8), count: 0, taken: 0}
end

func TextDict.set(key: string, value: string)
    var place = dict_find(self.keys, self.states, key)
    if place >= 0
        self.values[place] = value
        return
    end
    place = -1 - place
    if self.states[place] == 0
        self.taken += 1
    end
    self.keys[place] = key
    self.values[place] = value
    self.states[place] = 1
    self.count += 1
    if self.taken * 3 >= len(self.states) * 2
        self.grow()
    end
end

func TextDict.grow()
    var old_keys = self.keys
    var old_values = self.values
    var old_states = self.states
    var size = len(old_states) * 2
    if self.count * 3 < len(old_states)
        size = len(old_states)
    end
    self.keys = []
    self.values = []
    for i in 0..size
        self.keys.add("")
        self.values.add("")
    end
    self.states = dict_empty_states(size)
    self.count = 0
    self.taken = 0
    for i in 0..len(old_states)
        if old_states[i] == 1
            self.set(old_keys[i], old_values[i])
        end
    end
end

func TextDict.get(key: string): string
    var place = dict_find(self.keys, self.states, key)
    if place < 0
        fail(f"This dict has no key {key}.")
    end
    return self.values[place]
end

func TextDict.get_or(key: string, fallback: string): string
    var place = dict_find(self.keys, self.states, key)
    if place < 0
        return fallback
    end
    return self.values[place]
end

func TextDict.has(key: string): bool
    return dict_find(self.keys, self.states, key) >= 0
end

func TextDict.remove(key: string): bool
    var place = dict_find(self.keys, self.states, key)
    if place < 0
        return false
    end
    self.states[place] = 2
    self.keys[place] = ""
    self.values[place] = ""
    self.count -= 1
    return true
end

func TextDict.size(): int
    return self.count
end

func TextDict.key_list(): [string]
    var found: [string] = []
    for i in 0..len(self.states)
        if self.states[i] == 1
            found.add(self.keys[i])
        end
    end
    return found
end
)JAMOLIBRARY";
    if (name == "files.jamo") return R"JAMOLIBRARY(# files: folders, paths, copying, moving, deleting and listing files. use "files.jamo"
#
# read_file, write_file and exists are built in. These do the rest. The ones that change things answer true when
# they worked and false when they didn't, like a file that's in use, so a program can carry on and say why.

use "text.jamo"

extern "kernel32.dll" func CreateDirectoryW(name: int, security: int): bool
extern "kernel32.dll" func RemoveDirectoryW(name: int): bool
extern "kernel32.dll" func DeleteFileW(name: int): bool
extern "kernel32.dll" func CopyFileW(from: int, to: int, fail_if_there: bool): bool
extern "kernel32.dll" func MoveFileExW(from: int, to: int, flags: i32): bool
extern "kernel32.dll" func FindFirstFileW(pattern: int, data: int): int
extern "kernel32.dll" func FindNextFileW(handle: int, data: int): bool
extern "kernel32.dll" func FindClose(handle: int): bool
extern "kernel32.dll" func GetFileAttributesW(name: int): i32
extern "kernel32.dll" func GetFileAttributesExW(name: int, level: i32, data: int): bool
extern "kernel32.dll" func GetCurrentDirectoryW(size: i32, place: int): i32

const FILES_FOLDER_BIT = 16
const FILES_FIND_DATA_SIZE = 600
const FILES_NAME_AT = 44
# MOVEFILE_REPLACE_EXISTING and MOVEFILE_COPY_ALLOWED, so a move can go to another drive and replace what's there
const FILES_MOVE_FLAGS = 3

# --- Text from Windows ---

# Turns UTF-16 text that ends in a zero, the way Windows gives it, into Jamo's UTF-8 text
func files_from_wide(place: int): string
    var units = 0
    while peek_u16(place, units * 2) != 0
        units += 1
    end
    var out = memory(units * 3 + 1)
    var size = 0
    var i = 0
    while i < units
        var code = peek_u16(place, i * 2)
        i += 1
        if code >= 55296 and code < 56320 and i < units
            var low = peek_u16(place, i * 2)
            if low >= 56320 and low < 57344
                code = 65536 + (code - 55296) * 1024 + (low - 56320)
                i += 1
            end
        end
        if code < 128
            poke_u8(out, size, code)
            size += 1
        elif code < 2048
            poke_u8(out, size, 192 + code / 64)
            poke_u8(out, size + 1, 128 + code % 64)
            size += 2
        elif code < 65536
            poke_u8(out, size, 224 + code / 4096)
            poke_u8(out, size + 1, 128 + code / 64 % 64)
            poke_u8(out, size + 2, 128 + code % 64)
            size += 3
        else
            poke_u8(out, size, 240 + code / 262144)
            poke_u8(out, size + 1, 128 + code / 4096 % 64)
            poke_u8(out, size + 2, 128 + code / 64 % 64)
            poke_u8(out, size + 3, 128 + code % 64)
            size += 4
        end
    end
    return memory_text(out, size)
end

# --- Paths ---

func files_is_separator(code: int): bool
    return code == 47 or code == 92
end

# A folder and a name joined with a backslash, like path_join("saves", "slot1.txt") gives saves\\slot1.txt
func path_join(folder: string, name: string): string
    if folder == ""
        return name
    end
    if files_is_separator(byte(folder, len(folder) - 1))
        return folder + name
    end
    return folder + "\\" + name
end

# Where the last part of the path starts
func files_last_part(path: string): int
    var at = len(path)
    while at > 0 and not files_is_separator(byte(path, at - 1))
        at -= 1
    end
    return at
end

# The last part of a path: path_name("games\\ship.png") is ship.png
func path_name(path: string): string
    return slice(path, files_last_part(path), len(path))
end

# The folder a path is in: path_folder("games\\ship.png") is games. A path with no folder gives ""
func path_folder(path: string): string
    var at = files_last_part(path)
    if at == 0
        return ""
    end
    return slice(path, 0, at - 1)
end

# The ending after the name's last dot, like ".png", or "" when there isn't one
func path_ending(path: string): string
    var name = path_name(path)
    var dot = len(name) - 1
    while dot > 0 and byte(name, dot) != 46
        dot -= 1
    end
    if dot <= 0
        return ""
    end
    return slice(name, dot, len(name))
end

# The folder the program is running in
func current_folder(): string
    var place = memory(65536)
    var size = GetCurrentDirectoryW(32768, place)
    if size <= 0 or size >= 32768
        return ""
    end
    return files_from_wide(place)
end

# --- Looking ---

func is_folder(path: string): bool
    var attributes = GetFileAttributesW(wide(path))
    return attributes != -1 and attributes / FILES_FOLDER_BIT % 2 == 1
end

# A file's size in bytes, or -1 when there's no such file
func file_size(path: string): int
    var data = memory(40)
    if not GetFileAttributesExW(wide(path), 0, data)
        return -1
    end
    if peek_i32(data, 0) / FILES_FOLDER_BIT % 2 == 1
        return -1
    end
    # The size comes in two 4-byte halves, and the low one counts from 0 up to 4294967295
    var high = peek_i32(data, 28)
    var low = peek_i32(data, 32)
    if low < 0
        low += 4294967296
    end
    return high * 4294967296 + low
end

# The names of everything in a folder, files and folders alike, sorted. A folder that isn't there gives []
func list_files(folder: string): [string]
    var names: [string] = []
    var data = memory(FILES_FIND_DATA_SIZE)
    var pattern = path_join(folder, "*")
    if folder == ""
        pattern = "*"
    end
    var handle = FindFirstFileW(wide(pattern), data)
    if handle == -1
        return names
    end
    var more = true
    while more
        var name = files_from_wide(data + FILES_NAME_AT)
        if name != "." and name != ".."
            names.add(name)
        end
        more = FindNextFileW(handle, data)
    end
    FindClose(handle)
    sort_strings(names)
    return names
end

# --- Changing ---

# Makes a folder, and any folders it's in that aren't there yet. True if it's there afterward
func make_folder(path: string): bool
    if path == "" or is_folder(path)
        return path != ""
    end
    var parent = path_folder(path)
    if parent != "" and not is_folder(parent) and len(parent) > 2
        make_folder(parent)
    end
    CreateDirectoryW(wide(path), 0)
    return is_folder(path)
end

func delete_file(path: string): bool
    return DeleteFileW(wide(path))
end

# Deletes a folder, which must be empty
func delete_folder(path: string): bool
    return RemoveDirectoryW(wide(path))
end

# Copies a file, replacing one that's already at to
func copy_file(from: string, to: string): bool
    return CopyFileW(wide(from), wide(to), false)
end

# Moves or renames a file or folder, replacing a file that's already at to
func move_file(from: string, to: string): bool
    return MoveFileExW(wide(from), wide(to), FILES_MOVE_FLAGS)
end
)JAMOLIBRARY";
    if (name == "g3d.jamo") return R"JAMOLIBRARY(# g3d: 3D drawing with a camera, lights, fog, textures and meshes, on top of gfx. use "g3d.jamo"
#
# g3d draws into gfx's picture, which has a depth buffer, so 2D drawing like text can go on top
# afterward. It switches OpenGL to its own settings when it draws, and gfx switches them back.
# A mesh is a list of triangles, each corner 8 floats: place x, y and z, the normal's x, y and z, and
# the texture spot's u and v. Matrices are 16 32-bit floats, a column at a time, as OpenGL takes them.

use "gfx.jamo"

extern "msvcrt.dll" func sin(value: float): float
extern "msvcrt.dll" func cos(value: float): float
extern "msvcrt.dll" func tan(value: float): float
extern "OpenGL" func glUniformMatrix4fv(place: i32, count: i32, transpose: bool, values: int)
extern "OpenGL" func glUniform3f(place: i32, x: f32, y: f32, z: f32)
extern "OpenGL" func glUniform4f(place: i32, x: f32, y: f32, z: f32, w: f32)
extern "OpenGL" func glUniform1i(place: i32, value: i32)
extern "OpenGL" func glUniform1f(place: i32, value: f32)
extern "OpenGL" func glGenerateMipmap(target: i32)

struct Vec3
    x: float
    y: float
    z: float
end

struct Mesh
    vertex_array: int
    buffer: int
    count: int
    open: bool
end

struct Texture
    id: int
    width: int
    height: int
    open: bool
end

struct G3d
    gfx: Gfx
    shader: int
    places: [int]
    eye: Vec3
    target: Vec3
    field: float
    near: float
    far: float
    sun_direction: Vec3
    sun_color: Color
    ambient_color: Color
    fog_color: Color
    fog_start: float
    fog_end: float
    fog_on: bool
    lamp_places: [Vec3]
    lamp_colors: [Color]
    lamp_reaches: [float]
    changed: bool
    seen_width: int
    seen_height: int
    matrices: int
    white: Texture
    meshes: [Mesh]
    textures: [Texture]
    open: bool
    closed_by_program: bool
end

func vec3(x: float, y: float, z: float): Vec3
    return Vec3{x: x, y: y, z: z}
end

# --- Starting ---

func start_3d(g: Gfx): G3d
    g.must_be_open()
    var vertex = "#version 330 core\nlayout(location = 0) in vec3 place;\nlayout(location = 1) in vec3 normal;\nlayout(location = 2) in vec2 spot;\nuniform mat4 model;\nuniform mat4 view_projection;\nuniform mat4 normal_matrix;\nout vec3 world;\nout vec3 bent;\nout vec2 uv;\nvoid main() {\n    vec4 w = model * vec4(place, 1.0);\n    world = w.xyz;\n    bent = mat3(normal_matrix) * normal;\n    uv = spot;\n    gl_Position = view_projection * w;\n}\n"
    var fragment = "#version 330 core\nin vec3 world;\nin vec3 bent;\nin vec2 uv;\nuniform sampler2D picture;\nuniform vec4 color;\nuniform vec3 sun_direction;\nuniform vec3 sun_color;\nuniform vec3 ambient;\nuniform vec3 eye;\nuniform vec3 fog;\nuniform vec3 fog_color;\nuniform vec3 lamp_place[4];\nuniform vec3 lamp_color[4];\nuniform float lamp_reach[4];\nout vec4 result;\nvoid main() {\n    vec3 n = normalize(bent);\n    if (!gl_FrontFacing) n = -n;\n    vec4 base = color * texture(picture, uv);\n    vec3 light = ambient + sun_color * max(dot(n, -sun_direction), 0.0);\n    for (int i = 0; i < 4; i++) {\n        if (lamp_reach[i] > 0.0) {\n            vec3 d = lamp_place[i] - world;\n            float dist = max(length(d), 0.0001);\n            float fall = clamp(1.0 - dist / lamp_reach[i], 0.0, 1.0);\n            light += lamp_color[i] * max(dot(n, d / dist), 0.0) * fall * fall;\n        }\n    }\n    vec3 shade = base.rgb * light;\n    if (fog.z > 0.5) {\n        float f = clamp((distance(world, eye) - fog.x) / max(fog.y - fog.x, 0.0001), 0.0, 1.0);\n        shade = mix(shade, fog_color, f);\n    }\n    result = vec4(shade, base.a);\n}\n"
    var program = glCreateProgram()
    var first = gfx_compile(35633, vertex)
    var second = gfx_compile(35632, fragment)
    glAttachShader(program, first)
    glAttachShader(program, second)
    glLinkProgram(program)
    var result = memory(8)
    glGetProgramiv(program, 35714, result)
    if peek_i32(result, 0) == 0
        fail("The graphics card couldn't link g3d's drawing code.")
    end
    glDeleteShader(first)
    glDeleteShader(second)
    var names = ["model", "view_projection", "normal_matrix", "picture", "color", "sun_direction", "sun_color", "ambient", "eye", "fog", "fog_color"]
    var places: [int] = []
    for name in names
        places.add(glGetUniformLocation(program, gfx_c_text(name)))
    end
    for i in 0..4
        places.add(glGetUniformLocation(program, gfx_c_text(f"lamp_place[{i}]")))
        places.add(glGetUniformLocation(program, gfx_c_text(f"lamp_color[{i}]")))
        places.add(glGetUniformLocation(program, gfx_c_text(f"lamp_reach[{i}]")))
    end
    var g3 = G3d{gfx: g, shader: program, places: places, eye: vec3(0.0, 2.0, 6.0), target: vec3(0.0, 0.0, 0.0), field: 60.0, near: 0.1, far: 500.0, sun_direction: g3d_normalize(vec3(-0.4, -1.0, -0.3)), sun_color: rgb(204, 204, 204), ambient_color: rgb(77, 77, 77), fog_color: rgb(0, 0, 0), fog_start: 0.0, fog_end: 0.0, fog_on: false, lamp_places: [], lamp_colors: [], lamp_reaches: [], changed: true, seen_width: 0, seen_height: 0, matrices: memory(64 * 5), white: Texture{id: 0, width: 1, height: 1, open: true}, meshes: [], textures: [], open: true, closed_by_program: false}
    for i in 0..4
        g3.lamp_places.add(vec3(0.0, 0.0, 0.0))
        g3.lamp_colors.add(rgb(0, 0, 0))
        g3.lamp_reaches.add(0.0)
    end
    var white = memory(4)
    poke_i32(white, 0, -1)
    g3.white = g3.make_texture(white, 1, 1)
    return g3
end

func G3d.must_be_open()
    if not self.open
        fail("This 3D drawing is closed, so it can't be used.")
    end
    self.gfx.must_be_open()
end

# Switches OpenGL to g3d's settings, after drawing what gfx has waiting
func G3d.enter()
    if not self.gfx.in_3d
        self.gfx.flush()
        self.gfx.in_3d = true
        glUseProgram(self.shader)
        glEnable(2929)
        self.changed = true
    end
end

# --- The scene ---

# Puts the camera at eye, looking at target
func G3d.camera(eye: Vec3, target: Vec3)
    self.must_be_open()
    self.eye = eye
    self.target = target
    self.changed = true
end

# How wide the camera sees, in degrees, and the nearest and farthest it draws
func G3d.lens(field: float, near: float, far: float)
    self.must_be_open()
    if not (field > 0.0 and field < 180.0)
        fail(f"A lens sees from above 0.0 to below 180.0 degrees, not {field}.")
    end
    if not (near > 0.0 and far > near)
        fail(f"A lens needs 0.0 < near < far, but they're {near} and {far}.")
    end
    self.field = field
    self.near = near
    self.far = far
    self.changed = true
end

# Sunlight shining in a direction, like vec3(0.0, -1.0, 0.0) for straight down
func G3d.sun(direction: Vec3, color: Color)
    self.must_be_open()
    self.sun_direction = g3d_normalize(direction)
    self.sun_color = color
    self.changed = true
end

# The light that reaches everything, even in shade
func G3d.ambient(color: Color)
    self.must_be_open()
    self.ambient_color = color
    self.changed = true
end

# One of 4 lamps, 0 to 3, lighting things nearer than reach
func G3d.lamp(number: int, place: Vec3, color: Color, reach: float)
    self.must_be_open()
    if number < 0 or number > 3
        fail(f"g3d has lamps 0 to 3, not {number}.")
    end
    if not (reach > 0.0)
        fail(f"A lamp's reach must be above 0.0, not {reach}.")
    end
    self.lamp_places[number] = place
    self.lamp_colors[number] = color
    self.lamp_reaches[number] = reach
    self.changed = true
end

func G3d.lamp_off(number: int)
    self.must_be_open()
    if number < 0 or number > 3
        fail(f"g3d has lamps 0 to 3, not {number}.")
    end
    self.lamp_reaches[number] = 0.0
    self.changed = true
end

# Things fade into the fog color from start to end, counting from the camera
func G3d.fog(color: Color, start: float, end_at: float)
    self.must_be_open()
    if not (end_at > start)
        fail(f"Fog must end farther than it starts, but they're {start} and {end_at}.")
    end
    self.fog_color = color
    self.fog_start = start
    self.fog_end = end_at
    self.fog_on = true
    self.changed = true
end

func G3d.no_fog()
    self.must_be_open()
    self.fog_on = false
    self.changed = true
end

# Sends the camera, lights and fog to the graphics card, when they've changed
func G3d.send_scene()
    var width = self.gfx.width
    var height = self.gfx.height
    if not self.changed and width == self.seen_width and height == self.seen_height
        return
    end
    self.changed = false
    self.seen_width = width
    self.seen_height = height
    var view = self.matrices
    var projection = self.matrices + 64
    var both = self.matrices + 128
    g3d_look(view, self.eye, self.target)
    var aspect = 1.0
    if height > 0
        aspect = float(width) / float(height)
    end
    g3d_perspective(projection, self.field, aspect, self.near, self.far)
    g3d_multiply(both, projection, view)
    var p = self.places
    glUniformMatrix4fv(p[1], 1, false, both)
    glUniform1i(p[3], 0)
    glUniform3f(p[5], self.sun_direction.x, self.sun_direction.y, self.sun_direction.z)
    glUniform3f(p[6], self.sun_color.red, self.sun_color.green, self.sun_color.blue)
    glUniform3f(p[7], self.ambient_color.red, self.ambient_color.green, self.ambient_color.blue)
    glUniform3f(p[8], self.eye.x, self.eye.y, self.eye.z)
    var fog_on = 0.0
    if self.fog_on
        fog_on = 1.0
    end
    glUniform3f(p[9], self.fog_start, self.fog_end, fog_on)
    glUniform3f(p[10], self.fog_color.red, self.fog_color.green, self.fog_color.blue)
    for i in 0..4
        var place = self.lamp_places[i]
        var color = self.lamp_colors[i]
        glUniform3f(p[11 + i * 3], place.x, place.y, place.z)
        glUniform3f(p[12 + i * 3], color.red, color.green, color.blue)
        glUniform1f(p[13 + i * 3], self.lamp_reaches[i])
    end
end

# --- Drawing ---

# Draws a mesh at a place, turned by degrees around x, y and z, and stretched by size
func G3d.draw(mesh: Mesh, place: Vec3, turn: Vec3, size: Vec3, color: Color)
    self.draw_with(mesh, self.white, place, turn, size, color)
end

func G3d.draw_textured(mesh: Mesh, texture: Texture, place: Vec3, turn: Vec3, size: Vec3)
    if not texture.open
        fail("This texture is closed, so it can't be drawn.")
    end
    self.draw_with(mesh, texture, place, turn, size, rgb(255, 255, 255))
end

func G3d.draw_with(mesh: Mesh, texture: Texture, place: Vec3, turn: Vec3, size: Vec3, color: Color)
    self.must_be_open()
    if not mesh.open
        fail("This mesh is closed, so it can't be drawn.")
    end
    self.enter()
    self.send_scene()
    var model = self.matrices + 192
    var normals = self.matrices + 256
    g3d_model(model, normals, place, turn, size)
    var p = self.places
    glUniformMatrix4fv(p[0], 1, false, model)
    glUniformMatrix4fv(p[2], 1, false, normals)
    glUniform4f(p[4], color.red, color.green, color.blue, color.alpha)
    glBindTexture(3553, texture.id)
    glBindVertexArray(mesh.vertex_array)
    glDrawArrays(4, 0, mesh.count)
end

# --- Matrices ---

func g3d_set(matrix: int, row: int, column: int, value: float)
    poke_f32(matrix, (column * 4 + row) * 4, value)
end

func g3d_get(matrix: int, row: int, column: int): float
    return peek_f32(matrix, (column * 4 + row) * 4)
end

func g3d_clear(matrix: int)
    for i in 0..16
        poke_f32(matrix, i * 4, 0.0)
    end
end

func g3d_multiply(result: int, a: int, b: int)
    for row in 0..4
        for column in 0..4
            var total = 0.0
            for k in 0..4
                total += g3d_get(a, row, k) * g3d_get(b, k, column)
            end
            g3d_set(result, row, column, total)
        end
    end
end

func g3d_normalize(v: Vec3): Vec3
    var length = sqrt(v.x * v.x + v.y * v.y + v.z * v.z)
    if not (length > 0.000000001)
        return vec3(0.0, 0.0, -1.0)
    end
    return vec3(v.x / length, v.y / length, v.z / length)
end

func g3d_cross(a: Vec3, b: Vec3): Vec3
    return vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x)
end

# The camera's matrix. Looking straight up or down, the top of the screen points along -z or +z
func g3d_look(matrix: int, eye: Vec3, target: Vec3)
    var forward = g3d_normalize(vec3(target.x - eye.x, target.y - eye.y, target.z - eye.z))
    var up = vec3(0.0, 1.0, 0.0)
    if forward.y > 0.9999 or forward.y < -0.9999
        if forward.y < 0.0
            up = vec3(0.0, 0.0, -1.0)
        else
            up = vec3(0.0, 0.0, 1.0)
        end
    end
    var side = g3d_normalize(g3d_cross(forward, up))
    var top = g3d_cross(side, forward)
    g3d_clear(matrix)
    g3d_set(matrix, 0, 0, side.x)
    g3d_set(matrix, 0, 1, side.y)
    g3d_set(matrix, 0, 2, side.z)
    g3d_set(matrix, 0, 3, -(side.x * eye.x + side.y * eye.y + side.z * eye.z))
    g3d_set(matrix, 1, 0, top.x)
    g3d_set(matrix, 1, 1, top.y)
    g3d_set(matrix, 1, 2, top.z)
    g3d_set(matrix, 1, 3, -(top.x * eye.x + top.y * eye.y + top.z * eye.z))
    g3d_set(matrix, 2, 0, -forward.x)
    g3d_set(matrix, 2, 1, -forward.y)
    g3d_set(matrix, 2, 2, -forward.z)
    g3d_set(matrix, 2, 3, forward.x * eye.x + forward.y * eye.y + forward.z * eye.z)
    g3d_set(matrix, 3, 3, 1.0)
end

func g3d_perspective(matrix: int, field: float, aspect: float, near: float, far: float)
    var f = 1.0 / tan(field * 3.141592653589793 / 360.0)
    g3d_clear(matrix)
    g3d_set(matrix, 0, 0, f / aspect)
    g3d_set(matrix, 1, 1, f)
    g3d_set(matrix, 2, 2, (far + near) / (near - far))
    g3d_set(matrix, 2, 3, 2.0 * far * near / (near - far))
    g3d_set(matrix, 3, 2, -1.0)
end

# Moves, turns (around y, then x, then z) and stretches. The normals turn the same way but stretch the
# other way, so lighting stays right on stretched meshes
func g3d_model(model: int, normals: int, place: Vec3, turn: Vec3, size: Vec3)
    var degree = 3.141592653589793 / 180.0
    var cx = cos(turn.x * degree)
    var sx = sin(turn.x * degree)
    var cy = cos(turn.y * degree)
    var sy = sin(turn.y * degree)
    var cz = cos(turn.z * degree)
    var sz = sin(turn.z * degree)
    var a00 = cy
    var a01 = sy * sx
    var a02 = sy * cx
    var a10 = 0.0
    var a11 = cx
    var a12 = 0.0 - sx
    var a20 = 0.0 - sy
    var a21 = cy * sx
    var a22 = cy * cx
    g3d_clear(model)
    g3d_clear(normals)
    g3d_column(model, normals, 0, a00 * cz + a01 * sz, a10 * cz + a11 * sz, a20 * cz + a21 * sz, size.x)
    g3d_column(model, normals, 1, a01 * cz - a00 * sz, a11 * cz - a10 * sz, a21 * cz - a20 * sz, size.y)
    g3d_column(model, normals, 2, a02, a12, a22, size.z)
    g3d_set(model, 0, 3, place.x)
    g3d_set(model, 1, 3, place.y)
    g3d_set(model, 2, 3, place.z)
    g3d_set(model, 3, 3, 1.0)
    g3d_set(normals, 3, 3, 1.0)
end

# One turned column of both matrices, stretched one way for the model and the other for normals
func g3d_column(model: int, normals: int, column: int, x: float, y: float, z: float, stretch: float)
    g3d_set(model, 0, column, x * stretch)
    g3d_set(model, 1, column, y * stretch)
    g3d_set(model, 2, column, z * stretch)
    var away = stretch
    if away < 0.000001 and away > -0.000001
        away = 0.000001
    end
    g3d_set(normals, 0, column, x / away)
    g3d_set(normals, 1, column, y / away)
    g3d_set(normals, 2, column, z / away)
end

# --- Meshes ---

# A mesh from triangles: every 3 corners make one, and each corner is 8 floats (see the top)
func G3d.mesh(points: [float]): Mesh
    if len(points) % 24 != 0
        fail(f"A mesh needs 8 floats for each corner and 3 corners for each triangle, so a multiple of 24 floats, not {len(points)}.")
    end
    var data = memory(len(points) * 4 + 4)
    for i in 0..len(points)
        poke_f32(data, i * 4, points[i])
    end
    return self.make_mesh(data, len(points) / 8)
end

func G3d.make_mesh(data: int, count: int): Mesh
    self.must_be_open()
    self.enter()
    var ids = memory(8)
    glGenVertexArrays(1, ids)
    var vertex_array = peek_i32(ids, 0)
    glBindVertexArray(vertex_array)
    glGenBuffers(1, ids)
    var buffer = peek_i32(ids, 0)
    glBindBuffer(34962, buffer)
    glBufferData(34962, count * 32, data, 35044)
    glVertexAttribPointer(0, 3, 5126, false, 32, 0)
    glVertexAttribPointer(1, 3, 5126, false, 32, 12)
    glVertexAttribPointer(2, 2, 5126, false, 32, 24)
    glEnableVertexAttribArray(0)
    glEnableVertexAttribArray(1)
    glEnableVertexAttribArray(2)
    var mesh = Mesh{vertex_array: vertex_array, buffer: buffer, count: count, open: true}
    self.meshes.add(mesh)
    return mesh
end

func Mesh.close()
    if not self.open
        fail("This mesh is already closed.")
    end
    self.open = false
    var ids = memory(8)
    poke_i32(ids, 0, self.buffer)
    glDeleteBuffers(1, ids)
    poke_i32(ids, 0, self.vertex_array)
    glDeleteVertexArrays(1, ids)
end

func g3d_corner(points: [float], x: float, y: float, z: float, nx: float, ny: float, nz: float, u: float, v: float)
    points.add(x)
    points.add(y)
    points.add(z)
    points.add(nx)
    points.add(ny)
    points.add(nz)
    points.add(u)
    points.add(v)
end

# One side of a cube, as two triangles: a corner, then the two directions across the side
func g3d_side(points: [float], c: Vec3, a: Vec3, b: Vec3, n: Vec3)
    g3d_corner(points, c.x, c.y, c.z, n.x, n.y, n.z, 0.0, 1.0)
    g3d_corner(points, c.x + a.x, c.y + a.y, c.z + a.z, n.x, n.y, n.z, 1.0, 1.0)
    g3d_corner(points, c.x + a.x + b.x, c.y + a.y + b.y, c.z + a.z + b.z, n.x, n.y, n.z, 1.0, 0.0)
    g3d_corner(points, c.x, c.y, c.z, n.x, n.y, n.z, 0.0, 1.0)
    g3d_corner(points, c.x + a.x + b.x, c.y + a.y + b.y, c.z + a.z + b.z, n.x, n.y, n.z, 1.0, 0.0)
    g3d_corner(points, c.x + b.x, c.y + b.y, c.z + b.z, n.x, n.y, n.z, 0.0, 0.0)
end

# A cube 1 wide, centered on its place
func G3d.cube(): Mesh
    var points: [float] = []
    var h = 0.5
    g3d_side(points, vec3(-h, -h, h), vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0), vec3(0.0, 0.0, 1.0))
    g3d_side(points, vec3(h, -h, -h), vec3(-1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0), vec3(0.0, 0.0, -1.0))
    g3d_side(points, vec3(h, -h, h), vec3(0.0, 0.0, -1.0), vec3(0.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0))
    g3d_side(points, vec3(-h, -h, -h), vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 0.0), vec3(-1.0, 0.0, 0.0))
    g3d_side(points, vec3(-h, h, h), vec3(1.0, 0.0, 0.0), vec3(0.0, 0.0, -1.0), vec3(0.0, 1.0, 0.0))
    g3d_side(points, vec3(-h, -h, -h), vec3(1.0, 0.0, 0.0), vec3(0.0, 0.0, 1.0), vec3(0.0, -1.0, 0.0))
    return self.mesh(points)
end

# A flat square 1 wide on the ground, facing up, with its texture repeated tiles times each way
func G3d.plane(tiles: float): Mesh
    var points: [float] = []
    g3d_corner(points, -0.5, 0.0, 0.5, 0.0, 1.0, 0.0, 0.0, tiles)
    g3d_corner(points, 0.5, 0.0, 0.5, 0.0, 1.0, 0.0, tiles, tiles)
    g3d_corner(points, 0.5, 0.0, -0.5, 0.0, 1.0, 0.0, tiles, 0.0)
    g3d_corner(points, -0.5, 0.0, 0.5, 0.0, 1.0, 0.0, 0.0, tiles)
    g3d_corner(points, 0.5, 0.0, -0.5, 0.0, 1.0, 0.0, tiles, 0.0)
    g3d_corner(points, -0.5, 0.0, -0.5, 0.0, 1.0, 0.0, 0.0, 0.0)
    return self.mesh(points)
end

# A ball 1 wide. More detail makes it rounder, from 3 to 64
func G3d.sphere(detail: int): Mesh
    if detail < 3 or detail > 64
        fail(f"A sphere's detail goes from 3 to 64, not {detail}.")
    end
    var rings = detail
    var around = detail * 2
    var points: [float] = []
    var pi = 3.141592653589793
    for ring in 0..rings
        for step in 0..around
            var corners = [ring, step, ring + 1, step, ring + 1, step + 1, ring, step, ring + 1, step + 1, ring, step + 1]
            for c in 0..6
                var r = corners[c * 2]
                var s = corners[c * 2 + 1]
                var down = float(r) / float(rings) * pi
                var turn = float(s) / float(around) * 2.0 * pi
                var nx = sin(down) * cos(turn)
                var ny = cos(down)
                var nz = 0.0 - sin(down) * sin(turn)
                g3d_corner(points, nx * 0.5, ny * 0.5, nz * 0.5, nx, ny, nz, float(s) / float(around), float(r) / float(rings))
            end
        end
    end
    return self.mesh(points)
end

# --- Textures ---

# Loads a PNG picture to wrap onto meshes. It repeats past the edges
func G3d.load_texture(path: string): Texture
    var picture = decode_png(read_file(path))
    return self.make_texture(picture.pixels, picture.width, picture.height)
end

func G3d.make_texture(pixels: int, width: int, height: int): Texture
    self.must_be_open()
    self.enter()
    var ids = memory(8)
    glGenTextures(1, ids)
    var id = peek_i32(ids, 0)
    glBindTexture(3553, id)
    glPixelStorei(3317, 1)
    glTexImage2D(3553, 0, 32856, width, height, 0, 6408, 5121, pixels)
    glGenerateMipmap(3553)
    glTexParameteri(3553, 10241, 9987)
    glTexParameteri(3553, 10240, 9729)
    glTexParameteri(3553, 10242, 10497)
    glTexParameteri(3553, 10243, 10497)
    var texture = Texture{id: id, width: width, height: height, open: true}
    self.textures.add(texture)
    return texture
end

func Texture.close()
    if not self.open
        fail("This texture is already closed.")
    end
    self.open = false
    var ids = memory(8)
    poke_i32(ids, 0, self.id)
    glDeleteTextures(1, ids)
end

# --- OBJ model files ---

func g3d_bad_obj(path: string, line: int, why: string)
    fail(f"{path} line {line} can't be read as a model: {why}.")
end

# Reads a number written from place up to finish, like 12, -0.5 or 1.5e-3
func g3d_number(data: string, place: int, finish: int, path: string, line: int): float
    var at = place
    var sign = 1.0
    if at < finish and (byte(data, at) == 45 or byte(data, at) == 43)
        if byte(data, at) == 45
            sign = -1.0
        end
        at += 1
    end
    var value = 0.0
    var digits = 0
    while at < finish and byte(data, at) >= 48 and byte(data, at) <= 57
        value = value * 10.0 + float(byte(data, at) - 48)
        digits += 1
        at += 1
    end
    if at < finish and byte(data, at) == 46
        at += 1
        var scale = 0.1
        while at < finish and byte(data, at) >= 48 and byte(data, at) <= 57
            value += float(byte(data, at) - 48) * scale
            scale = scale / 10.0
            digits += 1
            at += 1
        end
    end
    if digits == 0
        g3d_bad_obj(path, line, f"{g3d_part(data, place, finish)} isn't a number")
    end
    if at < finish and (byte(data, at) == 101 or byte(data, at) == 69)
        at += 1
        var power_sign = 1
        if at < finish and (byte(data, at) == 45 or byte(data, at) == 43)
            if byte(data, at) == 45
                power_sign = -1
            end
            at += 1
        end
        var power = 0
        var power_digits = 0
        while at < finish and byte(data, at) >= 48 and byte(data, at) <= 57
            power = power * 10 + byte(data, at) - 48
            power_digits += 1
            at += 1
        end
        if power_digits == 0 or power > 300
            g3d_bad_obj(path, line, f"{g3d_part(data, place, finish)} isn't a number")
        end
        for i in 0..power
            if power_sign > 0
                value = value * 10.0
            else
                value = value / 10.0
            end
        end
    end
    if at != finish
        g3d_bad_obj(path, line, f"{g3d_part(data, place, finish)} isn't a number")
    end
    return sign * value
end

func g3d_part(words: string, from: int, to: int): string
    var part = ""
    var last = to
    if last - from > 30
        last = from + 30
    end
    for i in from..last
        part = part + words[i]
    end
    return part
end

# One face corner's number for a list of this many, counting from 1, or from the end when below 0
func g3d_index(data: string, place: int, finish: int, count: int, kind: string, path: string, line: int): int
    var number = int(g3d_number(data, place, finish, path, line))
    var index = number - 1
    if number < 0
        index = count + number
    end
    if number == 0 or index < 0 or index >= count
        g3d_bad_obj(path, line, f"there's no {kind} number {number}")
    end
    return index
end

# Reads an OBJ model: its points (v), texture spots (vt), normals (vn) and faces (f). Faces with more
# than 3 corners are cut into triangles, and faces without normals get flat ones
func G3d.load_obj(path: string): Mesh
    var data = read_file(path)
    var spots: [float] = []
    var uvs: [float] = []
    var bends: [float] = []
    var points: [float] = []
    var line = 1
    var place = 0
    var size = len(data)
    while place < size
        var finish = place
        while finish < size and byte(data, finish) != 10
            finish += 1
        end
        var words: [int] = []
        var at = place
        while at < finish
            while at < finish and (byte(data, at) == 32 or byte(data, at) == 9 or byte(data, at) == 13)
                at += 1
            end
            if at < finish
                if byte(data, at) == 35
                    at = finish
                else
                    var start = at
                    while at < finish and byte(data, at) != 32 and byte(data, at) != 9 and byte(data, at) != 13
                        at += 1
                    end
                    words.add(start)
                    words.add(at)
                end
            end
        end
        if len(words) > 0
            var first = words[0]
            var length = words[1] - words[0]
            var a = byte(data, first)
            var b = 0
            if length > 1
                b = byte(data, first + 1)
            end
            if length == 1 and a == 118
                if len(words) < 8
                    g3d_bad_obj(path, line, "a point needs x, y and z")
                end
                for k in 1..4
                    spots.add(g3d_number(data, words[k * 2], words[k * 2 + 1], path, line))
                end
            elif length == 2 and a == 118 and b == 116
                if len(words) < 4
                    g3d_bad_obj(path, line, "a texture spot needs at least u")
                end
                uvs.add(g3d_number(data, words[2], words[3], path, line))
                if len(words) >= 6
                    uvs.add(g3d_number(data, words[4], words[5], path, line))
                else
                    uvs.add(0.0)
                end
            elif length == 2 and a == 118 and b == 110
                if len(words) < 8
                    g3d_bad_obj(path, line, "a normal needs x, y and z")
                end
                for k in 1..4
                    bends.add(g3d_number(data, words[k * 2], words[k * 2 + 1], path, line))
                end
            elif length == 1 and a == 102
                var corners = len(words) / 2 - 1
                if corners < 3
                    g3d_bad_obj(path, line, "a face needs at least 3 corners")
                end
                var face: [int] = []
                for k in 1..corners + 1
                    var start = words[k * 2]
                    var stop = words[k * 2 + 1]
                    var parts = [start, stop, -1, -1, -1, -1]
                    var slash = start
                    var found = 0
                    while slash < stop
                        if byte(data, slash) == 47
                            if found == 0
                                parts[1] = slash
                                parts[2] = slash + 1
                                parts[3] = stop
                            else
                                parts[3] = slash
                                parts[4] = slash + 1
                                parts[5] = stop
                            end
                            found += 1
                        end
                        slash += 1
                    end
                    if found > 2
                        g3d_bad_obj(path, line, "a face corner has more than 3 parts")
                    end
                    face.add(g3d_index(data, parts[0], parts[1], len(spots) / 3, "point", path, line))
                    if parts[2] >= 0 and parts[3] > parts[2]
                        face.add(g3d_index(data, parts[2], parts[3], len(uvs) / 2, "texture spot", path, line))
                    else
                        face.add(-1)
                    end
                    if parts[4] >= 0 and parts[5] > parts[4]
                        face.add(g3d_index(data, parts[4], parts[5], len(bends) / 3, "normal", path, line))
                    else
                        face.add(-1)
                    end
                end
                for k in 1..corners - 1
                    var three = [0, k, k + 1]
                    var p = [0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0]
                    for c in 0..3
                        var s = face[three[c] * 3]
                        p[c * 3] = spots[s * 3]
                        p[c * 3 + 1] = spots[s * 3 + 1]
                        p[c * 3 + 2] = spots[s * 3 + 2]
                    end
                    var flat = g3d_normalize(g3d_cross(vec3(p[3] - p[0], p[4] - p[1], p[5] - p[2]), vec3(p[6] - p[0], p[7] - p[1], p[8] - p[2])))
                    for c in 0..3
                        var t = face[three[c] * 3 + 1]
                        var n = face[three[c] * 3 + 2]
                        var u = 0.0
                        var v = 0.0
                        if t >= 0
                            u = uvs[t * 2]
                            v = 1.0 - uvs[t * 2 + 1]
                        end
                        var normal = flat
                        if n >= 0
                            normal = vec3(bends[n * 3], bends[n * 3 + 1], bends[n * 3 + 2])
                        end
                        g3d_corner(points, p[c * 3], p[c * 3 + 1], p[c * 3 + 2], normal.x, normal.y, normal.z, u, v)
                    end
                end
            end
        end
        place = finish + 1
        line += 1
    end
    if len(points) == 0
        fail(f"{path} has no faces, so there's no model to draw.")
    end
    return self.mesh(points)
end

# --- Closing ---

func G3d.close()
    if self.closed_by_program
        fail("This 3D drawing is already closed.")
    end
    self.closed_by_program = true
    if not self.open
        return
    end
    self.open = false
    for mesh in self.meshes
        if mesh.open
            mesh.close()
        end
    end
    for texture in self.textures
        if texture.open
            texture.close()
        end
    end
    glDeleteProgram(self.shader)
    if self.gfx.open
        self.gfx.back_to_2d()
    end
end
)JAMOLIBRARY";
    if (name == "gfx.jamo") return R"JAMOLIBRARY(# gfx: 2D drawing in a window, with OpenGL 3.3. use "gfx.jamo"
#
# Shapes, text and pictures collect in a batch and go to the graphics card together. Everything draws into
# a picture the size of the window first, which show() copies onto the window. Rectangles, circles, lines
# and text all come from one texture, the atlas, so they can share a batch: letters from the Segoe UI font
# fill the top in cells 40 wide and 48 tall, with a soft circle and a white square below them. Each letter
# starts 4 pixels down its cell, so its lowest parts stay clear of the letter below when text is drawn small.

use "window.jamo"
use "png.jamo"

extern "user32.dll" func GetDC(window: int): int
extern "user32.dll" func ReleaseDC(window: int, dc: int): i32
extern "gdi32.dll" func ChoosePixelFormat(dc: int, format: int): i32
extern "gdi32.dll" func SetPixelFormat(dc: int, number: i32, format: int): bool
extern "gdi32.dll" func SwapBuffers(dc: int): bool
extern "gdi32.dll" func CreateCompatibleDC(dc: int): int
extern "gdi32.dll" func CreateDIBSection(dc: int, info: int, usage: i32, bits: int, section: int, offset: i32): int
extern "gdi32.dll" func SelectObject(dc: int, thing: int): int
extern "gdi32.dll" func CreateFontW(height: i32, width: i32, escapement: i32, orientation: i32, weight: i32, italic: i32, underline: i32, strike_out: i32, char_set: i32, precision: i32, clip: i32, quality: i32, pitch: i32, face: int): int
extern "gdi32.dll" func SetTextColor(dc: int, color: i32): i32
extern "gdi32.dll" func SetBkColor(dc: int, color: i32): i32
extern "gdi32.dll" func TextOutW(dc: int, x: i32, y: i32, text: int, count: i32): bool
extern "gdi32.dll" func GetCharWidth32W(dc: int, first: i32, last: i32, widths: int): bool
extern "gdi32.dll" func GdiFlush(): bool
extern "gdi32.dll" func DeleteObject(thing: int): bool
extern "gdi32.dll" func DeleteDC(dc: int): bool
extern "kernel32.dll" func CreateFileW(path: int, access: i32, share: i32, security: int, how: i32, flags: i32, template: int): int
extern "kernel32.dll" func WriteFile(file: int, data: int, size: i32, written: int, overlapped: int): bool
extern "kernel32.dll" func CloseHandle(thing: int): bool
extern "msvcrt.dll" func sqrt(value: float): float
extern "opengl32.dll" func wglCreateContext(dc: int): int
extern "opengl32.dll" func wglMakeCurrent(dc: int, context: int): bool
extern "opengl32.dll" func wglDeleteContext(context: int): bool
extern "OpenGL" func wglCreateContextAttribsARB(dc: int, share: int, attributes: int): int
extern "OpenGL" func glViewport(x: i32, y: i32, width: i32, height: i32)
extern "OpenGL" func glClearColor(red: f32, green: f32, blue: f32, alpha: f32)
extern "OpenGL" func glClear(mask: i32)
extern "OpenGL" func glEnable(what: i32)
extern "OpenGL" func glDisable(what: i32)
extern "OpenGL" func glBlendFunc(source: i32, target: i32)
extern "OpenGL" func glGenVertexArrays(count: i32, place: int)
extern "OpenGL" func glBindVertexArray(array: i32)
extern "OpenGL" func glDeleteVertexArrays(count: i32, place: int)
extern "OpenGL" func glGenBuffers(count: i32, place: int)
extern "OpenGL" func glBindBuffer(target: i32, buffer: i32)
extern "OpenGL" func glBufferData(target: i32, size: int, data: int, usage: i32)
extern "OpenGL" func glBufferSubData(target: i32, offset: int, size: int, data: int)
extern "OpenGL" func glDeleteBuffers(count: i32, place: int)
extern "OpenGL" func glVertexAttribPointer(index: i32, size: i32, kind: i32, normalized: bool, stride: i32, offset: int)
extern "OpenGL" func glEnableVertexAttribArray(index: i32)
extern "OpenGL" func glCreateShader(kind: i32): i32
extern "OpenGL" func glShaderSource(shader: i32, count: i32, texts: int, lengths: int)
extern "OpenGL" func glCompileShader(shader: i32)
extern "OpenGL" func glGetShaderiv(shader: i32, what: i32, place: int)
extern "OpenGL" func glGetShaderInfoLog(shader: i32, size: i32, length: int, log: int)
extern "OpenGL" func glDeleteShader(shader: i32)
extern "OpenGL" func glCreateProgram(): i32
extern "OpenGL" func glAttachShader(program: i32, shader: i32)
extern "OpenGL" func glLinkProgram(program: i32)
extern "OpenGL" func glGetProgramiv(program: i32, what: i32, place: int)
extern "OpenGL" func glUseProgram(program: i32)
extern "OpenGL" func glDeleteProgram(program: i32)
extern "OpenGL" func glGetUniformLocation(program: i32, name: int): i32
extern "OpenGL" func glUniform2f(place: i32, x: f32, y: f32)
extern "OpenGL" func glGenTextures(count: i32, place: int)
extern "OpenGL" func glBindTexture(target: i32, texture: i32)
extern "OpenGL" func glTexImage2D(target: i32, level: i32, inside: i32, width: i32, height: i32, border: i32, format: i32, kind: i32, data: int)
extern "OpenGL" func glTexParameteri(target: i32, what: i32, value: i32)
extern "OpenGL" func glDeleteTextures(count: i32, place: int)
extern "OpenGL" func glPixelStorei(what: i32, value: i32)
extern "OpenGL" func glDrawArrays(mode: i32, first: i32, count: i32)
extern "OpenGL" func glReadPixels(x: i32, y: i32, width: i32, height: i32, format: i32, kind: i32, place: int)
extern "OpenGL" func glGenFramebuffers(count: i32, place: int)
extern "OpenGL" func glBindFramebuffer(target: i32, buffer: i32)
extern "OpenGL" func glDeleteFramebuffers(count: i32, place: int)
extern "OpenGL" func glGenRenderbuffers(count: i32, place: int)
extern "OpenGL" func glBindRenderbuffer(target: i32, buffer: i32)
extern "OpenGL" func glRenderbufferStorage(target: i32, format: i32, width: i32, height: i32)
extern "OpenGL" func glFramebufferRenderbuffer(target: i32, attachment: i32, kind: i32, buffer: i32)
extern "OpenGL" func glDeleteRenderbuffers(count: i32, place: int)
extern "OpenGL" func glCheckFramebufferStatus(target: i32): i32
extern "OpenGL" func glBlitFramebuffer(from_x: i32, from_y: i32, from_x2: i32, from_y2: i32, to_x: i32, to_y: i32, to_x2: i32, to_y2: i32, mask: i32, filter: i32)

struct Color
    red: float
    green: float
    blue: float
    alpha: float
end

# Colors take numbers from 0 to 255, like most paint programs
func rgb(red: int, green: int, blue: int): Color
    return rgba(red, green, blue, 255)
end

func rgba(red: int, green: int, blue: int, alpha: int): Color
    return Color{red: float(red) / 255.0, green: float(green) / 255.0, blue: float(blue) / 255.0, alpha: float(alpha) / 255.0}
end

struct Image
    texture: int
    width: int
    height: int
    open: bool
end

struct Gfx
    window: Window
    dc: int
    context: int
    shader: int
    screen_place: int
    vertex_array: int
    vertex_buffer: int
    atlas: int
    glyph_widths: int
    batch: int
    vertices: int
    texture: int
    framebuffer: int
    color_buffer: int
    depth_buffer: int
    in_3d: bool
    width: int
    height: int
    images: [Image]
    open: bool
    closed_by_program: bool
end

func start_gfx(window: Window): Gfx
    window.must_be_open()
    var dc = GetDC(window.handle)
    var format = memory(40)
    poke_u16(format, 0, 40)
    poke_u16(format, 2, 1)
    poke_i32(format, 4, 37)
    poke_u8(format, 9, 32)
    poke_u8(format, 22, 24)
    SetPixelFormat(dc, ChoosePixelFormat(dc, format), format)
    var starter = wglCreateContext(dc)
    if starter == 0 or not wglMakeCurrent(dc, starter)
        fail("This PC can't draw with OpenGL in a window.")
    end
    var settings = memory(32)
    poke_i32(settings, 0, 8337)
    poke_i32(settings, 4, 3)
    poke_i32(settings, 8, 8338)
    poke_i32(settings, 12, 3)
    poke_i32(settings, 16, 37158)
    poke_i32(settings, 20, 1)
    var context = wglCreateContextAttribsARB(dc, 0, settings)
    if context == 0
        fail("OpenGL 3.3 isn't available on this PC, so it can't draw.")
    end
    wglMakeCurrent(dc, context)
    wglDeleteContext(starter)
    var g = Gfx{window: window, dc: dc, context: context, shader: 0, screen_place: 0, vertex_array: 0, vertex_buffer: 0, atlas: 0, glyph_widths: memory(400), batch: memory(6144 * 32), vertices: 0, texture: 0, framebuffer: 0, color_buffer: 0, depth_buffer: 0, in_3d: false, width: 0, height: 0, images: [], open: true, closed_by_program: false}
    g.make_shader()
    g.make_buffers()
    g.make_atlas()
    g.fit_window()
    glEnable(3042)
    glBlendFunc(770, 771)
    return g
end

# --- Setting up ---
func gfx_c_text(text: string): int
    var place = memory(len(text) + 1)
    for i in 0..len(text)
        poke_u8(place, i, byte(text, i))
    end
    return place
end

func gfx_compile(kind: int, source: string): int
    var shader = glCreateShader(kind)
    var texts = memory(8)
    poke_i64(texts, 0, gfx_c_text(source))
    glShaderSource(shader, 1, texts, 0)
    glCompileShader(shader)
    var result = memory(8)
    glGetShaderiv(shader, 35713, result)
    if peek_i32(result, 0) == 0
        fail("The graphics card couldn't compile gfx's drawing code.")
    end
    return shader
end

func Gfx.make_shader()
    var vertex = "#version 330 core\nlayout(location = 0) in vec2 place;\nlayout(location = 1) in vec2 spot;\nlayout(location = 2) in vec4 color;\nuniform vec2 screen;\nout vec2 texture_spot;\nout vec4 tint;\nvoid main() {\n    gl_Position = vec4(place.x / screen.x * 2.0 - 1.0, 1.0 - place.y / screen.y * 2.0, 0.0, 1.0);\n    texture_spot = spot;\n    tint = color;\n}\n"
    var fragment = "#version 330 core\nin vec2 texture_spot;\nin vec4 tint;\nuniform sampler2D picture;\nout vec4 result;\nvoid main() {\n    result = tint * texture(picture, texture_spot);\n}\n"
    var program = glCreateProgram()
    var first = gfx_compile(35633, vertex)
    var second = gfx_compile(35632, fragment)
    glAttachShader(program, first)
    glAttachShader(program, second)
    glLinkProgram(program)
    var result = memory(8)
    glGetProgramiv(program, 35714, result)
    if peek_i32(result, 0) == 0
        fail("The graphics card couldn't link gfx's drawing code.")
    end
    glDeleteShader(first)
    glDeleteShader(second)
    glUseProgram(program)
    self.shader = program
    self.screen_place = glGetUniformLocation(program, gfx_c_text("screen"))
end

func Gfx.make_buffers()
    var ids = memory(8)
    glGenVertexArrays(1, ids)
    self.vertex_array = peek_i32(ids, 0)
    glBindVertexArray(self.vertex_array)
    glGenBuffers(1, ids)
    self.vertex_buffer = peek_i32(ids, 0)
    glBindBuffer(34962, self.vertex_buffer)
    glBufferData(34962, 6144 * 32, 0, 35048)
    glVertexAttribPointer(0, 2, 5126, false, 32, 0)
    glVertexAttribPointer(1, 2, 5126, false, 32, 8)
    glVertexAttribPointer(2, 4, 5126, false, 32, 16)
    glEnableVertexAttribArray(0)
    glEnableVertexAttribArray(1)
    glEnableVertexAttribArray(2)
end

func gfx_texture(pixels: int, width: int, height: int, smooth: bool): int
    var ids = memory(8)
    glGenTextures(1, ids)
    var texture = peek_i32(ids, 0)
    glBindTexture(3553, texture)
    glPixelStorei(3317, 1)
    glTexImage2D(3553, 0, 32856, width, height, 0, 6408, 5121, pixels)
    var filter = 9728
    if smooth
        filter = 9729
    end
    glTexParameteri(3553, 10241, filter)
    glTexParameteri(3553, 10240, filter)
    glTexParameteri(3553, 10242, 33071)
    glTexParameteri(3553, 10243, 33071)
    return texture
end

# Windows draws each letter once, white on black, and the red of each pixel becomes how solid it is
func Gfx.make_atlas()
    var width = 640
    var height = 384
    var screen = CreateCompatibleDC(0)
    var info = memory(44)
    poke_i32(info, 0, 40)
    poke_i32(info, 4, width)
    poke_i32(info, 8, -height)
    poke_u16(info, 12, 1)
    poke_u16(info, 14, 32)
    var bits = memory(8)
    var picture = CreateDIBSection(screen, info, 0, bits, 0, 0)
    var old_picture = SelectObject(screen, picture)
    var font = CreateFontW(-32, 0, 0, 0, 500, 0, 0, 0, 1, 0, 0, 4, 0, wide("Segoe UI"))
    var old_font = SelectObject(screen, font)
    SetTextColor(screen, 16777215)
    SetBkColor(screen, 0)
    var letters = " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~"
    for i in 0..95
        TextOutW(screen, (i % 16) * 40 + 4, (i / 16) * 48 + 4, wide(letters[i]), 1)
    end
    GetCharWidth32W(screen, 32, 126, self.glyph_widths)
    GdiFlush()
    var source = peek_i64(bits, 0)
    var pixels = memory(width * height * 4)
    for i in 0..width * 288
        poke_u8(pixels, i * 4, 255)
        poke_u8(pixels, i * 4 + 1, 255)
        poke_u8(pixels, i * 4 + 2, 255)
        poke_u8(pixels, i * 4 + 3, peek_u8(source, i * 4 + 2))
    end
    for y in 0..64
        for x in 0..64
            var dx = float(x) - 31.5
            var dy = float(y) - 31.5
            var edge = 31.5 - sqrt(dx * dx + dy * dy)
            var solid = 0
            if edge >= 1.0
                solid = 255
            elif edge > 0.0
                solid = int(edge * 255.0)
            end
            var place = pixels + ((296 + y) * width + x) * 4
            poke_u8(place, 0, 255)
            poke_u8(place, 1, 255)
            poke_u8(place, 2, 255)
            poke_u8(place, 3, solid)
        end
    end
    for y in 300..308
        for x in 80..88
            poke_i32(pixels, (y * width + x) * 4, -1)
        end
    end
    self.atlas = gfx_texture(pixels, width, height, true)
    SelectObject(screen, old_font)
    SelectObject(screen, old_picture)
    DeleteObject(font)
    DeleteObject(picture)
    DeleteDC(screen)
end

# Makes the drawing picture match the window's size
func Gfx.fit_window()
    var width = self.window.width()
    var height = self.window.height()
    if width < 1 or height < 1 or (width == self.width and height == self.height)
        return
    end
    var ids = memory(8)
    if self.framebuffer == 0
        glGenFramebuffers(1, ids)
        self.framebuffer = peek_i32(ids, 0)
        glGenRenderbuffers(1, ids)
        self.color_buffer = peek_i32(ids, 0)
        glGenRenderbuffers(1, ids)
        self.depth_buffer = peek_i32(ids, 0)
    end
    self.back_to_2d()
    glBindFramebuffer(36160, self.framebuffer)
    glBindRenderbuffer(36161, self.color_buffer)
    glRenderbufferStorage(36161, 32856, width, height)
    glFramebufferRenderbuffer(36160, 36064, 36161, self.color_buffer)
    glBindRenderbuffer(36161, self.depth_buffer)
    glRenderbufferStorage(36161, 33190, width, height)
    glFramebufferRenderbuffer(36160, 36096, 36161, self.depth_buffer)
    if glCheckFramebufferStatus(36160) != 36053
        fail("The graphics card couldn't make a picture to draw in.")
    end
    self.width = width
    self.height = height
    glViewport(0, 0, width, height)
    glUniform2f(self.screen_place, float(width), float(height))
end

# --- The batch ---
func Gfx.must_be_open()
    if not self.open
        fail("This drawing is closed, so it can't be used.")
    end
end

func Gfx.flush()
    if self.vertices == 0
        return
    end
    glBindTexture(3553, self.texture)
    glBufferSubData(34962, 0, self.vertices * 32, self.batch)
    glDrawArrays(4, 0, self.vertices)
    self.vertices = 0
end

func Gfx.vertex(x: float, y: float, u: float, v: float, color: Color)
    var place = self.batch + self.vertices * 32
    poke_f32(place, 0, x)
    poke_f32(place, 4, y)
    poke_f32(place, 8, u)
    poke_f32(place, 12, v)
    poke_f32(place, 16, color.red)
    poke_f32(place, 20, color.green)
    poke_f32(place, 24, color.blue)
    poke_f32(place, 28, color.alpha)
    self.vertices += 1
end

# 3D drawing (g3d.jamo) changes OpenGL's settings, so 2D drawing puts its own back first
func Gfx.back_to_2d()
    self.in_3d = false
    glUseProgram(self.shader)
    glBindVertexArray(self.vertex_array)
    glBindBuffer(34962, self.vertex_buffer)
    glDisable(2929)
    glEnable(3042)
end

func Gfx.use_texture(texture: int)
    self.must_be_open()
    if self.in_3d
        self.back_to_2d()
    end
    if texture != self.texture or self.vertices + 6 > 6144
        self.flush()
        self.texture = texture
    end
end

# Four corners, going around, with the texture's corners matching
func Gfx.quad(x1: float, y1: float, x2: float, y2: float, x3: float, y3: float, x4: float, y4: float, u1: float, v1: float, u2: float, v2: float, color: Color)
    self.vertex(x1, y1, u1, v1, color)
    self.vertex(x2, y2, u2, v1, color)
    self.vertex(x3, y3, u2, v2, color)
    self.vertex(x1, y1, u1, v1, color)
    self.vertex(x3, y3, u2, v2, color)
    self.vertex(x4, y4, u1, v2, color)
end

# --- Drawing ---
func Gfx.clear(color: Color)
    self.must_be_open()
    self.flush()
    glClearColor(color.red, color.green, color.blue, color.alpha)
    glClear(16640)
end

func Gfx.rect(x: float, y: float, width: float, height: float, color: Color)
    self.use_texture(self.atlas)
    var u = 84.0 / 640.0
    var v = 304.0 / 384.0
    self.quad(x, y, x + width, y, x + width, y + height, x, y + height, u, v, u, v, color)
end

func Gfx.circle(x: float, y: float, radius: float, color: Color)
    self.use_texture(self.atlas)
    self.quad(x - radius, y - radius, x + radius, y - radius, x + radius, y + radius, x - radius, y + radius, 0.0, 296.0 / 384.0, 64.0 / 640.0, 360.0 / 384.0, color)
end

func Gfx.line(x1: float, y1: float, x2: float, y2: float, thickness: float, color: Color)
    self.use_texture(self.atlas)
    var dx = x2 - x1
    var dy = y2 - y1
    var length = sqrt(dx * dx + dy * dy)
    if length == 0.0
        return
    end
    var across_x = -dy / length * thickness / 2.0
    var across_y = dx / length * thickness / 2.0
    var u = 84.0 / 640.0
    var v = 304.0 / 384.0
    self.quad(x1 + across_x, y1 + across_y, x2 + across_x, y2 + across_y, x2 - across_x, y2 - across_y, x1 - across_x, y1 - across_y, u, v, u, v, color)
end

func Gfx.text_width(words: string, size: float): float
    var width = 0.0
    var widest = 0.0
    for i in 0..len(words)
        var code = byte(words, i)
        if code == 10
            width = 0.0
        elif code >= 32 and code <= 126
            width += float(peek_i32(self.glyph_widths, (code - 32) * 4)) * size / 32.0
        end
        if width > widest
            widest = width
        end
    end
    return widest
end

# size is the letters' height in pixels. Only English letters and symbols show
func Gfx.text(words: string, x: float, y: float, size: float, color: Color)
    self.use_texture(self.atlas)
    var scale = size / 32.0
    var left = x
    for i in 0..len(words)
        var code = byte(words, i)
        if code == 10
            left = x
            y += 44.0 * scale
        elif code >= 32 and code <= 126
            var index = code - 32
            var glyph_width = float(peek_i32(self.glyph_widths, index * 4))
            var u = float((index % 16) * 40) / 640.0
            var v = float((index / 16) * 48) / 384.0
            var u2 = u + (glyph_width + 8.0) / 640.0
            var v2 = v + 48.0 / 384.0
            var top = y - 4.0 * scale
            self.use_texture(self.atlas)
            self.quad(left - 4.0 * scale, top, left + (glyph_width + 4.0) * scale, top, left + (glyph_width + 4.0) * scale, top + 48.0 * scale, left - 4.0 * scale, top + 48.0 * scale, u, v, u2, v2, color)
            left += glyph_width * scale
        end
    end
end

func Gfx.load_image(path: string): Image
    self.must_be_open()
    var picture = decode_png(read_file(path))
    var image = Image{texture: gfx_texture(picture.pixels, picture.width, picture.height, true), width: picture.width, height: picture.height, open: true}
    self.images.add(image)
    return image
end

func Gfx.image(image: Image, x: float, y: float)
    self.image_sized(image, x, y, float(image.width), float(image.height))
end

func Gfx.image_sized(image: Image, x: float, y: float, width: float, height: float)
    if not image.open
        fail("This image is closed, so it can't be drawn.")
    end
    self.use_texture(image.texture)
    self.quad(x, y, x + width, y, x + width, y + height, x, y + height, 0.0, 0.0, 1.0, 1.0, rgb(255, 255, 255))
end

func Image.close()
    if not self.open
        fail("This image is already closed.")
    end
    var ids = memory(8)
    poke_i32(ids, 0, self.texture)
    glDeleteTextures(1, ids)
    self.open = false
end

# Puts this frame on the window
func Gfx.show()
    self.must_be_open()
    self.flush()
    glBindFramebuffer(36008, self.framebuffer)
    glBindFramebuffer(36009, 0)
    glBlitFramebuffer(0, 0, self.width, self.height, 0, 0, self.width, self.height, 16384, 9728)
    SwapBuffers(self.dc)
    glBindFramebuffer(36160, self.framebuffer)
    self.fit_window()
end

func Gfx.read_pixel(x: int, y: int): Color
    self.must_be_open()
    self.flush()
    var place = memory(8)
    glReadPixels(x, self.height - 1 - y, 1, 1, 6408, 5121, place)
    return rgba(peek_u8(place, 0), peek_u8(place, 1), peek_u8(place, 2), peek_u8(place, 3))
end

# Saves what's drawn so far as a BMP picture
func Gfx.save_bmp(path: string)
    self.must_be_open()
    self.flush()
    var size = self.width * self.height * 4
    var picture = memory(54 + size)
    poke_u8(picture, 0, 66)
    poke_u8(picture, 1, 77)
    poke_i32(picture, 2, 54 + size)
    poke_i32(picture, 10, 54)
    poke_i32(picture, 14, 40)
    poke_i32(picture, 18, self.width)
    poke_i32(picture, 22, self.height)
    poke_u16(picture, 26, 1)
    poke_u16(picture, 28, 32)
    poke_i32(picture, 34, size)
    glReadPixels(0, 0, self.width, self.height, 32993, 5121, picture + 54)
    var file = CreateFileW(wide(path), 1073741824, 0, 0, 2, 128, 0)
    if file == -1
        fail(f"can't write the file {path}.")
    end
    var written = memory(8)
    var fine = WriteFile(file, picture, 54 + size, written, 0)
    CloseHandle(file)
    if not fine
        fail(f"can't write the file {path}.")
    end
end

func Gfx.close()
    if self.closed_by_program
        fail("This drawing is already closed.")
    end
    self.closed_by_program = true
    if not self.open
        return
    end
    self.open = false
    for image in self.images
        if image.open
            image.close()
        end
    end
    var ids = memory(8)
    poke_i32(ids, 0, self.atlas)
    glDeleteTextures(1, ids)
    poke_i32(ids, 0, self.vertex_buffer)
    glDeleteBuffers(1, ids)
    poke_i32(ids, 0, self.vertex_array)
    glDeleteVertexArrays(1, ids)
    poke_i32(ids, 0, self.framebuffer)
    glDeleteFramebuffers(1, ids)
    poke_i32(ids, 0, self.color_buffer)
    glDeleteRenderbuffers(1, ids)
    poke_i32(ids, 0, self.depth_buffer)
    glDeleteRenderbuffers(1, ids)
    glDeleteProgram(self.shader)
    wglMakeCurrent(0, 0)
    wglDeleteContext(self.context)
    ReleaseDC(self.window.handle, self.dc)
end
)JAMOLIBRARY";
    if (name == "json.jamo") return R"JAMOLIBRARY(# json: reads and writes JSON, the text format most save files and web services use. use "json.jamo"
#
# A Json value is null, a bool, a number, text, a list or an object. Objects keep their keys in the order they
# were added. parse_json never stops the program: when the text isn't JSON, the value it gives back has an error
# that says what's wrong and on which line.

use "text.jamo"
use "builder.jamo"

const JSON_MOST_DEPTH = 500

enum JsonKind
    NULL
    BOOL
    NUMBER
    TEXT
    LIST
    OBJECT
end

struct Json
    kind: JsonKind
    truth: bool
    number: float
    text: string
    items: [Json]
    keys: [string]
    values: [Json]
    error: string
end

# --- Making values ---

func json_null(): Json
    return Json{kind: JsonKind.NULL, truth: false, number: 0.0, text: "", items: [], keys: [], values: [], error: ""}
end

func json_bool(value: bool): Json
    var made = json_null()
    made.kind = JsonKind.BOOL
    made.truth = value
    return made
end

func json_number(value: float): Json
    var made = json_null()
    made.kind = JsonKind.NUMBER
    made.number = value
    return made
end

func json_int(value: int): Json
    return json_number(float(value))
end

func json_text(value: string): Json
    var made = json_null()
    made.kind = JsonKind.TEXT
    made.text = value
    return made
end

func json_list(): Json
    var made = json_null()
    made.kind = JsonKind.LIST
    return made
end

func json_object(): Json
    var made = json_null()
    made.kind = JsonKind.OBJECT
    return made
end

# --- Using values ---

# Adds an item to the end of a list
func Json.add(item: Json)
    if self.kind != JsonKind.LIST
        fail("add only works on a JSON list. Make one with json_list().")
    end
    self.items.add(item)
end

# Sets a key in an object, replacing what it had
func Json.set(key: string, value: Json)
    if self.kind != JsonKind.OBJECT
        fail("set only works on a JSON object. Make one with json_object().")
    end
    for i in 0..len(self.keys)
        if self.keys[i] == key
            self.values[i] = value
            return
        end
    end
    self.keys.add(key)
    self.values.add(value)
end

func Json.has(key: string): bool
    return self.kind == JsonKind.OBJECT and index_of_key(self.keys, key) >= 0
end

func index_of_key(keys: [string], key: string): int
    for i in 0..len(keys)
        if keys[i] == key
            return i
        end
    end
    return -1
end

# An object's value for a key, or null when it has no such key
func Json.get(key: string): Json
    if self.kind != JsonKind.OBJECT
        return json_null()
    end
    var at = index_of_key(self.keys, key)
    if at < 0
        return json_null()
    end
    return self.values[at]
end

# A list's item at a place, counting from 0, or null when there's nothing there
func Json.at(place: int): Json
    if self.kind != JsonKind.LIST or place < 0 or place >= len(self.items)
        return json_null()
    end
    return self.items[place]
end

# How many items a list has, or keys an object has
func Json.size(): int
    if self.kind == JsonKind.LIST
        return len(self.items)
    end
    return len(self.keys)
end

func Json.is_null(): bool
    return self.kind == JsonKind.NULL
end

# The number as an int, dropping any decimals. Anything that isn't a number gives 0
func Json.as_int(): int
    if self.kind != JsonKind.NUMBER or not (self.number > -9223372036854775808.0 and self.number < 9223372036854775807.0)
        return 0
    end
    return int(self.number)
end

# --- Writing ---

func json_escape(text: string, out: TextBuilder)
    out.add("\"")
    for i in 0..len(text)
        var code = byte(text, i)
        if code == 34
            out.add("\\\"")
        elif code == 92
            out.add("\\\\")
        elif code == 10
            out.add("\\n")
        elif code == 13
            out.add("\\r")
        elif code == 9
            out.add("\\t")
        elif code < 32
            var digits = "0123456789abcdef"
            out.add("\\u00")
            out.add(digits[code / 16])
            out.add(digits[code % 16])
        else
            out.add(text[i])
        end
    end
    out.add("\"")
end

# Whole numbers are written without decimals. Others show up to 6 decimals, the way Jamo shows floats.
# JSON has no NaN or infinity, so they're written as null
func json_number_text(value: float): string
    # value - value is NaN exactly when value is NaN or infinite
    if value != value or value - value != 0.0
        return "null"
    end
    if value > -1000000000000000.0 and value < 1000000000000000.0 and float(int(value)) == value
        return string(int(value))
    end
    return string(value)
end

func json_write(value: Json, out: TextBuilder, pretty: bool, depth: int)
    match value.kind
        case JsonKind.NULL
            out.add("null")
        case JsonKind.BOOL
            out.add(string(value.truth))
        case JsonKind.NUMBER
            out.add(json_number_text(value.number))
        case JsonKind.TEXT
            json_escape(value.text, out)
        case JsonKind.LIST
            if len(value.items) == 0
                out.add("[]")
                return
            end
            out.add("[")
            for i in 0..len(value.items)
                if i > 0
                    out.add(",")
                end
                json_new_line(out, pretty, depth + 1)
                json_write(value.items[i], out, pretty, depth + 1)
            end
            json_new_line(out, pretty, depth)
            out.add("]")
        case JsonKind.OBJECT
            if len(value.keys) == 0
                out.add("{}")
                return
            end
            out.add("{")
            for i in 0..len(value.keys)
                if i > 0
                    out.add(",")
                end
                json_new_line(out, pretty, depth + 1)
                json_escape(value.keys[i], out)
                out.add(":")
                if pretty
                    out.add(" ")
                end
                json_write(value.values[i], out, pretty, depth + 1)
            end
            json_new_line(out, pretty, depth)
            out.add("}")
    end
end

func json_new_line(out: TextBuilder, pretty: bool, depth: int)
    if pretty
        out.add("\n")
        out.add(repeat("  ", depth))
    end
end

# The value as JSON on one line
func to_json(value: Json): string
    var out = new_builder()
    json_write(value, out, false, 0)
    return out.text()
end

# The value as JSON, one item a line and indented, for files people may read
func to_json_pretty(value: Json): string
    var out = new_builder()
    json_write(value, out, true, 0)
    return out.text()
end

# --- Reading ---

struct JsonReader
    text: string
    at: int
    line: int
    error: string
end

func JsonReader.fail_here(words: string)
    if self.error == ""
        self.error = f"line {self.line}: {words}"
    end
end

func JsonReader.next_byte(): int
    if self.at >= len(self.text)
        return -1
    end
    return byte(self.text, self.at)
end

func JsonReader.skip_space()
    while self.at < len(self.text)
        var code = byte(self.text, self.at)
        if code == 10
            self.line += 1
        elif code != 32 and code != 9 and code != 13
            return
        end
        self.at += 1
    end
end

func JsonReader.word(text: string): bool
    if slice_or_empty(self.text, self.at, self.at + len(text)) == text
        self.at += len(text)
        return true
    end
    return false
end

func slice_or_empty(text: string, from: int, to: int): string
    if to > len(text)
        return ""
    end
    return slice(text, from, to)
end

func JsonReader.read_value(depth: int): Json
    if depth > JSON_MOST_DEPTH
        self.fail_here("lists and objects go too deep, past 500 inside each other.")
        return json_null()
    end
    self.skip_space()
    var code = self.next_byte()
    if code == -1
        self.fail_here("the text ended where a value was expected.")
        return json_null()
    end
    if code == 123
        return self.read_object(depth)
    end
    if code == 91
        return self.read_list(depth)
    end
    if code == 34
        return json_text(self.read_text())
    end
    if code == 45 or (code >= 48 and code <= 57)
        return self.read_number()
    end
    if self.word("true")
        return json_bool(true)
    end
    if self.word("false")
        return json_bool(false)
    end
    if self.word("null")
        return json_null()
    end
    self.fail_here(f"I didn't expect {self.text[self.at]} here.")
    return json_null()
end

func JsonReader.read_list(depth: int): Json
    var list = json_list()
    self.at += 1
    self.skip_space()
    if self.next_byte() == 93
        self.at += 1
        return list
    end
    while self.error == ""
        list.items.add(self.read_value(depth + 1))
        self.skip_space()
        var code = self.next_byte()
        self.at += 1
        if code == 93
            return list
        end
        if code != 44
            self.at -= 1
            self.fail_here("a list needs a comma between items, or ] at its end.")
        end
    end
    return list
end

func JsonReader.read_object(depth: int): Json
    var object = json_object()
    self.at += 1
    self.skip_space()
    if self.next_byte() == 125
        self.at += 1
        return object
    end
    while self.error == ""
        self.skip_space()
        if self.next_byte() != 34
            self.fail_here("an object's keys must be text in double quotes.")
            return object
        end
        var key = self.read_text()
        self.skip_space()
        if self.next_byte() != 58
            self.fail_here("a key needs a : before its value.")
            return object
        end
        self.at += 1
        object.set(key, self.read_value(depth + 1))
        self.skip_space()
        var code = self.next_byte()
        self.at += 1
        if code == 125
            return object
        end
        if code != 44
            self.at -= 1
            self.fail_here("an object needs a comma between keys, or } at its end.")
        end
    end
    return object
end

func json_hex(code: int): int
    if code >= 48 and code <= 57
        return code - 48
    end
    if code >= 97 and code <= 102
        return code - 87
    end
    if code >= 65 and code <= 70
        return code - 55
    end
    return -1
end

func JsonReader.read_hex4(): int
    if self.at + 4 > len(self.text)
        self.fail_here("\\u needs 4 hex digits after it.")
        return 0
    end
    var value = 0
    for i in 0..4
        var digit = json_hex(byte(self.text, self.at + i))
        if digit < 0
            self.fail_here("\\u needs 4 hex digits after it.")
            return 0
        end
        value = value * 16 + digit
    end
    self.at += 4
    return value
end

func json_add_utf8(out: TextBuilder, code: int)
    var place = memory(4)
    var size = 0
    if code < 128
        poke_u8(place, 0, code)
        size = 1
    elif code < 2048
        poke_u8(place, 0, 192 + code / 64)
        poke_u8(place, 1, 128 + code % 64)
        size = 2
    elif code < 65536
        poke_u8(place, 0, 224 + code / 4096)
        poke_u8(place, 1, 128 + code / 64 % 64)
        poke_u8(place, 2, 128 + code % 64)
        size = 3
    else
        poke_u8(place, 0, 240 + code / 262144)
        poke_u8(place, 1, 128 + code / 4096 % 64)
        poke_u8(place, 2, 128 + code / 64 % 64)
        poke_u8(place, 3, 128 + code % 64)
        size = 4
    end
    out.add(memory_text(place, size))
end

# Reads text in double quotes, starting at the opening quote
func JsonReader.read_text(): string
    var out = new_builder()
    self.at += 1
    while self.at < len(self.text)
        var code = byte(self.text, self.at)
        if code == 34
            self.at += 1
            return out.text()
        end
        if code < 32
            self.fail_here("text can't have a line break or tab in it. Write them as \\n and \\t.")
            return out.text()
        end
        if code != 92
            out.add(self.text[self.at])
            self.at += 1
            continue
        end
        self.at += 1
        var escape = self.next_byte()
        self.at += 1
        if escape == 34
            out.add("\"")
        elif escape == 92
            out.add("\\")
        elif escape == 47
            out.add("/")
        elif escape == 98
            out.add(memory_text(json_byte(8), 1))
        elif escape == 102
            out.add(memory_text(json_byte(12), 1))
        elif escape == 110
            out.add("\n")
        elif escape == 114
            out.add("\r")
        elif escape == 116
            out.add("\t")
        elif escape == 117
            var unit = self.read_hex4()
            # Two \\u escapes in a row can make one letter past the first 65,536, like an emoji
            if unit >= 55296 and unit < 56320 and slice_or_empty(self.text, self.at, self.at + 2) == "\\u"
                self.at += 2
                var low = self.read_hex4()
                if low >= 56320 and low < 57344
                    unit = 65536 + (unit - 55296) * 1024 + (low - 56320)
                else
                    json_add_utf8(out, unit)
                    unit = low
                end
            end
            json_add_utf8(out, unit)
        else
            self.at -= 1
            self.fail_here("that isn't one of JSON's escapes: \\\" \\\\ \\/ \\b \\f \\n \\r \\t or \\u.")
            return out.text()
        end
    end
    self.fail_here("text was never closed with a double quote.")
    return out.text()
end

func json_byte(value: int): int
    var place = memory(1)
    poke_u8(place, 0, value)
    return place
end

func is_digit_code(code: int): bool
    return code >= 48 and code <= 57
end

# Reads a number. The exponent moves the decimal point in the digits themselves, so the answer rounds exactly
func JsonReader.read_number(): Json
    var start = self.at
    var negative = false
    if self.next_byte() == 45
        negative = true
        self.at += 1
    end
    var whole_start = self.at
    while is_digit_code(self.next_byte())
        self.at += 1
    end
    var whole = slice(self.text, whole_start, self.at)
    if whole == "" or (len(whole) > 1 and byte(whole, 0) == 48)
        self.fail_here("a number needs digits, and can't start with 0 unless it's 0 itself.")
        return json_null()
    end
    var parts = ""
    if self.next_byte() == 46
        self.at += 1
        var parts_start = self.at
        while is_digit_code(self.next_byte())
            self.at += 1
        end
        parts = slice(self.text, parts_start, self.at)
        if parts == ""
            self.fail_here("a number's dot needs digits after it.")
            return json_null()
        end
    end
    var exponent = 0
    var code = self.next_byte()
    if code == 101 or code == 69
        self.at += 1
        var exponent_negative = false
        if self.next_byte() == 43 or self.next_byte() == 45
            exponent_negative = self.next_byte() == 45
            self.at += 1
        end
        var exponent_start = self.at
        while is_digit_code(self.next_byte())
            self.at += 1
        end
        var exponent_text = slice(self.text, exponent_start, self.at)
        if exponent_text == "" or len(exponent_text) > 5
            self.fail_here("a number's e needs up to 5 digits after it.")
            return json_null()
        end
        exponent = int(exponent_text)
        if exponent_negative
            exponent = 0 - exponent
        end
    end
    # Moves the dot: 1.5e3 is the digits 15 with the dot 3 places further right, so 1500
    var digits = whole + parts
    var point = len(whole) + exponent
    var text = ""
    if point <= 0
        text = "0." + repeat("0", 0 - point) + digits
    elif point >= len(digits)
        text = digits + repeat("0", point - len(digits))
    else
        text = slice(digits, 0, point) + "." + slice(digits, point, len(digits))
    end
    if not is_float(text)
        self.at = start
        self.fail_here("this number is too large or too small for a float.")
        return json_null()
    end
    var value = float(text)
    if negative
        value = 0.0 - value
    end
    return json_number(value)
end

# Reads JSON text. When it isn't JSON, the answer is null with an error saying why, and the line
func parse_json(text: string): Json
    var reader = JsonReader{text: text, at: 0, line: 1, error: ""}
    var value = reader.read_value(0)
    reader.skip_space()
    if reader.error == "" and reader.at < len(text)
        reader.fail_here("there's more after the end of the JSON value.")
    end
    if reader.error != ""
        var failed = json_null()
        failed.error = reader.error
        return failed
    end
    return value
end
)JAMOLIBRARY";
    if (name == "list.jamo") return R"JAMOLIBRARY(# list: finding, putting in and taking out items of int and string arrays. use "list.jamo"
#
# Sorting and turning arrays around are in text.jamo: sort_ints, sort_strings, reverse_ints and reverse_strings.
# Jamo has no way to write one function for every kind of array, so each comes in an int and a string version.

func list_check_place(at: int, count: int, word: string)
    if at < 0 or at > count
        fail(f"{word} needs a place from 0 to {count}, not {at}.")
    end
end

# --- Finding ---

# Where value first is in items, counting from 0, or -1 when it isn't there
func index_of_int(items: [int], value: int): int
    for i in 0..len(items)
        if items[i] == value
            return i
        end
    end
    return -1
end

func index_of_string(items: [string], value: string): int
    for i in 0..len(items)
        if items[i] == value
            return i
        end
    end
    return -1
end

func contains_int(items: [int], value: int): bool
    return index_of_int(items, value) >= 0
end

func contains_string(items: [string], value: string): bool
    return index_of_string(items, value) >= 0
end

# --- Putting in and taking out. These change the array itself ---

# Puts value at place at, moving the items from there on along by one. at can be len(items), to add at the end
func insert_int(items: [int], at: int, value: int)
    list_check_place(at, len(items), "insert_int")
    items.add(value)
    var i = len(items) - 1
    while i > at
        items[i] = items[i - 1]
        i -= 1
    end
    items[at] = value
end

func insert_string(items: [string], at: int, value: string)
    list_check_place(at, len(items), "insert_string")
    items.add(value)
    var i = len(items) - 1
    while i > at
        items[i] = items[i - 1]
        i -= 1
    end
    items[at] = value
end

# Takes out the item at place at, moving the ones after it back by one, and gives it back
func remove_int(items: [int], at: int): int
    list_check_place(at, len(items) - 1, "remove_int")
    var removed = items[at]
    for i in at..len(items) - 1
        items[i] = items[i + 1]
    end
    items.pop()
    return removed
end

func remove_string(items: [string], at: int): string
    list_check_place(at, len(items) - 1, "remove_string")
    var removed = items[at]
    for i in at..len(items) - 1
        items[i] = items[i + 1]
    end
    items.pop()
    return removed
end
)JAMOLIBRARY";
    if (name == "log.jamo") return R"JAMOLIBRARY(# log: messages with levels, to the screen, a file or both. use "log.jamo"
#
#     var log = new_log("game.log")
#     log.info("Level 2 started")
#     log.warn("Low on memory")
#
# Each message gets the time and its level: [14:03:27] WARN  Low on memory. A file is added to, never replaced.

use "time.jamo"
use "text.jamo"

extern "kernel32.dll" func CreateFileW(name: int, access: i32, share: i32, security: int, how: i32, flags: i32, template: int): int
extern "kernel32.dll" func WriteFile(file: int, data: int, size: i32, written: int, overlapped: int): bool
extern "kernel32.dll" func CloseHandle(handle: int): bool

# FILE_APPEND_DATA, shared for reading and writing, OPEN_ALWAYS, so it's made if it isn't there yet
const LOG_APPEND = 4
const LOG_SHARE = 3
const LOG_OPEN_ALWAYS = 4

struct Log
    file: string
    level: int
    to_screen: bool
end

# A log that writes to the screen and, unless file is "", to that file too
func new_log(file: string): Log
    return Log{file: file, level: 1, to_screen: true}
end

func log_level_number(name: string): int
    var names = ["debug", "info", "warn", "error"]
    for i in 0..len(names)
        if names[i] == name
            return i
        end
    end
    fail(f"A log's level is debug, info, warn or error, not {name}.")
    return 0
end

# Only messages at this level or above are kept: debug, info (the start), warn or error
func Log.set_level(name: string)
    self.level = log_level_number(name)
end

func Log.screen(on: bool)
    self.to_screen = on
end

func log_append(path: string, text: string): bool
    var file = CreateFileW(wide(path), LOG_APPEND, LOG_SHARE, 0, LOG_OPEN_ALWAYS, 128, 0)
    if file == -1
        return false
    end
    var place = memory(len(text) + 1)
    for i in 0..len(text)
        poke_u8(place, i, byte(text, i))
    end
    var written = memory(8)
    var wrote = WriteFile(file, place, len(text), written, 0)
    CloseHandle(file)
    return wrote
end

func Log.write(level: int, words: string)
    if level < self.level
        return
    end
    var names = ["DEBUG", "INFO ", "WARN ", "ERROR"]
    var line = "[" + now().time_text() + "] " + names[level] + " " + words
    if self.to_screen
        print(line)
    end
    if self.file != ""
        log_append(self.file, line + "\r\n")
    end
end

func Log.debug(words: string)
    self.write(0, words)
end

func Log.info(words: string)
    self.write(1, words)
end

func Log.warn(words: string)
    self.write(2, words)
end

func Log.error(words: string)
    self.write(3, words)
end
)JAMOLIBRARY";
    if (name == "map.jamo") return R"JAMOLIBRARY(# map: look things up by any key. use "map.jamo"
#
# Map<K, V> maps keys to values of any type. Keys can be ints, strings, bools or enums, since they're
# compared with == and hashed with hash_of. The entries sit in order in keys and values, and slots is the
# hash table: each slot holds its entry's place plus 1, 0 when empty, or -1 where an entry was removed, so
# entries placed after it can still be found. The table doubles when it's two thirds full.

struct Map<K, V>
    keys: [K]
    values: [V]
    slots: [int]
    removed: int
end

func new_map<K, V>(): Map<K, V>
    var keys: [K] = []
    var values: [V] = []
    return Map<K, V>{keys: keys, values: values, slots: map_empty_slots(8), removed: 0}
end

func map_empty_slots(size: int): [int]
    var slots: [int] = []
    for i in 0..size
        slots.add(0)
    end
    return slots
end

# The slot holding the key, or -1 - the slot where it can go when it's missing
func Map<K, V>.find(key: K): int
    var size = len(self.slots)
    var place = hash_of(key) % size
    var free = -1
    while true
        var slot = self.slots[place]
        if slot == 0
            if free >= 0
                return -1 - free
            end
            return -1 - place
        end
        if slot > 0 and self.keys[slot - 1] == key
            return place
        end
        if slot < 0 and free < 0
            free = place
        end
        place = (place + 1) % size
    end
    return -1
end

func Map<K, V>.set(key: K, value: V)
    var place = self.find(key)
    if place >= 0
        self.values[self.slots[place] - 1] = value
        return
    end
    if (len(self.keys) + self.removed + 1) * 3 > len(self.slots) * 2
        self.grow()
        place = self.find(key)
    end
    var at = -1 - place
    if self.slots[at] < 0
        self.removed -= 1
    end
    self.keys.add(key)
    self.values.add(value)
    self.slots[at] = len(self.keys)
end

# Doubles the table when most of it is in use, or rebuilds it the same size to clear out removed slots
func Map<K, V>.grow()
    var size = len(self.slots)
    if len(self.keys) * 3 >= size
        size = size * 2
    end
    self.slots = map_empty_slots(size)
    self.removed = 0
    for i in 0..len(self.keys)
        var place = -1 - self.find(self.keys[i])
        self.slots[place] = i + 1
    end
end

func Map<K, V>.get(key: K): V
    var place = self.find(key)
    if place < 0
        fail("This Map doesn't have that key.")
    end
    return self.values[self.slots[place] - 1]
end

func Map<K, V>.get_or(key: K, fallback: V): V
    var place = self.find(key)
    if place < 0
        return fallback
    end
    return self.values[self.slots[place] - 1]
end

func Map<K, V>.has(key: K): bool
    return self.find(key) >= 0
end

# The last entry moves into the removed one's place, so the entries stay packed together
func Map<K, V>.remove(key: K): bool
    var place = self.find(key)
    if place < 0
        return false
    end
    var at = self.slots[place] - 1
    self.slots[place] = -1
    self.removed += 1
    var last = len(self.keys) - 1
    if at != last
        var moved = self.find(self.keys[last])
        self.slots[moved] = at + 1
        self.keys[at] = self.keys[last]
        self.values[at] = self.values[last]
    end
    self.keys.pop()
    self.values.pop()
    return true
end

func Map<K, V>.size(): int
    return len(self.keys)
end

# A copy of the keys, in the order they were added, except that removing one moves the last key into its place
func Map<K, V>.key_list(): [K]
    var out: [K] = []
    for key in self.keys
        out.add(key)
    end
    return out
end

func Map<K, V>.value_list(): [V]
    var out: [V] = []
    for value in self.values
        out.add(value)
    end
    return out
end
)JAMOLIBRARY";
    if (name == "math.jamo") return R"JAMOLIBRARY(# math: square roots, angles, powers, rounding, smallest and largest, and random numbers. use "math.jamo"
#
# Jamo has no program-wide variables, so the random generator keeps its state in the .exe's own header
# in memory: Jamo writes bytes 2 to 59 of the DOS header as zeros, and Windows never reads them once the
# program is loaded. math uses bytes 8 to 23 (the state at 8, and 1 at 16 once it's seeded), and
# time.jamo uses 24 to 47. That page is read-only at first, so the first write makes it writable.
#
# The generator is a 64-bit LCG with Knuth's MMIX numbers, which wraps the way Jamo's ints do. Jamo has
# no bit operators, so xorshift would be slow. An LCG's low bits repeat quickly, so only the top 32 bits
# of each step are used.

extern "msvcrt.dll" func sqrt(value: float): float
extern "msvcrt.dll" func sin(value: float): float
extern "msvcrt.dll" func cos(value: float): float
extern "msvcrt.dll" func tan(value: float): float
extern "msvcrt.dll" func atan2(y: float, x: float): float
extern "msvcrt.dll" func pow(base: float, power: float): float
extern "msvcrt.dll" func fabs(value: float): float
extern "kernel32.dll" func GetModuleHandleW(name: int): int
extern "kernel32.dll" func VirtualProtect(place: int, size: int, protect: i32, old: int): bool
extern "kernel32.dll" func QueryPerformanceCounter(place: int): bool
extern "kernel32.dll" func GetCurrentProcessId(): i32

# --- Numbers ---

func pi(): float
    return 3.141592653589793
end

# The size of a float, without its sign
func abs(value: float): float
    return fabs(value)
end

# The size of an int, without its sign. The smallest int has no positive partner, so it comes back as it is
func abs_int(value: int): int
    if value < 0
        return -value
    end
    return value
end

# --- Rounding, from float to int ---

func math_check_fits(value: float, name: string)
    if not (value >= -9223372036854775808.0 and value < 9223372036854775808.0)
        fail(f"{name} needs a float that fits in an int, but this is {value}.")
    end
end

# The nearest int at or below
func floor(value: float): int
    math_check_fits(value, "floor")
    var whole = int(value)
    if float(whole) > value
        return whole - 1
    end
    return whole
end

# The nearest int at or above
func ceil(value: float): int
    math_check_fits(value, "ceil")
    var whole = int(value)
    if float(whole) < value
        return whole + 1
    end
    return whole
end

# The nearest int. Halves round away from zero, so 2.5 gives 3 and -2.5 gives -3
func round(value: float): int
    math_check_fits(value, "round")
    var whole = int(value)
    var part = value - float(whole)
    if part >= 0.5
        return whole + 1
    end
    if part <= -0.5
        return whole - 1
    end
    return whole
end

# --- Smallest and largest ---

func min(a: float, b: float): float
    if b < a
        return b
    end
    return a
end

func max(a: float, b: float): float
    if b > a
        return b
    end
    return a
end

func clamp(value: float, low: float, high: float): float
    if not (low <= high)
        fail(f"clamp needs low <= high, but they're {low} and {high}.")
    end
    if value < low
        return low
    end
    if value > high
        return high
    end
    return value
end

# The value part of the way from a to b: lerp(0.0, 10.0, 0.25) is 2.5. Past 0.0 to 1.0 it goes beyond them
func lerp(a: float, b: float, part: float): float
    return a + (b - a) * part
end

func min_int(a: int, b: int): int
    if b < a
        return b
    end
    return a
end

func max_int(a: int, b: int): int
    if b > a
        return b
    end
    return a
end

func clamp_int(value: int, low: int, high: int): int
    if low > high
        fail(f"clamp_int needs low <= high, but they're {low} and {high}.")
    end
    if value < low
        return low
    end
    if value > high
        return high
    end
    return value
end

# --- Random numbers ---

func math_header(): int
    return GetModuleHandleW(0)
end

func math_header_writable(header: int)
    if not VirtualProtect(header, 64, 4, memory(8))
        fail("math couldn't set up its random numbers.")
    end
end

# Starts the same numbers over again: the same seed always gives the same numbers
func random_seed(seed: int)
    var header = math_header()
    math_header_writable(header)
    poke_i64(header, 8, seed)
    poke_i64(header, 16, 1)
end

func math_seed_from_clock()
    var count = memory(8)
    QueryPerformanceCounter(count)
    random_seed(peek_i64(count, 0) * 31 + GetCurrentProcessId())
end

# The top 32 bits of a 64-bit value, as a number from 0 to 4294967295
func math_top_bits(value: int): int
    var top = value / 4294967296
    if value % 4294967296 < 0
        top = top - 1
    end
    if top < 0
        top = top + 4294967296
    end
    return top
end

# The generator's next 32 random bits
func math_random_bits(): int
    var header = math_header()
    if peek_i64(header, 16) == 0
        math_seed_from_clock()
    end
    var state = peek_i64(header, 8) * 6364136223846793005 + 1442695040888963407
    poke_i64(header, 8, state)
    return math_top_bits(state)
end

# A whole number from low to high, both included. Every number is equally likely
func random_int(low: int, high: int): int
    if low > high
        fail(f"random_int needs low <= high, but they're {low} and {high}.")
    end
    var span = high - low
    if span >= 0 and span < 9223372036854775807
        var count = span + 1
        var extra = (9223372036854775807 % count + 1) % count
        while true
            var bits = math_random_bits() * 2147483648 + math_random_bits() / 2
            if bits <= 9223372036854775807 - extra
                return low + bits % count
            end
        end
    end
    # More than half of all ints: draw any int until one lands inside
    while true
        var any = math_random_bits() * 4294967296 + math_random_bits()
        if any >= low and any <= high
            return any
        end
    end
    return low
end

# A float from 0.0 up to, but not including, 1.0
func random_float(): float
    var bits = math_random_bits() * 2097152 + math_random_bits() / 2048
    return float(bits) / 9007199254740992.0
end
)JAMOLIBRARY";
    if (name == "net.jamo") return R"JAMOLIBRARY(# net: web requests over HTTP and HTTPS, with WinHTTP. use "net.jamo"
#
# A request waits until the answer has come, or the time runs out. Problems along the way, like a server
# that can't be reached or takes too long, don't stop the program: the response's error says what went
# wrong, and its status is 0. Only mistakes in the program itself, like a web address that isn't one,
# stop it.
#
# Background requests don't wait: start_get and start_post give back a Request at once, and done() checks
# on it each frame. WinHTTP tells us about progress by calling net_signal on its own threads, so that
# function only writes numbers into the request's block, which is memory from LocalAlloc that the memory
# cleaner never sees. Everything else happens on the program's own thread, in done(). A block is only
# given back once WinHTTP says the request's handle is closed, since it may still write there until
# then. The block: sent at 0, headers at 4, data waiting at 8 with how much at 12, read done at 16,
# failed at 20 with the error at 24, closed at 28. The 64 KB from 64 take each piece of the answer, and
# what's sent with a post follows at 65600. After that come the server name, the method, the path and the
# headers as Windows text, since WinHTTP may read them after the call that took them has returned.
#
# Links are TCP connections between programs, for multiplayer games. One program listens on a port and
# others connect to it. Each send() is one whole message: it goes out with its length in front, as 4
# bytes, so messages() gives back exactly what was sent, however TCP splits it up on the way. Sockets
# never wait: messages() sends what's waiting to go and reads what has come, once a frame.

extern "winhttp.dll" func WinHttpOpen(agent: int, access: i32, proxy: int, bypass: int, flags: i32): int
extern "winhttp.dll" func WinHttpSetTimeouts(handle: int, resolve: i32, connect: i32, send: i32, receive: i32): bool
extern "winhttp.dll" func WinHttpConnect(session: int, host: int, port: i32, reserved: i32): int
extern "winhttp.dll" func WinHttpOpenRequest(connection: int, verb: int, path: int, version: int, referrer: int, accept: int, flags: i32): int
extern "winhttp.dll" func WinHttpSendRequest(request: int, headers: int, headers_length: i32, data: int, data_length: i32, total_length: i32, context: int): bool
extern "winhttp.dll" func WinHttpReceiveResponse(request: int, reserved: int): bool
extern "winhttp.dll" func WinHttpQueryHeaders(request: int, level: i32, name: int, buffer: int, length: int, index: int): bool
extern "winhttp.dll" func WinHttpQueryDataAvailable(request: int, size: int): bool
extern "winhttp.dll" func WinHttpReadData(request: int, buffer: int, size: i32, read: int): bool
extern "winhttp.dll" func WinHttpCloseHandle(handle: int): bool
extern "winhttp.dll" func WinHttpSetOption(handle: int, option: i32, buffer: int, length: i32): bool
extern "winhttp.dll" func WinHttpSetStatusCallback(handle: int, callback: int, flags: i32, reserved: int): int
extern "kernel32.dll" func GetLastError(): i32
extern "kernel32.dll" func Sleep(milliseconds: i32)
extern "kernel32.dll" func QueryPerformanceCounter(place: int): bool
extern "kernel32.dll" func QueryPerformanceFrequency(place: int): bool
extern "winmm.dll" func timeBeginPeriod(period: i32): i32
extern "winmm.dll" func timeEndPeriod(period: i32): i32
extern "ws2_32.dll" func WSAStartup(version: i32, data: int): i32
extern "ws2_32.dll" func WSAGetLastError(): i32
extern "ws2_32.dll" func socket(family: i32, kind: i32, protocol: i32): int
extern "ws2_32.dll" func closesocket(s: int): i32
extern "ws2_32.dll" func ioctlsocket(s: int, command: i32, value: int): i32
extern "ws2_32.dll" func setsockopt(s: int, level: i32, name: i32, value: int, length: i32): i32
extern "ws2_32.dll" func bind(s: int, address: int, length: i32): i32
extern "ws2_32.dll" func listen(s: int, backlog: i32): i32
extern "ws2_32.dll" func accept(s: int, address: int, length: int): int
extern "ws2_32.dll" func connect(s: int, address: int, length: i32): i32
extern "ws2_32.dll" func send(s: int, data: int, length: i32, flags: i32): i32
extern "ws2_32.dll" func recv(s: int, data: int, length: i32, flags: i32): i32
extern "ws2_32.dll" func WSAPoll(places: int, count: i32, wait: i32): i32
extern "ws2_32.dll" func getsockopt(s: int, level: i32, name: i32, value: int, length: int): i32
extern "kernel32.dll" func LocalAlloc(flags: i32, size: int): int
extern "kernel32.dll" func LocalFree(memory: int): int

struct Response
    status: int
    text: string
    error: string
end

struct Net
    agent: int
    session: int
    background: int
    closing: [int]
    seconds: float
    sockets_started: bool
    open: bool
end

# A program waiting for others to connect, on one port
struct Listener
    socket: int
    port: int
    open: bool
end

# A connection to another program. messages() gives back what came, a whole message at a time
struct Link
    socket: int
    connecting: bool
    open: bool
    error: string
    outgoing: int
    out_size: int
    out_start: int
    out_used: int
    incoming: int
    in_size: int
    in_used: int
    piece: int
end

# A request running in the background. Check done() each frame, then read response()
struct Request
    net: Net
    block: int
    connection: int
    request: int
    stage: int
    status: int
    buffer: int
    capacity: int
    used: int
    waiting: int
    address: NetAddress
    result: Response
    deadline: float
    clock: int
    finished: bool
end

struct NetAddress
    secure: bool
    host: string
    port: int
    path: string
end

# --- Starting ---

func start_net(): Net
    var agent = net_lasting_wide("Jamo")
    var session = WinHttpOpen(agent, 4, 0, 0, 0)
    if session == 0
        fail("Windows couldn't start web requests.")
    end
    var n = Net{agent: agent, session: session, background: 0, closing: [], seconds: 30.0, sockets_started: false, open: true}
    n.set_timeout(30.0)
    return n
end

# How long a request may take at each stage before it gives up with a timeout error
func Net.set_timeout(seconds: float)
    self.must_be_open()
    if not (seconds > 0.0 and seconds <= 3600.0)
        fail(f"A timeout goes from above 0.0 to 3600.0 seconds, not {seconds}.")
    end
    self.seconds = seconds
    var milliseconds = int(seconds * 1000.0)
    WinHttpSetTimeouts(self.session, milliseconds, milliseconds, milliseconds, milliseconds)
    if self.background != 0
        WinHttpSetTimeouts(self.background, milliseconds, milliseconds, milliseconds, milliseconds)
    end
end

func Net.must_be_open()
    if not self.open
        fail("This net is closed, so it can't be used.")
    end
end

func Net.close()
    if not self.open
        fail("This net is already closed.")
    end
    self.open = false
    WinHttpCloseHandle(self.session)
    if self.background != 0
        WinHttpCloseHandle(self.background)
    end
    LocalFree(self.agent)
end

# --- Requests ---

func Net.get(url: string): Response
    return self.request("GET", url, "")
end

# Sends text to a web address, as plain UTF-8 text
func Net.post(url: string, text: string): Response
    return self.request("POST", url, text)
end

func Net.request(verb: string, url: string, body: string): Response
    var r = self.start(verb, url, body)
    timeBeginPeriod(1)
    while not r.done()
        Sleep(1)
    end
    timeEndPeriod(1)
    return r.response()
end

# Sets every time limit on one request, including the wait for the answer's headers, which
# WinHttpSetTimeouts leaves at 90 seconds
func net_time_limits(request: int, seconds: float)
    var milliseconds = int(seconds * 1000.0)
    WinHttpSetTimeouts(request, milliseconds, milliseconds, milliseconds, milliseconds)
    var value = memory(8)
    poke_i32(value, 0, milliseconds)
    WinHttpSetOption(request, 7, value, 4)
end

func net_failed(code: int, address: NetAddress): Response
    var where = f"{address.host}:{address.port}"
    var why = f"the request failed (WinHTTP error {code})"
    if code == 12002
        why = f"{where} took too long to answer"
    elif code == 12007
        why = f"couldn't find the server {address.host}"
    elif code == 12029
        why = f"couldn't connect to {where}"
    elif code == 12030 or code == 12031
        why = f"the connection to {where} was dropped"
    elif code == 12152
        why = f"{where} closed the connection or sent an answer that isn't valid HTTP"
    elif code == 12175 or code == 12044 or code == 12045 or code == 12037 or code == 12038
        why = f"the secure connection to {where} couldn't be trusted"
    end
    return Response{status: 0, text: "", error: why}
end

# --- Web addresses ---

func net_address(url: string): NetAddress
    var secure = false
    var start = 0
    if net_starts(url, "http://")
        start = 7
    elif net_starts(url, "https://")
        secure = true
        start = 8
    else
        fail(f"net takes web addresses starting with http:// or https://, not {url}.")
    end
    var place = start
    var host = ""
    while place < len(url) and url[place] != ":" and url[place] != "/" and url[place] != "?"
        host = host + url[place]
        place += 1
    end
    if host == ""
        fail(f"The web address {url} has no server name.")
    end
    var port = 80
    if secure
        port = 443
    end
    if place < len(url) and url[place] == ":"
        place += 1
        port = 0
        var digits = 0
        while place < len(url) and byte(url, place) >= 48 and byte(url, place) <= 57
            port = port * 10 + byte(url, place) - 48
            digits += 1
            place += 1
            if port > 65535
                fail(f"The web address {url} has a port above 65535.")
            end
        end
        if digits == 0 or port == 0
            fail(f"The web address {url} has a port that isn't a number from 1 to 65535.")
        end
    end
    var path = ""
    while place < len(url)
        path = path + url[place]
        place += 1
    end
    if path == "" or byte(path, 0) == 63
        path = "/" + path
    end
    return NetAddress{secure: secure, host: host, port: port, path: path}
end

func net_starts(text: string, front: string): bool
    if len(text) < len(front)
        return false
    end
    for i in 0..len(front)
        if byte(text, i) != byte(front, i)
            return false
        end
    end
    return true
end

# --- Background requests ---

func Net.start_get(url: string): Request
    return self.start("GET", url, "")
end

func Net.start_post(url: string, text: string): Request
    return self.start("POST", url, text)
end

func Net.start(verb: string, url: string, body: string): Request
    self.must_be_open()
    var address = net_address(url)
    self.tidy()
    if self.background == 0
        self.background = WinHttpOpen(self.agent, 4, 0, 0, 268435456)
        if self.background == 0
            fail("Windows couldn't start background web requests.")
        end
        WinHttpSetStatusCallback(self.background, callback(net_signal), 25103360, 0)
        self.set_timeout(self.seconds)
    end
    var header_text = ""
    if verb == "POST"
        header_text = "Content-Type: text/plain; charset=utf-8"
    end
    var texts = 65600 + len(body) + 8 - len(body) % 2
    var block = LocalAlloc(64, texts + (len(address.host) + len(verb) + len(address.path) + len(header_text) + 4) * 2)
    if block == 0
        fail("There isn't enough memory for another web request.")
    end
    for i in 0..len(body)
        poke_u8(block, 65600 + i, byte(body, i))
    end
    var host_text = block + texts
    var verb_text = host_text + net_copy_wide(host_text, address.host)
    var path_text = verb_text + net_copy_wide(verb_text, verb)
    var headers = path_text + net_copy_wide(path_text, address.path)
    net_copy_wide(headers, header_text)
    var r = Request{net: self, block: block, connection: 0, request: 0, stage: 1, status: 0, buffer: memory(4096), capacity: 4096, used: 0, waiting: 0, address: address, result: Response{status: 0, text: "", error: ""}, deadline: 0.0, clock: memory(16), finished: false}
    r.deadline = net_clock(r.clock) + self.seconds
    r.connection = WinHttpConnect(self.background, host_text, address.port, 0)
    if r.connection == 0
        r.fail_with(GetLastError())
        return r
    end
    var flags = 0
    if address.secure
        flags = 8388608
    end
    r.request = WinHttpOpenRequest(r.connection, verb_text, path_text, 0, 0, 0, flags)
    if r.request == 0
        r.fail_with(GetLastError())
        return r
    end
    net_time_limits(r.request, self.seconds)
    var headers_length = 0
    if verb == "POST"
        headers_length = -1
    else
        headers = 0
    end
    if not WinHttpSendRequest(r.request, headers, headers_length, block + 65600, len(body), len(body), block)
        r.fail_with(GetLastError())
    end
    return r
end

# Takes the request's next steps, and answers true once it has finished, worked or not
func Request.done(): bool
    if self.finished
        return true
    end
    var block = self.block
    var moving = true
    var stage_before = self.stage
    var used_before = self.used
    while moving and not self.finished
        moving = false
        if peek_i32(block, 20) != 0
            self.fail_with(peek_i32(block, 24))
        elif self.stage == 1 and peek_i32(block, 0) != 0
            self.stage = 2
            moving = true
            if not WinHttpReceiveResponse(self.request, 0)
                self.fail_with(GetLastError())
            end
        elif self.stage == 2 and peek_i32(block, 4) != 0
            var number = memory(8)
            var length = memory(8)
            poke_i32(length, 0, 4)
            if WinHttpQueryHeaders(self.request, 536870931, 0, number, length, 0)
                self.status = peek_i32(number, 0)
                self.ask_for_data()
                moving = true
            else
                self.fail_with(GetLastError())
            end
        elif self.stage == 3 and peek_i32(block, 8) != 0
            var count = peek_i32(block, 12)
            if count <= 0
                self.finish(Response{status: self.status, text: memory_text(self.buffer, self.used), error: ""})
            else
                if count > 65536
                    count = 65536
                end
                self.waiting = count
                self.stage = 4
                poke_i32(block, 16, 0)
                moving = true
                if not WinHttpReadData(self.request, block + 64, count, 0)
                    self.fail_with(GetLastError())
                end
            end
        elif self.stage == 4 and peek_i32(block, 16) != 0
            self.keep(block + 64, self.waiting)
            self.ask_for_data()
            moving = true
        end
    end
    if not self.finished
        var now = net_clock(self.clock)
        if self.stage != stage_before or self.used != used_before
            self.deadline = now + self.net.seconds
        elif now > self.deadline
            self.fail_with(12002)
        end
    end
    return self.finished
end

# Seconds on the PC's steady clock. WinHTTP's own time limits only get checked when something arrives,
# so each request keeps its own deadline instead
func net_clock(place: int): float
    QueryPerformanceCounter(place)
    QueryPerformanceFrequency(place + 8)
    return float(peek_i64(place, 0)) / float(peek_i64(place, 8))
end

func Request.ask_for_data()
    self.stage = 3
    poke_i32(self.block, 8, 0)
    if not WinHttpQueryDataAvailable(self.request, 0)
        self.fail_with(GetLastError())
    end
end

# Copies a piece of the answer out of the block, into memory that grows
func Request.keep(from: int, count: int)
    if self.used + count > self.capacity
        while self.used + count > self.capacity
            self.capacity = self.capacity * 2
        end
        var bigger = memory(self.capacity)
        for i in 0..(self.used + 7) / 8
            poke_i64(bigger, i * 8, peek_i64(self.buffer, i * 8))
        end
        self.buffer = bigger
    end
    for i in 0..count
        poke_u8(self.buffer, self.used + i, peek_u8(from, i))
    end
    self.used += count
end

# What came back. Only after done() has answered true
func Request.response(): Response
    if not self.finished
        fail("This request hasn't finished yet. Check done() first.")
    end
    return self.result
end

# Stops a request that isn't needed any more
func Request.cancel()
    if not self.finished
        self.finish(Response{status: 0, text: "", error: "the request was cancelled"})
    end
end

func Request.fail_with(code: int)
    self.finish(net_failed(code, self.address))
end

func Request.finish(result: Response)
    self.result = result
    self.finished = true
    if self.request != 0
        WinHttpCloseHandle(self.request)
        self.net.closing.add(self.block)
    else
        LocalFree(self.block)
    end
    if self.connection != 0
        WinHttpCloseHandle(self.connection)
    end
    self.request = 0
    self.connection = 0
end

# Gives back the blocks of requests whose handles WinHTTP has finished closing
func Net.tidy()
    var still: [int] = []
    for block in self.closing
        if peek_i32(block, 28) != 0
            LocalFree(block)
        else
            still.add(block)
        end
    end
    self.closing = still
end

# How many finished requests WinHTTP is still closing
func Net.closing_count(): int
    self.tidy()
    return len(self.closing)
end

# WinHTTP calls this on its own threads. It only writes numbers into the request's block: it must never
# make text, arrays or structs, because the memory cleaner works on the program's own thread
func net_signal(handle: int, block: int, status: int, info: int): int
    if block == 0
        return 0
    end
    var kind = status % 4294967296
    if kind < 0
        kind += 4294967296
    end
    if kind == 4194304
        poke_i32(block, 0, 1)
    elif kind == 131072
        poke_i32(block, 4, 1)
    elif kind == 262144
        poke_i32(block, 12, peek_i32(info, 0))
        poke_i32(block, 8, 1)
    elif kind == 524288
        poke_i32(block, 16, 1)
    elif kind == 2097152
        poke_i32(block, 24, peek_i32(info, 8))
        poke_i32(block, 20, 1)
    elif kind == 2048
        poke_i32(block, 28, 1)
    end
    return 0
end

# --- Links between programs ---

func Net.start_sockets()
    self.must_be_open()
    if not self.sockets_started
        if WSAStartup(514, memory(512)) != 0
            fail("Windows couldn't start network connections.")
        end
        self.sockets_started = true
    end
end

# Waits for other programs to connect on a port, from any PC that can reach this one. Windows may ask
# whether to let the program through its firewall
func Net.listen(port: int): Listener
    return self.listen_on(port, 0)
end

# The same, but only programs on this PC can connect. Good for testing
func Net.listen_here(port: int): Listener
    return self.listen_on(port, 16777343)
end

func Net.listen_on(port: int, address: int): Listener
    self.start_sockets()
    if port < 1 or port > 65535
        fail(f"A port is a number from 1 to 65535, not {port}.")
    end
    var s = net_new_socket()
    var yes = memory(8)
    poke_i32(yes, 0, 1)
    setsockopt(s, 65535, -5, yes, 4)
    var place = net_socket_address(address, port)
    if bind(s, place, 16) != 0
        var code = WSAGetLastError()
        closesocket(s)
        if code == 10048
            fail(f"Port {port} is already in use by another program.")
        end
        fail(f"Windows couldn't listen on port {port} (error {code}).")
    end
    listen(s, 16)
    net_no_waiting(s)
    return Listener{socket: s, port: port, open: true}
end

# The links of programs that connected since the last call. Call it once a frame
func Listener.new_links(): [Link]
    if not self.open
        fail("This listener is closed, so it can't be used.")
    end
    var links: [Link] = []
    while true
        var s = accept(self.socket, 0, 0)
        if s == -1
            return links
        end
        net_no_waiting(s)
        links.add(net_link(s, false))
    end
    return links
end

func Listener.close()
    if not self.open
        fail("This listener is already closed.")
    end
    self.open = false
    closesocket(self.socket)
end

# Connects to a program listening on a port. host is an address like "127.0.0.1", or "localhost".
# The link starts out connecting: check connected(), or just send, since messages wait until it's ready
func Net.connect(host: string, port: int): Link
    self.start_sockets()
    if port < 1 or port > 65535
        fail(f"A port is a number from 1 to 65535, not {port}.")
    end
    var address = net_ipv4(host)
    var s = net_new_socket()
    net_no_waiting(s)
    var link = net_link(s, true)
    if connect(s, net_socket_address(address, port), 16) != 0
        var code = WSAGetLastError()
        if code != 10035
            link.broken(f"couldn't connect to {host}:{port} (error {code})")
        end
    else
        link.connecting = false
    end
    return link
end

func net_link(s: int, connecting: bool): Link
    return Link{socket: s, connecting: connecting, open: true, error: "", outgoing: memory(4096), out_size: 4096, out_start: 0, out_used: 0, incoming: memory(4096), in_size: 4096, in_used: 0, piece: memory(65536)}
end

func net_new_socket(): int
    var s = socket(2, 1, 6)
    if s == -1
        fail("Windows couldn't make a network connection.")
    end
    return s
end

# Sockets that never wait
func net_no_waiting(s: int)
    var yes = memory(8)
    poke_i32(yes, 0, 1)
    ioctlsocket(s, -2147195266, yes)
end

# A sockaddr_in: the family, the port with its high byte first, then the address
func net_socket_address(address: int, port: int): int
    var place = memory(16)
    poke_u16(place, 0, 2)
    poke_u8(place, 2, port / 256)
    poke_u8(place, 3, port % 256)
    poke_i32(place, 4, address)
    return place
end

# Turns "1.2.3.4" or "localhost" into the 4 bytes an address takes, first number lowest
func net_ipv4(host: string): int
    if host == "localhost"
        return 16777343
    end
    var parts = [0, 0, 0, 0]
    var part = 0
    var digits = 0
    for i in 0..len(host)
        var b = byte(host, i)
        if b >= 48 and b <= 57 and digits < 3
            parts[part] = parts[part] * 10 + b - 48
            digits += 1
        elif b == 46 and digits > 0 and part < 3
            part += 1
            digits = 0
        else
            fail(f"net connects to addresses like 192.168.1.20 or localhost, not {host}.")
        end
    end
    if part != 3 or digits == 0 or parts[0] > 255 or parts[1] > 255 or parts[2] > 255 or parts[3] > 255
        fail(f"net connects to addresses like 192.168.1.20 or localhost, not {host}.")
    end
    return parts[0] + parts[1] * 256 + parts[2] * 65536 + parts[3] * 16777216
end

# True once the link has finished connecting
func Link.connected(): bool
    self.check_connecting()
    return self.open and not self.connecting
end

func Link.check_connecting()
    if not self.connecting or not self.open
        return
    end
    var poll = memory(16)
    poke_i64(poll, 0, self.socket)
    poke_u16(poll, 8, 16)
    if WSAPoll(poll, 1, 0) <= 0
        return
    end
    var events = peek_u16(poll, 10)
    if events % 2 == 1 or events / 2 % 2 == 1
        var code = memory(8)
        var size = memory(8)
        poke_i32(size, 0, 4)
        getsockopt(self.socket, 65535, 4103, code, size)
        self.broken(f"couldn't connect (error {peek_i32(code, 0)})")
    elif events / 16 % 2 == 1
        self.connecting = false
    end
end

# Sends one message. It goes out as soon as it can, during messages()
func Link.send(text: string)
    if not self.open
        if self.error != ""
            return
        end
        fail("This link is closed, so it can't send.")
    end
    var length = len(text)
    if length > 16777216
        fail(f"A message can be up to 16 MB, but this one is {length} bytes.")
    end
    self.make_room(length + 4)
    var at = self.outgoing + self.out_used
    poke_u8(at, 0, length % 256)
    poke_u8(at, 1, length / 256 % 256)
    poke_u8(at, 2, length / 65536 % 256)
    poke_u8(at, 3, length / 16777216)
    for i in 0..length
        poke_u8(at, 4 + i, byte(text, i))
    end
    self.out_used += length + 4
    self.flush()
end

func Link.make_room(extra: int)
    if self.out_used + extra <= self.out_size
        return
    end
    if self.out_start > 0
        for i in self.out_start..self.out_used
            poke_u8(self.outgoing, i - self.out_start, peek_u8(self.outgoing, i))
        end
        self.out_used -= self.out_start
        self.out_start = 0
        if self.out_used + extra <= self.out_size
            return
        end
    end
    var size = self.out_size
    while self.out_used + extra > size
        size = size * 2
    end
    var bigger = memory(size)
    for i in 0..(self.out_used + 7) / 8
        poke_i64(bigger, i * 8, peek_i64(self.outgoing, i * 8))
    end
    self.outgoing = bigger
    self.out_size = size
end

# Sends as much of what's waiting as the connection takes now, up to 64 KB at a time
func Link.flush()
    self.check_connecting()
    while self.open and not self.connecting and self.out_start < self.out_used
        var count = self.out_used - self.out_start
        if count > 65536
            count = 65536
        end
        var sent = send(self.socket, self.outgoing + self.out_start, count, 0)
        if sent < 0
            var code = WSAGetLastError()
            if code != 10035
                self.broken(f"the connection was lost (error {code})")
            end
            return
        end
        self.out_start += sent
    end
    if self.out_start == self.out_used
        self.out_start = 0
        self.out_used = 0
    end
end

# Sends what's waiting, reads what has come, and gives back the whole messages. Call it once a frame
func Link.messages(): [string]
    var found: [string] = []
    if not self.open
        if self.error != ""
            return found
        end
        fail("This link is closed, so it can't be used.")
    end
    self.flush()
    if not self.open or self.connecting
        return found
    end
    while self.open
        var count = recv(self.socket, self.piece, 65536, 0)
        if count > 0
            self.keep_incoming(count)
        elif count == 0
            self.take_messages(found)
            self.broken("the other side closed the connection")
            return found
        else
            var code = WSAGetLastError()
            if code != 10035
                self.take_messages(found)
                self.broken(f"the connection was lost (error {code})")
                return found
            end
            break
        end
    end
    self.take_messages(found)
    return found
end

func Link.keep_incoming(count: int)
    if self.in_used + count > self.in_size
        var size = self.in_size
        while self.in_used + count > size
            size = size * 2
        end
        var bigger = memory(size)
        for i in 0..(self.in_used + 7) / 8
            poke_i64(bigger, i * 8, peek_i64(self.incoming, i * 8))
        end
        self.incoming = bigger
        self.in_size = size
    end
    for i in 0..count
        poke_u8(self.incoming, self.in_used + i, peek_u8(self.piece, i))
    end
    self.in_used += count
end

# Moves every whole message from what has come into found
func Link.take_messages(found: [string])
    var place = 0
    while self.in_used - place >= 4
        var at = self.incoming + place
        var length = peek_u8(at, 0) + peek_u8(at, 1) * 256 + peek_u8(at, 2) * 65536 + peek_u8(at, 3) * 16777216
        if length > 16777216
            self.broken("the other side sent a message over 16 MB, so the link was closed")
            return
        end
        if self.in_used - place - 4 < length
            break
        end
        found.add(memory_text(at + 4, length))
        place += 4 + length
    end
    for i in place..self.in_used
        poke_u8(self.incoming, i - place, peek_u8(self.incoming, i))
    end
    self.in_used -= place
end

# Why the link stopped, or "" while it works
func Link.problem(): string
    return self.error
end

func Link.is_open(): bool
    self.check_connecting()
    return self.open
end

func Link.broken(why: string)
    if self.open
        self.open = false
        self.error = why
        closesocket(self.socket)
    end
end

func Link.close()
    if not self.open
        if self.error != ""
            return
        end
        fail("This link is already closed.")
    end
    self.open = false
    closesocket(self.socket)
end

# Copies text as Windows text, ending in a 0, to a place outside Jamo's memory. Gives back the bytes used
func net_copy_wide(place: int, text: string): int
    var from = wide(text)
    var i = 0
    while peek_u16(from, i * 2) != 0
        poke_u16(place, i * 2, peek_u16(from, i * 2))
        i += 1
    end
    poke_u16(place, i * 2, 0)
    return i * 2 + 2
end

# Windows text that stays put until it's given back with LocalFree
func net_lasting_wide(text: string): int
    var place = LocalAlloc(64, len(text) * 2 + 2)
    net_copy_wide(place, text)
    return place
end
)JAMOLIBRARY";
    if (name == "png.jamo") return R"JAMOLIBRARY(# png: reads PNG pictures into memory as rows of red, green, blue and alpha bytes. use "png.jamo"
# It includes its own reader for PNG's compression (deflate). Damaged files stop with a clear error.

struct PngImage
    width: int
    height: int
    pixels: int
end

struct PngInflater
    data: int
    size: int
    place: int
    bits: int
    bit_count: int
    out: int
    out_size: int
    out_place: int
    powers: [int]
end

struct PngCode
    counts: [int]
    symbols: [int]
end

func png_damaged(why: string)
    fail(f"This PNG file is damaged or not a PNG: {why}.")
end

func png_number(data: string, place: int): int
    return byte(data, place) * 16777216 + byte(data, place + 1) * 65536 + byte(data, place + 2) * 256 + byte(data, place + 3)
end

func png_name(data: string, place: int): string
    return data[place] + data[place + 1] + data[place + 2] + data[place + 3]
end

func decode_png(data: string): PngImage
    var signature = [137, 80, 78, 71, 13, 10, 26, 10]
    if len(data) < 8
        png_damaged("it's too short")
    end
    for i in 0..8
        if byte(data, i) != signature[i]
            png_damaged("it doesn't start like a PNG file")
        end
    end
    var width = 0
    var height = 0
    var color_type = -1
    var palette: [int] = []
    var see_through: [int] = []
    var parts: [int] = []
    var packed_size = 0
    var place = 8
    var finished = false
    while not finished
        if place + 12 > len(data)
            png_damaged("it ends too soon")
        end
        var size = png_number(data, place)
        var name = png_name(data, place + 4)
        var start = place + 8
        if size > len(data) - start - 4
            png_damaged(f"the {name} part says it's longer than the file")
        end
        match name
            case "IHDR"
                if size != 13
                    png_damaged("its header is the wrong size")
                end
                width = png_number(data, start)
                height = png_number(data, start + 4)
                var depth = byte(data, start + 8)
                color_type = byte(data, start + 9)
                if width < 1 or height < 1 or width > 16384 or height > 16384
                    png_damaged(f"its size of {width} by {height} isn't allowed")
                end
                if depth != 8
                    fail(f"Jamo can only read PNG files with 8 bits for each color, and this one has {depth}.")
                end
                if color_type != 0 and color_type != 2 and color_type != 3 and color_type != 4 and color_type != 6
                    png_damaged(f"its color type {color_type} doesn't exist")
                end
                if byte(data, start + 12) != 0
                    fail("Jamo can't read interlaced PNG files yet. Save it again without interlacing.")
                end
            case "PLTE"
                for i in 0..size
                    palette.add(byte(data, start + i))
                end
            case "tRNS"
                for i in 0..size
                    see_through.add(byte(data, start + i))
                end
            case "IDAT"
                parts.add(start)
                parts.add(size)
                packed_size += size
            case "IEND"
                finished = true
        end
        place = start + size + 4
    end
    if color_type < 0
        png_damaged("it has no header")
    end
    if color_type == 3 and len(palette) == 0
        png_damaged("it uses a palette but doesn't have one")
    end
    var packed = memory(packed_size + 1)
    var at = 0
    var i = 0
    while i < len(parts)
        for j in 0..parts[i + 1]
            poke_u8(packed, at, byte(data, parts[i] + j))
            at += 1
        end
        i += 2
    end
    var channels = png_channels(color_type)
    var row_bytes = width * channels
    var raw = png_inflate(packed, packed_size, height * (row_bytes + 1))
    png_unfilter(raw, width, height, channels)
    return PngImage{width: width, height: height, pixels: png_to_rgba(raw, width, height, color_type, palette, see_through)}
end

func png_channels(color_type: int): int
    match color_type
        case 0
            return 1
        case 2
            return 3
        case 3
            return 1
        case 4
            return 2
    end
    return 4
end

# --- Filters: each row was stored as its difference from the pixels before it ---
func png_paeth(left: int, above: int, corner: int): int
    var guess = left + above - corner
    var to_left = guess - left
    var to_above = guess - above
    var to_corner = guess - corner
    if to_left < 0
        to_left = -to_left
    end
    if to_above < 0
        to_above = -to_above
    end
    if to_corner < 0
        to_corner = -to_corner
    end
    if to_left <= to_above and to_left <= to_corner
        return left
    end
    if to_above <= to_corner
        return above
    end
    return corner
end

func png_unfilter(raw: int, width: int, height: int, channels: int)
    var row_bytes = width * channels
    for y in 0..height
        var row = raw + y * (row_bytes + 1)
        var filter = peek_u8(row, 0)
        if filter > 4
            png_damaged(f"row {y} uses filter {filter}, which doesn't exist")
        end
        for x in 0..row_bytes
            var left = 0
            if x >= channels
                left = peek_u8(row, 1 + x - channels)
            end
            var above = 0
            var corner = 0
            if y > 0
                above = peek_u8(row - row_bytes - 1, 1 + x)
                if x >= channels
                    corner = peek_u8(row - row_bytes - 1, 1 + x - channels)
                end
            end
            var value = peek_u8(row, 1 + x)
            match filter
                case 1
                    value += left
                case 2
                    value += above
                case 3
                    value += (left + above) / 2
                case 4
                    value += png_paeth(left, above, corner)
            end
            poke_u8(row, 1 + x, value % 256)
        end
    end
end

func png_to_rgba(raw: int, width: int, height: int, color_type: int, palette: [int], see_through: [int]): int
    var pixels = memory(width * height * 4)
    var channels = png_channels(color_type)
    for y in 0..height
        var row = raw + y * (width * channels + 1) + 1
        for x in 0..width
            var from = row + x * channels
            var to = pixels + (y * width + x) * 4
            var red = 0
            var green = 0
            var blue = 0
            var alpha = 255
            match color_type
                case 0
                    red = peek_u8(from, 0)
                    green = red
                    blue = red
                case 2
                    red = peek_u8(from, 0)
                    green = peek_u8(from, 1)
                    blue = peek_u8(from, 2)
                case 3
                    var index = peek_u8(from, 0)
                    if index * 3 + 2 >= len(palette)
                        png_damaged("a pixel uses a color past the end of the palette")
                    end
                    red = palette[index * 3]
                    green = palette[index * 3 + 1]
                    blue = palette[index * 3 + 2]
                    if index < len(see_through)
                        alpha = see_through[index]
                    end
                case 4
                    red = peek_u8(from, 0)
                    green = red
                    blue = red
                    alpha = peek_u8(from, 1)
                case 6
                    red = peek_u8(from, 0)
                    green = peek_u8(from, 1)
                    blue = peek_u8(from, 2)
                    alpha = peek_u8(from, 3)
            end
            poke_u8(to, 0, red)
            poke_u8(to, 1, green)
            poke_u8(to, 2, blue)
            poke_u8(to, 3, alpha)
        end
    end
    return pixels
end

# --- Deflate, read the way zlib's small "puff" reader does it ---
func png_inflate(packed: int, size: int, out_size: int): int
    if size < 2
        png_damaged("its picture data is missing")
    end
    var first = peek_u8(packed, 0)
    var second = peek_u8(packed, 1)
    if first % 16 != 8 or (first * 256 + second) % 31 != 0 or (second / 32) % 2 != 0
        png_damaged("its picture data doesn't start right")
    end
    var powers: [int] = []
    var power = 1
    for i in 0..33
        powers.add(power)
        power *= 2
    end
    var reader = PngInflater{data: packed, size: size, place: 2, bits: 0, bit_count: 0, out: memory(out_size + 1), out_size: out_size, out_place: 0, powers: powers}
    var last = false
    while not last
        last = reader.take(1) == 1
        var kind = reader.take(2)
        match kind
            case 0
                reader.copy_stored()
            case 1
                reader.copy_coded(png_fixed_lengths(), png_fixed_distances())
            case 2
                reader.copy_dynamic()
            else
                png_damaged("its picture data uses a block type that doesn't exist")
        end
    end
    if reader.out_place != out_size
        png_damaged(f"its picture data makes {reader.out_place} bytes, but its size needs {out_size}")
    end
    return reader.out
end

func PngInflater.take(count: int): int
    while self.bit_count < count
        if self.place >= self.size
            png_damaged("its picture data ends too soon")
        end
        self.bits += peek_u8(self.data, self.place) * self.powers[self.bit_count]
        self.place += 1
        self.bit_count += 8
    end
    var value = self.bits % self.powers[count]
    self.bits = self.bits / self.powers[count]
    self.bit_count -= count
    return value
end

func PngInflater.put(value: int)
    if self.out_place >= self.out_size
        png_damaged("its picture data is too big for its size")
    end
    poke_u8(self.out, self.out_place, value)
    self.out_place += 1
end

func PngInflater.copy_stored()
    self.bits = 0
    self.bit_count = 0
    if self.place + 4 > self.size
        png_damaged("its picture data ends too soon")
    end
    var length = peek_u8(self.data, self.place) + peek_u8(self.data, self.place + 1) * 256
    var check = peek_u8(self.data, self.place + 2) + peek_u8(self.data, self.place + 3) * 256
    if length + check != 65535
        png_damaged("a stored block's length doesn't match its check")
    end
    self.place += 4
    if self.place + length > self.size
        png_damaged("its picture data ends too soon")
    end
    for i in 0..length
        self.put(peek_u8(self.data, self.place + i))
    end
    self.place += length
end

func png_make_code(lengths: [int], start: int, count: int): PngCode
    var counts: [int] = []
    for i in 0..16
        counts.add(0)
    end
    for i in 0..count
        counts[lengths[start + i]] += 1
    end
    var offsets: [int] = [0, 0]
    for length in 1..15
        offsets.add(offsets[length] + counts[length])
    end
    var symbols: [int] = []
    for i in 0..count
        symbols.add(0)
    end
    for i in 0..count
        var length = lengths[start + i]
        if length != 0
            symbols[offsets[length]] = i
            offsets[length] += 1
        end
    end
    return PngCode{counts: counts, symbols: symbols}
end

func PngInflater.decode(code: PngCode): int
    var value = 0
    var first = 0
    var index = 0
    for length in 1..16
        value += self.take(1)
        var count = code.counts[length]
        if value - count < first
            return code.symbols[index + value - first]
        end
        index += count
        first = (first + count) * 2
        value *= 2
    end
    png_damaged("its picture data has a code that doesn't exist")
    return 0
end

func png_fixed_lengths(): PngCode
    var lengths: [int] = []
    for i in 0..288
        if i < 144
            lengths.add(8)
        elif i < 256
            lengths.add(9)
        elif i < 280
            lengths.add(7)
        else
            lengths.add(8)
        end
    end
    return png_make_code(lengths, 0, 288)
end

func png_fixed_distances(): PngCode
    var lengths: [int] = []
    for i in 0..30
        lengths.add(5)
    end
    return png_make_code(lengths, 0, 30)
end

func PngInflater.copy_dynamic()
    var length_count = self.take(5) + 257
    var distance_count = self.take(5) + 1
    var code_count = self.take(4) + 4
    if length_count > 286 or distance_count > 30
        png_damaged("its picture data asks for too many codes")
    end
    var order = [16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15]
    var code_lengths: [int] = []
    for i in 0..19
        code_lengths.add(0)
    end
    for i in 0..code_count
        code_lengths[order[i]] = self.take(3)
    end
    var lengths_code = png_make_code(code_lengths, 0, 19)
    var lengths: [int] = []
    while len(lengths) < length_count + distance_count
        var symbol = self.decode(lengths_code)
        if symbol < 16
            lengths.add(symbol)
        else
            var repeat = 0
            var value = 0
            if symbol == 16
                if len(lengths) == 0
                    png_damaged("its picture data repeats a length before there is one")
                end
                value = lengths[len(lengths) - 1]
                repeat = 3 + self.take(2)
            elif symbol == 17
                repeat = 3 + self.take(3)
            else
                repeat = 11 + self.take(7)
            end
            if len(lengths) + repeat > length_count + distance_count
                png_damaged("its picture data repeats too many lengths")
            end
            for i in 0..repeat
                lengths.add(value)
            end
        end
    end
    self.copy_coded(png_make_code(lengths, 0, length_count), png_make_code(lengths, length_count, distance_count))
end

func PngInflater.copy_coded(lengths: PngCode, distances: PngCode)
    var length_base = [3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258]
    var length_extra = [0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0]
    var distance_base = [1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577]
    var distance_extra = [0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13]
    while true
        var symbol = self.decode(lengths)
        if symbol < 256
            self.put(symbol)
        elif symbol == 256
            return
        else
            symbol -= 257
            if symbol >= 29
                png_damaged("its picture data has a length that doesn't exist")
            end
            var length = length_base[symbol] + self.take(length_extra[symbol])
            var which = self.decode(distances)
            if which >= 30
                png_damaged("its picture data has a distance that doesn't exist")
            end
            var distance = distance_base[which] + self.take(distance_extra[which])
            if distance > self.out_place
                png_damaged("its picture data points before its start")
            end
            for i in 0..length
                self.put(peek_u8(self.out, self.out_place - distance))
            end
        end
    end
end
)JAMOLIBRARY";
    if (name == "sound.jamo") return R"JAMOLIBRARY(# sound: plays WAV files and made-up tones through the speakers, many at once. use "sound.jamo"
#
# Sound goes out through waveOut, 44100 frames a second in 16-bit stereo, as six short buffers that
# take turns: while the speakers play some, update() mixes the next ones. Windows reads those buffers
# and writes their headers whenever it likes, so they live in memory from VirtualAlloc, which the
# memory cleaner never sees, and only close() gives it back. The block: six 48-byte headers, then at
# 512 the six buffers of 2048 bytes each.
# A clip keeps its frames as 16-bit stereo with 32768 added, so each sample reads back with peek_u16.

extern "winmm.dll" func waveOutGetNumDevs(): i32
extern "winmm.dll" func waveOutOpen(handle: int, device: i32, format: int, callback: int, instance: int, flags: i32): i32
extern "winmm.dll" func waveOutPrepareHeader(handle: int, header: int, size: i32): i32
extern "winmm.dll" func waveOutUnprepareHeader(handle: int, header: int, size: i32): i32
extern "winmm.dll" func waveOutWrite(handle: int, header: int, size: i32): i32
extern "winmm.dll" func waveOutReset(handle: int): i32
extern "winmm.dll" func waveOutClose(handle: int): i32
extern "winmm.dll" func waveOutSetVolume(handle: int, volume: i32): i32
extern "winmm.dll" func waveOutGetPosition(handle: int, time: int, size: i32): i32
extern "kernel32.dll" func VirtualAlloc(place: int, size: int, kind: i32, protect: i32): int
extern "kernel32.dll" func VirtualFree(place: int, size: int, kind: i32): bool

struct Clip
    samples: int
    frames: int
end

struct Voice
    clip: Clip
    place: int
    volume: float
    looping: bool
    playing: bool
end

struct Sound
    device: int
    block: int
    next: int
    mix: int
    voices: [Voice]
    volume: float
    gaps: int
    started: bool
    silent: bool
    open: bool
    closed_by_program: bool
end

# --- Starting and stopping ---

# Opens the PC's default speakers
func start_sound(): Sound
    return start_sound_device(-1)
end

# Opens one sound device by number, from 0. A PC without it runs on in silence, with a note saying so
func start_sound_device(number: int): Sound
    var s = Sound{device: 0, block: 0, next: 0, mix: memory(8192), voices: [], volume: 1.0, gaps: 0, started: false, silent: false, open: true, closed_by_program: false}
    if waveOutGetNumDevs() == 0
        print("Note: this PC has no sound device, so the program runs without sound.")
        return s
    end
    var format = memory(24)
    poke_u16(format, 0, 1)
    poke_u16(format, 2, 2)
    poke_i32(format, 4, 44100)
    poke_i32(format, 8, 176400)
    poke_u16(format, 12, 4)
    poke_u16(format, 14, 16)
    var handle = memory(8)
    var result = waveOutOpen(handle, number, format, 0, 0, 0)
    if result != 0
        if number == -1
            print("Note: the speakers couldn't be opened, so the program runs without sound.")
        else
            print(f"Note: there's no sound device {number} to use, so the program runs without sound.")
        end
        return s
    end
    s.device = peek_i64(handle, 0)
    s.block = VirtualAlloc(0, 512 + 6 * 2048, 12288, 4)
    if s.block == 0
        waveOutClose(s.device)
        fail("There isn't enough memory to play sound.")
    end
    for i in 0..6
        var header = s.block + i * 48
        poke_i64(header, 0, s.block + 512 + i * 2048)
        poke_i32(header, 8, 2048)
        waveOutPrepareHeader(s.device, header, 48)
        waveOutWrite(s.device, header, 48)
    end
    return s
end

# Keeps tests silent: the speakers still play, at no volume. If Windows can't turn the volume down,
# only silence goes out
func Sound.quiet()
    self.must_be_open()
    if self.device != 0 and waveOutSetVolume(self.device, 0) != 0
        self.silent = true
    end
end

func Sound.must_be_open()
    if not self.open
        fail("This sound player is closed, so it can't be used.")
    end
end

# True when sound really reaches a device
func Sound.has_device(): bool
    self.must_be_open()
    return self.device != 0
end

# Stops every sound and lets the speakers go
func Sound.close()
    if self.closed_by_program
        fail("This sound player is already closed.")
    end
    self.closed_by_program = true
    self.open = false
    self.voices = []
    if self.device != 0
        waveOutReset(self.device)
        for i in 0..6
            waveOutUnprepareHeader(self.device, self.block + i * 48, 48)
        end
        waveOutClose(self.device)
        VirtualFree(self.block, 0, 32768)
        self.device = 0
        self.block = 0
    end
end

# --- Playing ---

func Sound.play(clip: Clip): Voice
    return self.start_voice(clip, false)
end

# Plays a clip over and over until it's stopped
func Sound.play_loop(clip: Clip): Voice
    return self.start_voice(clip, true)
end

func Sound.start_voice(clip: Clip, looping: bool): Voice
    self.must_be_open()
    var voice = Voice{clip: clip, place: 0, volume: 1.0, looping: looping, playing: self.device != 0 and clip.frames > 0}
    if voice.playing
        if len(self.voices) == 64
            self.voices[0].playing = false
            for i in 1..64
                self.voices[i - 1] = self.voices[i]
            end
            self.voices.pop()
        end
        self.voices.add(voice)
    end
    return voice
end

# Sets the volume of everything, from 0.0 to 1.0. Above 1.0 makes it louder, up to 4.0
func Sound.set_volume(volume: float)
    self.must_be_open()
    self.volume = sound_volume(volume)
end

func Sound.stop_all()
    self.must_be_open()
    for voice in self.voices
        voice.playing = false
    end
    self.voices = []
end

# How many sounds are playing now
func Sound.playing(): int
    self.must_be_open()
    var count = 0
    for voice in self.voices
        if voice.playing
            count = count + 1
        end
    end
    return count
end

# How many times the speakers ran out of sound, because update() wasn't called often enough
func Sound.gaps(): int
    self.must_be_open()
    return self.gaps
end

# How many frames the speakers have played since the start
func Sound.played(): int
    self.must_be_open()
    if self.device == 0
        return 0
    end
    var time = memory(16)
    poke_i32(time, 0, 2)
    waveOutGetPosition(self.device, time, 12)
    return peek_i32(time, 4)
end

func Voice.stop()
    self.playing = false
end

func Voice.is_playing(): bool
    return self.playing
end

func Voice.set_volume(volume: float)
    self.volume = sound_volume(volume)
end

func sound_volume(volume: float): float
    if volume < 0.0 or volume > 4.0 or volume != volume
        fail(f"A volume goes from 0.0 to 4.0, not {volume}.")
    end
    return volume
end

# --- Mixing ---

# Mixes the next sound for the speakers. Call it once a frame
func Sound.update()
    self.must_be_open()
    if self.device == 0
        return
    end
    var done = 0
    while done < 6
        var header = self.block + self.next * 48
        var flags = peek_i32(header, 24)
        if flags % 2 == 0
            break
        end
        self.fill(self.block + 512 + self.next * 2048)
        poke_i32(header, 24, flags - 1)
        waveOutWrite(self.device, header, 48)
        self.next = (self.next + 1) % 6
        done = done + 1
    end
    if done == 6 and self.started
        self.gaps = self.gaps + 1
    end
    self.started = true
    var kept: [Voice] = []
    for voice in self.voices
        if voice.playing
            kept.add(voice)
        end
    end
    if len(kept) != len(self.voices)
        self.voices = kept
    end
end

# Mixes 512 frames into one buffer
func Sound.fill(data: int)
    var mix = self.mix
    for i in 0..1024
        poke_i64(mix, i * 8, 0)
    end
    for voice in self.voices
        if voice.playing
            var level = int(voice.volume * self.volume * 32768.0)
            var samples = voice.clip.samples
            var frames = voice.clip.frames
            var at = 0
            while at < 512 and voice.playing
                var count = frames - voice.place
                if count > 512 - at
                    count = 512 - at
                end
                var from = voice.place * 4
                var to = at * 16
                for f in 0..count
                    poke_i64(mix, to, peek_i64(mix, to) + (peek_u16(samples, from) - 32768) * level)
                    poke_i64(mix, to + 8, peek_i64(mix, to + 8) + (peek_u16(samples, from + 2) - 32768) * level)
                    from = from + 4
                    to = to + 16
                end
                at = at + count
                voice.place = voice.place + count
                if voice.place == frames
                    if voice.looping
                        voice.place = 0
                    else
                        voice.playing = false
                    end
                end
            end
        end
    end
    for i in 0..1024
        var value = peek_i64(mix, i * 8) / 32768
        if value > 32767
            value = 32767
        elif value < -32768
            value = -32768
        end
        if self.silent
            value = 0
        end
        poke_u16(data, i * 2, (value + 65536) % 65536)
    end
end

# --- Clips ---

# How long a clip lasts, in seconds
func Clip.seconds(): float
    return float(self.frames) / 44100.0
end

# Makes a tone: "sine", "square", "saw", "triangle" or "noise", fading in and out over 5 ms
func tone(frequency: float, seconds: float, wave: string): Clip
    if not (frequency > 0.0 and frequency <= 20000.0)
        fail(f"A tone's frequency goes from above 0.0 to 20000.0, not {frequency}.")
    end
    if not (seconds >= 0.0 and seconds <= 600.0)
        fail(f"A tone lasts from 0.0 to 600.0 seconds, not {seconds}.")
    end
    var kind = 0
    if wave == "sine"
        kind = 0
    elif wave == "square"
        kind = 1
    elif wave == "saw"
        kind = 2
    elif wave == "triangle"
        kind = 3
    elif wave == "noise"
        kind = 4
    else
        fail(f"sound doesn't know the wave {wave}. Use sine, square, saw, triangle or noise.")
    end
    var frames = int(seconds * 44100.0 + 0.5)
    var samples = memory(frames * 4 + 4)
    var step = frequency / 44100.0
    var phase = 0.0
    var seed = 12345
    var fade = 220
    for f in 0..frames
        var value = 0.0
        if kind == 0
            value = sound_sine(phase)
        elif kind == 1
            if phase < 0.5
                value = 1.0
            else
                value = -1.0
            end
        elif kind == 2
            value = 2.0 * phase - 1.0
        elif kind == 3
            if phase < 0.5
                value = 4.0 * phase - 1.0
            else
                value = 3.0 - 4.0 * phase
            end
        else
            seed = (seed * 1103515245 + 12345) % 2147483648
            value = float(seed) / 1073741824.0 - 1.0
        end
        if f < fade
            value = value * float(f) / float(fade)
        end
        if frames - 1 - f < fade
            value = value * float(frames - 1 - f) / float(fade)
        end
        var sample = int(value * 16384.0) + 32768
        poke_u16(samples, f * 4, sample)
        poke_u16(samples, f * 4 + 2, sample)
        phase = phase + step
        if phase >= 1.0
            phase = phase - 1.0
        end
    end
    return Clip{samples: samples, frames: frames}
end

# The sine of a whole turn times x, for x from 0.0 to 1.0
func sound_sine(x: float): float
    var sign = 1.0
    var t = x
    if t >= 0.5
        t = t - 0.5
        sign = -1.0
    end
    if t > 0.25
        t = 0.5 - t
    end
    var a = t * 6.283185307179586
    var a2 = a * a
    return sign * a * (1.0 - a2 / 6.0 * (1.0 - a2 / 20.0 * (1.0 - a2 / 42.0 * (1.0 - a2 / 72.0 * (1.0 - a2 / 110.0)))))
end

# --- WAV files ---

func sound_damaged(path: string, why: string)
    fail(f"{path} is damaged or not a WAV file: {why}.")
end

func sound_number(data: string, place: int, size: int): int
    var value = 0
    for i in 0..size
        value = value + byte(data, place + i) * sound_power(i)
    end
    return value
end

func sound_power(i: int): int
    if i == 0
        return 1
    elif i == 1
        return 256
    elif i == 2
        return 65536
    end
    return 16777216
end

# Reads a WAV file: 8, 16, 24 or 32-bit, or 32-bit float, in mono or stereo, at any rate
func load_sound(path: string): Clip
    var data = read_file(path)
    if len(data) < 12 or sound_number(data, 0, 4) != 1179011410 or sound_number(data, 8, 4) != 1163280727
        sound_damaged(path, "it doesn't start like one")
    end
    var place = 12
    var kind = -1
    var channels = 0
    var rate = 0
    var bits = 0
    var start = -1
    var size = 0
    while place + 8 <= len(data) and start < 0
        var name = sound_number(data, place, 4)
        var length = sound_number(data, place + 4, 4)
        var body = place + 8
        if name == 544501094
            if length < 16 or body + 16 > len(data)
                sound_damaged(path, "its format part is too short")
            end
            kind = sound_number(data, body, 2)
            channels = sound_number(data, body + 2, 2)
            rate = sound_number(data, body + 4, 4)
            bits = sound_number(data, body + 14, 2)
            if kind == 65534
                if length < 26 or body + 26 > len(data)
                    sound_damaged(path, "its format part is too short")
                end
                kind = sound_number(data, body + 24, 2)
            end
        elif name == 1635017060
            if kind < 0
                sound_damaged(path, "its sound comes before its format")
            end
            start = body
            size = length
            if size > len(data) - body
                size = len(data) - body
            end
        end
        place = body + length + length % 2
    end
    if kind < 0
        sound_damaged(path, "it has no format part")
    end
    if start < 0
        sound_damaged(path, "it has no sound part")
    end
    if channels != 1 and channels != 2
        fail(f"{path} has {channels} channels, but sound only plays mono and stereo WAV files.")
    end
    if rate < 1000 or rate > 384000
        sound_damaged(path, f"its rate of {rate} frames a second isn't usable")
    end
    var supported = (kind == 1 and (bits == 8 or bits == 16 or bits == 24 or bits == 32)) or (kind == 3 and bits == 32)
    if not supported
        fail(f"{path} is a kind of WAV sound can't read yet (format {kind}, {bits} bits). Use 8, 16, 24 or 32-bit, or 32-bit float.")
    end
    var width = bits / 8
    var in_frames = size / (width * channels)
    var raw = memory(in_frames * width * channels + 8)
    for i in 0..in_frames * width * channels
        poke_u8(raw, i, byte(data, start + i))
    end
    var left = memory(in_frames * 4 + 8)
    var right = memory(in_frames * 4 + 8)
    for f in 0..in_frames
        var at = f * width * channels
        poke_i32(left, f * 4, sound_sample(raw, at, kind, bits))
        if channels == 2
            poke_i32(right, f * 4, sound_sample(raw, at + width, kind, bits))
        else
            poke_i32(right, f * 4, peek_i32(left, f * 4))
        end
    end
    if rate == 44100
        var samples = memory(in_frames * 4 + 4)
        for f in 0..in_frames
            poke_u16(samples, f * 4, peek_i32(left, f * 4) + 32768)
            poke_u16(samples, f * 4 + 2, peek_i32(right, f * 4) + 32768)
        end
        return Clip{samples: samples, frames: in_frames}
    end
    var frames = in_frames * 44100 / rate
    var samples = memory(frames * 4 + 4)
    for f in 0..frames
        var spot = f * rate
        var first = spot / 44100
        var part = spot % 44100
        var second = first + 1
        if second >= in_frames
            second = in_frames - 1
        end
        var l = (peek_i32(left, first * 4) * (44100 - part) + peek_i32(left, second * 4) * part) / 44100
        var r = (peek_i32(right, first * 4) * (44100 - part) + peek_i32(right, second * 4) * part) / 44100
        poke_u16(samples, f * 4, l + 32768)
        poke_u16(samples, f * 4 + 2, r + 32768)
    end
    return Clip{samples: samples, frames: frames}
end

# One sample as a 16-bit number
func sound_sample(raw: int, at: int, kind: int, bits: int): int
    if kind == 3
        var value = peek_f32(raw, at)
        if not (value >= -1.0)
            return -32768
        elif value > 1.0
            return 32767
        end
        var whole = int(value * 32767.0)
        return whole
    end
    if bits == 8
        return (peek_u8(raw, at) - 128) * 256
    end
    # The top 16 bits of the sample
    var top = peek_u16(raw, at + bits / 8 - 2)
    if top > 32767
        top = top - 65536
    end
    return top
end
)JAMOLIBRARY";
    if (name == "term.jamo") return R"JAMOLIBRARY(# term: colors, tables, progress bars and menus for programs in the console. use "term.jamo"
#
# Colors use the codes every modern terminal understands. Windows' own console needs them turned on first, which
# these functions do for you. Widths count bytes, so text in other languages may not line up exactly.

use "text.jamo"

extern "kernel32.dll" func GetStdHandle(which: i32): int
extern "kernel32.dll" func GetConsoleMode(console: int, mode: int): bool
extern "kernel32.dll" func SetConsoleMode(console: int, mode: i32): bool

const TERM_COLOR_CODES = 4

# The escape character that starts every terminal code
func term_escape(): string
    var place = memory(1)
    poke_u8(place, 0, 27)
    return memory_text(place, 1)
end

# Turns on color codes in Windows' own console. Answers false when there's no console to turn them on in
func term_colors_on(): bool
    var console = GetStdHandle(-11)
    var mode = memory(8)
    if not GetConsoleMode(console, mode)
        return false
    end
    var now = peek_i32(mode, 0)
    if now / TERM_COLOR_CODES % 2 == 0
        SetConsoleMode(console, now + TERM_COLOR_CODES)
    end
    return true
end

func term_code(number: int): string
    return term_escape() + "[" + string(number) + "m"
end

# The text in a color: black, red, green, yellow, blue, magenta, cyan, white or gray
func colored(text: string, color: string): string
    term_colors_on()
    var names = ["black", "red", "green", "yellow", "blue", "magenta", "cyan", "white"]
    var number = -1
    for i in 0..len(names)
        if names[i] == color
            number = 30 + i
        end
    end
    if color == "gray"
        number = 90
    end
    if number < 0
        fail(f"colored doesn't know the color {color}. It has black, red, green, yellow, blue, magenta, cyan, white and gray.")
    end
    return term_code(number) + text + term_code(0)
end

func bold(text: string): string
    term_colors_on()
    return term_code(1) + text + term_code(0)
end

# Clears the console and puts the cursor at the top
func clear_screen()
    term_colors_on()
    var e = term_escape()
    print(e + "[2J" + e + "[H")
end

# --- Lining text up ---

# The text with spaces after it, up to width bytes
func pad_right(text: string, width: int): string
    if len(text) >= width
        return text
    end
    return text + repeat(" ", width - len(text))
end

# The text with spaces before it, up to width bytes
func pad_left(text: string, width: int): string
    if len(text) >= width
        return text
    end
    return repeat(" ", width - len(text)) + text
end

# The rows as a table, each column as wide as its widest text. The first row is the heading, with a line under it
func table(rows: [[string]]): string
    if len(rows) == 0
        return ""
    end
    var widths: [int] = []
    for row in rows
        for i in 0..len(row)
            if i >= len(widths)
                widths.add(0)
            end
            if len(row[i]) > widths[i]
                widths[i] = len(row[i])
            end
        end
    end
    var lines: [string] = []
    for r in 0..len(rows)
        var cells: [string] = []
        for i in 0..len(rows[r])
            if i == len(rows[r]) - 1
                cells.add(rows[r][i])
            else
                cells.add(pad_right(rows[r][i], widths[i]))
            end
        end
        lines.add(join(cells, "  "))
        if r == 0 and len(rows) > 1
            var dashes: [string] = []
            for width in widths
                dashes.add(repeat("-", width))
            end
            lines.add(join(dashes, "  "))
        end
    end
    return join(lines, "\n")
end

# A bar like [##########----------] 50%, width characters wide between the brackets
func progress_bar(done: int, total: int, width: int): string
    var part = 0
    if total > 0
        part = done * 100 / total
    end
    if part < 0
        part = 0
    end
    if part > 100
        part = 100
    end
    var filled = part * width / 100
    return "[" + repeat("#", filled) + repeat("-", width - filled) + "] " + string(part) + "%"
end

# Shows the options numbered from 1, and asks until a number is typed. Answers with the option's place,
# counting from 0, or -1 when the input runs out
func menu(title: string, options: [string]): int
    print(title)
    for i in 0..len(options)
        print(f"  {i + 1}. {options[i]}")
    end
    var tries = 0
    while tries < 1000
        print(f"Pick 1 to {len(options)}:")
        var typed = trim(input())
        if is_int(typed)
            var picked = int(typed)
            if picked >= 1 and picked <= len(options)
                return picked - 1
            end
        end
        if typed == ""
            tries += 100
        end
        tries += 1
    end
    return -1
end
)JAMOLIBRARY";
    if (name == "test.jamo") return R"JAMOLIBRARY(# test: checks for your own code, with a summary at the end. use "test.jamo"
#
#     var t = new_tests()
#     t.equal_int("adding", add(2, 3), 5)
#     t.check("is alive", enemy.is_alive())
#     t.finish()
#
# finish shows how many passed, and ends the program with exit code 1 when any failed, so other tools can tell.

extern "kernel32.dll" func ExitProcess(code: i32)

struct Tests
    passed: int
    failed: int
end

func new_tests(): Tests
    return Tests{passed: 0, failed: 0}
end

func Tests.pass_or_fail(name: string, ok: bool, problem: string)
    if ok
        self.passed += 1
        return
    end
    self.failed += 1
    print(f"FAIL {name}: {problem}")
end

func Tests.check(name: string, ok: bool)
    self.pass_or_fail(name, ok, "it was false")
end

func Tests.equal_int(name: string, got: int, want: int)
    self.pass_or_fail(name, got == want, f"got {got}, wanted {want}")
end

func Tests.equal_text(name: string, got: string, want: string)
    self.pass_or_fail(name, got == want, f"got \"{got}\", wanted \"{want}\"")
end

func Tests.equal_bool(name: string, got: bool, want: bool)
    self.pass_or_fail(name, got == want, f"got {got}, wanted {want}")
end

# Floats rarely come out exactly, so this passes when got is within close of want
func Tests.equal_float(name: string, got: float, want: float, close: float)
    var gap = got - want
    if gap < 0.0
        gap = 0.0 - gap
    end
    self.pass_or_fail(name, gap <= close, f"got {got}, wanted {want}")
end

# Shows the summary. When any check failed, the program ends with exit code 1
func Tests.finish()
    var total = self.passed + self.failed
    if self.failed == 0
        print(f"All {total} checks passed.")
        return
    end
    print(f"{self.failed} of {total} checks failed.")
    ExitProcess(1)
end
)JAMOLIBRARY";
    if (name == "text.jamo") return R"JAMOLIBRARY(# text: splitting, searching, cutting, changing and joining text, checking numbers, and sorting arrays. use "text.jamo"
#
# Places count bytes from 0, like text[i] and len. Results are built in memory and made into text with
# memory_text in one go, so long text doesn't get joined a letter at a time.

# --- Building text ---

# Copies the bytes of text into memory at a place, and gives back the place after them
func text_put(place: int, text: string): int
    var count = len(text)
    for i in 0..count
        poke_u8(place, i, byte(text, i))
    end
    return place + count
end

# True if part is in text starting at that place. The place must leave room for part
func text_matches_at(text: string, part: string, at: int): bool
    for i in 0..len(part)
        if byte(text, at + i) != byte(part, i)
            return false
        end
    end
    return true
end

# --- Searching ---

# Where part first starts in text, or -1 if it isn't there. Empty part is found at 0
func find(text: string, part: string): int
    var size = len(part)
    if size == 0
        return 0
    end
    var first = byte(part, 0)
    for at in 0..len(text) - size + 1
        if byte(text, at) == first and text_matches_at(text, part, at)
            return at
        end
    end
    return -1
end

func contains(text: string, part: string): bool
    return find(text, part) >= 0
end

func starts_with(text: string, part: string): bool
    return len(part) <= len(text) and text_matches_at(text, part, 0)
end

func ends_with(text: string, part: string): bool
    return len(part) <= len(text) and text_matches_at(text, part, len(text) - len(part))
end

# --- Cutting ---

# The bytes from place from up to, but not including, place to. slice("hello", 1, 3) is "el"
func slice(text: string, from: int, to: int): string
    if from < 0 or to > len(text) or from > to
        fail(f"slice needs 0 <= from <= to <= {len(text)}, the text's length, but from is {from} and to is {to}.")
    end
    var count = to - from
    var place = memory(count + 1)
    for i in 0..count
        poke_u8(place, i, byte(text, from + i))
    end
    return memory_text(place, count)
end

# The pieces of text between each by. split("a,b,,c", ",") gives ["a", "b", "", "c"]
func split(text: string, by: string): [string]
    var size = len(by)
    if size == 0
        fail("split needs something to split by, but it was given empty text.")
    end
    var count = len(text)
    var place = memory(count + 1)
    text_put(place, text)
    var parts: [string] = []
    var start = 0
    var at = 0
    var first = byte(by, 0)
    while at <= count - size
        if byte(text, at) == first and text_matches_at(text, by, at)
            parts.add(memory_text(place + start, at - start))
            at += size
            start = at
        else
            at += 1
        end
    end
    parts.add(memory_text(place + start, count - start))
    return parts
end

func text_is_space(code: int): bool
    return code == 32 or code == 9 or code == 10 or code == 13
end

# Text without the spaces, tabs and line endings at its start and end
func trim(text: string): string
    var from = 0
    var to = len(text)
    while from < to and text_is_space(byte(text, from))
        from += 1
    end
    while to > from and text_is_space(byte(text, to - 1))
        to -= 1
    end
    if from == 0 and to == len(text)
        return text
    end
    return slice(text, from, to)
end

# --- Changing ---

# Text with every old changed to new, from left to right
func replace(text: string, old: string, new: string): string
    var size = len(old)
    if size == 0
        fail("replace needs some text to look for, but it was given empty text.")
    end
    var count = len(text)
    var first = byte(old, 0)
    var found = 0
    var at = 0
    while at <= count - size
        if byte(text, at) == first and text_matches_at(text, old, at)
            found += 1
            at += size
        else
            at += 1
        end
    end
    if found == 0
        return text
    end
    var total = count + found * (len(new) - size)
    var place = memory(total + 1)
    var out = 0
    at = 0
    while at < count
        if at <= count - size and byte(text, at) == first and text_matches_at(text, old, at)
            out = text_put(place + out, new) - place
            at += size
        else
            poke_u8(place, out, byte(text, at))
            out += 1
            at += 1
        end
    end
    return memory_text(place, total)
end

# Text with a to z made into A to Z. Other letters stay as they are
func upper(text: string): string
    var count = len(text)
    var place = memory(count + 1)
    for i in 0..count
        var code = byte(text, i)
        if code >= 97 and code <= 122
            code -= 32
        end
        poke_u8(place, i, code)
    end
    return memory_text(place, count)
end

# Text with A to Z made into a to z. Other letters stay as they are
func lower(text: string): string
    var count = len(text)
    var place = memory(count + 1)
    for i in 0..count
        var code = byte(text, i)
        if code >= 65 and code <= 90
            code += 32
        end
        poke_u8(place, i, code)
    end
    return memory_text(place, count)
end

# --- Joining ---

# The parts in order with between in each gap. join(["a", "b"], ", ") is "a, b"
func join(parts: [string], between: string): string
    var total = 0
    for part in parts
        total += len(part)
    end
    if len(parts) > 1
        total += (len(parts) - 1) * len(between)
    end
    var place = memory(total + 1)
    var at = place
    for i in 0..len(parts)
        if i > 0
            at = text_put(at, between)
        end
        at = text_put(at, parts[i])
    end
    return memory_text(place, total)
end

# The text that many times over. repeat("ab", 3) is "ababab"
func repeat(text: string, times: int): string
    if times < 0
        fail(f"repeat needs 0 or more times, not {times}.")
    end
    var count = len(text)
    if count > 0 and times > 4611686018427387903 / count
        fail(f"repeat would make text too long: {count} bytes {times} times.")
    end
    var total = count * times
    var place = memory(total + 1)
    var at = place
    for i in 0..times
        at = text_put(at, text)
    end
    return memory_text(place, total)
end

# --- Checking numbers ---

# The same limits float() has. From here up, a decimal rounds past the largest float: 2^1024 - 2^970
const TEXT_FLOAT_HIGH = "179769313486231580793728971405303415079934132710037826936173778980444968292764750946649017977587207096330286416692887910946555547851940402630657488671505820681908902000708383676273854845817711531764475730270069855571366959622842914819860834936475292719074168444365510704342711559699508093042880177904174497792"
# At or below this, a decimal that isn't 0 rounds to 0: 2^-1075, all 1075 digits after the dot
const TEXT_FLOAT_LOW = "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000024703282292062327208828439643411068618252990130716238221279284125033775363510437593264991818081799618989828234772285886546332835517796989819938739800539093906315035659515570226392290858392449105184435931802849936536152500319370457678249219365623669863658480757001585769269903706311928279558551332927834338409351978015531246597263579574622766465272827220056374006485499977096599470454020828166226237857393450736339007967761930577506740176324673600968951340535537458516661134223766678604162159680461914467291840300530057530849048765391711386591646239524912623653881879636239373280423891018672348497668235089863388587925628302755995657524455507255189313690836254779186948667994968324049705821028513185451396213837722826145437693412532098591327667236328125"

func text_all_digits(text: string): bool
    if len(text) == 0
        return false
    end
    for i in 0..len(text)
        var code = byte(text, i)
        if code < 48 or code > 57
            return false
        end
    end
    return true
end

func text_without_leading_zeros(digits: string): string
    var first = 0
    while first < len(digits) and byte(digits, first) == 48
        first += 1
    end
    return slice(digits, first, len(digits))
end

# Below 0 when the whole number a is smaller than b, with neither having leading zeros
func text_compare_digits(a: string, b: string): int
    if len(a) != len(b)
        return len(a) - len(b)
    end
    for i in 0..len(a)
        if byte(a, i) != byte(b, i)
            return byte(a, i) - byte(b, i)
        end
    end
    return 0
end

# True when int(text) would work: digits with an optional minus sign, small enough for 64 bits
func is_int(text: string): bool
    var digits = text
    var largest = "9223372036854775807"
    if starts_with(text, "-")
        digits = slice(text, 1, len(text))
        largest = "9223372036854775808"
    end
    if not text_all_digits(digits)
        return false
    end
    return text_compare_digits(text_without_leading_zeros(digits), largest) <= 0
end

# True when float(text) would work: digits with an optional minus sign, then maybe a dot and more digits,
# and not so large it rounds past the largest float or so small that a number that isn't 0 rounds to 0
func is_float(text: string): bool
    var number = text
    if starts_with(text, "-")
        number = slice(text, 1, len(text))
    end
    var whole = number
    var parts = ""
    var dot = find(number, ".")
    if dot >= 0
        whole = slice(number, 0, dot)
        parts = slice(number, dot + 1, len(number))
        if not text_all_digits(parts)
            return false
        end
    end
    if not text_all_digits(whole)
        return false
    end
    whole = text_without_leading_zeros(whole)
    if whole != ""
        return text_compare_digits(whole, TEXT_FLOAT_HIGH) < 0
    end
    var last = len(parts)
    while last > 0 and byte(parts, last - 1) == 48
        last -= 1
    end
    if last == 0
        return true
    end
    # Both have the dot in the same place, so the one with the first larger digit is larger
    for i in 0..last
        var mine = byte(parts, i)
        var bound = 48
        if i < len(TEXT_FLOAT_LOW)
            bound = byte(TEXT_FLOAT_LOW, i)
        end
        if mine != bound
            return mine > bound
        end
    end
    return last > len(TEXT_FLOAT_LOW)
end

# --- Sorting and turning arrays around. These change the array itself ---

# Sorts ints from smallest to largest
func sort_ints(items: [int])
    var count = len(items)
    if count < 2
        return
    end
    var from = items
    var to: [int] = []
    for item in items
        to.add(item)
    end
    var flipped = false
    var width = 1
    while width < count
        var left = 0
        while left < count
            var middle = left + width
            if middle > count
                middle = count
            end
            var right = middle + width
            if right > count
                right = count
            end
            var a = left
            var b = middle
            for place in left..right
                if b >= right or (a < middle and from[a] <= from[b])
                    to[place] = from[a]
                    a += 1
                else
                    to[place] = from[b]
                    b += 1
                end
            end
            left = right
        end
        var spare = from
        from = to
        to = spare
        flipped = not flipped
        width *= 2
    end
    if flipped
        for i in 0..count
            items[i] = from[i]
        end
    end
end

# True if a comes before b, by comparing their bytes. All capitals come before small letters
func text_before(a: string, b: string): bool
    var a_size = len(a)
    var b_size = len(b)
    var shorter = a_size
    if b_size < shorter
        shorter = b_size
    end
    for i in 0..shorter
        var x = byte(a, i)
        var y = byte(b, i)
        if x != y
            return x < y
        end
    end
    return a_size < b_size
end

# Sorts text by its bytes, so "Zoo" comes before "apple", and "app" before "apple"
func sort_strings(items: [string])
    var count = len(items)
    if count < 2
        return
    end
    var from = items
    var to: [string] = []
    for item in items
        to.add(item)
    end
    var flipped = false
    var width = 1
    while width < count
        var left = 0
        while left < count
            var middle = left + width
            if middle > count
                middle = count
            end
            var right = middle + width
            if right > count
                right = count
            end
            var a = left
            var b = middle
            for place in left..right
                if b >= right or (a < middle and not text_before(from[b], from[a]))
                    to[place] = from[a]
                    a += 1
                else
                    to[place] = from[b]
                    b += 1
                end
            end
            left = right
        end
        var spare = from
        from = to
        to = spare
        flipped = not flipped
        width *= 2
    end
    if flipped
        for i in 0..count
            items[i] = from[i]
        end
    end
end

func reverse_ints(items: [int])
    var last = len(items) - 1
    for i in 0..len(items) / 2
        var keep = items[i]
        items[i] = items[last - i]
        items[last - i] = keep
    end
end

func reverse_strings(items: [string])
    var last = len(items) - 1
    for i in 0..len(items) / 2
        var keep = items[i]
        items[i] = items[last - i]
        items[last - i] = keep
    end
end
)JAMOLIBRARY";
    if (name == "time.jamo") return R"JAMOLIBRARY(# time: seconds since the program started, today's date and time, and waiting. use "time.jamo"
#
# Jamo has no program-wide variables, so the clock count when the program started is kept in bytes 24
# to 47 of the .exe's DOS header in memory, which Windows never reads once the program is loaded (see
# math.jamo, which uses 8 to 23). The first call works it out from when Windows made the process, so
# it counts from the real start, and later calls only read the performance counter, with no memory.

extern "kernel32.dll" func QueryPerformanceCounter(place: int): bool
extern "kernel32.dll" func QueryPerformanceFrequency(place: int): bool
extern "kernel32.dll" func Sleep(milliseconds: i32)
extern "kernel32.dll" func GetModuleHandleW(name: int): int
extern "kernel32.dll" func VirtualProtect(place: int, size: int, protect: i32, old: int): bool
extern "kernel32.dll" func GetCurrentProcess(): int
extern "kernel32.dll" func GetProcessTimes(process: int, created: int, ended: int, kernel: int, user: int): bool
extern "kernel32.dll" func GetSystemTimePreciseAsFileTime(place: int)
extern "kernel32.dll" func GetLocalTime(place: int)

struct DateTime
    year: int
    month: int
    day: int
    weekday: int
    hour: int
    minute: int
    second: int
    millisecond: int
end

# --- Seconds ---

# Works out the counter's value when the program started, on the first call. The counter's speed
# never changes while Windows runs, so it's kept at 40, and 32 is room for the counter to write to
func time_setup(header: int)
    if not VirtualProtect(header, 64, 4, memory(8))
        fail("time couldn't set up its clock.")
    end
    QueryPerformanceFrequency(header + 40)
    var created = memory(8)
    var unused = memory(8)
    var clock = memory(8)
    GetProcessTimes(GetCurrentProcess(), created, unused, unused, unused)
    GetSystemTimePreciseAsFileTime(clock)
    QueryPerformanceCounter(header + 32)
    var ticks = peek_i64(clock, 0) - peek_i64(created, 0)
    if ticks < 0
        ticks = 0
    end
    poke_i64(header, 24, peek_i64(header, 32) - int(float(ticks) / 10000000.0 * float(peek_i64(header, 40))))
end

# Seconds since the program started, as a float with better than a microsecond's detail. It only goes up
func seconds_since_start(): float
    var header = GetModuleHandleW(0)
    if peek_i64(header, 24) == 0
        time_setup(header)
    end
    QueryPerformanceCounter(header + 32)
    var seconds = float(peek_i64(header, 32) - peek_i64(header, 24)) / float(peek_i64(header, 40))
    if seconds < 0.0
        return 0.0
    end
    return seconds
end

# Waits this many seconds, to about a millisecond, without using the processor
func sleep(seconds: float)
    if not (seconds >= 0.0 and seconds <= 1000000000.0)
        fail(f"sleep needs from 0.0 to 1000000000.0 seconds, not {seconds}.")
    end
    var left = int(seconds * 1000.0 + 0.5)
    while left > 1000000000
        Sleep(1000000000)
        left = left - 1000000000
    end
    Sleep(left)
end

# Waits this many milliseconds
func sleep_ms(milliseconds: int)
    sleep(float(milliseconds) / 1000.0)
end

# Whole milliseconds since the program started
func time_ms(): int
    return int(seconds_since_start() * 1000.0)
end

# --- The date and time of day ---

# The PC's date and time now, in its own time zone. weekday is 0 for Sunday up to 6 for Saturday
func now(): DateTime
    var place = memory(16)
    GetLocalTime(place)
    return DateTime{year: peek_u16(place, 0), month: peek_u16(place, 2), weekday: peek_u16(place, 4), day: peek_u16(place, 6), hour: peek_u16(place, 8), minute: peek_u16(place, 10), second: peek_u16(place, 12), millisecond: peek_u16(place, 14)}
end

func time_two_digits(number: int): string
    if number < 10
        return f"0{number}"
    end
    return string(number)
end

# Like 2026-11-03
func DateTime.date_text(): string
    return f"{self.year}-{time_two_digits(self.month)}-{time_two_digits(self.day)}"
end

# Like 09:05:30, on a 24-hour clock
func DateTime.time_text(): string
    return f"{time_two_digits(self.hour)}:{time_two_digits(self.minute)}:{time_two_digits(self.second)}"
end

# Like Tuesday
func DateTime.weekday_name(): string
    var names = ["Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"]
    return names[self.weekday]
end
)JAMOLIBRARY";
    if (name == "ui.jamo") return R"JAMOLIBRARY(# ui: labels, panels, buttons, checkboxes, sliders, lists and text boxes, drawn with gfx. use "ui.jamo"
#
# Controls are drawn fresh every frame between begin() and finish(), and each call answers with the
# control's value, so there's nothing to set up or keep in step. They stack down the window from the top
# left, as wide as the window or the panel they're in. Every control that can take the keyboard gets a
# number from the order it's drawn in: focus holds the number of the one that has the keyboard, and Tab
# moves to the next number at the end of the frame.

use "gfx.jamo"

# How everything looks. Change any field of ui.theme to restyle
struct Theme
    background: Color
    panel: Color
    text: Color
    soft_text: Color
    control: Color
    hover: Color
    accent: Color
    on_accent: Color
    field: Color
    text_size: float
    gap: float
    padding: float
    height: float
    round: float
end

struct Ui
    gfx: Gfx
    window: Window
    theme: Theme
    surface: Color
    left: float
    y: float
    width: float
    columns: int
    column: int
    row_height: float
    count: int
    focus: int
    active: int
    pressed: bool
    claimed: bool
    caret: int
    view: int
    blink_from: int
    frame: int
    scrolls: [int]
    last_x: float
    last_y: float
    last_width: float
    last_height: float
end

# --- Starting ---

func start_ui(g: Gfx): Ui
    return Ui{gfx: g, window: g.window, theme: light_theme(), surface: rgb(0, 0, 0), left: 0.0, y: 0.0, width: 0.0, columns: 1, column: 0, row_height: 0.0, count: 0, focus: -1, active: -1, pressed: false, claimed: false, caret: -1, view: 0, blink_from: 0, frame: 0, scrolls: [], last_x: 0.0, last_y: 0.0, last_width: 0.0, last_height: 0.0}
end

func light_theme(): Theme
    return Theme{background: rgb(236, 238, 234), panel: rgb(252, 252, 250), text: rgb(26, 30, 28), soft_text: rgb(84, 92, 88), control: rgb(224, 228, 223), hover: rgb(206, 212, 206), accent: rgb(22, 112, 90), on_accent: rgb(255, 255, 255), field: rgb(238, 240, 236), text_size: 17.0, gap: 8.0, padding: 16.0, height: 36.0, round: 6.0}
end

func dark_theme(): Theme
    return Theme{background: rgb(22, 24, 28), panel: rgb(33, 36, 41), text: rgb(236, 238, 240), soft_text: rgb(160, 166, 172), control: rgb(50, 54, 61), hover: rgb(66, 71, 80), accent: rgb(72, 196, 156), on_accent: rgb(12, 24, 20), field: rgb(25, 27, 32), text_size: 17.0, gap: 8.0, padding: 16.0, height: 36.0, round: 6.0}
end

# Starts a frame of controls, at the window's top left
func Ui.begin()
    self.frame += 1
    self.count = 0
    self.claimed = false
    self.pressed = self.window.mouse_pressed("left")
    self.place(self.theme.padding, self.theme.padding, float(self.window.width()) - 2.0 * self.theme.padding)
    self.surface = self.theme.background
end

# Ends the frame: a click on nothing drops the keyboard focus, and Tab or Shift+Tab moves it
func Ui.finish()
    if self.pressed and not self.claimed
        self.focus = -1
    end
    if not self.window.mouse_down("left")
        self.active = -1
    end
    if self.window.key_repeated("tab") and self.count > 0
        var next = self.focus + 1
        if self.window.key_down("shift")
            next = self.focus - 1
            if next < 0
                next = self.count - 1
            end
        elif next >= self.count
            next = 0
        end
        self.focus_on(next)
    end
end

func Ui.focus_on(id: int)
    if self.focus != id
        self.focus = id
        self.caret = -1
        self.view = 0
        self.blink_from = self.frame
    end
end

# --- Layout ---

# Puts the next controls at x and y, this wide
func Ui.place(x: float, y: float, width: float)
    self.left = x
    self.y = y
    self.width = width
    self.columns = 1
    self.column = 0
end

# Draws a panel, and puts the next controls inside it
func Ui.panel(x: float, y: float, width: float, height: float)
    ui_round(self.gfx, x, y, width, height, self.theme.round * 1.5, self.theme.panel)
    self.place(x + self.theme.padding, y + self.theme.padding, width - 2.0 * self.theme.padding)
    self.surface = self.theme.panel
end

# The next few controls sit side by side, sharing the width
func Ui.columns(count: int)
    if count < 1
        fail(f"ui needs at least 1 column, not {count}.")
    end
    self.columns = count
    self.column = 0
    self.row_height = 0.0
end

# Leaves an empty gap before the next control
func Ui.space(height: float)
    self.y += height
end

# Saves where the next control goes in last_x, last_y, last_width and last_height
func Ui.next(height: float)
    var x = self.left
    var width = self.width
    if self.columns > 1
        width = (self.width - self.theme.gap * float(self.columns - 1)) / float(self.columns)
        x = self.left + float(self.column) * (width + self.theme.gap)
    end
    self.last_x = x
    self.last_y = self.y
    self.last_width = width
    self.last_height = height
    if self.columns > 1
        if height > self.row_height
            self.row_height = height
        end
        self.column += 1
        if self.column == self.columns
            self.y += self.row_height + self.theme.gap
            self.columns = 1
            self.column = 0
        end
    else
        self.y += height + self.theme.gap
    end
end

func Ui.hovered(): bool
    var x = float(self.window.mouse_x())
    var y = float(self.window.mouse_y())
    return x >= self.last_x and x < self.last_x + self.last_width and y >= self.last_y and y < self.last_y + self.last_height
end

# Gives the control its number, and takes the keyboard for it when it's clicked
func Ui.control(drag: bool): int
    var id = self.count
    self.count += 1
    if self.pressed and self.hovered()
        self.claimed = true
        self.focus_on(id)
        if drag
            self.active = id
        end
    end
    return id
end

# True when the mouse went down on this control and came back up over it
func Ui.clicked(id: int): bool
    return self.active == id and not self.window.mouse_down("left") and self.hovered()
end

# --- Drawing ---

func Ui.text_at(words: string, x: float, top: float, height: float, color: Color)
    var size = self.theme.text_size
    self.gfx.text(words, x, top + height / 2.0 - size * 0.62, size, color)
end

func Ui.ring(id: int)
    if self.focus == id
        ui_round(self.gfx, self.last_x - 3.0, self.last_y - 3.0, self.last_width + 6.0, self.last_height + 6.0, self.theme.round + 3.0, self.theme.accent)
        ui_round(self.gfx, self.last_x - 1.0, self.last_y - 1.0, self.last_width + 2.0, self.last_height + 2.0, self.theme.round + 1.0, self.surface)
    end
end

# A rectangle with round corners
func ui_round(g: Gfx, x: float, y: float, width: float, height: float, radius: float, color: Color)
    var r = radius
    if r > width / 2.0
        r = width / 2.0
    end
    if r > height / 2.0
        r = height / 2.0
    end
    if r < 1.0
        g.rect(x, y, width, height, color)
        return
    end
    g.rect(x + r, y, width - 2.0 * r, height, color)
    g.rect(x, y + r, r, height - 2.0 * r, color)
    g.rect(x + width - r, y + r, r, height - 2.0 * r, color)
    g.circle(x + r, y + r, r, color)
    g.circle(x + width - r, y + r, r, color)
    g.circle(x + r, y + height - r, r, color)
    g.circle(x + width - r, y + height - r, r, color)
end

# Part of a text, from one place up to another
func ui_part(words: string, from: int, to: int): string
    var part = ""
    for i in from..to
        part = part + words[i]
    end
    return part
end

# The front of a text that fits in a width, with ... when it's cut
func Ui.fit(words: string, width: float): string
    var size = self.theme.text_size
    if self.gfx.text_width(words, size) <= width
        return words
    end
    var room = width - self.gfx.text_width("...", size)
    var used = 0.0
    var count = 0
    while count < len(words)
        var next = self.gfx.text_width(words[count], size)
        if used + next > room
            break
        end
        used += next
        count += 1
    end
    return ui_part(words, 0, count) + "..."
end

# A number with up to 2 decimals, and none when it's whole
func ui_short(value: float): string
    var hundredths = int(value * 100.0 + 0.5)
    if value < 0.0
        hundredths = int(value * 100.0 - 0.5)
    end
    if hundredths % 100 == 0
        return string(hundredths / 100)
    end
    return string(float(hundredths) / 100.0)
end

# --- Controls ---

func Ui.label(words: string)
    self.next(self.theme.text_size * 1.6)
    self.text_at(words, self.last_x, self.last_y, self.last_height, self.theme.text)
end

# Larger text, for the top of a page or panel
func Ui.title(words: string)
    var size = self.theme.text_size * 1.4
    self.next(size * 1.6)
    self.gfx.text(words, self.last_x, self.last_y + self.last_height / 2.0 - size * 0.62, size, self.theme.text)
end

# Text in the softer color, for notes and hints
func Ui.note(words: string)
    self.next(self.theme.text_size * 1.6)
    self.text_at(words, self.last_x, self.last_y, self.last_height, self.theme.soft_text)
end

# True in the frame it's clicked, or pressed with Space or Enter while it has the keyboard
func Ui.button(words: string): bool
    self.next(self.theme.height)
    var id = self.control(true)
    var clicked = self.clicked(id)
    if self.focus == id and (self.window.key_pressed("space") or self.window.key_pressed("enter"))
        clicked = true
    end
    var color = self.theme.control
    if self.hovered()
        color = self.theme.hover
    end
    self.ring(id)
    ui_round(self.gfx, self.last_x, self.last_y, self.last_width, self.last_height, self.theme.round, color)
    var shown = self.fit(words, self.last_width - 16.0)
    var x = self.last_x + (self.last_width - self.gfx.text_width(shown, self.theme.text_size)) / 2.0
    self.text_at(shown, x, self.last_y, self.last_height, self.theme.text)
    return clicked
end

# Answers whether it's checked, after any click or Space
func Ui.checkbox(words: string, checked: bool): bool
    self.next(28.0)
    var id = self.control(true)
    var result = checked
    if self.clicked(id)
        result = not result
    end
    if self.focus == id and self.window.key_pressed("space")
        result = not result
    end
    var box = 22.0
    var top = self.last_y + 3.0
    if self.focus == id
        ui_round(self.gfx, self.last_x - 3.0, top - 3.0, box + 6.0, box + 6.0, 8.0, self.theme.accent)
        ui_round(self.gfx, self.last_x - 1.0, top - 1.0, box + 2.0, box + 2.0, 6.0, self.surface)
    end
    if result
        ui_round(self.gfx, self.last_x, top, box, box, 5.0, self.theme.accent)
        self.gfx.line(self.last_x + 5.5, top + 11.5, self.last_x + 9.5, top + 15.5, 2.5, self.theme.on_accent)
        self.gfx.line(self.last_x + 9.5, top + 15.5, self.last_x + 16.5, top + 7.0, 2.5, self.theme.on_accent)
    else
        var color = self.theme.soft_text
        if self.hovered()
            color = self.theme.text
        end
        ui_round(self.gfx, self.last_x, top, box, box, 5.0, color)
        ui_round(self.gfx, self.last_x + 2.0, top + 2.0, box - 4.0, box - 4.0, 3.5, self.theme.field)
    end
    self.text_at(self.fit(words, self.last_width - box - 12.0), self.last_x + box + 12.0, self.last_y, self.last_height, self.theme.text)
    return result
end

# Answers the value, from low to high, after any drag or arrow key
func Ui.slider(words: string, value: float, low: float, high: float): float
    if not (high > low)
        fail(f"A slider's high end must be above its low end, but they're {low} and {high}.")
    end
    self.next(self.theme.height)
    var label_width = self.last_width * 0.35
    var track_x = self.last_x + label_width + 10.0
    var track_width = self.last_width - label_width - 76.0
    if track_width < 20.0
        track_width = 20.0
    end
    var id = self.control(false)
    var mouse = float(self.window.mouse_x())
    if self.pressed and self.hovered() and mouse >= track_x - 12.0 and mouse <= track_x + track_width + 12.0
        self.active = id
    end
    var result = value
    if self.active == id and self.window.mouse_down("left")
        result = low + (mouse - track_x) / track_width * (high - low)
    end
    if self.focus == id
        var step = (high - low) / 20.0
        if self.window.key_repeated("left") or self.window.key_repeated("down")
            result -= step
        end
        if self.window.key_repeated("right") or self.window.key_repeated("up")
            result += step
        end
        if self.window.key_pressed("home")
            result = low
        end
        if self.window.key_pressed("end")
            result = high
        end
    end
    if result < low or result != result
        result = low
    end
    if result > high
        result = high
    end
    self.text_at(self.fit(words, label_width), self.last_x, self.last_y, self.last_height, self.theme.text)
    var middle = self.last_y + self.last_height / 2.0
    var knob = track_x + (result - low) / (high - low) * track_width
    ui_round(self.gfx, track_x, middle - 2.0, track_width, 4.0, 2.0, self.theme.hover)
    ui_round(self.gfx, track_x, middle - 2.0, knob - track_x, 4.0, 2.0, self.theme.accent)
    if self.focus == id
        self.gfx.circle(knob, middle, 14.0, self.theme.accent)
        self.gfx.circle(knob, middle, 12.0, self.surface)
    end
    self.gfx.circle(knob, middle, 9.0, self.theme.accent)
    self.text_at(ui_short(result), track_x + track_width + 16.0, self.last_y, self.last_height, self.theme.soft_text)
    return result
end

# Answers the chosen item's place, or -1 for none. Shows this many rows, and scrolls for the rest
func Ui.list(items: [string], selected: int, rows: int): int
    if rows < 1
        fail(f"A list shows at least 1 row, not {rows}.")
    end
    var row_height = 30.0
    self.next(float(rows) * row_height + 8.0)
    var id = self.control(false)
    while len(self.scrolls) <= id
        self.scrolls.add(0)
    end
    var scroll = self.scrolls[id]
    var count = len(items)
    var pick = selected
    var over = self.hovered()
    var top = self.last_y + 4.0
    if self.pressed and over
        var row = int((float(self.window.mouse_y()) - top) / row_height) + scroll
        if row >= 0 and row < count and float(self.window.mouse_y()) >= top
            pick = row
        end
    end
    if over
        scroll -= self.window.wheel()
    end
    if self.focus == id and count > 0
        var moved = false
        if self.window.key_repeated("up")
            pick -= 1
            moved = true
        end
        if self.window.key_repeated("down")
            pick += 1
            moved = true
        end
        if self.window.key_pressed("home")
            pick = 0
            moved = true
        end
        if self.window.key_pressed("end")
            pick = count - 1
            moved = true
        end
        if moved and pick < 0
            pick = 0
        end
        if moved and pick >= count
            pick = count - 1
        end
        if pick != selected
            if pick < scroll
                scroll = pick
            end
            if pick >= scroll + rows
                scroll = pick - rows + 1
            end
        end
    end
    if scroll > count - rows
        scroll = count - rows
    end
    if scroll < 0
        scroll = 0
    end
    self.scrolls[id] = scroll
    self.ring(id)
    ui_round(self.gfx, self.last_x, self.last_y, self.last_width, self.last_height, self.theme.round, self.theme.field)
    var text_room = self.last_width - 32.0
    var row_width = self.last_width - 8.0
    if count > rows
        row_width -= 10.0
        text_room -= 10.0
    end
    var mouse_row = -1
    if over
        mouse_row = int((float(self.window.mouse_y()) - top) / row_height) + scroll
    end
    var shown = scroll
    while shown < count and shown < scroll + rows
        var y = top + float(shown - scroll) * row_height
        var color = self.theme.text
        if shown == pick
            ui_round(self.gfx, self.last_x + 4.0, y, row_width, row_height, self.theme.round - 2.0, self.theme.accent)
            color = self.theme.on_accent
        elif shown == mouse_row
            ui_round(self.gfx, self.last_x + 4.0, y, row_width, row_height, self.theme.round - 2.0, self.theme.control)
        end
        self.text_at(self.fit(items[shown], text_room), self.last_x + 16.0, y, row_height, color)
        shown += 1
    end
    if count > rows
        var track = self.last_height - 8.0
        var thumb = track * float(rows) / float(count)
        var thumb_y = top + (track - thumb) * float(scroll) / float(count - rows)
        ui_round(self.gfx, self.last_x + self.last_width - 10.0, thumb_y, 5.0, thumb, 2.5, self.theme.hover)
    end
    return pick
end

# Answers the text after any typing. Click it or Tab to it, then type, with Backspace, Delete, the
# arrows, Home and End
func Ui.text_box(words: string): string
    self.next(self.theme.height)
    var id = self.control(false)
    var size = self.theme.text_size
    var inner_x = self.last_x + 12.0
    var inner_width = self.last_width - 24.0
    var text = words
    if self.pressed and self.hovered()
        self.caret = self.caret_at(text, float(self.window.mouse_x()) - inner_x)
        self.blink_from = self.frame
    end
    if self.focus == id
        if self.caret < 0 or self.caret > len(text)
            self.caret = len(text)
        end
        var before = self.caret
        var before_text = text
        var typed = self.window.typed()
        if len(typed) > 0
            text = ui_part(text, 0, self.caret) + typed + ui_part(text, self.caret, len(text))
            self.caret += len(typed)
        end
        if self.window.key_repeated("backspace") and self.caret > 0
            text = ui_part(text, 0, self.caret - 1) + ui_part(text, self.caret, len(text))
            self.caret -= 1
        end
        if self.window.key_repeated("delete") and self.caret < len(text)
            text = ui_part(text, 0, self.caret) + ui_part(text, self.caret + 1, len(text))
        end
        if self.window.key_repeated("left") and self.caret > 0
            self.caret -= 1
        end
        if self.window.key_repeated("right") and self.caret < len(text)
            self.caret += 1
        end
        if self.window.key_pressed("home")
            self.caret = 0
        end
        if self.window.key_pressed("end")
            self.caret = len(text)
        end
        if self.caret != before or text != before_text
            self.blink_from = self.frame
        end
    end
    self.ring(id)
    ui_round(self.gfx, self.last_x, self.last_y, self.last_width, self.last_height, self.theme.round, self.theme.field)
    var start = 0
    if self.focus == id
        start = self.view
        if self.caret < start
            start = self.caret
        end
        while start < self.caret and self.gfx.text_width(ui_part(text, start, self.caret), size) > inner_width
            start += 1
        end
        self.view = start
    end
    var finish = start
    var used = 0.0
    while finish < len(text)
        var next = self.gfx.text_width(text[finish], size)
        if used + next > inner_width
            break
        end
        used += next
        finish += 1
    end
    self.text_at(ui_part(text, start, finish), inner_x, self.last_y, self.last_height, self.theme.text)
    if self.focus == id and (self.frame - self.blink_from) / 30 % 2 == 0
        var caret_x = inner_x + self.gfx.text_width(ui_part(text, start, self.caret), size)
        self.gfx.rect(caret_x, self.last_y + self.last_height / 2.0 - size * 0.65, 2.0, size * 1.3, self.theme.accent)
    end
    return text
end

# The place in a text nearest to a spot this far into the box, counting from the box's view
func Ui.caret_at(words: string, spot: float): int
    var start = 0
    if self.focus == self.count - 1
        start = self.view
    end
    var used = 0.0
    var place = start
    while place < len(words)
        var next = self.gfx.text_width(words[place], self.theme.text_size)
        if used + next / 2.0 > spot
            break
        end
        used += next
        place += 1
    end
    return place
end
)JAMOLIBRARY";
    if (name == "window.jamo") return R"JAMOLIBRARY(# window: opens a window, and reads the keyboard, mouse and frame timing. use "window.jamo"
#
# Each window keeps what Windows tells it in a block of memory, whose address Windows stores with the
# window. The block: closed at 0, width at 8, height at 16, mouse x at 24, mouse y at 32, wheel at 40,
# mouse buttons at 48 to 50, focus at 56, minimized at 64, resized at 72, then a byte for each key
# being held at 128, pressed at 384, and let go at 640. Those three, from wheel to let go, collect what
# happens between frames. Each frame they move to the copies the program reads: wheel at 44,
# resized at 76, pressed at 896 and let go at 1152. After those: keys pressed or repeating while held at
# 1408, copied to 1664; typed letters, a count at 1920 then up to 64 bytes from 1928, copied to 2000 and
# 2008; and mouse buttons pressed at 2080, copied to 2088.

extern "kernel32.dll" func GetModuleHandleW(name: int): int
extern "kernel32.dll" func QueryPerformanceCounter(place: int): bool
extern "kernel32.dll" func QueryPerformanceFrequency(place: int): bool
extern "kernel32.dll" func Sleep(milliseconds: i32)
extern "winmm.dll" func timeBeginPeriod(period: i32): i32
extern "winmm.dll" func timeEndPeriod(period: i32): i32
extern "user32.dll" func RegisterClassExW(details: int): i32
extern "user32.dll" func CreateWindowExW(extra_style: i32, class_name: int, title: int, style: i32, x: i32, y: i32, width: i32, height: i32, parent: int, menu: int, instance: int, extra: int): int
extern "user32.dll" func DefWindowProcW(window: int, message: i32, first: int, second: int): int
extern "user32.dll" func DestroyWindow(window: int): bool
extern "user32.dll" func ShowWindow(window: int, how: i32): bool
extern "user32.dll" func PeekMessageW(message: int, window: int, first: i32, last: i32, remove: i32): bool
extern "user32.dll" func TranslateMessage(message: int): bool
extern "user32.dll" func DispatchMessageW(message: int): int
extern "user32.dll" func SetWindowLongPtrW(window: int, index: i32, value: int): int
extern "user32.dll" func GetWindowLongPtrW(window: int, index: i32): int
extern "user32.dll" func AdjustWindowRect(rect: int, style: i32, has_menu: bool): bool
extern "user32.dll" func LoadCursorW(instance: int, name: int): int
extern "user32.dll" func SetWindowTextW(window: int, text: int): bool
extern "user32.dll" func SetWindowPos(window: int, after: int, x: i32, y: i32, width: i32, height: i32, flags: i32): bool

struct Window
    handle: int
    state: int
    timing: int
    open: bool
    closed_by_program: bool
    shown: bool
    quiet_start: bool
    frame_seconds: float
end

func open_window(title: string, width: int, height: int): Window
    var style = 13565952
    var instance = GetModuleHandleW(0)
    var class_name = wide("JamoWindow")
    var details = memory(80)
    poke_i32(details, 0, 80)
    poke_i32(details, 4, 35)
    poke_i64(details, 8, callback(window_message))
    poke_i64(details, 24, instance)
    poke_i64(details, 40, LoadCursorW(0, 32512))
    poke_i64(details, 64, class_name)
    RegisterClassExW(details)
    var rect = memory(16)
    poke_i32(rect, 8, width)
    poke_i32(rect, 12, height)
    AdjustWindowRect(rect, style, false)
    var outer_width = peek_i32(rect, 8) - peek_i32(rect, 0)
    var outer_height = peek_i32(rect, 12) - peek_i32(rect, 4)
    var handle = CreateWindowExW(0, class_name, wide(title), style, -2147483648, -2147483648, outer_width, outer_height, 0, 0, instance, 0)
    if handle == 0
        fail("Windows couldn't open a window.")
    end
    var state = memory(2560)
    poke_i32(state, 8, width)
    poke_i32(state, 16, height)
    SetWindowLongPtrW(handle, -21, state)
    timeBeginPeriod(1)
    var timing = memory(32)
    QueryPerformanceFrequency(timing + 8)
    QueryPerformanceCounter(timing)
    return Window{handle: handle, state: state, timing: timing, open: true, closed_by_program: false, shown: false, quiet_start: false, frame_seconds: 0.0}
end

# Keeps the window from taking the keyboard when it first appears
func Window.quiet()
    self.quiet_start = true
end

# Handles what Windows sent, and answers false once the window is closed. Call it once a frame
func Window.is_open(): bool
    if not self.open
        return false
    end
    if not self.shown
        self.shown = true
        if self.quiet_start
            ShowWindow(self.handle, 4)
        else
            ShowWindow(self.handle, 5)
        end
    end
    var message = memory(48)
    while PeekMessageW(message, 0, 0, 0, 1)
        TranslateMessage(message)
        DispatchMessageW(message)
    end
    for i in 0..64
        poke_i64(self.state, 896 + i * 8, peek_i64(self.state, 384 + i * 8))
        poke_i64(self.state, 384 + i * 8, 0)
    end
    poke_i32(self.state, 44, peek_i32(self.state, 40))
    poke_i32(self.state, 40, 0)
    poke_u8(self.state, 76, peek_u8(self.state, 72))
    poke_u8(self.state, 72, 0)
    for i in 0..32
        poke_i64(self.state, 1664 + i * 8, peek_i64(self.state, 1408 + i * 8))
        poke_i64(self.state, 1408 + i * 8, 0)
    end
    for i in 0..9
        poke_i64(self.state, 2000 + i * 8, peek_i64(self.state, 1920 + i * 8))
    end
    poke_i64(self.state, 1920, 0)
    poke_i64(self.state, 2088, peek_i64(self.state, 2080))
    poke_i64(self.state, 2080, 0)
    if peek_u8(self.state, 0) != 0
        self.shut()
        return false
    end
    return true
end

func Window.shut()
    if self.open
        self.open = false
        SetWindowLongPtrW(self.handle, -21, 0)
        DestroyWindow(self.handle)
        timeEndPeriod(1)
    end
end

func Window.close()
    if self.closed_by_program
        fail("This window is already closed.")
    end
    self.closed_by_program = true
    self.shut()
end

func Window.must_be_open()
    if not self.open
        fail("This window is closed, so it can't be used.")
    end
end

func Window.key_down(name: string): bool
    self.must_be_open()
    return peek_u8(self.state, 128 + window_key_code(name)) != 0
end

func Window.key_pressed(name: string): bool
    self.must_be_open()
    return peek_u8(self.state, 896 + window_key_code(name)) != 0
end

func Window.key_released(name: string): bool
    self.must_be_open()
    return peek_u8(self.state, 1152 + window_key_code(name)) != 0
end

func Window.mouse_x(): int
    return peek_i32(self.state, 24)
end

func Window.mouse_y(): int
    return peek_i32(self.state, 32)
end

func Window.mouse_down(button: string): bool
    self.must_be_open()
    return peek_u8(self.state, 48 + window_button(button)) != 0
end

# True in the frame a mouse button went down, even if it came back up in the same frame
func Window.mouse_pressed(button: string): bool
    self.must_be_open()
    return peek_u8(self.state, 2088 + window_button(button)) != 0
end

# True in the frame a key went down, and again each time it repeats while held, like in a text box
func Window.key_repeated(name: string): bool
    self.must_be_open()
    return peek_u8(self.state, 1664 + window_key_code(name)) != 0
end

# The letters typed this frame. Only English letters, digits, spaces and symbols come through
func Window.typed(): string
    self.must_be_open()
    var letters = " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~"
    var words = ""
    for i in 0..peek_u8(self.state, 2000)
        words = words + letters[peek_u8(self.state, 2008 + i) - 32]
    end
    return words
end

# How far the mouse wheel turned this frame. Away from you is more than 0
func Window.wheel(): int
    return peek_i32(self.state, 44)
end

func Window.width(): int
    return peek_i32(self.state, 8)
end

func Window.height(): int
    return peek_i32(self.state, 16)
end

func Window.resized(): bool
    return peek_u8(self.state, 76) != 0
end

func Window.has_focus(): bool
    return peek_u8(self.state, 56) != 0
end

func Window.minimized(): bool
    return peek_u8(self.state, 64) != 0
end

func Window.set_title(title: string)
    self.must_be_open()
    SetWindowTextW(self.handle, wide(title))
end

func Window.move(x: int, y: int)
    self.must_be_open()
    SetWindowPos(self.handle, 0, x, y, 0, 0, 21)
end

func Window.resize(width: int, height: int)
    self.must_be_open()
    var rect = memory(16)
    poke_i32(rect, 8, width)
    poke_i32(rect, 12, height)
    AdjustWindowRect(rect, 13565952, false)
    SetWindowPos(self.handle, 0, 0, 0, peek_i32(rect, 8) - peek_i32(rect, 0), peek_i32(rect, 12) - peek_i32(rect, 4), 22)
end

# Waits until it's time for the next frame. A game that falls behind starts counting again from now
func Window.wait_frame(frames_a_second: int)
    var frequency = peek_i64(self.timing, 8)
    var last = peek_i64(self.timing, 0)
    var target = last + frequency / frames_a_second
    var now = 0
    while true
        QueryPerformanceCounter(self.timing + 16)
        now = peek_i64(self.timing, 16)
        if now >= target
            break
        end
        var left = (target - now) * 1000 / frequency
        if left > 1
            Sleep(left - 1)
        end
    end
    self.frame_seconds = float(now - last) / float(frequency)
    if now - target > frequency / frames_a_second
        poke_i64(self.timing, 0, now)
    else
        poke_i64(self.timing, 0, target)
    end
end

# How long the last frame took, in seconds
func Window.frame_time(): float
    return self.frame_seconds
end

# --- Inside the library ---
func window_button(button: string): int
    match button
        case "left"
            return 0
        case "right"
            return 1
        case "middle"
            return 2
    end
    fail(f"window doesn't know the mouse button {button}. It knows left, right and middle.")
    return 0
end

func window_key_code(name: string): int
    var letters = "abcdefghijklmnopqrstuvwxyz"
    if len(name) == 1
        for i in 0..26
            if letters[i] == name
                return 65 + i
            end
        end
        var digits = "0123456789"
        for i in 0..10
            if digits[i] == name
                return 48 + i
            end
        end
    end
    match name
        case "left"
            return 37
        case "up"
            return 38
        case "right"
            return 39
        case "down"
            return 40
        case "space"
            return 32
        case "enter"
            return 13
        case "escape"
            return 27
        case "shift"
            return 16
        case "ctrl"
            return 17
        case "alt"
            return 18
        case "tab"
            return 9
        case "backspace"
            return 8
        case "delete"
            return 46
        case "home"
            return 36
        case "end"
            return 35
    end
    fail(f"window doesn't know the key {name}. It knows a to z, 0 to 9, the arrows, space, enter, escape, shift, ctrl, alt, tab, backspace, delete, home and end.")
    return 0
end

# Windows packs two 16-bit numbers into one. These unpack them, with their signs
func window_low_part(value: int): int
    var whole = value % 4294967296
    if whole < 0
        whole += 4294967296
    end
    var part = whole % 65536
    if part >= 32768
        part -= 65536
    end
    return part
end

func window_high_part(value: int): int
    var whole = value % 4294967296
    if whole < 0
        whole += 4294967296
    end
    var part = whole / 65536
    if part >= 32768
        part -= 65536
    end
    return part
end

func window_key(state: int, code: int, down: bool)
    var key = code % 256
    if down
        if peek_u8(state, 128 + key) == 0
            poke_u8(state, 384 + key, 1)
        end
        poke_u8(state, 128 + key, 1)
        poke_u8(state, 1408 + key, 1)
    else
        poke_u8(state, 128 + key, 0)
        poke_u8(state, 640 + key, 1)
    end
end

func window_mouse(state: int, place: int)
    poke_i32(state, 24, window_low_part(place))
    poke_i32(state, 32, window_high_part(place))
end

func window_message(handle: int, message: int, first: int, second: int): int
    var kind = message % 4294967296
    if kind < 0
        kind += 4294967296
    end
    var state = GetWindowLongPtrW(handle, -21)
    if state == 0
        return DefWindowProcW(handle, kind, first, second)
    end
    match kind
        case 16
            poke_u8(state, 0, 1)
            return 0
        case 20
            return 1
        case 5
            if first != 1
                poke_i32(state, 8, window_low_part(second))
                poke_i32(state, 16, window_high_part(second))
                poke_u8(state, 72, 1)
            end
            if first == 1
                poke_u8(state, 64, 1)
            else
                poke_u8(state, 64, 0)
            end
        case 7
            poke_u8(state, 56, 1)
        case 8
            poke_u8(state, 56, 0)
            for i in 0..256
                poke_u8(state, 128 + i, 0)
            end
            for i in 0..3
                poke_u8(state, 48 + i, 0)
            end
        case 256
            window_key(state, first, true)
        case 260
            window_key(state, first, true)
        case 257
            window_key(state, first, false)
        case 261
            window_key(state, first, false)
        case 258
            var count = peek_u8(state, 1920)
            if first >= 32 and first <= 126 and count < 64
                poke_u8(state, 1928 + count, first)
                poke_u8(state, 1920, count + 1)
            end
        case 512
            window_mouse(state, second)
        case 513
            window_mouse(state, second)
            poke_u8(state, 48, 1)
            poke_u8(state, 2080, 1)
        case 514
            window_mouse(state, second)
            poke_u8(state, 48, 0)
        case 516
            window_mouse(state, second)
            poke_u8(state, 49, 1)
            poke_u8(state, 2081, 1)
        case 517
            window_mouse(state, second)
            poke_u8(state, 49, 0)
        case 519
            window_mouse(state, second)
            poke_u8(state, 50, 1)
            poke_u8(state, 2082, 1)
        case 520
            window_mouse(state, second)
            poke_u8(state, 50, 0)
        case 522
            poke_i32(state, 40, peek_i32(state, 40) + window_high_part(first) / 120)
    end
    return DefWindowProcW(handle, kind, first, second)
end
)JAMOLIBRARY";
    return nullptr;
}
