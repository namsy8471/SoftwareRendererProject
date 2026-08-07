#pragma once

#include <array>
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <version>

#if defined(__cpp_lib_inplace_vector) && __cpp_lib_inplace_vector >= 202406L
#include <inplace_vector>
#endif

namespace sr
{
#if defined(__cpp_lib_inplace_vector) && __cpp_lib_inplace_vector >= 202406L
    // MSVC가 C++26 std::inplace_vector를 제공하면 표준 구현을 그대로 사용한다.
    template <typename T, std::size_t Capacity>
    using FixedCapacityVector = std::inplace_vector<T, Capacity>;
#else
    // MSVC v145의 현재 STL에는 inplace_vector가 아직 없다. 클리핑처럼 최대
    // 크기가 수학적으로 정해진 버퍼를 vector로 힙 할당하지 않도록 동일한
    // 최소 API의 fallback을 제공하며, 표준 구현이 들어오면 위 alias로 교체된다.
    template <typename T, std::size_t Capacity>
    class FixedCapacityVector
    {
    public:
        using value_type = T;
        using iterator = T*;
        using const_iterator = const T*;

        [[nodiscard]] constexpr std::size_t size() const noexcept { return m_size; }
        [[nodiscard]] constexpr bool empty() const noexcept { return m_size == 0; }
        [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

        constexpr void clear() noexcept { m_size = 0; }
        constexpr void reserve(std::size_t requested) const
        {
            if (requested > Capacity) throw std::length_error("FixedCapacityVector capacity exceeded");
        }

        template <typename... Args>
        constexpr T& emplace_back(Args&&... args)
        {
            if (m_size == Capacity) throw std::length_error("FixedCapacityVector capacity exceeded");
            m_storage[m_size] = T(std::forward<Args>(args)...);
            return m_storage[m_size++];
        }

        constexpr void push_back(const T& value) { emplace_back(value); }
        constexpr void push_back(T&& value) { emplace_back(std::move(value)); }

        [[nodiscard]] constexpr T& operator[](std::size_t index) noexcept { return m_storage[index]; }
        [[nodiscard]] constexpr const T& operator[](std::size_t index) const noexcept { return m_storage[index]; }
        [[nodiscard]] constexpr T& back() noexcept { return m_storage[m_size - 1]; }
        [[nodiscard]] constexpr const T& back() const noexcept { return m_storage[m_size - 1]; }

        [[nodiscard]] constexpr iterator begin() noexcept { return m_storage.data(); }
        [[nodiscard]] constexpr const_iterator begin() const noexcept { return m_storage.data(); }
        [[nodiscard]] constexpr iterator end() noexcept { return m_storage.data() + m_size; }
        [[nodiscard]] constexpr const_iterator end() const noexcept { return m_storage.data() + m_size; }

    private:
        std::array<T, Capacity> m_storage{};
        std::size_t m_size = 0;
    };
#endif
}
