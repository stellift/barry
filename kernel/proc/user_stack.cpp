#include "kernel/proc/user_stack.hpp"

#include "kernel/base/array.hpp"
#include "kernel/base/assert.hpp"
#include "kernel/base/bytes.hpp"

namespace barry::proc {
namespace {

constexpr usize STACK_SETUP_CAPACITY = 8_KiB;
constexpr usize RANDOM_BYTE_COUNT = 16;
constexpr u64 STACK_ALIGNMENT = 16;
constexpr u64 CLOCK_TICKS_PER_SECOND = 100;

struct AuxiliaryEntry {
    AuxiliaryVector key;
    u64 value;
};

class StackWriter {
public:
    StackWriter(Span<u8> buffer, VirtAddr base) : m_buffer(buffer), m_base(base) {}

    void write_word(VirtAddr address, u64 value) {
        auto const offset = m_base.distance_to(address);
        VERIFY(offset + sizeof(u64) <= m_buffer.size());
        __builtin_memcpy(m_buffer.data() + offset, &value, sizeof(u64));
    }

    void write_bytes(VirtAddr address, ReadonlyBytes source) {
        auto const offset = m_base.distance_to(address);
        VERIFY(offset + source.size() <= m_buffer.size());
        copy_bytes(m_buffer.slice(offset, source.size()), source);
    }

private:
    Span<u8> m_buffer;
    VirtAddr m_base;
};

u64 total_string_bytes(Span<StringView const> strings) {
    u64 total = 0;
    for (auto const& text : strings)
        total += text.length() + 1;
    return total;
}

usize build_auxiliary_vector(Array<AuxiliaryEntry, 16>& entries, LoadedImage const& image,
                             VirtAddr random_bytes) {
    usize count = 0;

    entries[count++] = { AuxiliaryVector::PageSize, PAGE_SIZE };
    entries[count++] = { AuxiliaryVector::EntryPoint, image.entry_point.raw() };
    entries[count++] = { AuxiliaryVector::ClockTicks, CLOCK_TICKS_PER_SECOND };
    entries[count++] = { AuxiliaryVector::UserId, 0 };
    entries[count++] = { AuxiliaryVector::EffectiveUserId, 0 };
    entries[count++] = { AuxiliaryVector::GroupId, 0 };
    entries[count++] = { AuxiliaryVector::EffectiveGroupId, 0 };
    entries[count++] = { AuxiliaryVector::Secure, 0 };
    entries[count++] = { AuxiliaryVector::Random, random_bytes.raw() };

    if (!image.program_headers.is_null()) {
        entries[count++] = { AuxiliaryVector::ProgramHeaders, image.program_headers.raw() };
        entries[count++] = { AuxiliaryVector::ProgramHeaderEntrySize,
                             image.program_header_entry_size };
        entries[count++] = { AuxiliaryVector::ProgramHeaderCount, image.program_header_count };
    }

    entries[count++] = { AuxiliaryVector::Null, 0 };
    return count;
}

}  // namespace

KResultOr<InitialUserStack> build_initial_user_stack(ProcessAddressSpace& space,
                                                     LoadedImage const& image,
                                                     Span<StringView const> arguments,
                                                     Span<StringView const> environment) {
    TRY(space.map_anonymous(user_stack_range(), mm::USER_DATA_FLAGS));

    Array<AuxiliaryEntry, 16> auxiliary { {} };

    u64 const string_bytes = total_string_bytes(arguments) + total_string_bytes(environment);
    VirtAddr const random_bytes = USER_STACK_TOP.subtract(RANDOM_BYTE_COUNT);
    VirtAddr const strings_base = random_bytes.subtract(string_bytes);

    usize const auxiliary_count = build_auxiliary_vector(auxiliary, image, random_bytes);
    u64 const slot_count = 1 + arguments.size() + 1 + environment.size() + 1
                           + auxiliary_count * 2;

    VirtAddr const stack_pointer = VirtAddr(
        align_down<u64>(strings_base.subtract(slot_count * sizeof(u64)).raw(), STACK_ALIGNMENT));

    u64 const used = stack_pointer.distance_to(USER_STACK_TOP);
    if (used > STACK_SETUP_CAPACITY)
        return Error(Errno::E2BIG, "the initial process stack does not fit");

    Array<u8, STACK_SETUP_CAPACITY> buffer { {} };
    StackWriter writer(buffer.span().first(used), stack_pointer);

    VirtAddr string_cursor = strings_base;
    VirtAddr slot_cursor = stack_pointer;

    writer.write_word(slot_cursor, arguments.size());
    slot_cursor = slot_cursor.offset(sizeof(u64));

    for (auto const& argument : arguments) {
        writer.write_word(slot_cursor, string_cursor.raw());
        slot_cursor = slot_cursor.offset(sizeof(u64));
        writer.write_bytes(string_cursor, argument.bytes());
        string_cursor = string_cursor.offset(argument.length() + 1);
    }

    writer.write_word(slot_cursor, 0);
    slot_cursor = slot_cursor.offset(sizeof(u64));

    for (auto const& variable : environment) {
        writer.write_word(slot_cursor, string_cursor.raw());
        slot_cursor = slot_cursor.offset(sizeof(u64));
        writer.write_bytes(string_cursor, variable.bytes());
        string_cursor = string_cursor.offset(variable.length() + 1);
    }

    writer.write_word(slot_cursor, 0);
    slot_cursor = slot_cursor.offset(sizeof(u64));

    for (usize index = 0; index < auxiliary_count; ++index) {
        writer.write_word(slot_cursor, to_underlying(auxiliary[index].key));
        writer.write_word(slot_cursor.offset(sizeof(u64)), auxiliary[index].value);
        slot_cursor = slot_cursor.offset(2 * sizeof(u64));
    }

    for (usize index = 0; index < RANDOM_BYTE_COUNT; ++index)
        buffer[used - RANDOM_BYTE_COUNT + index] = static_cast<u8>(0xa5 ^ index);

    TRY(space.copy_in(stack_pointer, buffer.span().first(used)));

    return InitialUserStack { stack_pointer, used };
}

}  // namespace barry::proc
