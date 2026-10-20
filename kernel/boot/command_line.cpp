#include "kernel/boot/command_line.hpp"

namespace barry::boot {
namespace {

constexpr char SEPARATOR = ' ';
constexpr char ASSIGNMENT = '=';

bool is_separator(char character) {
    return character == SEPARATOR || character == '\t';
}

}  // namespace

Optional<StringView> CommandLine::Iterator::next() {
    while (m_offset < m_text.length() && is_separator(m_text[m_offset]))
        ++m_offset;

    if (m_offset >= m_text.length())
        return nullopt;

    usize const start = m_offset;
    while (m_offset < m_text.length() && !is_separator(m_text[m_offset]))
        ++m_offset;

    return m_text.substring(start, m_offset - start);
}

bool CommandLine::has_flag(StringView name) const {
    Iterator iterator(m_text, 0);

    while (auto token = iterator.next()) {
        StringView const word = token.value();
        if (word == name)
            return true;

        auto const assignment = word.find(ASSIGNMENT);
        if (assignment.has_value() && word.substring(0, assignment.value()) == name)
            return true;
    }

    return false;
}

Optional<StringView> CommandLine::value_of(StringView name) const {
    Iterator iterator(m_text, 0);

    while (auto token = iterator.next()) {
        StringView const word = token.value();
        auto const assignment = word.find(ASSIGNMENT);
        if (!assignment.has_value())
            continue;
        if (word.substring(0, assignment.value()) != name)
            continue;
        return word.substring(assignment.value() + 1);
    }

    return nullopt;
}

}  // namespace barry::boot
