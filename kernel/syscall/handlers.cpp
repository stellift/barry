#include "kernel/syscall/handlers.hpp"

#include "kernel/arch/x86_64/cpu.hpp"
#include "kernel/base/array.hpp"
#include "kernel/base/utility.hpp"
#include "kernel/fs/inode_file_description.hpp"
#include "kernel/fs/virtual_file_system.hpp"
#include "kernel/mm/user_access.hpp"
#include "kernel/proc/process.hpp"
#include "kernel/sched/scheduler.hpp"
#include "kernel/syscall/dispatcher.hpp"

namespace barry::syscall {
namespace {

constexpr usize TRANSFER_BUFFER_SIZE = 1024;
constexpr usize MAX_IOVEC_COUNT = 16;

constexpr u64 PROTECTION_READ = 1;
constexpr u64 PROTECTION_WRITE = 2;
constexpr u64 PROTECTION_EXECUTE = 4;

constexpr u64 MAP_PRIVATE = 0x02;
constexpr u64 MAP_ANONYMOUS = 0x20;

constexpr u64 ARCH_SET_FS = 0x1002;
constexpr u64 ARCH_GET_FS = 0x1003;

constexpr i64 NO_FILE_DESCRIPTOR = -1;

struct [[gnu::packed]] UserIoVector {
    u64 base;
    u64 length;
};

struct [[gnu::packed]] UserStat {
    u64 device;
    u64 inode;
    u64 link_count;
    u32 mode;
    u32 user_id;
    u32 group_id;
    u32 padding;
    u64 raw_device;
    i64 size;
    i64 block_size;
    i64 block_count;
    i64 access_seconds;
    i64 access_nanoseconds;
    i64 modify_seconds;
    i64 modify_nanoseconds;
    i64 change_seconds;
    i64 change_nanoseconds;
    i64 reserved[3];
};

static_assert(sizeof(UserStat) == 144);

constexpr usize MAX_PATH_BYTES = 256;
constexpr usize MAX_ARGUMENT_COUNT = 16;
constexpr usize MAX_ARGUMENT_BYTES = 128;
constexpr i64 CURRENT_DIRECTORY_DESCRIPTOR = -100;
constexpr i64 DEFAULT_BLOCK_SIZE = 512;

constexpr u64 OPEN_WRITE_ONLY = 1;
constexpr u64 OPEN_READ_WRITE = 2;
constexpr u64 OPEN_CREATE = 0100;
constexpr u64 OPEN_TRUNCATE = 01000;

struct UserStringList {
    Array<Array<char, MAX_ARGUMENT_BYTES>, MAX_ARGUMENT_COUNT> storage { {} };
    Array<StringView, MAX_ARGUMENT_COUNT> views { {} };
    usize count { 0 };

    Span<StringView const> span() const {
        return Span<StringView const>(views.data(), count);
    }
};

KResultOr<StringView> copy_user_path(mm::AddressSpace const& tables, Span<char> buffer,
                                     u64 address) {
    if (address == 0)
        return Error(Errno::EFAULT, "the path pointer is null");

    usize const length
        = TRY(mm::copy_cstring_from_user(tables, buffer, UserPtr<char const>(address)));
    return StringView(buffer.data(), length);
}

KResult copy_user_string_list(mm::AddressSpace const& tables, UserStringList& list,
                              u64 vector_address) {
    list.count = 0;
    if (vector_address == 0)
        return {};

    for (usize index = 0; index < MAX_ARGUMENT_COUNT; ++index) {
        UserPtr<u64 const> const slot(VirtAddr(vector_address + index * sizeof(u64)));
        u64 const pointer = TRY(mm::read_user_object(tables, slot));

        if (pointer == 0)
            return {};

        list.views[index] = TRY(copy_user_path(tables, list.storage[index].span(), pointer));
        list.count = index + 1;
    }

    return Error(Errno::E2BIG, "too many arguments");
}

KResultOr<u64> open_resolved_path(proc::Process& process, StringView path, u64 flags,
                                  u64 permissions) {
    auto existing = fs::resolve_path(path);

    RefPtr<fs::Inode> inode;
    if (existing.has_value()) {
        inode = existing.release_value();
        if (inode->is_file() && (flags & OPEN_TRUNCATE) != 0)
            TRY(inode->replace_contents(ReadonlyBytes {}));
    } else if ((flags & OPEN_CREATE) != 0) {
        inode = TRY(fs::create_file(path, static_cast<u32>(permissions)));
    } else {
        return Unexpected(existing.release_error());
    }

    if (inode->is_directory() && (flags & (OPEN_WRITE_ONLY | OPEN_READ_WRITE)) != 0)
        return Error(Errno::EISDIR, "a directory cannot be opened for writing");

    auto description = TRY(fs::InodeFileDescription::create(inode));
    auto const descriptor = TRY(process.file_descriptors().insert(move(description)));
    return static_cast<u64>(descriptor);
}

void fill_stat(UserStat& target, fs::Inode const& inode) {
    target = UserStat {};
    target.inode = inode.identifier();
    target.link_count = 1;
    target.mode = inode.mode();
    target.size = static_cast<i64>(inode.size());
    target.block_size = DEFAULT_BLOCK_SIZE;
    target.block_count
        = static_cast<i64>(div_ceil<u64>(inode.size(), static_cast<u64>(DEFAULT_BLOCK_SIZE)));
}

KResultOr<proc::Process*> require_current_process() {
    auto* process = proc::current_process();
    if (process == nullptr)
        return Error(Errno::EINVAL, "this system call needs a user process");
    return process;
}

mm::AddressSpace const& current_page_tables(proc::Process& process) {
    return process.address_space().page_tables();
}

mm::PageFlags page_flags_for_protection(u64 protection) {
    mm::PageFlags flags = mm::PageFlags::Present | mm::PageFlags::UserAccessible;
    if ((protection & PROTECTION_WRITE) != 0)
        flags |= mm::PageFlags::Writable;
    if ((protection & PROTECTION_EXECUTE) == 0)
        flags |= mm::PageFlags::NoExecute;
    return flags;
}

KResultOr<u64> write_from_user_buffer(proc::Process& process, fs::FileDescriptor descriptor,
                                      UserPtr<u8 const> source, u64 length) {
    auto description = TRY(process.file_descriptors().lookup(descriptor));

    Array<u8, TRANSFER_BUFFER_SIZE> buffer { {} };
    u64 written = 0;

    while (written < length) {
        u64 const chunk = min<u64>(TRANSFER_BUFFER_SIZE, length - written);
        TRY(mm::copy_from_user(current_page_tables(process), buffer.span().first(chunk),
                               source.offset(written)));

        usize const accepted = TRY(description->write(buffer.span().first(chunk)));
        written += accepted;

        if (accepted < chunk)
            break;
    }

    return written;
}

KResultOr<u64> handle_write(Arguments const& arguments) {
    auto& process = *TRY(require_current_process());
    return write_from_user_buffer(process, static_cast<fs::FileDescriptor>(arguments.first),
                                  UserPtr<u8 const>(arguments.second), arguments.third);
}

KResultOr<u64> handle_writev(Arguments const& arguments) {
    auto& process = *TRY(require_current_process());
    auto const descriptor = static_cast<fs::FileDescriptor>(arguments.first);

    u64 const count = arguments.third;
    if (count > MAX_IOVEC_COUNT)
        return Error(Errno::EINVAL, "too many scatter-gather entries");

    u64 written = 0;
    for (u64 index = 0; index < count; ++index) {
        UserPtr<UserIoVector const> const entry(
            VirtAddr(arguments.second + index * sizeof(UserIoVector)));
        auto const vector = TRY(mm::read_user_object(current_page_tables(process), entry));

        if (vector.length == 0)
            continue;

        written += TRY(write_from_user_buffer(process, descriptor,
                                              UserPtr<u8 const>(vector.base), vector.length));
    }

    return written;
}

KResultOr<u64> handle_read(Arguments const& arguments) {
    auto& process = *TRY(require_current_process());
    auto description
        = TRY(process.file_descriptors().lookup(static_cast<fs::FileDescriptor>(arguments.first)));

    u64 const length = min<u64>(arguments.third, TRANSFER_BUFFER_SIZE);
    if (length == 0)
        return u64 { 0 };

    Array<u8, TRANSFER_BUFFER_SIZE> buffer { {} };
    usize const filled = TRY(description->read(buffer.span().first(length)));

    TRY(mm::copy_to_user(current_page_tables(process), UserPtr<u8>(arguments.second),
                         buffer.span().first(filled)));
    return filled;
}

KResultOr<u64> handle_close(Arguments const& arguments) {
    auto& process = *TRY(require_current_process());
    TRY(process.file_descriptors().close(static_cast<fs::FileDescriptor>(arguments.first)));
    return u64 { 0 };
}

KResultOr<u64> handle_brk(Arguments const& arguments) {
    auto& process = *TRY(require_current_process());
    auto const result = TRY(process.address_space().move_program_break(VirtAddr(arguments.first)));
    return result.raw();
}

KResultOr<u64> handle_mmap(Arguments const& arguments) {
    auto& process = *TRY(require_current_process());

    u64 const length = arguments.second;
    u64 const protection = arguments.third;
    u64 const flags = arguments.fourth;
    auto const descriptor = static_cast<i64>(arguments.fifth);

    if (length == 0)
        return Error(Errno::EINVAL, "mmap needs a positive length");
    if ((flags & MAP_ANONYMOUS) == 0 || descriptor != NO_FILE_DESCRIPTOR)
        return Error(Errno::ENOSYS, "only anonymous mappings are supported");
    if ((flags & MAP_PRIVATE) == 0)
        return Error(Errno::ENOSYS, "only private mappings are supported");
    if ((protection & ~(PROTECTION_READ | PROTECTION_WRITE | PROTECTION_EXECUTE)) != 0)
        return Error(Errno::EINVAL, "unsupported protection bits");

    auto const base = TRY(process.address_space().reserve_mapping(
        length, page_flags_for_protection(protection)));
    return base.raw();
}

KResultOr<u64> handle_munmap(Arguments const& arguments) {
    auto& process = *TRY(require_current_process());

    VirtAddr const base { arguments.first };
    u64 const length = align_up<u64>(arguments.second, PAGE_SIZE);

    if (!base.is_page_aligned())
        return Error(Errno::EINVAL, "munmap needs a page aligned address");

    TRY(process.address_space().unmap(VirtRange { base, length }));
    return u64 { 0 };
}

KResultOr<u64> handle_mprotect(Arguments const&) {
    return u64 { 0 };
}

KResultOr<u64> handle_ioctl(Arguments const&) {
    return Error(Errno::ENOTTY, "no ioctl is implemented yet");
}

KResultOr<u64> handle_fstat(Arguments const& arguments) {
    auto& process = *TRY(require_current_process());
    auto description
        = TRY(process.file_descriptors().lookup(static_cast<fs::FileDescriptor>(arguments.first)));

    auto const inode = description->inode();
    if (inode.is_null())
        return Error(Errno::ENOSYS, "this descriptor has no inode to describe");

    UserStat target {};
    fill_stat(target, *inode);
    TRY(mm::write_user_object(current_page_tables(process), UserPtr<UserStat>(arguments.second),
                              target));
    return u64 { 0 };
}

KResultOr<u64> handle_stat(Arguments const& arguments) {
    auto& process = *TRY(require_current_process());
    auto const& tables = current_page_tables(process);

    Array<char, MAX_PATH_BYTES> path_buffer { {} };
    auto const path = TRY(copy_user_path(tables, path_buffer.span(), arguments.first));
    auto const inode = TRY(fs::resolve_path(path));

    UserStat target {};
    fill_stat(target, *inode);
    TRY(mm::write_user_object(tables, UserPtr<UserStat>(arguments.second), target));
    return u64 { 0 };
}

KResultOr<u64> handle_open(Arguments const& arguments) {
    auto& process = *TRY(require_current_process());
    auto const& tables = current_page_tables(process);

    Array<char, MAX_PATH_BYTES> path_buffer { {} };
    auto const path = TRY(copy_user_path(tables, path_buffer.span(), arguments.first));
    return open_resolved_path(process, path, arguments.second, arguments.third);
}

KResultOr<u64> handle_openat(Arguments const& arguments) {
    auto const directory = static_cast<i64>(arguments.first);
    if (directory != CURRENT_DIRECTORY_DESCRIPTOR)
        return Error(Errno::ENOSYS, "only the current directory is supported");

    auto& process = *TRY(require_current_process());
    auto const& tables = current_page_tables(process);

    Array<char, MAX_PATH_BYTES> path_buffer { {} };
    auto const path = TRY(copy_user_path(tables, path_buffer.span(), arguments.second));
    return open_resolved_path(process, path, arguments.third, arguments.fourth);
}

KResultOr<u64> handle_lseek(Arguments const& arguments) {
    auto& process = *TRY(require_current_process());
    auto description
        = TRY(process.file_descriptors().lookup(static_cast<fs::FileDescriptor>(arguments.first)));

    if (arguments.third > to_underlying(fs::SeekOrigin::End))
        return Error(Errno::EINVAL, "unsupported seek origin");

    return TRY(description->seek(static_cast<i64>(arguments.second),
                                 static_cast<fs::SeekOrigin>(arguments.third)));
}

KResultOr<u64> handle_execve(Arguments const& arguments) {
    auto& process = *TRY(require_current_process());
    auto const& tables = current_page_tables(process);

    Array<char, MAX_PATH_BYTES> path_buffer { {} };
    auto const path = TRY(copy_user_path(tables, path_buffer.span(), arguments.first));

    UserStringList program_arguments;
    UserStringList environment;
    TRY(copy_user_string_list(tables, program_arguments, arguments.second));
    TRY(copy_user_string_list(tables, environment, arguments.third));

    auto const inode = TRY(fs::resolve_path(path));
    if (!inode->is_file())
        return Error(Errno::EACCES, "only regular files can be executed");

    Optional<proc::Process::PreparedImage> prepared;
    {
        auto const contents = TRY(inode->read_entire_file());
        prepared = TRY(process.prepare_image(contents.bytes(), program_arguments.span(),
                                             environment.span()));
    }

    process.commit_image(prepared.release_value());
}

KResultOr<u64> handle_get_process_id(Arguments const&) {
    auto& process = *TRY(require_current_process());
    return process.id();
}

KResultOr<u64> handle_zero_identity(Arguments const&) {
    return u64 { 0 };
}

KResultOr<u64> handle_set_thread_id_address(Arguments const&) {
    auto* thread = sched::scheduler().current_thread();
    if (thread == nullptr)
        return Error(Errno::EINVAL, "no current thread");
    return thread->id();
}

KResultOr<u64> handle_arch_prctl(Arguments const& arguments) {
    auto& process = *TRY(require_current_process());
    auto* thread = sched::scheduler().current_thread();
    if (thread == nullptr)
        return Error(Errno::EINVAL, "no current thread");

    switch (arguments.first) {
    case ARCH_SET_FS: {
        VirtAddr const address { arguments.second };
        if (!proc::is_user_range(VirtRange { address, 1 }) && !address.is_null())
            return Error(Errno::EFAULT, "the thread pointer is outside user space");
        process.set_thread_pointer(address);
        thread->set_thread_pointer(address);
        arch::write_msr(arch::MSR_FS_BASE, address.raw());
        return u64 { 0 };
    }
    case ARCH_GET_FS:
        return process.thread_pointer().raw();
    default:
        return Error(Errno::EINVAL, "unsupported arch_prctl request");
    }
}

KResultOr<u64> handle_fork(Arguments const&) {
    auto* thread = sched::scheduler().current_thread();
    if (thread == nullptr || thread->syscall_frame() == nullptr)
        return Error(Errno::EINVAL, "fork needs a user system call frame");

    return TRY(proc::Process::fork_current(*thread->syscall_frame()));
}

KResultOr<u64> handle_wait4(Arguments const& arguments) {
    auto& process = *TRY(require_current_process());
    auto const requested = static_cast<i64>(arguments.first);

    for (;;) {
        auto const reaped = proc::process_table().reap_exited_child(process.id(), requested);

        if (reaped.has_value()) {
            if (arguments.second != 0) {
                i32 const status = (reaped->exit_status & 0xff) << 8;
                TRY(mm::write_user_object(current_page_tables(process),
                                          UserPtr<i32>(arguments.second), status));
            }
            return reaped->id;
        }

        if (!proc::process_table().has_unreaped_children(process.id()))
            return Error(Errno::ECHILD, "the process has no children left to wait for");

        sched::scheduler().yield();
    }
}

[[noreturn]] void terminate_current_process(i32 status) {
    if (auto* process = proc::current_process())
        process->note_exit(status);
    sched::scheduler().exit_current_thread();
}

KResultOr<u64> handle_exit(Arguments const& arguments) {
    terminate_current_process(static_cast<i32>(arguments.first & 0xff));
}

}  // namespace

KResult install_default_handlers() {
    TRY(register_handler(Number::Read, &handle_read));
    TRY(register_handler(Number::Write, &handle_write));
    TRY(register_handler(Number::Close, &handle_close));
    TRY(register_handler(Number::Open, &handle_open));
    TRY(register_handler(Number::Openat, &handle_openat));
    TRY(register_handler(Number::Stat, &handle_stat));
    TRY(register_handler(Number::Fstat, &handle_fstat));
    TRY(register_handler(Number::Lseek, &handle_lseek));
    TRY(register_handler(Number::Execve, &handle_execve));
    TRY(register_handler(Number::Mmap, &handle_mmap));
    TRY(register_handler(Number::Mprotect, &handle_mprotect));
    TRY(register_handler(Number::Munmap, &handle_munmap));
    TRY(register_handler(Number::Brk, &handle_brk));
    TRY(register_handler(Number::Ioctl, &handle_ioctl));
    TRY(register_handler(Number::Writev, &handle_writev));
    TRY(register_handler(Number::GetPid, &handle_get_process_id));
    TRY(register_handler(Number::Fork, &handle_fork));
    TRY(register_handler(Number::Wait4, &handle_wait4));
    TRY(register_handler(Number::Exit, &handle_exit));
    TRY(register_handler(Number::GetUid, &handle_zero_identity));
    TRY(register_handler(Number::GetGid, &handle_zero_identity));
    TRY(register_handler(Number::GetEuid, &handle_zero_identity));
    TRY(register_handler(Number::GetEgid, &handle_zero_identity));
    TRY(register_handler(Number::ArchPrctl, &handle_arch_prctl));
    TRY(register_handler(Number::SetThreadIdAddress, &handle_set_thread_id_address));
    TRY(register_handler(Number::ExitGroup, &handle_exit));
    return {};
}

}  // namespace barry::syscall
