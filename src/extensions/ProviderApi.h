#pragma once
#include <QFuture>
#include <optional>
#include <variant>
namespace litecode::extensions {
namespace detail {
template<typename T> struct ProviderResultType {
    using Type = std::variant<T, std::optional<T>, QFuture<T>, QFuture<std::optional<T>>>;
};
template<> struct ProviderResultType<void> { using Type = std::variant<std::monostate, QFuture<void>>; };
}
template<typename T> using ProviderResult = typename detail::ProviderResultType<T>::Type;
}
