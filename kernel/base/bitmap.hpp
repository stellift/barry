#pragma once

#include "kernel/base/assert.hpp"
#include "kernel/base/bytes.hpp"
#include "kernel/base/optional.hpp"
#include "kernel/base/span.hpp"
#include "kernel/base/types.hpp"

namespace barry {

class Bitmap {
public:
    using Word = u64;
    static constexpr usize BITS_PER_WORD = sizeof(Word) * 8;

    static constexpr usize words_needed_for(usize bit_count) {
        return (bit_count + BITS_PER_WORD - 1) / BITS_PER_WORD;
    }

    static constexpr usize bytes_needed_for(usize bit_count) {
        return words_needed_for(bit_count) * sizeof(Word);
    }

    Bitmap() = default;

    Bitmap(Span<Word> storage, usize bit_count) : m_storage(storage), m_bit_count(bit_count) {
        VERIFY(storage.size() >= words_needed_for(bit_count));
    }

    usize size() const { return m_bit_count; }

    bool get(usize index) const {
        VERIFY(index < m_bit_count);
        return (m_storage[index / BITS_PER_WORD] & mask_for(index)) != 0;
    }

    void set(usize index) {
        VERIFY(index < m_bit_count);
        m_storage[index / BITS_PER_WORD] |= mask_for(index);
    }

    void clear(usize index) {
        VERIFY(index < m_bit_count);
        m_storage[index / BITS_PER_WORD] &= ~mask_for(index);
    }

    void set_range(usize start, usize count, bool value) {
        VERIFY(start + count <= m_bit_count);
        for (usize index = start; index < start + count; ++index) {
            if (value)
                set(index);
            else
                clear(index);
        }
    }

    void fill(bool value) {
        fill_bytes(as_writable_bytes(m_storage), value ? 0xff : 0x00);
    }

    bool is_range_clear(usize start, usize count) const {
        if (start + count > m_bit_count)
            return false;
        for (usize index = start; index < start + count; ++index) {
            if (get(index))
                return false;
        }
        return true;
    }

    Optional<usize> find_first_clear(usize from = 0) const;
    Optional<usize> find_clear_run(usize count, usize from = 0) const;

private:
    static constexpr Word mask_for(usize index) {
        return Word { 1 } << (index % BITS_PER_WORD);
    }

    Span<Word> m_storage;
    usize m_bit_count { 0 };
};

}  // namespace barry
