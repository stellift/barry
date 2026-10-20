#pragma once

#include "kernel/base/addr.hpp"
#include "kernel/base/error.hpp"
#include "kernel/base/span.hpp"
#include "kernel/base/string_view.hpp"
#include "kernel/base/traits.hpp"
#include "kernel/base/types.hpp"

namespace barry {

class FormatSink {
public:
    virtual void write(StringView text) = 0;

protected:
    ~FormatSink() = default;
};

enum class Base : u8 {
    Decimal = 10,
    Hex = 16,
    Binary = 2,
    Octal = 8,
};

struct FormatSpec {
    Base base { Base::Decimal };
    u8 min_width { 0 };
    bool zero_pad { false };
    bool show_base_prefix { false };
    bool uppercase { false };
};

class Formatter {
public:
    explicit Formatter(FormatSink& sink) : m_sink(sink) {}

    void write_string(StringView text) { m_sink.write(text); }
    void write_char(char character) { m_sink.write(StringView(&character, 1)); }
    void write_padding(char fill, usize count);
    void write_unsigned(u64 value, FormatSpec const& spec);
    void write_signed(i64 value, FormatSpec const& spec);
    void write_pointer(void const* pointer);
    void write_bool(bool value);

private:
    FormatSink& m_sink;
};

void format_value(Formatter&, FormatSpec const&, StringView);
void format_value(Formatter&, FormatSpec const&, char const*);
void format_value(Formatter&, FormatSpec const&, char);
void format_value(Formatter&, FormatSpec const&, bool);
void format_value(Formatter&, FormatSpec const&, PhysAddr);
void format_value(Formatter&, FormatSpec const&, VirtAddr);
void format_value(Formatter&, FormatSpec const&, Error const&);
void format_value(Formatter&, FormatSpec const&, Errno);

template<typename T>
    requires(is_pointer_v<T> && !is_same_v<T, char const*> && !is_same_v<T, char*>)
void format_value(Formatter& formatter, FormatSpec const&, T pointer) {
    formatter.write_pointer(static_cast<void const*>(pointer));
}

template<UnsignedIntegral T>
    requires(!is_same_v<T, bool> && !is_same_v<T, char>)
void format_value(Formatter& formatter, FormatSpec const& spec, T value) {
    formatter.write_unsigned(static_cast<u64>(value), spec);
}

template<SignedIntegral T>
    requires(!is_same_v<T, char>)
void format_value(Formatter& formatter, FormatSpec const& spec, T value) {
    formatter.write_signed(static_cast<i64>(value), spec);
}

template<Enum T>
void format_value(Formatter& formatter, FormatSpec const& spec, T value) {
    format_value(formatter, spec, to_underlying(value));
}

template<typename T>
void format_value(Formatter& formatter, FormatSpec const& spec, UserPtr<T> pointer) {
    FormatSpec hex_spec = spec;
    hex_spec.base = Base::Hex;
    hex_spec.show_base_prefix = true;
    formatter.write_string("user:"_sv);
    formatter.write_unsigned(pointer.address().raw(), hex_spec);
}

Optional<FormatSpec> parse_format_spec(StringView body);

class FormatArgumentList {
public:
    using Emitter = void (*)(Formatter&, FormatSpec const&, void const*);

    struct Argument {
        void const* value { nullptr };
        Emitter emit { nullptr };
    };

    constexpr FormatArgumentList() = default;
    constexpr FormatArgumentList(Argument const* arguments, usize count)
        : m_arguments(arguments), m_count(count) {}

    constexpr usize count() const { return m_count; }

    void emit(usize index, Formatter& formatter, FormatSpec const& spec) const {
        VERIFY(index < m_count);
        m_arguments[index].emit(formatter, spec, m_arguments[index].value);
    }

private:
    Argument const* m_arguments { nullptr };
    usize m_count { 0 };
};

template<typename T>
void emit_argument(Formatter& formatter, FormatSpec const& spec, void const* value) {
    format_value(formatter, spec, *static_cast<T const*>(value));
}

void vformat(FormatSink& sink, StringView format, FormatArgumentList const& arguments);

template<typename... Args>
void format_to(FormatSink& sink, StringView format, Args const&... args) {
    if constexpr (sizeof...(Args) == 0) {
        vformat(sink, format, {});
    } else {
        FormatArgumentList::Argument const storage[] = {
            FormatArgumentList::Argument { &args, &emit_argument<Args> }...
        };
        vformat(sink, format, FormatArgumentList(storage, sizeof...(Args)));
    }
}

}  // namespace barry
