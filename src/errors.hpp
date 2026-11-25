// Errors: collects the mistakes found in Jamo files, and prints them with the line and a marker.
#pragma once

#include <string>
#include <vector>

struct Spot {
    int line = 0;    // Counts from 1
    int column = 0;  // Counts from 0
    int length = 1;
    int file = -1;   // Which file. -1 means the one being read now
};

struct ErrorMessage {
    Spot spot;
    std::string words;
    std::string hint;
};

class Errors {
public:
    Errors() = default;
    Errors(std::string file_name, const std::string& source);

    int add_file(std::string file_name, const std::string& source);
    void use_file(int file) { current = file; }
    int current_file() const { return current; }
    const std::string& name_of_file(int file) const { return files[static_cast<size_t>(file)].name; }

    void add(Spot spot, const std::string& words, const std::string& hint = "");
    void warn(Spot spot, const std::string& words, const std::string& hint = "");
    std::string warnings_text() const;
    bool any() const { return !messages.empty(); }
    size_t count() const { return messages.size(); }
    bool is_full() const;
    std::string text() const;

    std::vector<ErrorMessage> messages;
    std::vector<ErrorMessage> warnings;

private:
    std::string text_of(const ErrorMessage& message, const std::string& label) const;

    struct File {
        std::string name;
        std::vector<std::string> lines;
    };

    std::vector<File> files;
    int current = -1;
};
