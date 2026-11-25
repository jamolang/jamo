// jamo: compiles Jamo source into real Windows programs. jamo build file.jamo, or jamo run file.jamo.
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "checker.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include "codegen.hpp"
#include "modules.hpp"
#include "pe.hpp"

namespace fs = std::filesystem;

namespace {

const int FAILED = 1;
const int CLEAN_EVERY_TIME = 1;
const int CLEANER_CHECKS = 2;
const int CLEANER_OFF = 4;
const int NO_OPTIMIZING = 8;
const int WINDOW_PROGRAM = 16;
const int RENAME_ATTEMPTS = 20;
const DWORD RENAME_WAIT_MS = 50;
const char* const VERSION = "0.1.0";
const char* const USAGE =
    "Usage:\n"
    "  jamo run file.jamo     Builds it and runs it. Anything after the file goes to the program\n"
    "  jamo build file.jamo   Writes file.exe beside the source\n"
    "  jamo build --window file.jamo   Writes a window program, with no console behind it\n"
    "  jamo check file.jamo   Finds mistakes without writing an .exe\n"
    "  jamo new name          Makes a folder called name, with a program to start from\n"
    "  jamo new --game name   Makes it with a small window game to start from instead\n"
    "  jamo version           Shows which Jamo this is\n"
    "  jamo help              Shows this\n";
// The game jamo new --game starts from: a player that moves, a coin to catch, and a score
const char* const NEW_GAME =
    "use \"gfx.jamo\"\n"
    "use \"math.jamo\"\n"
    "\n"
    "const WIDTH = 640\n"
    "const HEIGHT = 480\n"
    "const SIZE = 32.0\n"
    "const SPEED = 4.0\n"
    "\n"
    "# Everything the game keeps track of lives in one struct, passed to the functions that need it\n"
    "struct Game\n"
    "    x: float\n"
    "    y: float\n"
    "    coin_x: float\n"
    "    coin_y: float\n"
    "    score: int\n"
    "end\n"
    "\n"
    "func Game.move(window: Window)\n"
    "    if window.key_down(\"left\")\n"
    "        self.x -= SPEED\n"
    "    end\n"
    "    if window.key_down(\"right\")\n"
    "        self.x += SPEED\n"
    "    end\n"
    "    if window.key_down(\"up\")\n"
    "        self.y -= SPEED\n"
    "    end\n"
    "    if window.key_down(\"down\")\n"
    "        self.y += SPEED\n"
    "    end\n"
    "    self.x = clamp(self.x, 0.0, float(WIDTH) - SIZE)\n"
    "    self.y = clamp(self.y, 0.0, float(HEIGHT) - SIZE)\n"
    "end\n"
    "\n"
    "# Touching the coin scores a point, and the coin jumps somewhere new\n"
    "func Game.catch_coin()\n"
    "    if abs(self.x + SIZE / 2.0 - self.coin_x) < 24.0 and abs(self.y + SIZE / 2.0 - self.coin_y) < 24.0\n"
    "        self.score += 1\n"
    "        self.coin_x = 20.0 + random_float() * float(WIDTH - 40)\n"
    "        self.coin_y = 60.0 + random_float() * float(HEIGHT - 80)\n"
    "    end\n"
    "end\n"
    "\n"
    "func Game.draw(screen: Gfx)\n"
    "    screen.clear(rgb(24, 28, 44))\n"
    "    screen.circle(self.coin_x, self.coin_y, 10.0, rgb(255, 210, 60))\n"
    "    screen.rect(self.x, self.y, SIZE, SIZE, rgb(90, 200, 255))\n"
    "    screen.text(f\"Coins: {self.score}\", 16.0, 16.0, 24.0, rgb(255, 255, 255))\n"
    "end\n"
    "\n"
    "func main()\n"
    "    var window = open_window(\"My game\", WIDTH, HEIGHT)\n"
    "    var screen = start_gfx(window)\n"
    "    var game = Game{x: 300.0, y: 220.0, coin_x: 120.0, coin_y: 120.0, score: 0}\n"
    "    while window.is_open()\n"
    "        if window.key_pressed(\"escape\")\n"
    "            window.close()\n"
    "        end\n"
    "        game.move(window)\n"
    "        game.catch_coin()\n"
    "        game.draw(screen)\n"
    "        screen.show()\n"
    "        window.wait_frame(60)\n"
    "    end\n"
    "end\n";
const char* const NEW_PROGRAM =
    "func main()\n"
    "    print(\"Hello from Jamo!\")\n"
    "end\n";

std::string as_text(const fs::path& path) {
    const std::u8string words = path.u8string();
    return std::string(words.begin(), words.end());
}

// Writes beside the real file first, so a failure can't leave a broken program behind
bool write_exe(const fs::path& path, const std::vector<uint8_t>& bytes) {
    fs::path partial = path;
    partial += L".part";
    {
        std::ofstream file(partial, std::ios::binary | std::ios::trunc);
        if (!file) return false;
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!file) return false;
    }
    // Windows can hold the old .exe for a moment, like while it's being scanned for viruses, so it tries again for up to a second
    std::error_code problem;
    for (int attempt = 0; attempt < RENAME_ATTEMPTS; attempt++) {
        problem.clear();
        fs::rename(partial, path, problem);
        if (!problem) break;
        Sleep(RENAME_WAIT_MS);
    }
    if (problem) {
        std::error_code ignored;
        fs::remove(partial, ignored);
        return false;
    }
    return true;
}

bool load_and_check(const fs::path& source_path, Program& program) {
    if (source_path.extension() != L".jamo") {
        std::fprintf(stderr, "%s isn't a Jamo file. Jamo files end in .jamo.\n", as_text(source_path).c_str());
        return false;
    }
    Errors errors;
    if (!load_program(source_path, errors, program)) {
        std::fprintf(stderr, "I can't read a file called %s.\n", as_text(source_path).c_str());
        return false;
    }
    if (!errors.any()) check_program(program, errors);
    if (errors.any()) {
        std::fputs(errors.text().c_str(), stderr);
        return false;
    }
    std::fputs(errors.warnings_text().c_str(), stderr);
    return true;
}

bool build(const fs::path& source_path, fs::path& exe_path, int cleaner_mode) {
    Program program;
    if (!load_and_check(source_path, program)) return false;
    const std::vector<uint8_t> exe = build_exe(generate_code(program, as_text(source_path.filename()), cleaner_mode));
    exe_path = source_path;
    exe_path.replace_extension(L".exe");
    if (!write_exe(exe_path, exe)) {
        std::fprintf(stderr, "I can't write %s. If that program is still running, close it and try again.\n", as_text(exe_path).c_str());
        return false;
    }
    return true;
}

int make_new_program(const fs::path& folder, bool game) {
    std::error_code problem;
    if (fs::exists(folder, problem)) {
        std::fprintf(stderr, "%s already exists, so I left it alone. Pick a different name.\n", as_text(folder).c_str());
        return FAILED;
    }
    fs::create_directories(folder, problem);
    std::ofstream file(folder / L"main.jamo", std::ios::binary);
    file << (game ? NEW_GAME : NEW_PROGRAM);
    file.close();
    if (problem || !file) {
        std::fprintf(stderr, "I can't make a folder called %s.\n", as_text(folder).c_str());
        return FAILED;
    }
    const std::string main_file = as_text(folder / L"main.jamo");
    std::printf("Made %s\nRun it with: jamo run \"%s\"\n", main_file.c_str(), main_file.c_str());
    if (game) std::printf("When it's ready to share: jamo build --window \"%s\"\n", main_file.c_str());
    return 0;
}

// Adds an input to a command line, in the way programs read it back
void add_to_command(std::wstring& command, const std::wstring& input) {
    command += L" \"";
    size_t backslashes = 0;
    for (const wchar_t letter : input) {
        if (letter == L'\\') {
            backslashes++;
            continue;
        }
        command.append(letter == L'"' ? backslashes * 2 + 1 : backslashes, L'\\');
        backslashes = 0;
        command.push_back(letter);
    }
    command.append(backslashes * 2, L'\\');
    command.push_back(L'"');
}

int run(const fs::path& exe_path, const std::vector<std::wstring>& inputs) {
    std::error_code problem;
    const fs::path full_path = fs::absolute(exe_path, problem);
    std::wstring command = L"\"" + (problem ? exe_path : full_path).wstring() + L"\"";
    for (const std::wstring& input : inputs) add_to_command(command, input);
    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process = {};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup, &process)) {
        std::fprintf(stderr, "I built %s, but Windows wouldn't start it (error %lu).\n", as_text(exe_path).c_str(), GetLastError());
        return FAILED;
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exit_code = FAILED;
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hProcess);
    CloseHandle(process.hThread);
    return static_cast<int>(exit_code);
}

// For 'jamo tokens': text with \, new lines, tabs and carriage returns written as escapes, so each token fits on one line
std::string escaped(const std::string& text) {
    std::string out;
    for (const char letter : text) {
        if (letter == '\\') out += "\\\\";
        else if (letter == '\n') out += "\\n";
        else if (letter == '\t') out += "\\t";
        else if (letter == '\r') out += "\\r";
        else out.push_back(letter);
    }
    return out;
}

// Prints every token and every lexer error, one a line. For developing Jamo: the lexer written in Jamo must print the same
int print_tokens(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::fprintf(stderr, "I can't read %s.\n", as_text(path).c_str());
        return FAILED;
    }
    const std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    Errors errors(as_text(path.filename()), source);
    const std::vector<Token> tokens = read_tokens(source, errors);
    std::string out;
    for (const Token& token : tokens) {
        int64_t real_bits = 0;
        std::memcpy(&real_bits, &token.real, sizeof(real_bits));
        out += std::to_string(static_cast<int>(token.kind)) + " " + std::to_string(token.spot.line) + " " + std::to_string(token.spot.column) + " " + std::to_string(token.spot.length) +
               " [" + escaped(token.text) + "] [" + escaped(token.value) + "] " + std::to_string(static_cast<int64_t>(token.number)) + " " + std::to_string(real_bits) + "\n";
    }
    for (const ErrorMessage& message : errors.messages) {
        out += "error " + std::to_string(message.spot.line) + " " + std::to_string(message.spot.column) + " " + std::to_string(message.spot.length) + " " + message.words + " | " + message.hint + "\n";
    }
    std::fwrite(out.data(), 1, out.size(), stdout);
    return 0;
}

// For 'jamo ast': the whole tree, one item a line, indented by how deep it is
class TreePrinter {
public:
    explicit TreePrinter(const Program& the_program, bool show_checked = false) : program(the_program), checked(show_checked) {}

    std::string text() {
        for (size_t i = 0; i < 100000; i++) {
            const Type type = static_cast<Type>(static_cast<int>(Type::FirstCustom) + static_cast<int>(i));
            if (!type_exists(type)) break;
        }
        for (const UseLine& use : program.uses) line(0, "use [" + escaped(use.path) + "] " + spot(use.spot));
        for (const StructDecl& decl : program.structs) {
            line(0, "struct " + decl.name + " " + spot(decl.spot) + " " + type_name(decl.type) + params(decl.type_params, decl.type_param_spots));
            for (const StructField& field : decl.fields) line(1, "field " + field.name + " " + spot(field.spot) + " " + type_name(field.type));
        }
        for (const EnumDecl& decl : program.enums) {
            line(0, "enum " + decl.name + " " + spot(decl.spot) + " " + type_name(decl.type));
            for (size_t i = 0; i < decl.members.size(); i++) line(1, "member " + decl.members[i] + " " + spot(decl.member_spots[i]));
        }
        for (const ConstDecl& decl : program.consts) {
            line(0, "const " + decl.name + " " + spot(decl.spot));
            expr(1, "value", decl.value.get());
        }
        for (const auto& global : program.globals) {
            line(0, "global");
            stmt(1, *global);
        }
        for (const auto& function : program.functions) {
            line(0, "func " + function->name + " " + spot(function->spot) + " owner " + type_name(function->owner) + " " + spot(function->owner_spot) + " returns " + type_name(function->return_type) +
                    " dll [" + escaped(function->dll) + "] " + std::to_string(function->returns_i32) + std::to_string(function->returns_f32) + (checked ? " slots " + std::to_string(function->slot_count) : "") +
                    params(function->type_params, function->type_param_spots));
            for (size_t i = 0; i < function->inputs.size(); i++) {
                const Input& input = function->inputs[i];
                const bool is_i32 = i < function->input_is_i32.size() && function->input_is_i32[i];
                const bool is_f32 = i < function->input_is_f32.size() && function->input_is_f32[i];
                line(1, "input " + input.name + " " + spot(input.spot) + " " + type_name(input.type) + " " + std::to_string(is_i32) + std::to_string(is_f32));
            }
            block(1, "body", function->body);
        }
        return out;
    }

private:
    // The types the parser made, in order, since later steps number them the same way
    bool type_exists(Type type) {
        const int index = static_cast<int>(type) - static_cast<int>(Type::FirstCustom);
        if (index >= static_cast<int>(type_count())) return false;
        line(0, "type " + std::to_string(index) + " " + program.types.name(type));
        return true;
    }

    size_t type_count() {
        size_t count = 0;
        while (true) {
            const Type type = static_cast<Type>(static_cast<int>(Type::FirstCustom) + static_cast<int>(count));
            if (!program.types.exists(type)) return count;
            count++;
        }
    }

    std::string type_name(Type type) const { return program.types.name(type); }

    static std::string spot(Spot place) {
        return std::to_string(place.line) + " " + std::to_string(place.column) + " " + std::to_string(place.length);
    }

    void line(int depth, const std::string& words) {
        out += std::string(static_cast<size_t>(depth) * 2, ' ') + words + "\n";
    }

    void expr(int depth, const std::string& role, const Expr* e) {
        if (!e) return;
        int64_t real_bits = 0;
        std::memcpy(&real_bits, &e->real, sizeof(real_bits));
        std::string fields;
        for (size_t i = 0; i < e->field_names.size(); i++) fields += " " + e->field_names[i] + "@" + spot(e->field_spots[i]);
        line(depth, role + " expr " + std::to_string(static_cast<int>(e->kind)) + " " + spot(e->spot) + " depth " + std::to_string(e->depth) + " number " + std::to_string(static_cast<int64_t>(e->number)) +
                        " real " + std::to_string(real_bits) + " truth " + std::to_string(e->truth) + " text [" + escaped(e->text) + "] name [" + e->name + "] op " + std::to_string(static_cast<int>(e->op)) +
                        " to " + type_name(e->convert_to) + " fields" + fields + (checked ? " type " + type_name(e->type) + " slot " + std::to_string(e->slot) + " numbers" + numbers(e->field_numbers) : "") + type_list(e->type_args));
        expr(depth + 1, "left", e->left.get());
        expr(depth + 1, "right", e->right.get());
        for (const auto& input : e->inputs) expr(depth + 1, "input", input.get());
    }

    void block(int depth, const std::string& role, const Block& statements) {
        line(depth, role + " block " + std::to_string(statements.size()));
        for (const auto& s : statements) stmt(depth + 1, *s);
    }

    void stmt(int depth, const Stmt& s) {
        line(depth, "stmt " + std::to_string(static_cast<int>(s.kind)) + " " + spot(s.spot) + " name [" + s.name + "] " + spot(s.name_spot) + " operation " + std::to_string(s.has_operation) + " " +
                        std::to_string(static_cast<int>(s.operation)) + " type " + std::to_string(s.has_type) + " " + type_name(s.declared_type) + " else " + std::to_string(s.has_else) +
                        (checked ? " slots " + std::to_string(s.slot) + " " + std::to_string(s.end_slot) + " " + std::to_string(s.array_slot) + " covers " + std::to_string(s.covers_all) : ""));
        expr(depth + 1, "target", s.target.get());
        expr(depth + 1, "value", s.value.get());
        expr(depth + 1, "end", s.range_end.get());
        block(depth + 1, "body", s.body);
        for (const ElifPart& part : s.elifs) {
            expr(depth + 1, "elif", part.condition.get());
            block(depth + 1, "elif-body", part.body);
        }
        for (const CasePart& part : s.cases) {
            expr(depth + 1, "case", part.value.get());
            block(depth + 1, "case-body", part.body);
        }
        block(depth + 1, "other", s.other_body);
    }

    // A generic's types, only when it has some, so other trees print as before
    std::string params(const std::vector<Type>& types, const std::vector<Spot>& spots) const {
        if (types.empty()) return "";
        std::string out = " generic";
        for (size_t i = 0; i < types.size(); i++) out += " " + type_name(types[i]) + "@" + spot(spots[i]);
        return out;
    }

    std::string type_list(const std::vector<Type>& types) const {
        if (types.empty()) return "";
        std::string out = " types";
        for (const Type type : types) out += " " + type_name(type);
        return out;
    }

    static std::string numbers(const std::vector<int>& values) {
        std::string out;
        for (const int value : values) out += " " + std::to_string(value);
        return out;
    }

    const Program& program;
    bool checked = false;
    std::string out;
};

// Loads every file the program uses and checks it, the way jamo check does, then prints the checked tree, the errors and the warnings.
// For developing Jamo: the checker written in Jamo must print the same
int print_checked(const fs::path& path) {
    Errors errors;
    Program program;
    if (!load_program(path, errors, program)) {
        std::fprintf(stderr, "I can't read a file called %s.\n", as_text(path).c_str());
        return FAILED;
    }
    const bool loaded = !errors.any();
    if (loaded) check_program(program, errors);
    std::string out = "loaded " + std::to_string(loaded) + "\n";
    for (const std::string& name : program.file_names) out += "file " + name + "\n";
    if (loaded) out += TreePrinter(program, true).text();
    out += "errors\n" + errors.text();
    out += "warnings\n" + errors.warnings_text();
    std::fwrite(out.data(), 1, out.size(), stdout);
    return 0;
}

// Checks a program, then prints everything the code generator makes: the machine code, its gaps for the .exe writer,
// the call sites, the imports and the strings. For developing Jamo: the code generator written in Jamo must print the same.
// flags are the test flags: 1, 2 and 4 for the cleaner, 8 for no optimizing
int print_code(const fs::path& path, int flags) {
    Program program;
    if (!load_and_check(path, program)) return FAILED;
    const MachineCode code = generate_code(program, as_text(path.filename()), flags);
    static const char* const digits = "0123456789abcdef";
    std::string out = "bytes " + std::to_string(code.bytes.size()) + "\n";
    for (size_t i = 0; i < code.bytes.size(); i++) {
        out += digits[code.bytes[i] >> 4];
        out += digits[code.bytes[i] & 15];
        if (i % 32 == 31 || i + 1 == code.bytes.size()) out += "\n";
    }
    for (const DataFixup& fixup : code.fixups) {
        out += "fixup " + std::to_string(fixup.offset) + " " + std::to_string(static_cast<int>(fixup.target)) + " " + std::to_string(fixup.string) + " " + std::to_string(fixup.extra) + "\n";
    }
    for (const CallSite& site : code.call_sites) out += "call " + std::to_string(site.offset) + " " + std::to_string(site.string) + "\n";
    for (const auto& [dll, name] : code.imports) out += "import " + dll + " " + name + "\n";
    for (size_t i = 0; i < code.strings.size(); i++) out += "string " + std::to_string(i) + " [" + escaped(code.strings[i]) + "]\n";
    std::fwrite(out.data(), 1, out.size(), stdout);
    return 0;
}

// Prints the tree the parser makes for one file, then every error. For developing Jamo: the parser written in Jamo must print the same
int print_tree(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::fprintf(stderr, "I can't read %s.\n", as_text(path).c_str());
        return FAILED;
    }
    const std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    Errors errors(as_text(path.filename()), source);
    const std::vector<Token> tokens = read_tokens(source, errors);
    Program program;
    parse_into(tokens, errors, program);
    std::string out = TreePrinter(program).text();
    for (const ErrorMessage& message : errors.messages) {
        out += "error " + std::to_string(message.spot.line) + " " + std::to_string(message.spot.column) + " " + std::to_string(message.spot.length) + " " + message.words + " | " + message.hint + "\n";
    }
    std::fwrite(out.data(), 1, out.size(), stdout);
    return 0;
}

}  // namespace

int wmain(int count, wchar_t** words) {
    try {
        const std::wstring command = count >= 2 ? words[1] : L"";
        if (count == 2 && command == L"help") {
            std::fputs(USAGE, stdout);
            return 0;
        }
        if (count == 2 && command == L"version") {
            std::printf("Jamo %s\n", VERSION);
            return 0;
        }
        // A name starting with -- is a mistyped option, like jamo new --game with the name left out
        if (count >= 3 && command == L"new" && std::wstring(words[count - 1]).rfind(L"--", 0) == 0 && std::wstring(words[2]).rfind(L"--", 0) == 0) {
            std::fputs(USAGE, stderr);
            return FAILED;
        }
        if (count == 3 && command == L"new") return make_new_program(fs::path(words[2]), false);
        if (count == 4 && command == L"new" && std::wstring(words[2]) == L"--game") return make_new_program(fs::path(words[3]), true);
        if (count == 4 && command == L"new" && std::wstring(words[3]) == L"--game") return make_new_program(fs::path(words[2]), true);
        if (count == 3 && command == L"tokens") return print_tokens(fs::path(words[2]));
        if (count == 3 && command == L"ast") return print_tree(fs::path(words[2]));
        if (count == 3 && command == L"checked") return print_checked(fs::path(words[2]));
        if (count == 4 && command == L"code") return print_code(fs::path(words[3]), _wtoi(words[2]));
        if (count == 3 && command == L"check") {
            Program program;
            return load_and_check(fs::path(words[2]), program) ? 0 : FAILED;
        }
        // These test the Memory Cleaner and the optimizer, and aren't for normal programs
        int cleaner_mode = 0;
        int first = 2;
        for (; first < count; first++) {
            const std::wstring word = words[first];
            if (word == L"--gc-test") {
                cleaner_mode |= CLEAN_EVERY_TIME;
            } else if (word == L"--gc-check") {
                cleaner_mode |= CLEANER_CHECKS;
            } else if (word == L"--gc-off") {
                cleaner_mode |= CLEANER_OFF;
            } else if (word == L"--no-optimize") {
                cleaner_mode |= NO_OPTIMIZING;
            } else if (word == L"--window") {
                cleaner_mode |= WINDOW_PROGRAM;
            } else {
                break;
            }
        }
        if (first >= count || (first + 1 < count && command != L"run")) {
            std::fputs(USAGE, stderr);
            return FAILED;
        }
        if (command != L"build" && command != L"run") {
            std::fputs(USAGE, stderr);
            return FAILED;
        }
        fs::path exe_path;
        if (!build(fs::path(words[first]), exe_path, cleaner_mode)) return FAILED;
        return command == L"run" ? run(exe_path, std::vector<std::wstring>(words + first + 1, words + count)) : 0;
    } catch (const std::exception& problem) {
        std::fprintf(stderr, "Jamo itself ran into a problem: %s\n", problem.what());
        return FAILED;
    }
}
