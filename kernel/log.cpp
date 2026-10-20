#include "kernel/log.hpp"

#include "kernel/dev/serial.hpp"
#include "kernel/base/array.hpp"

namespace barry {
namespace {

constexpr usize MAX_LOG_SINKS = 4;

Array<LogSink*, MAX_LOG_SINKS> g_sinks { {} };
usize g_sink_count = 0;

}  // namespace

bool register_log_sink(LogSink& sink) {
    if (g_sink_count >= MAX_LOG_SINKS)
        return false;
    g_sinks[g_sink_count++] = &sink;
    return true;
}

void log_write(StringView text) {
    dev::debug_port().write(text);
    for (usize i = 0; i < g_sink_count; ++i)
        g_sinks[i]->write(text);
}

}  // namespace barry
