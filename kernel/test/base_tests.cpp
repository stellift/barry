#include "kernel/base/array.hpp"
#include "kernel/base/bitmap.hpp"
#include "kernel/base/bytes.hpp"
#include "kernel/base/checked.hpp"
#include "kernel/base/expected.hpp"
#include "kernel/base/fixed_string_buffer.hpp"
#include "kernel/base/intrusive_list.hpp"
#include "kernel/base/optional.hpp"
#include "kernel/base/span.hpp"
#include "kernel/base/string_view.hpp"
#include "kernel/test/framework.hpp"

namespace barry {
namespace {

KResultOr<u32> doubled_if_small(u32 value) {
    if (value > 100)
        return Error(Errno::ERANGE, "value too large");
    return value * 2;
}

KResultOr<u32> doubled_twice(u32 value) {
    u32 const once = TRY(doubled_if_small(value));
    return TRY(doubled_if_small(once));
}

struct Item : IntrusiveListNode<Item> {
    explicit Item(u32 value) : id(value) {}
    u32 id;
};

template<usize Capacity, typename... Args>
StringView formatted(FixedStringBuffer<Capacity>& buffer, StringView format,
                     Args const&... args) {
    buffer.clear();
    format_to(buffer, format, args...);
    return buffer.view();
}

}  // namespace

KERNEL_TEST(expected_carries_values_and_errors) {
    auto const ok = doubled_if_small(21);
    EXPECT(ok.has_value());
    EXPECT_EQ(ok.value(), 42u);

    auto const failed = doubled_if_small(101);
    EXPECT(failed.is_error());
    EXPECT(failed.error() == Errno::ERANGE);
}

KERNEL_TEST(try_macro_propagates_the_first_error) {
    auto const ok = doubled_twice(10);
    EXPECT(ok.has_value());
    EXPECT_EQ(ok.value(), 40u);

    auto const failed = doubled_twice(60);
    EXPECT(failed.is_error());
    EXPECT(failed.error() == Errno::ERANGE);
}

KERNEL_TEST(expected_void_specialization) {
    KResult const ok {};
    EXPECT(ok.has_value());

    KResult const failed = Error(Errno::EINVAL);
    EXPECT(failed.is_error());
    EXPECT(failed.error() == Errno::EINVAL);
}

KERNEL_TEST(optional_tracks_presence) {
    Optional<u32> value;
    EXPECT(!value.has_value());
    EXPECT_EQ(value.value_or(7u), 7u);

    value = 11u;
    EXPECT(value.has_value());
    EXPECT_EQ(value.value(), 11u);

    EXPECT_EQ(value.release_value(), 11u);
    EXPECT(!value.has_value());
}

KERNEL_TEST(span_slices_and_bounds) {
    Array<u32, 5> numbers { { 1, 2, 3, 4, 5 } };
    auto const span = numbers.span();

    EXPECT_EQ(span.size(), 5u);
    EXPECT_EQ(span.first(), 1u);
    EXPECT_EQ(span.last(), 5u);

    auto const middle = span.slice(1, 3);
    EXPECT_EQ(middle.size(), 3u);
    EXPECT_EQ(middle[0], 2u);

    EXPECT(!span.at(5).has_value());
    EXPECT(!span.try_slice(3, 4).has_value());
}

KERNEL_TEST(bitmap_finds_clear_runs) {
    Array<Bitmap::Word, 4> storage { { 0, 0, 0, 0 } };
    Bitmap bitmap(storage.span(), 200);

    bitmap.fill(true);
    EXPECT(!bitmap.find_first_clear().has_value());

    bitmap.set_range(64, 10, false);
    auto const first_clear = bitmap.find_first_clear();
    EXPECT(first_clear.has_value());
    EXPECT_EQ(first_clear.value(), 64u);

    auto const run = bitmap.find_clear_run(10);
    EXPECT(run.has_value());
    EXPECT_EQ(run.value(), 64u);
    EXPECT(!bitmap.find_clear_run(11).has_value());

    bitmap.clear(5);
    EXPECT(bitmap.is_range_clear(5, 1));
    EXPECT(!bitmap.is_range_clear(5, 2));
}

KERNEL_TEST(checked_arithmetic_detects_overflow) {
    EXPECT(checked_add<u32>(1, 2).has_value());
    EXPECT_EQ(checked_add<u32>(1, 2).value(), 3u);
    EXPECT(!checked_add<u32>(0xffffffff, 1).has_value());
    EXPECT(!checked_mul<u32>(0x10000, 0x10000).has_value());
    EXPECT(try_add<u32>(0xffffffff, 1).is_error());
    EXPECT(!narrow<u8>(300u).has_value());
    EXPECT_EQ(narrow<u8>(200u).value(), 200u);
}

KERNEL_TEST(alignment_helpers) {
    EXPECT_EQ(align_up<u64>(1, 4096), 4096u);
    EXPECT_EQ(align_down<u64>(4097, 4096), 4096u);
    EXPECT(is_aligned<u64>(8192, 4096));
    EXPECT(is_power_of_two<u64>(64));
    EXPECT(!is_power_of_two<u64>(63));
    EXPECT_EQ(div_ceil<u64>(7, 3), 3u);
}

KERNEL_TEST(string_view_comparison_and_search) {
    StringView const text = "barry kernel"_sv;
    EXPECT_EQ(text.length(), 12u);
    EXPECT(text.starts_with("barry"_sv));
    EXPECT(!text.starts_with("kernel"_sv));
    EXPECT_EQ(text.substring(6), "kernel"_sv);

    auto const space = text.find(' ');
    EXPECT(space.has_value());
    EXPECT_EQ(space.value(), 5u);
}

KERNEL_TEST(byte_operations_copy_fill_and_compare) {
    Array<u8, 8> source { { 1, 2, 3, 4, 5, 6, 7, 8 } };
    Array<u8, 8> destination { { 0, 0, 0, 0, 0, 0, 0, 0 } };

    copy_bytes(destination.span(), source.span());
    EXPECT(bytes_equal(destination.span(), source.span()));

    fill_bytes(destination.span(), 0xab);
    EXPECT_EQ(destination[0], 0xabu);
    EXPECT_EQ(destination[7], 0xabu);

    zero_bytes(destination.span());
    EXPECT_EQ(destination[3], 0u);
    EXPECT(compare_bytes(destination.span(), source.span()) != 0);
}

KERNEL_TEST(move_bytes_handles_overlap) {
    Array<u8, 8> buffer { { 1, 2, 3, 4, 5, 6, 7, 8 } };
    move_bytes(buffer.span().slice(2, 4), buffer.span().slice(0, 4));

    EXPECT_EQ(buffer[2], 1u);
    EXPECT_EQ(buffer[3], 2u);
    EXPECT_EQ(buffer[4], 3u);
    EXPECT_EQ(buffer[5], 4u);
}

KERNEL_TEST(formatter_renders_numbers_and_addresses) {
    FixedStringBuffer<128> buffer;

    EXPECT_EQ(formatted(buffer, "{}"_sv, 42u), "42"_sv);
    EXPECT_EQ(formatted(buffer, "{}"_sv, -7), "-7"_sv);
    EXPECT_EQ(formatted(buffer, "{x}"_sv, 255u), "ff"_sv);
    EXPECT_EQ(formatted(buffer, "{#x}"_sv, 255u), "0xff"_sv);
    EXPECT_EQ(formatted(buffer, "{#06x}"_sv, 255u), "0x00ff"_sv);
    EXPECT_EQ(formatted(buffer, "{b}"_sv, 5u), "101"_sv);
    EXPECT_EQ(formatted(buffer, "{}"_sv, true), "true"_sv);
    EXPECT_EQ(formatted(buffer, "a{}b{}c"_sv, 1u, 2u), "a1b2c"_sv);
    EXPECT_EQ(formatted(buffer, "{{literal}}"_sv), "{literal}"_sv);
    EXPECT_EQ(formatted(buffer, "{}"_sv, VirtAddr(0x1000)), "0x1000"_sv);
}

KERNEL_TEST(fixed_string_buffer_truncates_instead_of_overflowing) {
    FixedStringBuffer<4> buffer;
    buffer.write("abcdefgh"_sv);
    EXPECT_EQ(buffer.length(), 4u);
    EXPECT(buffer.is_truncated());
    EXPECT_EQ(buffer.view(), "abcd"_sv);
}

KERNEL_TEST(intrusive_list_links_and_unlinks) {
    IntrusiveList<Item> list;
    Item first { 1 };
    Item second { 2 };
    Item third { 3 };

    EXPECT(list.is_empty());

    list.append(first);
    list.append(second);
    list.prepend(third);

    EXPECT_EQ(list.size(), 3u);
    EXPECT_EQ(list.first()->id, 3u);
    EXPECT_EQ(list.last()->id, 2u);

    u32 sum = 0;
    for (auto const& item : list)
        sum += item.id;
    EXPECT_EQ(sum, 6u);

    list.remove(second);
    EXPECT_EQ(list.size(), 2u);

    Item* taken = list.take_first();
    EXPECT(taken != nullptr);
    EXPECT_EQ(taken->id, 3u);

    list.remove(first);
    EXPECT(list.is_empty());
}

KERNEL_TEST(address_types_do_not_mix) {
    PhysAddr const physical { 0x2000 };
    VirtAddr const virtual_address { 0xffffffff80001234 };

    EXPECT(physical.is_page_aligned());
    EXPECT_EQ(physical.page_frame_number(), 2u);
    EXPECT_EQ(PhysAddr::from_page_frame_number(2), physical);

    EXPECT(!virtual_address.is_page_aligned());
    EXPECT_EQ(virtual_address.page_offset(), 0x234u);
    EXPECT(virtual_address.is_kernel_space());
    EXPECT(virtual_address.is_canonical());
    EXPECT(!VirtAddr(0x0000800000000000).is_canonical());

    VirtRange const range { VirtAddr(0x1000), 0x2000 };
    EXPECT(range.contains(VirtAddr(0x1fff)));
    EXPECT(!range.contains(VirtAddr(0x3000)));
    EXPECT(range.overlaps(VirtRange { VirtAddr(0x2000), 0x1000 }));
}

}  // namespace barry
