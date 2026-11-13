#pragma once

#include <string>
#include <vector>
#include <unordered_map>

namespace sc2 {

struct LanguageMeta {
    std::string code;      // e.g. "zh_CN", "en_US"
    std::string name;      // e.g. "简体中文", "English"
    std::string file_path; // filesystem path to language resource file
};

class I18n {
public:
    static void Init(const std::string& default_lang = "zh_CN");
    static void RefreshLanguages();
    static const std::vector<LanguageMeta>& GetAvailableLanguages();
    static bool SetLanguage(const std::string& code);
    static bool SetLanguageByIndex(int index);
    static int GetCurrentLanguageIndex();
    static std::string GetCurrentLanguageCode();
    static bool IsChinese();

    // Primary translation lookup: retrieves localized text for key from loaded lang file
    static const char* Get(const char* key);
    static const char* Get(const char* key, const char* fallback);

    // Transition helper for dual-literal calls
    static const char* Text(const char* zh, const char* en);
};

// Global shorthand: TR("key")
inline const char* TR(const char* key) {
    return I18n::Get(key);
}

// Global shorthand: TR("key", "fallback")
inline const char* TR(const char* key, const char* fallback) {
    return I18n::Get(key, fallback);
}

} // namespace sc2
