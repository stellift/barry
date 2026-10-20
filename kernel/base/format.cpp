#include "kernel/base/format.hpp"

#include "kernel/base/array.hpp"

namespace barry {
namespace {

constexpr StringView DIGITS_LOWER = "0123456789abcdef"_sv;
constexpr StringView DIGITS_UPPER = "0123456789ABCDEF"_sv;
constexpr usize MAX_DIGITS = 64;
constexpr usize MAX_WIDTH = 64;

StringView base_prefix_for(Base base, bool uppercase) {
    switch (base) {
    case Base::Hex:
        return uppercase ? "0X"_sv : "0x"_sv;
    case Base::Binary:
        return "0b"_sv;
    case Base::Octal:
        return "0o"_sv;
    case Base::Decimal:
        return ""_sv;
    }
    return ""_sv;
}

bool is_decimal_digit(char character) {
    return character >= '0' && character <= '9';
}

}  // namespace

void Formatter::write_padding(char fill, usize count) {
    for (usize i = 0; i < count; ++i)
        write_char(fill);
}

void Formatter::write_unsigned(u64 value, FormatSpec const& spec) {
    Array<char, MAX_DIGITS> digits {};
    StringView const alphabet = spec.uppercase ? DIGITS_UPPER : DIGITS_LOWER;
    auto const radix = static_cast<u64>(to_underlying(spec.base));

    usize digit_count = 0;
    do {
        digits[digit_count++] = alphabet[value % radix];
        value /= radix;
    } while (value != 0 && digit_count < MAX_DIGITS);

    StringView const prefix
        = spec.show_base_prefix ? base_prefix_for(spec.base, spec.uppercase) : ""_sv;
    usize const printed_length = digit_count + prefix.length();
    usize const padding
        = spec.min_width > printed_length ? spec.min_width - printed_length : 0;

    if (!spec.zero_pad)
        write_padding(' ', padding);
    write_string(prefix);
    if (spec.zero_pad)
        write_padding('0', padding);

    for (usize i = digit_count; i > 0; --i)
        write_char(digits[i - 1]);
}

void Formatter::write_signed(i64 value, FormatSpec const& spec) {
    if (value >= 0) {
        write_unsigned(static_cast<u64>(value), spec);
        return;
    }

    auto const magnitude = static_cast<u64>(-(value + 1)) + 1;
    FormatSpec adjusted = spec;
    if (adjusted.min_width > 0)
        --adjusted.min_width;

    if (adjusted.zero_pad) {
        write_char('-');
        write_unsigned(magnitude, adjusted);
        return;
    }

    usize digit_count = 1;
    for (u64 remaining = magnitude / static_cast<u64>(to_underlying(spec.base)); remaining != 0;
         remaining /= static_cast<u64>(to_underlying(spec.base)))
        ++digit_count;

    usize const padding = adjusted.min_width > digit_count ? adjusted.min_width - digit_count : 0;
    write_padding(' ', padding);
    write_char('-');
    adjusted.min_width = 0;
    write_unsigned(magnitude, adjusted);
}

void Formatter::write_pointer(void const* pointer) {
    FormatSpec spec {};
    spec.base = Base::Hex;
    spec.min_width = 16;
    spec.zero_pad = true;
    spec.show_base_prefix = true;
    write_unsigned(reinterpret_cast<u64>(pointer), spec);
}

void Formatter::write_bool(bool value) {
    write_string(value ? "true"_sv : "false"_sv);
}

void format_value(Formatter& formatter, FormatSpec const&, StringView text) {
    formatter.write_string(text);
}

void format_value(Formatter& formatter, FormatSpec const&, char const* text) {
    formatter.write_string(text != nullptr ? StringView(text) : "(null)"_sv);
}

void format_value(Formatter& formatter, FormatSpec const&, char character) {
    formatter.write_char(character);
}

void format_value(Formatter& formatter, FormatSpec const&, bool value) {
    formatter.write_bool(value);
}

void format_value(Formatter& formatter, FormatSpec const&, PhysAddr address) {
    FormatSpec spec {};
    spec.base = Base::Hex;
    spec.show_base_prefix = true;
    formatter.write_string("phys:"_sv);
    formatter.write_unsigned(address.raw(), spec);
}

void format_value(Formatter& formatter, FormatSpec const&, VirtAddr address) {
    FormatSpec spec {};
    spec.base = Base::Hex;
    spec.show_base_prefix = true;
    formatter.write_unsigned(address.raw(), spec);
}

void format_value(Formatter& formatter, FormatSpec const& spec, Errno code) {
    format_value(formatter, spec, errno_name(code));
}

void format_value(Formatter& formatter, FormatSpec const& spec, Error const& error) {
    format_value(formatter, spec, error.name());
    if (error.context() != nullptr) {
        formatter.write_string(" ("_sv);
        formatter.write_string(StringView(error.context()));
        formatter.write_char(')');
    }
}

Optional<FormatSpec> parse_format_spec(StringView body) {
    FormatSpec spec {};
    usize index = 0;

    if (index < body.length() && body[index] == '#') {
        spec.show_base_prefix = true;
        ++index;
    }

    if (index < body.length() && body[index] == '0') {
        spec.zero_pad = true;
        ++index;
    }

    u32 width = 0;
    while (index < body.length() && is_decimal_digit(body[index])) {
        width = width * 10 + static_cast<u32>(body[index] - '0');
        if (width > MAX_WIDTH)
            return nullopt;
        ++index;
    }
    spec.min_width = static_cast<u8>(width);

    if (index == body.length())
        return spec;

    switch (body[index]) {
    case 'x':
        spec.base = Base::Hex;
        break;
    case 'X':
        spec.base = Base::Hex;
        spec.uppercase = true;
        break;
    case 'b':
        spec.base = Base::Binary;
        break;
    case 'o':
        spec.base = Base::Octal;
        break;
    case 'd':
        spec.base = Base::Decimal;
        break;
    default:
        return nullopt;
    }
    ++index;

    return index == body.length() ? Optional<FormatSpec>(spec) : nullopt;
}

void vformat(FormatSink& sink, StringView format, FormatArgumentList const& arguments) {
    Formatter formatter(sink);
    usize next_argument = 0;
    usize index = 0;

    while (index < format.length()) {
        char const character = format[index];

        if (character == '{' && index + 1 < format.length() && format[index + 1] == '{') {
            formatter.write_char('{');
            index += 2;
            continue;
        }

        if (character == '}' && index + 1 < format.length() && format[index + 1] == '}') {
            formatter.write_char('}');
            index += 2;
            continue;
        }

        if (character != '{') {
            formatter.write_char(character);
            ++index;
            continue;
        }

        usize const body_start = index + 1;
        usize body_end = body_start;
        while (body_end < format.length() && format[body_end] != '}')
            ++body_end;

        if (body_end == format.length()) {
            formatter.write_string(format.substring(index));
            return;
        }

        StringView const body = format.substring(body_start, body_end - body_start);
        index = body_end + 1;

        if (next_argument >= arguments.count()) {
            formatter.write_string("{missing}"_sv);
            continue;
        }

        auto const spec = parse_format_spec(body);
        if (!spec.has_value()) {
            formatter.write_string("{badspec}"_sv);
            continue;
        }

        arguments.emit(next_argument++, formatter, spec.value());
    }
}

}  // namespace barry
