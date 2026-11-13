#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

namespace sc2 {

enum class Language : int {
    Chinese = 0,
    English = 1
};

class I18n {
public:
    static Language s_language;

    static inline Language GetLanguage() { return s_language; }
    static inline void SetLanguage(Language lang) { s_language = lang; }
    static inline bool IsChinese() { return s_language == Language::Chinese; }

    // Fast bilingual translation inline: returns zh if Chinese, en if English
    static inline const char* Text(const char* zh, const char* en) {
        return (s_language == Language::Chinese) ? zh : en;
    }

    // Key lookup translation (returns key if not found or if English)
    static const char* Get(const char* key);
};

// Global shorthand for inline bilingual text
inline const char* TR(const char* zh, const char* en) {
    return I18n::Text(zh, en);
}

} // namespace sc2
