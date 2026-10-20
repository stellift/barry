#include "kernel/base/bitmap.hpp"

namespace barry {
namespace {

constexpr Bitmap::Word ALL_BITS_SET = ~Bitmap::Word { 0 };

}  // namespace

Optional<usize> Bitmap::find_first_clear(usize from) const {
    for (usize index = from; index < m_bit_count;) {
        usize const word_index = index / BITS_PER_WORD;
        Word const word = m_storage[word_index];

        if (word == ALL_BITS_SET) {
            index = (word_index + 1) * BITS_PER_WORD;
            continue;
        }

        usize const bit_in_word = index % BITS_PER_WORD;
        Word const remaining = ~word >> bit_in_word;
        if (remaining == 0) {
            index = (word_index + 1) * BITS_PER_WORD;
            continue;
        }

        usize const found = index + static_cast<usize>(__builtin_ctzll(remaining));
        return found < m_bit_count ? Optional<usize>(found) : nullopt;
    }
    return nullopt;
}

Optional<usize> Bitmap::find_clear_run(usize count, usize from) const {
    if (count == 0 || count > m_bit_count)
        return nullopt;

    usize candidate = from;
    while (candidate + count <= m_bit_count) {
        auto const start = find_first_clear(candidate);
        if (!start.has_value())
            return nullopt;

        usize const run_start = start.value();
        if (run_start + count > m_bit_count)
            return nullopt;

        usize index = run_start;
        while (index < run_start + count && !get(index))
            ++index;

        if (index == run_start + count)
            return run_start;

        candidate = index + 1;
    }
    return nullopt;
}

}  // namespace barry
