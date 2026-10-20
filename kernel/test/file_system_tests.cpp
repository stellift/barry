#include "kernel/base/array.hpp"
#include "kernel/base/bytes.hpp"
#include "kernel/boot/initramfs.hpp"
#include "kernel/fs/console.hpp"
#include "kernel/fs/inode_file_description.hpp"
#include "kernel/fs/virtual_file_system.hpp"
#include "kernel/proc/embedded_image.hpp"
#include "kernel/test/framework.hpp"

namespace barry {
namespace {

constexpr StringView PROGRAM_PATH = "/bin/hello"_sv;
constexpr StringView SCRATCH_PATH = "/tmp/notes.txt"_sv;
constexpr StringView SCRATCH_TEXT = "barry writes files"_sv;

}  // namespace

KERNEL_TEST(the_initramfs_populates_the_root_filesystem) {
    auto const root = fs::root_directory();
    EXPECT(!root.is_null());
    EXPECT(root->is_directory());
    EXPECT(root->entry_count() > 0);

    auto const directory = fs::resolve_path("/bin"_sv);
    EXPECT(directory.has_value());
    EXPECT(directory.value()->is_directory());

    auto const program = fs::resolve_path(PROGRAM_PATH);
    EXPECT(program.has_value());
    EXPECT(program.value()->is_file());
    EXPECT_EQ(program.value()->size(), proc::embedded_user_image().size());
}

KERNEL_TEST(path_resolution_rejects_malformed_and_missing_paths) {
    EXPECT_ERROR(fs::resolve_path("relative/path"_sv));
    EXPECT_ERROR(fs::resolve_path(""_sv));
    EXPECT_ERROR(fs::resolve_path("/does/not/exist"_sv));
    EXPECT_ERROR(fs::resolve_path("/bin/hello/inside-a-file"_sv));
}

KERNEL_TEST(files_round_trip_their_contents) {
    auto const file = TRY_OR_FAIL(fs::create_file(SCRATCH_PATH, 0644));
    EXPECT(file->is_file());
    EXPECT_EQ(file->size(), 0u);

    EXPECT_OK(file->replace_contents(SCRATCH_TEXT.bytes()));
    EXPECT_EQ(file->size(), SCRATCH_TEXT.length());

    Array<u8, 64> buffer { {} };
    auto const count = file->read_at(0, buffer.span());
    EXPECT(count.has_value());
    EXPECT_EQ(count.value(), SCRATCH_TEXT.length());
    EXPECT(bytes_equal(buffer.span().first(SCRATCH_TEXT.length()), SCRATCH_TEXT.bytes()));

    EXPECT_ERROR(fs::create_file(SCRATCH_PATH, 0644));

    auto const resolved = fs::resolve_path(SCRATCH_PATH);
    EXPECT(resolved.has_value());
    EXPECT_EQ(resolved.value()->identifier(), file->identifier());
}

KERNEL_TEST(file_descriptions_track_their_offset) {
    auto const file = TRY_OR_FAIL(fs::create_file("/tmp/offsets.bin"_sv, 0644));
    EXPECT_OK(file->replace_contents(SCRATCH_TEXT.bytes()));

    auto const description = TRY_OR_FAIL(fs::InodeFileDescription::create(file));

    Array<u8, 6> head { {} };
    auto const first = description->read(head.span());
    EXPECT(first.has_value());
    EXPECT_EQ(first.value(), head.span().size());
    EXPECT(bytes_equal(head.span(), SCRATCH_TEXT.bytes().first(head.span().size())));

    auto const position = description->seek(0, fs::SeekOrigin::Current);
    EXPECT(position.has_value());
    EXPECT_EQ(position.value(), head.span().size());

    auto const end = description->seek(0, fs::SeekOrigin::End);
    EXPECT(end.has_value());
    EXPECT_EQ(end.value(), SCRATCH_TEXT.length());

    EXPECT_ERROR(description->seek(-1, fs::SeekOrigin::Start));
}

KERNEL_TEST(the_console_reports_when_no_input_is_waiting) {
    Array<u8, 8> buffer { {} };
    auto const result = fs::console().read(buffer.span());

    if (result.is_error())
        EXPECT(result.error() == Errno::EAGAIN);
    else
        EXPECT(result.value() > 0);

    EXPECT(fs::console().bytes_written() > 0);
    EXPECT(fs::console().is_terminal());
}

KERNEL_TEST(a_malformed_cpio_archive_is_rejected) {
    Array<u8, 64> garbage { {} };
    EXPECT_ERROR(boot::populate_from_cpio_archive(garbage.span()));
    EXPECT_ERROR(boot::populate_from_cpio_archive(ReadonlyBytes {}));
}

}  // namespace barry
