#include "modules.hpp"

#include <cwctype>
#include <map>
#include <fstream>
#include <iterator>

#include "libraries.hpp"
#include "parser.hpp"

namespace fs = std::filesystem;

namespace {

const uintmax_t LARGEST_SOURCE = 64 * 1024 * 1024;
const std::wstring BUILT_IN = L"built in:";
const char* const BUILT_IN_LABEL = " (built in)";

bool is_built_in(const fs::path& path) {
    return path.wstring().rfind(BUILT_IN, 0) == 0;
}

std::string built_in_name(const fs::path& path) {
    const std::wstring words = path.wstring().substr(BUILT_IN.size());
    return std::string(words.begin(), words.end());
}

std::string as_text(const fs::path& path) {
    const std::u8string words = path.generic_u8string();
    return std::string(words.begin(), words.end());
}

bool read_source(const fs::path& path, std::string& out) {
    std::error_code problem;
    if (!fs::is_regular_file(path, problem)) return false;
    if (fs::file_size(path, problem) > LARGEST_SOURCE) return false;
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return !file.bad();
}

// Windows treats ENEMY.jamo and enemy.jamo as one file, so they must load only once
std::wstring same_file_key(const fs::path& path) {
    std::wstring key = path.wstring();
    for (wchar_t& letter : key) letter = static_cast<wchar_t>(std::towlower(static_cast<wint_t>(letter)));
    return key;
}

bool is_inside(const fs::path& path, const fs::path& folder) {
    const fs::path relative = path.lexically_relative(folder);
    return !relative.empty() && *relative.begin() != "..";
}

class Loader {
public:
    Loader(const fs::path& the_root, Errors& the_errors, Program& the_program) : root(the_root), errors(the_errors), program(the_program) {}

    void add_file(const fs::path& path, Spot from) {
        seen[same_file_key(path)] = files.size();
        files.push_back(path);
        used_from.push_back(from);
        uses_of.emplace_back();
        with_as.push_back(0);
    }

    // Each file after the files it uses, from the main file, so a library's variables are ready before its users'
    std::vector<int> setup_order() const {
        std::vector<int> order;
        std::vector<bool> done(files.size(), false);
        visit(0, done, order);
        return order;
    }

    bool load_all() {
        for (size_t i = 0; i < files.size(); i++) {
            using_file = i;
            std::string source;
            if (is_built_in(files[i])) {
                const std::string name = built_in_name(files[i]);
                source = builtin_library(name);
                program.own_label.clear();
                errors.add_file(name + BUILT_IN_LABEL, source);
                program.file_names.push_back(name + BUILT_IN_LABEL);
                const std::vector<Token> tokens = read_tokens(source, errors);
                if (errors.any()) return true;
                program.uses.clear();
                parse_into(tokens, errors, program);
                if (errors.any()) return true;
                for (const UseLine& use : program.uses) add_use(use, root);
                if (errors.any()) return true;
                continue;
            }
            if (!read_source(files[i], source)) {
                if (i == 0) return false;
                errors.add(used_from[i], "I can't read the file " + as_text(files[i].lexically_relative(root)) + ".");
                return true;
            }
            const std::string name = as_text(files[i].lexically_relative(root));
            errors.add_file(name, source);
            program.file_names.push_back(name);
            program.own_label = with_as[i] == 2 ? name : "";
            if (with_as[i] == 2) program.own_files.push_back(static_cast<int>(i));
            const std::vector<Token> tokens = read_tokens(source, errors);
            if (errors.any()) return true;
            program.uses.clear();
            parse_into(tokens, errors, program);
            if (errors.any()) return true;
            for (const UseLine& use : program.uses) add_use(use, files[i].parent_path());
            if (errors.any()) return true;
        }
        return true;
    }

private:
    void visit(size_t file, std::vector<bool>& done, std::vector<int>& order) const {
        if (done[file]) return;
        done[file] = true;
        for (const size_t used : uses_of[file]) visit(used, done, order);
        order.push_back(static_cast<int>(file));
    }

    void note_use(const fs::path& path) {
        uses_of[using_file].push_back(seen.at(same_file_key(path)));
    }

    void add_use(const UseLine& use, const fs::path& folder) {
        // The libraries built into jamo.exe always win, so a program's own file can't hide one
        if (builtin_library(use.path)) {
            if (!use.alias.empty()) {
                errors.add(use.alias_spot, "Built-in libraries can't be used with as.");
                return;
            }
            const fs::path place(BUILT_IN + std::wstring(use.path.begin(), use.path.end()));
            if (!seen.count(same_file_key(place))) add_file(place, use.spot);
            note_use(place);
            return;
        }
        const fs::path written(std::u8string(use.path.begin(), use.path.end()));
        if (same_file_key(written.extension()) != L".jamo") {
            errors.add(use.spot, "use needs a file that ends in .jamo.");
            return;
        }
        const char* const outside = "This file is outside the program's folder, so it can't be used.";
        const fs::path joined = (folder / written).lexically_normal();
        if (written.has_root_path() || !is_inside(joined, root)) {
            errors.add(use.spot, outside);
            return;
        }
        std::error_code problem;
        const fs::path found = fs::canonical(joined, problem);
        if (problem) {
            errors.add(use.spot, "I can't find a file called " + use.path + ".", "It looks in the same folder as the file using it.");
            return;
        }
        if (!is_inside(found, root)) {
            errors.add(use.spot, outside);
            return;
        }
        if (!seen.count(same_file_key(found))) add_file(found, use.spot);
        note_use(found);
        note_alias(use, seen.at(same_file_key(found)));
    }

    // use ... as: a file is used with as everywhere or nowhere, since its names are read one way or the other
    void note_alias(const UseLine& use, size_t target) {
        const int how = use.alias.empty() ? 1 : 2;
        if (target == 0 && how == 2) {
            errors.add(use.alias_spot, "The main file can't be used with as.");
            return;
        }
        if (with_as[target] != 0 && with_as[target] != how) {
            errors.add(use.spot, use.path + (how == 1 ? " is used with as in another file, so it needs as here too." : " is used without as in another file, so it can't have as here."));
            return;
        }
        with_as[target] = how;
        if (how == 1) return;
        for (const Alias& alias : program.aliases) {
            if (alias.file == static_cast<int>(using_file) && alias.name == use.alias && alias.target != static_cast<int>(target)) {
                errors.add(use.alias_spot, use.alias + " already names another file here.");
                return;
            }
        }
        program.aliases.push_back({static_cast<int>(using_file), use.alias, static_cast<int>(target)});
    }

    const fs::path& root;
    Errors& errors;
    Program& program;
    std::vector<fs::path> files;
    std::vector<Spot> used_from;
    std::map<std::wstring, size_t> seen;          // Each file's place in files
    std::vector<std::vector<size_t>> uses_of;     // The files each one uses, in the order of its use lines
    std::vector<int> with_as;                     // 0 not used yet, 1 used without as, 2 used with as
    size_t using_file = 0;
};

}  // namespace

// Each interface's maker, as Jamo: a generic function that wraps a struct's methods in functions written in place
std::string interface_makers(const Program& program) {
    std::string out;
    for (const std::string& name : program.interfaces) {
        const StructDecl* decl = nullptr;
        for (const StructDecl& candidate : program.structs) {
            if (candidate.name == name) decl = &candidate;
        }
        if (!decl) continue;
        out += "func jamo_make_" + name + "<Made>(value: Made): " + name + "\n";
        std::string fields;
        for (const StructField& field : decl->fields) {
            const std::vector<Type>& inputs = program.types.inputs_of(field.type);
            const Type answer = program.types.answer_of(field.type);
            std::string given;
            std::string passed;
            for (size_t i = 0; i < inputs.size(); i++) {
                given += (i > 0 ? ", " : "") + std::string("a") + std::to_string(i + 1) + ": " + program.types.name(inputs[i]);
                passed += (i > 0 ? ", " : "") + std::string("a") + std::to_string(i + 1);
            }
            out += "    func jamo_method_" + field.name + "(" + given + ")" + (answer == Type::Nothing ? "" : ": " + program.types.name(answer)) + "\n";
            out += std::string("        ") + (answer == Type::Nothing ? "" : "return ") + "value." + field.name + "(" + passed + ")\n    end\n";
            fields += (fields.empty() ? "" : ", ") + field.name + ": jamo_method_" + field.name;
        }
        out += "    return " + name + "{" + fields + "}\nend\n\n";
    }
    return out;
}

bool load_program(const fs::path& main_file, Errors& errors, Program& program) {
    std::error_code problem;
    const fs::path full_path = fs::canonical(main_file, problem);
    if (problem) return false;
    const fs::path root = full_path.parent_path();
    Loader loader(root, errors, program);
    loader.add_file(full_path, Spot{});
    if (!loader.load_all()) return false;
    if (!errors.any()) program.setup_order = loader.setup_order();
    if (!errors.any() && !program.interfaces.empty()) {
        load_extra_file("(interfaces) (built in)", interface_makers(program), errors, program);
        const int file = static_cast<int>(program.file_names.size()) - 1;
        for (auto& function : program.functions) {
            if (function->spot.file == file && function->name.rfind("jamo_make_", 0) == 0) function->name = "(making " + function->name.substr(10) + ")";
        }
    }
    errors.use_file(0);
    return true;
}

void load_extra_file(const std::string& name, const std::string& source, Errors& errors, Program& program) {
    program.own_label.clear();
    errors.add_file(name, source);
    program.file_names.push_back(name);
    const std::vector<Token> tokens = read_tokens(source, errors);
    if (!errors.any()) {
        program.uses.clear();
        parse_into(tokens, errors, program);
    }
    errors.use_file(0);
}
