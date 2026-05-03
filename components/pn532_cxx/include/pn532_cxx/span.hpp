#pragma once

// C++17/20 span compatibility header
// Provides std::span for C++20+, custom minimal implementation for C++17

#if __cplusplus >= 202002L
  #include <span>
  namespace pn532 {
    using std::span;
  }
#else
#include <cstddef>
#include <array>
#include <vector>
#include <type_traits>
  namespace pn532 {
    template<typename T>
    class span {
    public:
      using element_type = T;
      using value_type = typename std::remove_cv<T>::type;
      using size_type = std::size_t;
      using pointer = T*;
      using const_pointer = const T*;
      using reference = T&;
      using const_reference = const T&;

      constexpr span() noexcept : _data(nullptr), _size(0) {}

      constexpr span(pointer data, size_type size) noexcept
        : _data(data), _size(size) {}

      template<std::size_t N>
      constexpr span(std::array<value_type, N>& arr) noexcept
        : _data(arr.data()), _size(N) {}

      template<std::size_t N>
      constexpr span(const std::array<value_type, N>& arr) noexcept
        : _data(arr.data()), _size(N) {}

      constexpr span(std::vector<value_type>& vec) noexcept
        : _data(vec.data()), _size(vec.size()) {}

      constexpr span(const std::vector<value_type>& vec) noexcept
        : _data(vec.data()), _size(vec.size()) {}

      template<std::size_t N>
      constexpr span(value_type (&arr)[N]) noexcept
        : _data(arr), _size(N) {}

      template<std::size_t N>
      constexpr span(const value_type (&arr)[N]) noexcept
        : _data(arr), _size(N) {}

      constexpr span(const span& other) noexcept = default;

      constexpr span& operator=(const span& other) noexcept = default;

      constexpr pointer data() const noexcept { return _data; }
      constexpr size_type size() const noexcept { return _size; }
      constexpr bool empty() const noexcept { return _size == 0; }

      constexpr reference operator[](size_type idx) const { return _data[idx]; }

      constexpr pointer begin() const noexcept { return _data; }
      constexpr pointer end() const noexcept { return _data + _size; }

      constexpr span subspan(size_type offset, size_type count = static_cast<size_type>(-1)) const {
        if (offset >= _size) {
          return span();
        }
        if (count == static_cast<size_type>(-1) || offset + count > _size) {
          count = _size - offset;
        }
        return span(_data + offset, count);
      }

    private:
      pointer _data;
      size_type _size;
    };

    template<typename T>
    class span<const T> {
    public:
      using element_type = const T;
      using value_type = typename std::remove_cv<T>::type;
      using size_type = std::size_t;
      using pointer = const T*;
      using const_pointer = const T*;
      using reference = const T&;
      using const_reference = const T&;

      constexpr span() noexcept : _data(nullptr), _size(0) {}

      constexpr span(pointer data, size_type size) noexcept
        : _data(data), _size(size) {}

      template<std::size_t N>
      constexpr span(const std::array<value_type, N>& arr) noexcept
        : _data(arr.data()), _size(N) {}

      template<std::size_t N>
      constexpr span(std::array<value_type, N>& arr) noexcept
        : _data(arr.data()), _size(N) {}

      constexpr span(const std::vector<value_type>& vec) noexcept
        : _data(vec.data()), _size(vec.size()) {}

      constexpr span(std::vector<value_type>& vec) noexcept
        : _data(vec.data()), _size(vec.size()) {}

      template<std::size_t N>
      constexpr span(const value_type (&arr)[N]) noexcept
        : _data(arr), _size(N) {}

      template<std::size_t N>
      constexpr span(value_type (&arr)[N]) noexcept
        : _data(arr), _size(N) {}

      constexpr span(const span<value_type>& other) noexcept
        : _data(other.data()), _size(other.size()) {}

      constexpr span(const span& other) noexcept = default;

      constexpr span& operator=(const span& other) noexcept = default;

      constexpr pointer data() const noexcept { return _data; }
      constexpr size_type size() const noexcept { return _size; }
      constexpr bool empty() const noexcept { return _size == 0; }

      constexpr reference operator[](size_type idx) const { return _data[idx]; }

      constexpr pointer begin() const noexcept { return _data; }
      constexpr pointer end() const noexcept { return _data + _size; }

      constexpr span subspan(size_type offset, size_type count = static_cast<size_type>(-1)) const {
        if (offset >= _size) {
          return span();
        }
        if (count == static_cast<size_type>(-1) || offset + count > _size) {
          count = _size - offset;
        }
        return span(_data + offset, count);
      }

    private:
      pointer _data;
      size_type _size;
    };
  }
#endif
