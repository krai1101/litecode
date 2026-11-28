#pragma once
namespace litecode::extensions {
struct WindowState final {
    bool focused{};
    bool active{};
    bool operator==(const WindowState&) const = default;
};
} // namespace litecode::extensions
