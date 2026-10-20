#pragma once

#include "kernel/base/expected.hpp"
#include "kernel/base/ref_ptr.hpp"
#include "kernel/base/span.hpp"
#include "kernel/base/types.hpp"

namespace barry::fs {

class Inode;

enum class SeekOrigin : u8 {
    Start = 0,
    Current = 1,
    End = 2,
};

class OpenFileDescription : public RefCounted<OpenFileDescription> {
public:
    virtual ~OpenFileDescription() = default;

    virtual KResultOr<usize> read(Bytes destination) = 0;
    virtual KResultOr<usize> write(ReadonlyBytes source) = 0;

    virtual KResultOr<u64> seek(i64 offset, SeekOrigin origin);

    virtual RefPtr<Inode> inode() const;

    virtual bool is_terminal() const { return false; }

protected:
    OpenFileDescription() = default;
};

}  // namespace barry::fs
