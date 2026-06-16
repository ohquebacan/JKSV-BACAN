#pragma once
#include "strings/names.hpp"

#include <string_view>

namespace strings
{
    // Attempts to load strings from file in RomFS.
    bool initialize();

    // Returns string with name and index. Returns nullptr if string doesn't exist.
    const char *get_by_name(std::string_view name, int index) noexcept;

    // Whether the loaded UI language is Spanish (ES/ES419).
    bool is_spanish() noexcept;

    // Returns `spanish` when the UI language is Spanish, otherwise `english`. Used by BACÁN's added strings to
    // support EN + ES without touching the full per-language translation files.
    const char *tr(const char *english, const char *spanish) noexcept;
} // namespace strings
