#include "kernel/proc/process.hpp"

#include "kernel/arch/x86_64/cpu.hpp"
#include "kernel/base/assert.hpp"
#include "kernel/fs/console.hpp"
#include "kernel/log.hpp"
#include "kernel/proc/elf.hpp"
#include "kernel/proc/user_stack.hpp"
#include "kernel/sched/scheduler.hpp"

namespace barry::proc {
namespace {

ProcessTable g_process_table;
Atomic<ProcessId> g_next_process_id { 1 };

constexpr fs::FileDescriptor STANDARD_INPUT = 0;
constexpr fs::FileDescriptor STANDARD_OUTPUT = 1;
constexpr fs::FileDescriptor STANDARD_ERROR = 2;

KResult install_standard_descriptors(Process& process) {
    auto& table = process.file_descriptors();
    TRY(table.insert_at(STANDARD_INPUT, fs::console_description()));
    TRY(table.insert_at(STANDARD_OUTPUT, fs::console_description()));
    TRY(table.insert_at(STANDARD_ERROR, fs::console_description()));
    return {};
}

void user_thread_entry(void* argument) {
    auto* process = static_cast<Process*>(argument);
    VERIFY(process != nullptr);

    if (auto* thread = sched::scheduler().current_thread()) {
        thread->set_thread_pointer(process->thread_pointer());
        arch::write_msr(arch::MSR_FS_BASE, process->thread_pointer().raw());
    }

    arch::UserContext context = process->initial_context();
    barry_enter_user_mode(&context);
}

bool matches_requested_child(Process const& child, ProcessId parent, i64 requested) {
    if (child.parent_id() != parent)
        return false;
    if (requested <= 0)
        return true;
    return child.id() == static_cast<ProcessId>(requested);
}

}  // namespace

Process::Process(ProcessId id, StringView name, OwnPtr<ProcessAddressSpace> address_space)
    : m_id(id), m_name(name), m_address_space(move(address_space)) {}

void Process::note_exit(i32 status) {
    m_exit_status.store(status, Order::Release);
    m_state.store(to_underlying(ProcessState::Exited), Order::Release);
}

KResultOr<ProcessId> Process::spawn_from_image(StringView name, ReadonlyBytes image,
                                               Span<StringView const> arguments,
                                               Span<StringView const> environment) {
    auto address_space = TRY(ProcessAddressSpace::create());

    auto const loaded = TRY(load_static_executable(*address_space, image));
    auto const stack = TRY(build_initial_user_stack(*address_space, loaded, arguments,
                                                    environment));

    address_space->set_program_break_start(loaded.highest_address);

    ProcessId const id = g_next_process_id.fetch_add(1, Order::Relaxed);
    PhysAddr const root = address_space->root();

    auto created = TRY(make_ref<Process>(id, name, move(address_space)));
    created->set_initial_context(
        arch::UserContext::for_fresh_process(loaded.entry_point, stack.stack_pointer));

    TRY(install_standard_descriptors(*created));
    TRY(g_process_table.adopt(created));

    TRY(sched::scheduler().spawn_user_thread(name, &user_thread_entry, created.get(), root));
    return id;
}

KResultOr<Process::PreparedImage> Process::prepare_image(ReadonlyBytes image,
                                                         Span<StringView const> arguments,
                                                         Span<StringView const> environment) {
    auto address_space = TRY(ProcessAddressSpace::create());

    auto const loaded = TRY(load_static_executable(*address_space, image));
    auto const stack = TRY(build_initial_user_stack(*address_space, loaded, arguments,
                                                    environment));
    address_space->set_program_break_start(loaded.highest_address);

    PreparedImage prepared {};
    prepared.address_space = move(address_space);
    prepared.context
        = arch::UserContext::for_fresh_process(loaded.entry_point, stack.stack_pointer);
    return prepared;
}

void Process::commit_image(PreparedImage prepared) {
    VERIFY(!prepared.address_space.is_null());

    PhysAddr const root = prepared.address_space->root();

    arch::write_cr3(root);

    m_address_space = move(prepared.address_space);
    m_initial_context = prepared.context;
    m_thread_pointer = VirtAddr {};

    if (auto* thread = sched::scheduler().current_thread()) {
        thread->set_address_space_root(root);
        thread->set_thread_pointer(VirtAddr {});
    }
    arch::write_msr(arch::MSR_FS_BASE, 0);

    barry_enter_user_mode(&m_initial_context);
}

KResultOr<ProcessId> Process::fork_current(arch::SyscallFrame const& frame) {
    auto* parent = current_process();
    if (parent == nullptr)
        return Error(Errno::EINVAL, "fork needs a calling user process");

    auto address_space = TRY(parent->address_space().clone());
    PhysAddr const root = address_space->root();
    ProcessId const id = g_next_process_id.fetch_add(1, Order::Relaxed);

    auto child = TRY(make_ref<Process>(id, parent->name(), move(address_space)));
    child->set_parent_id(parent->id());
    child->set_thread_pointer(parent->thread_pointer());
    child->file_descriptors() = parent->file_descriptors();

    arch::UserContext context = frame.to_user_context();
    context.rax = 0;
    child->set_initial_context(context);

    TRY(g_process_table.adopt(child));
    TRY(sched::scheduler().spawn_user_thread(child->name(), &user_thread_entry, child.get(),
                                             root));
    return id;
}

Optional<ReapedChild> ProcessTable::reap_exited_child(ProcessId parent, i64 requested_child) {
    auto guard = m_state.lock();

    for (usize index = 0; index < guard->count; ++index) {
        Process* const candidate = guard->processes[index];
        if (candidate == nullptr || !candidate->has_exited())
            continue;
        if (!matches_requested_child(*candidate, parent, requested_child))
            continue;
        if (!candidate->try_mark_reaped())
            continue;

        return ReapedChild { candidate->id(), candidate->exit_status() };
    }

    return nullopt;
}

bool ProcessTable::has_unreaped_children(ProcessId parent) const {
    auto guard = m_state.lock();

    for (usize index = 0; index < guard->count; ++index) {
        Process* const candidate = guard->processes[index];
        if (candidate == nullptr || candidate->parent_id() != parent)
            continue;
        if (!candidate->is_reaped())
            return true;
    }

    return false;
}

KResult ProcessTable::adopt(RefPtr<Process> process) {
    if (process.is_null())
        return Error(Errno::EINVAL, "cannot adopt a null process");

    auto guard = m_state.lock();
    if (guard->count >= MAX_PROCESSES)
        return Error(Errno::EAGAIN, "the process table is full");

    guard->processes[guard->count++] = process.leak();
    return {};
}

RefPtr<Process> ProcessTable::find(ProcessId id) const {
    auto guard = m_state.lock();

    for (usize index = 0; index < guard->count; ++index) {
        Process* const candidate = guard->processes[index];
        if (candidate != nullptr && candidate->id() == id)
            return RefPtr<Process>(*candidate);
    }

    return nullptr;
}

usize ProcessTable::count() const {
    auto guard = m_state.lock();
    return guard->count;
}

ProcessTable& process_table() {
    return g_process_table;
}

Process* current_process() {
    auto* thread = sched::scheduler().current_thread();
    if (thread == nullptr)
        return nullptr;
    return static_cast<Process*>(thread->process());
}

}  // namespace barry::proc
