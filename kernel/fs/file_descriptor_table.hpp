#pragma once

#include "kernel/base/array.hpp"
#include "kernel/base/expected.hpp"
#include "kernel/base/ref_ptr.hpp"
#include "kernel/fs/open_file_description.hpp"

namespace barry::fs {

using FileDescriptor = i32;

class FileDescriptorTable {
public:
    static constexpr usize MAX_DESCRIPTORS = 64;

    KResultOr<FileDescriptor> insert(RefPtr<OpenFileDescription> description);
    KResult insert_at(FileDescriptor descriptor, RefPtr<OpenFileDescription> description);

    KResultOr<RefPtr<OpenFileDescription>> lookup(FileDescriptor descriptor) const;

    KResult close(FileDescriptor descriptor);

    usize open_count() const;

private:
    static bool is_in_range(FileDescriptor descriptor) {
        return descriptor >= 0 && static_cast<usize>(descriptor) < MAX_DESCRIPTORS;
    }

    Array<RefPtr<OpenFileDescription>, MAX_DESCRIPTORS> m_descriptions { {} };
};

}  // namespace barry::fs
