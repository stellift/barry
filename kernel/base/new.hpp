#pragma once

#include "kernel/base/traits.hpp"
#include "kernel/base/utility.hpp"

[[nodiscard]] void* operator new(barry::usize, void* ptr) noexcept;
[[nodiscard]] void* operator new[](barry::usize, void* ptr) noexcept;
void operator delete(void*, void*) noexcept;
void operator delete[](void*, void*) noexcept;

namespace barry {

template<typename T, typename... Args>
constexpr T* construct_at(T* location, Args&&... args) {
    return ::new (static_cast<void*>(location)) T(forward<Args>(args)...);
}

template<typename T>
constexpr void destroy_at(T* location) {
    if constexpr (!is_trivially_destructible_v<T>)
        location->~T();
}

template<typename T>
class alignas(T) RawStorage {
public:
    RawStorage() = default;

    T* ptr() { return reinterpret_cast<T*>(&m_bytes[0]); }
    T const* ptr() const { return reinterpret_cast<T const*>(&m_bytes[0]); }

    T& ref() { return *ptr(); }
    T const& ref() const { return *ptr(); }

    template<typename... Args>
    T* construct(Args&&... args) {
        return construct_at(ptr(), forward<Args>(args)...);
    }

    void destroy() { destroy_at(ptr()); }

private:
    u8 m_bytes[sizeof(T)];
};

}
