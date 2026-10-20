#pragma once

#include "kernel/base/allocator.hpp"
#include "kernel/base/expected.hpp"
#include "kernel/base/span.hpp"
#include "kernel/base/types.hpp"
#include "kernel/base/utility.hpp"

namespace barry {

class ByteBuffer {
public:
    static KResultOr<ByteBuffer> create_zeroed(usize size);

    ByteBuffer() = default;
    ~ByteBuffer();

    ByteBuffer(ByteBuffer const&) = delete;
    ByteBuffer& operator=(ByteBuffer const&) = delete;

    ByteBuffer(ByteBuffer&& other);
    ByteBuffer& operator=(ByteBuffer&& other);

    usize size() const { return m_size; }
    bool is_empty() const { return m_size == 0; }

    Bytes bytes() { return Bytes(m_data, m_size); }
    ReadonlyBytes bytes() const { return ReadonlyBytes(m_data, m_size); }

    KResult resize(usize new_size);

private:
    ByteBuffer(u8* data, usize size, usize capacity)
        : m_data(data), m_size(size), m_capacity(capacity) {}

    void release();

    u8* m_data { nullptr };
    usize m_size { 0 };
    usize m_capacity { 0 };
};

}  // namespace barry
