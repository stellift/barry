#pragma once

#include "kernel/base/format.hpp"
#include "kernel/base/string_view.hpp"

namespace barry {

class LogSink {
public:
    virtual void write(StringView text) = 0;

protected:
    ~LogSink() = default;
};

bool register_log_sink(LogSink& sink);
void log_write(StringView text);

namespace detail {

class GlobalLogSink final : public FormatSink {
public:
    void write(StringView text) override { log_write(text); }
};

}  // namespace detail

template<typename... Args>
void dbg(StringView format, Args const&... args) {
    detail::GlobalLogSink sink;
    format_to(sink, format, args...);
}

template<typename... Args>
void dbgln(StringView format, Args const&... args) {
    detail::GlobalLogSink sink;
    format_to(sink, format, args...);
    sink.write("\n"_sv);
}

inline void dbgln() {
    log_write("\n"_sv);
}

}  // namespace barry
