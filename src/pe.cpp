#include "pe.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <string>

namespace {

const uint32_t SECTION_ALIGN = 0x1000;
const uint32_t FILE_ALIGN = 0x200;
const uint64_t IMAGE_BASE = 0x140000000ULL;
const uint32_t TEXT_RVA = SECTION_ALIGN;
const uint32_t HEADERS_SIZE = FILE_ALIGN;
const uint32_t DOS_HEADER_SIZE = 64;
const uint16_t OPTIONAL_HEADER_SIZE = 240;
const uint64_t STACK_RESERVE = 8 * 1024 * 1024;  // Room for deep recursion
const uint64_t STACK_COMMIT = 0x1000;
const uint64_t HEAP_RESERVE = 0x100000;
const uint64_t HEAP_COMMIT = 0x1000;
const uint32_t IMPORT_DESCRIPTOR_SIZE = 20;
const uint32_t THUNK_SIZE = 8;
const uint32_t VALUE_SIZE = 8;
const uint32_t BUILDER_SIZE = 24;
const uint32_t CLEANER_SIZE = 432;  // Its values, a free list for each of the 32 size classes, then its kept table and list
const uint32_t ERROR_TEXT_SIZE = 8 + 4096;  // A window program's run error, kept for its message box: the length, then the text
const uint32_t LETTER_SIZE = 16;    // Each one-letter string: its length, its letter and a new line

uint32_t round_up(uint32_t value, uint32_t step) {
    return (value + step - 1) / step * step;
}

class Writer {
public:
    std::vector<uint8_t> bytes;

    void u8(uint8_t value) { bytes.push_back(value); }

    void u16(uint16_t value) { put(value, 2); }
    void u32(uint32_t value) { put(value, 4); }
    void u64(uint64_t value) { put(value, 8); }

    void text(const std::string& words, size_t room) {
        for (size_t i = 0; i < room; i++) u8(i < words.size() ? static_cast<uint8_t>(words[i]) : 0);
    }

    void pad_to(size_t size) {
        if (bytes.size() < size) bytes.resize(size, 0);
    }

private:
    void put(uint64_t value, int count) {
        for (int i = 0; i < count; i++) bytes.push_back(static_cast<uint8_t>(value >> (i * 8)));
    }
};

// The second section: what the program imports from Windows, the values it keeps while running, then its strings
struct DataSection {
    std::vector<uint8_t> bytes;
    uint32_t import_table_rva = 0;
    uint32_t import_table_size = 0;
    uint32_t address_table_rva = 0;
    uint32_t address_table_size = 0;
    std::map<DataRef, uint32_t> places;
    std::vector<uint32_t> string_places;
    std::vector<uint32_t> import_places;
};

std::string lowercase(std::string words) {
    for (char& letter : words) letter = static_cast<char>(std::tolower(static_cast<unsigned char>(letter)));
    return words;
}

DataSection build_data_section(uint32_t rva, const std::vector<std::string>& strings, const std::vector<CallSite>& call_sites, const std::vector<std::pair<std::string, std::string>>& extra, bool window) {
    std::vector<std::pair<DataRef, std::string>> imports = {
        {DataRef::GetStdHandle, "GetStdHandle"},
        {DataRef::WriteFile, "WriteFile"},
        {DataRef::ExitProcess, "ExitProcess"},
        {DataRef::GetProcessHeap, "GetProcessHeap"},
        {DataRef::HeapAlloc, "HeapAlloc"},
        {DataRef::CreateFileW, "CreateFileW"},
        {DataRef::ReadFile, "ReadFile"},
        {DataRef::CloseHandle, "CloseHandle"},
        {DataRef::GetFileAttributesW, "GetFileAttributesW"},
        {DataRef::GetCommandLineW, "GetCommandLineW"},
        {DataRef::MultiByteToWideChar, "MultiByteToWideChar"},
        {DataRef::HeapFree, "HeapFree"},
    };
    // Only a window program loads MessageBoxA, so only it imports these
    if (window) {
        imports.push_back({DataRef::LoadLibraryA, "LoadLibraryA"});
        imports.push_back({DataRef::GetProcAddress, "GetProcAddress"});
    }

    // Each library gets a descriptor, a lookup table and an address table. The address tables sit together, for the import directory
    struct Library {
        std::string name;
        std::vector<std::string> functions;
        std::vector<int> places;  // Where each one's address goes: a DataRef, or -1 - an index into extra
    };
    std::vector<Library> libraries = {{"kernel32.dll", {}, {}}};
    for (const auto& import : imports) {
        libraries[0].functions.push_back(import.second);
        libraries[0].places.push_back(static_cast<int>(import.first));
    }
    for (size_t i = 0; i < extra.size(); i++) {
        auto same = [&](const Library& library) { return lowercase(library.name) == lowercase(extra[i].first); };
        auto found = std::find_if(libraries.begin(), libraries.end(), same);
        if (found == libraries.end()) {
            libraries.push_back({extra[i].first, {}, {}});
            found = libraries.end() - 1;
        }
        found->functions.push_back(extra[i].second);
        found->places.push_back(-1 - static_cast<int>(i));
    }

    DataSection section;
    const uint32_t descriptors_bytes = static_cast<uint32_t>(libraries.size() + 1) * IMPORT_DESCRIPTOR_SIZE;
    uint32_t next = descriptors_bytes;
    std::vector<uint32_t> lookup_at, addresses_at;
    for (const Library& library : libraries) {
        lookup_at.push_back(next);
        next += static_cast<uint32_t>(library.functions.size() + 1) * THUNK_SIZE;
    }
    const uint32_t all_addresses_at = next;
    for (const Library& library : libraries) {
        addresses_at.push_back(next);
        next += static_cast<uint32_t>(library.functions.size() + 1) * THUNK_SIZE;
    }
    const uint32_t all_addresses_bytes = next - all_addresses_at;
    std::vector<std::vector<uint32_t>> name_places(libraries.size());
    std::vector<uint32_t> library_places;
    for (size_t l = 0; l < libraries.size(); l++) {
        for (const std::string& function : libraries[l].functions) {
            name_places[l].push_back(next);
            next += 2 + static_cast<uint32_t>(function.size()) + 1;  // A hint, the name and its end
            next += next % 2;
        }
        library_places.push_back(next);
        next += static_cast<uint32_t>(libraries[l].name.size()) + 1;
    }
    next = round_up(next, VALUE_SIZE);
    const uint32_t heap_handle_at = next;
    const uint32_t current_line_at = next + VALUE_SIZE;
    const uint32_t builder_at = current_line_at + VALUE_SIZE;
    const uint32_t visit_head_at = builder_at + BUILDER_SIZE;
    const uint32_t stdin_at = visit_head_at + VALUE_SIZE;
    const uint32_t cleaner_at = stdin_at + BUILDER_SIZE;
    const uint32_t letters_at = cleaner_at + CLEANER_SIZE;
    const uint32_t error_text_at = letters_at + 256 * LETTER_SIZE;
    next = error_text_at + ERROR_TEXT_SIZE;
    std::vector<uint32_t> text_places;
    for (const std::string& text : strings) {
        text_places.push_back(next);
        next = round_up(next + VALUE_SIZE + static_cast<uint32_t>(text.size()) + 1, VALUE_SIZE);
    }
    const uint32_t call_table_at = next;
    next += VALUE_SIZE + static_cast<uint32_t>(call_sites.size()) * VALUE_SIZE;
    const uint32_t globals_at = next;
    next += VALUE_SIZE;

    Writer out;
    for (size_t l = 0; l < libraries.size(); l++) {
        out.u32(rva + lookup_at[l]);
        out.u32(0);
        out.u32(0);
        out.u32(rva + library_places[l]);
        out.u32(rva + addresses_at[l]);
    }
    out.pad_to(descriptors_bytes);
    // The lookup tables and address tables start out the same. Windows fills in the second when it loads the program
    for (const auto* tables : {&lookup_at, &addresses_at}) {
        for (size_t l = 0; l < libraries.size(); l++) {
            out.pad_to((*tables)[l]);
            for (size_t i = 0; i < libraries[l].functions.size(); i++) out.u64(rva + name_places[l][i]);
            out.u64(0);
        }
    }
    section.import_places.resize(extra.size());
    for (size_t l = 0; l < libraries.size(); l++) {
        for (size_t i = 0; i < libraries[l].functions.size(); i++) {
            out.pad_to(name_places[l][i]);
            out.u16(0);
            out.text(libraries[l].functions[i], libraries[l].functions[i].size() + 1);
            const uint32_t place = rva + addresses_at[l] + static_cast<uint32_t>(i) * THUNK_SIZE;
            const int which = libraries[l].places[i];
            if (which >= 0) {
                section.places[static_cast<DataRef>(which)] = place;
            } else {
                section.import_places[static_cast<size_t>(-1 - which)] = place;
            }
        }
        out.pad_to(library_places[l]);
        out.text(libraries[l].name, libraries[l].name.size() + 1);
    }
    // Other programs never reach the code that calls these, so it points at ExitProcess instead
    if (!window) section.places[DataRef::LoadLibraryA] = section.places[DataRef::GetProcAddress] = section.places.at(DataRef::ExitProcess);
    section.places[DataRef::HeapHandle] = rva + heap_handle_at;
    section.places[DataRef::CurrentLine] = rva + current_line_at;
    section.places[DataRef::Builder] = rva + builder_at;
    section.places[DataRef::VisitHead] = rva + visit_head_at;
    section.places[DataRef::Stdin] = rva + stdin_at;
    section.places[DataRef::Cleaner] = rva + cleaner_at;
    // Every one-letter string, made once, so text[i] can share them since text never changes
    section.places[DataRef::Letters] = rva + letters_at;
    section.places[DataRef::ErrorText] = rva + error_text_at;
    for (uint32_t letter = 0; letter < 256; letter++) {
        out.pad_to(letters_at + letter * LETTER_SIZE);
        out.u64(1);
        out.u8(static_cast<uint8_t>(letter));
        out.u8('\n');  // Like every string, so print can write it with its new line in one go
    }
    for (size_t i = 0; i < strings.size(); i++) {
        out.pad_to(text_places[i]);
        out.u64(strings[i].size());
        out.text(strings[i], strings[i].size());
        out.u8('\n');
        section.string_places.push_back(rva + text_places[i]);
    }
    out.pad_to(call_table_at);
    section.places[DataRef::CallTable] = rva + call_table_at;
    section.places[DataRef::Globals] = rva + globals_at;
    out.u64(call_sites.size());
    for (const CallSite& site : call_sites) {
        out.u32(TEXT_RVA + static_cast<uint32_t>(site.offset));
        out.u32(rva + text_places[static_cast<size_t>(site.string)]);
    }
    out.pad_to(next);

    section.bytes = std::move(out.bytes);
    section.import_table_rva = rva;
    section.import_table_size = descriptors_bytes;
    section.address_table_rva = rva + all_addresses_at;
    section.address_table_size = all_addresses_bytes;
    return section;
}

void write_section_header(Writer& out, const std::string& name, uint32_t real_size, uint32_t rva, uint32_t file_size, uint32_t file_at, uint32_t flags) {
    out.text(name, 8);
    out.u32(real_size);
    out.u32(rva);
    out.u32(file_size);
    out.u32(file_at);
    out.u32(0);
    out.u32(0);
    out.u16(0);
    out.u16(0);
    out.u32(flags);
}

}  // namespace

std::vector<uint8_t> build_exe(const MachineCode& code) {
    const uint32_t text_size = static_cast<uint32_t>(code.bytes.size());
    const uint32_t data_rva = TEXT_RVA + round_up(text_size, SECTION_ALIGN);
    const DataSection data = build_data_section(data_rva, code.strings, code.call_sites, code.imports, code.window);
    const uint32_t data_size = static_cast<uint32_t>(data.bytes.size());
    const uint32_t text_file_size = round_up(text_size, FILE_ALIGN);
    const uint32_t data_file_size = round_up(data_size, FILE_ALIGN);
    const uint32_t data_file_at = HEADERS_SIZE + text_file_size;
    const uint32_t image_size = data_rva + round_up(data_size, SECTION_ALIGN);

    // Fill in the machine code's distances to the imports and text
    std::vector<uint8_t> text = code.bytes;
    for (const DataFixup& fixup : code.fixups) {
        const uint32_t next_instruction = TEXT_RVA + static_cast<uint32_t>(fixup.offset) + 4;
        const uint32_t place = fixup.target == DataRef::String ? data.string_places.at(static_cast<size_t>(fixup.string))
            : fixup.target == DataRef::Import ? data.import_places.at(static_cast<size_t>(fixup.string))
            : data.places.at(fixup.target);
        const uint32_t distance = place + static_cast<uint32_t>(fixup.extra) - next_instruction;
        for (size_t i = 0; i < 4; i++) text[fixup.offset + i] = static_cast<uint8_t>(distance >> (i * 8));
    }

    Writer out;
    // DOS header, which only needs its signature and where the real header is.
    // math.jamo and time.jamo keep values in bytes 8 to 47 while the program runs, so those must stay zero here
    out.text("MZ", 2);
    out.pad_to(DOS_HEADER_SIZE - 4);
    out.u32(DOS_HEADER_SIZE);

    out.text("PE", 4);
    // COFF header
    out.u16(0x8664);                 // x86-64
    out.u16(2);                      // Two sections
    out.u32(0);                      // No timestamp, so the same source always gives the same file
    out.u32(0);
    out.u32(0);
    out.u16(OPTIONAL_HEADER_SIZE);
    out.u16(0x0023);                 // An executable with no relocations, able to use large addresses

    // Optional header
    out.u16(0x020B);                 // 64-bit
    out.u8(1);                       // Linker version, which nothing reads
    out.u8(0);
    out.u32(text_file_size);
    out.u32(data_file_size);
    out.u32(0);
    out.u32(TEXT_RVA);               // Starts at the first byte of code
    out.u32(TEXT_RVA);
    out.u64(IMAGE_BASE);
    out.u32(SECTION_ALIGN);
    out.u32(FILE_ALIGN);
    out.u16(6);                      // Windows Vista or newer
    out.u16(0);
    out.u16(0);
    out.u16(0);
    out.u16(6);
    out.u16(0);
    out.u32(0);
    out.u32(image_size);
    out.u32(HEADERS_SIZE);
    out.u32(0);                      // Checksum, which normal programs don't need
    out.u16(code.window ? 2 : 3);    // A window program, or one for the console
    out.u16(0x8100);                 // Data can't run as code, and it's aware of Terminal Server
    out.u64(STACK_RESERVE);
    out.u64(STACK_COMMIT);
    out.u64(HEAP_RESERVE);
    out.u64(HEAP_COMMIT);
    out.u32(0);
    out.u32(16);                     // Data directories
    for (uint32_t directory = 0; directory < 16; directory++) {
        if (directory == 1) {
            out.u32(data.import_table_rva);
            out.u32(data.import_table_size);
        } else if (directory == 12) {
            out.u32(data.address_table_rva);
            out.u32(data.address_table_size);
        } else {
            out.u32(0);
            out.u32(0);
        }
    }

    write_section_header(out, ".text", text_size, TEXT_RVA, text_file_size, HEADERS_SIZE, 0x60000020);    // Code that runs and reads
    write_section_header(out, ".idata", data_size, data_rva, data_file_size, data_file_at, 0xC0000040);  // Data that reads and writes
    out.pad_to(HEADERS_SIZE);

    out.bytes.insert(out.bytes.end(), text.begin(), text.end());
    out.pad_to(data_file_at);
    out.bytes.insert(out.bytes.end(), data.bytes.begin(), data.bytes.end());
    out.pad_to(data_file_at + data_file_size);
    return std::move(out.bytes);
}
