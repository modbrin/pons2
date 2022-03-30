#pragma once

#include <variant>

namespace pons {
template <typename T> struct Result : protected std::variant<std::monostate, T> {
    explicit constexpr Result() noexcept = default;
    constexpr Result(T const &&t) noexcept : std::variant<std::monostate, T>{t} {}

    constexpr bool valid() const noexcept { return std::holds_alternative<T>(*this); }
    constexpr bool invalid() const noexcept { return !valid(); }
    constexpr auto get() const noexcept { return (valid() ? std::get<T>(*this) : T()); }
};
} // namespace pons