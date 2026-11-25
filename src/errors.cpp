#include "errors.hpp"

#include <algorithm>

namespace {

const size_t MOST_MESSAGES = 20;  // More than this is usually one mistake repeating

// Keeps the marker lined up under the mistake, and strange bytes out of the terminal
std::string readable_line(const std::string& line) {
    std::string out;
    for (char letter : line) {
        const unsigned char code = static_cast<unsigned char>(letter);
        if (letter == '\t') {
            out.push_back(' ');
        } else if (code < 32 || code >= 127) {
            out.push_back('?');
        } else {
            out.push_back(letter);
        }
    }
    return out;
}

}  // namespace

Errors::Errors(std::string name, const std::string& source) {
    add_file(std::move(name), source);
}

int Errors::add_file(std::string name, const std::string& source) {
    File file;
    file.name = std::move(name);
    std::string line;
    for (char letter : source) {
        if (letter == '\n') {
            file.lines.push_back(line);
            line.clear();
        } else if (letter != '\r') {
            line.push_back(letter);
        }
    }
    file.lines.push_back(line);
    files.push_back(std::move(file));
    current = static_cast<int>(files.size()) - 1;
    return current;
}

void Errors::add(Spot spot, const std::string& words, const std::string& hint) {
    if (spot.file < 0) spot.file = current;
    if (!is_full()) messages.push_back({spot, words, hint});
}

bool Errors::is_full() const {
    return messages.size() >= MOST_MESSAGES;
}

void Errors::warn(Spot spot, const std::string& words, const std::string& hint) {
    if (spot.file < 0) spot.file = current;
    // The built-in libraries are tested already, and their warnings would only get in the way
    const std::string& name = files[static_cast<size_t>(spot.file)].name;
    if (name.size() > 11 && name.compare(name.size() - 11, 11, " (built in)") == 0) return;
    warnings.push_back({spot, words, hint});
}

std::string Errors::text_of(const ErrorMessage& message, const std::string& label) const {
    std::string out;
    const File& file = files[static_cast<size_t>(message.spot.file)];
    out += file.name + " line " + std::to_string(message.spot.line) + ": " + label + message.words + "\n";
    const size_t at = static_cast<size_t>(message.spot.line - 1);
    if (message.spot.line >= 1 && at < file.lines.size()) {
        out += "    " + readable_line(file.lines[at]) + "\n";
        out += "    " + std::string(static_cast<size_t>(message.spot.column), ' ');
        // A file with Windows line endings has an extra character that isn't shown, so the marker stops at the line's end
        const int room = static_cast<int>(file.lines[at].size()) - message.spot.column;
        const int length = std::min(message.spot.length, room);
        out += std::string(static_cast<size_t>(length < 1 ? 1 : length), '^') + "\n";
    }
    if (!message.hint.empty()) out += message.hint + "\n";
    return out;
}

std::string Errors::text() const {
    std::string out;
    for (const ErrorMessage& message : messages) out += text_of(message, "");
    return out;
}

// In the order they appear, up to the same limit as errors
std::string Errors::warnings_text() const {
    std::vector<ErrorMessage> sorted = warnings;
    std::stable_sort(sorted.begin(), sorted.end(), [](const ErrorMessage& a, const ErrorMessage& b) {
        return a.spot.file != b.spot.file ? a.spot.file < b.spot.file : a.spot.line < b.spot.line;
    });
    std::string out;
    for (size_t i = 0; i < sorted.size() && i < MOST_MESSAGES; i++) out += text_of(sorted[i], "Warning: ");
    return out;
}
