#include "kernel/base/bytes.hpp"

#include "kernel/base/assert.hpp"

namespace barry {
namespace {

using Word = u64;

constexpr usize WORD_SIZE = sizeof(Word);

bool is_word_aligned(void const* pointer) {
    return (reinterpret_cast<uptr>(pointer) % WORD_SIZE) == 0;
}

bool can_copy_by_word(u8 const* destination, u8 const* source, usize count) {
    return count >= WORD_SIZE && is_word_aligned(destination) && is_word_aligned(source);
}

void copy_forward(u8* destination, u8 const* source, usize count) {
    usize index = 0;

    if (can_copy_by_word(destination, source, count)) {
        auto* destination_words = reinterpret_cast<Word*>(destination);
        auto const* source_words = reinterpret_cast<Word const*>(source);
        usize const word_count = count / WORD_SIZE;

        for (usize word = 0; word < word_count; ++word)
            destination_words[word] = source_words[word];

        index = word_count * WORD_SIZE;
    }

    for (; index < count; ++index)
        destination[index] = source[index];
}

void copy_backward(u8* destination, u8 const* source, usize count) {
    for (usize index = count; index > 0; --index)
        destination[index - 1] = source[index - 1];
}

bool regions_overlap_with_destination_after_source(u8 const* destination, u8 const* source,
                                                   usize count) {
    return destination > source && destination < source + count;
}

}  // namespace

void copy_bytes(Bytes destination, ReadonlyBytes source) {
    VERIFY(destination.size() >= source.size());
    if (source.is_empty())
        return;
    copy_forward(destination.data(), source.data(), source.size());
}

void move_bytes(Bytes destination, ReadonlyBytes source) {
    VERIFY(destination.size() >= source.size());
    if (source.is_empty() || destination.data() == source.data())
        return;

    if (regions_overlap_with_destination_after_source(destination.data(), source.data(),
                                                      source.size()))
        copy_backward(destination.data(), source.data(), source.size());
    else
        copy_forward(destination.data(), source.data(), source.size());
}

void fill_bytes(Bytes destination, u8 value) {
    usize index = 0;

    if (destination.size() >= WORD_SIZE && is_word_aligned(destination.data())) {
        Word pattern = 0;
        for (usize byte = 0; byte < WORD_SIZE; ++byte)
            pattern |= static_cast<Word>(value) << (byte * 8);

        auto* words = reinterpret_cast<Word*>(destination.data());
        usize const word_count = destination.size() / WORD_SIZE;

        for (usize word = 0; word < word_count; ++word)
            words[word] = pattern;

        index = word_count * WORD_SIZE;
    }

    for (; index < destination.size(); ++index)
        destination[index] = value;
}

void zero_bytes(Bytes destination) {
    fill_bytes(destination, 0);
}

int compare_bytes(ReadonlyBytes left, ReadonlyBytes right) {
    usize const common_length = min(left.size(), right.size());

    for (usize index = 0; index < common_length; ++index) {
        if (left[index] != right[index])
            return static_cast<int>(left[index]) - static_cast<int>(right[index]);
    }

    if (left.size() == right.size())
        return 0;
    return left.size() < right.size() ? -1 : 1;
}

bool bytes_equal(ReadonlyBytes left, ReadonlyBytes right) {
    return left.size() == right.size() && compare_bytes(left, right) == 0;
}

void wipe_bytes(Bytes destination) {
    zero_bytes(destination);
    __asm__ volatile("" ::"r"(destination.data()) : "memory");
}

}  // namespace barry
