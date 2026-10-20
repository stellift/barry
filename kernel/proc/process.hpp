#pragma once

#include "kernel/arch/x86_64/user_mode.hpp"
#include "kernel/arch/x86_64/syscall.hpp"
#include "kernel/base/atomic.hpp"
#include "kernel/base/optional.hpp"
#include "kernel/base/expected.hpp"
#include "kernel/base/own_ptr.hpp"
#include "kernel/base/ref_ptr.hpp"
#include "kernel/base/string_view.hpp"
#include "kernel/fs/file_descriptor_table.hpp"
#include "kernel/proc/process_address_space.hpp"
#include "kernel/sync/spin_lock.hpp"

namespace barry::proc {

using ProcessId = u64;

enum class ProcessState : u8 {
    Running,
    Exited,
};

class Process : public RefCounted<Process> {
public:
    static KResultOr<ProcessId> spawn_from_image(StringView name, ReadonlyBytes image,
                                                 Span<StringView const> arguments,
                                                 Span<StringView const> environment);

    static KResultOr<ProcessId> fork_current(arch::SyscallFrame const& frame);

    struct PreparedImage {
        OwnPtr<ProcessAddressSpace> address_space;
        arch::UserContext context {};
    };

    KResultOr<PreparedImage> prepare_image(ReadonlyBytes image,
                                           Span<StringView const> arguments,
                                           Span<StringView const> environment);

    [[noreturn]] void commit_image(PreparedImage prepared);

    Process(ProcessId id, StringView name, OwnPtr<ProcessAddressSpace> address_space);

    Process(Process const&) = delete;
    Process& operator=(Process const&) = delete;

    ProcessId id() const { return m_id; }
    StringView name() const { return m_name; }

    ProcessAddressSpace& address_space() { return *m_address_space; }
    ProcessAddressSpace const& address_space() const { return *m_address_space; }

    fs::FileDescriptorTable& file_descriptors() { return m_file_descriptors; }

    arch::UserContext const& initial_context() const { return m_initial_context; }
    void set_initial_context(arch::UserContext const& context) { m_initial_context = context; }

    VirtAddr thread_pointer() const { return m_thread_pointer; }
    void set_thread_pointer(VirtAddr address) { m_thread_pointer = address; }

    ProcessState state() const {
        return static_cast<ProcessState>(m_state.load(Order::Acquire));
    }

    bool has_exited() const { return state() == ProcessState::Exited; }

    i32 exit_status() const { return m_exit_status.load(Order::Acquire); }

    void note_exit(i32 status);

    ProcessId parent_id() const { return m_parent_id; }
    void set_parent_id(ProcessId id) { m_parent_id = id; }

    bool is_reaped() const { return m_reaped.load(Order::Acquire); }
    bool try_mark_reaped() {
        bool expected = false;
        return m_reaped.compare_exchange_strong(expected, true);
    }

private:
    ProcessId m_id;
    StringView m_name;
    OwnPtr<ProcessAddressSpace> m_address_space;
    fs::FileDescriptorTable m_file_descriptors;
    arch::UserContext m_initial_context {};
    VirtAddr m_thread_pointer {};
    ProcessId m_parent_id { 0 };
    Atomic<u8> m_state { to_underlying(ProcessState::Running) };
    Atomic<i32> m_exit_status { 0 };
    Atomic<bool> m_reaped { false };
};

struct ReapedChild {
    ProcessId id { 0 };
    i32 exit_status { 0 };
};

class ProcessTable {
public:
    static constexpr usize MAX_PROCESSES = 64;

    KResult adopt(RefPtr<Process> process);
    RefPtr<Process> find(ProcessId id) const;
    usize count() const;

    Optional<ReapedChild> reap_exited_child(ProcessId parent, i64 requested_child);
    bool has_unreaped_children(ProcessId parent) const;

private:
    struct State {
        Array<Process*, MAX_PROCESSES> processes { {} };
        usize count { 0 };
    };

    mutable sync::Protected<State> m_state;
};

ProcessTable& process_table();

Process* current_process();

}  // namespace barry::proc
