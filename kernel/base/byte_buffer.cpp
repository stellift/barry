#include "kernel/base/byte_buffer.hpp"

#include "kernel/base/bytes.hpp"

namespace barry {
namespace {

constexpr usize MINIMUM_CAPACITY = 64;

usize capacity_for(usize size) {
    return max(MINIMUM_CAPACITY, round_up_to_power_of_two(size));
}

}  // namespace

KResultOr<ByteBuffer> ByteBuffer::create_zeroed(usize size) {
    if (size == 0)
        return ByteBuffer {};

    usize const capacity = capacity_for(size);
    void* storage = TRY(allocate_raw(capacity, DEFAULT_ALLOCATION_ALIGNMENT));

    auto* data = static_cast<u8*>(storage);
    zero_bytes(Bytes(data, capacity));
    return ByteBuffer(data, size, capacity);
}

ByteBuffer::ByteBuffer(ByteBuffer&& other)
    : m_data(exchange(other.m_data, nullptr)),
      m_size(exchange(other.m_size, usize { 0 })),
      m_capacity(exchange(other.m_capacity, usize { 0 })) {}

ByteBuffer& ByteBuffer::operator=(ByteBuffer&& other) {
    if (this != &other) {
        release();
        m_data = exchange(other.m_data, nullptr);
        m_size = exchange(other.m_size, usize { 0 });
        m_capacity = exchange(other.m_capacity, usize { 0 });
    }
    return *this;
}

ByteBuffer::~ByteBuffer() {
    release();
}

void ByteBuffer::release() {
    if (m_data != nullptr)
        deallocate_raw(m_data);
    m_data = nullptr;
    m_size = 0;
    m_capacity = 0;
}

KResult ByteBuffer::resize(usize new_size) {
    if (new_size <= m_capacity) {
        if (new_size > m_size)
            zero_bytes(Bytes(m_data + m_size, new_size - m_size));
        m_size = new_size;
        return {};
    }

    auto grown = TRY(create_zeroed(new_size));
    if (m_size != 0)
        copy_bytes(grown.bytes(), bytes());

    *this = move(grown);
    return {};
}

}  // namespace barry
