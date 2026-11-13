#include "i18n.h"
#include <nlohmann/json.hpp>
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <algorithm>

namespace sc2 {

static std::vector<LanguageMeta> s_languages;
static std::string s_current_code = "zh_CN";
static bool s_is_chinese = true;
static std::unordered_map<std::string, std::string> s_dict;

static std::filesystem::path GetAppDirectory() {
    wchar_t buf[MAX_PATH] = { 0 };
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::filesystem::path p(buf);
    return p.parent_path();
}

void I18n::RefreshLanguages() {
    s_languages.clear();

    std::vector<std::filesystem::path> search_dirs;
    std::filesystem::path app_dir = GetAppDirectory();
    search_dirs.push_back(app_dir / "assets" / "lang");
    search_dirs.push_back(app_dir / "lang");
    search_dirs.push_back("assets/lang");
    search_dirs.push_back("lang");

    std::unordered_map<std::string, LanguageMeta> discovered;

    for (const auto& dir : search_dirs) {
        if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir)) {
            continue;
        }

        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".json") {
                continue;
            }

            std::string code = entry.path().stem().string();
            std::string name = code;
            std::string fpath = entry.path().string();

            std::ifstream f(entry.path());
            if (f.is_open()) {
                try {
                    nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
                    if (!j.is_discarded() && j.contains("_meta") && j["_meta"].is_object()) {
                        if (j["_meta"].contains("code") && j["_meta"]["code"].is_string()) {
                            code = j["_meta"]["code"].get<std::string>();
                        }
                        if (j["_meta"].contains("name") && j["_meta"]["name"].is_string()) {
                            name = j["_meta"]["name"].get<std::string>();
                        }
                    }
                } catch (...) {
                    // Ignore parse errors for metadata discovery
                }
            }

            if (discovered.find(code) == discovered.end()) {
                LanguageMeta meta;
                meta.code = code;
                meta.name = name;
                meta.file_path = fpath;
                discovered[code] = meta;
            }
        }
    }

    // Default fallbacks if files not found yet
    if (discovered.find("zh_CN") == discovered.end()) {
        LanguageMeta meta;
        meta.code = "zh_CN";
        meta.name = "简体中文";
        meta.file_path = "assets/lang/zh_CN.json";
        discovered["zh_CN"] = meta;
    }
    if (discovered.find("en_US") == discovered.end()) {
        LanguageMeta meta;
        meta.code = "en_US";
        meta.name = "English";
        meta.file_path = "assets/lang/en_US.json";
        discovered["en_US"] = meta;
    }

    for (const auto& [code, meta] : discovered) {
        s_languages.push_back(meta);
    }
    std::sort(s_languages.begin(), s_languages.end(), [](const LanguageMeta& a, const LanguageMeta& b) {
        if (a.code == "zh_CN") return true;
        if (b.code == "zh_CN") return false;
        if (a.code == "en_US") return true;
        if (b.code == "en_US") return false;
        return a.code < b.code;
    });
}

void I18n::Init(const std::string& default_lang) {
    RefreshLanguages();
    SetLanguage(default_lang);
}

const std::vector<LanguageMeta>& I18n::GetAvailableLanguages() {
    if (s_languages.empty()) {
        RefreshLanguages();
    }
    return s_languages;
}

bool I18n::SetLanguage(const std::string& code) {
    if (s_languages.empty()) {
        RefreshLanguages();
    }

    const LanguageMeta* target = nullptr;
    for (const auto& l : s_languages) {
        if (l.code == code) {
            target = &l;
            break;
        }
    }

    if (!target && !s_languages.empty()) {
        target = &s_languages[0];
    }

    if (!target) return false;

    s_current_code = target->code;
    s_is_chinese = (s_current_code == "zh_CN");
    s_dict.clear();

    // Load entire dictionary from external plain JSON resource file
    std::vector<std::string> paths_to_try = {
        target->file_path,
        "assets/lang/" + s_current_code + ".json",
        "lang/" + s_current_code + ".json"
    };

    for (const auto& p : paths_to_try) {
        if (p.empty() || !std::filesystem::exists(p)) continue;
        std::ifstream f(p);
        if (f.is_open()) {
            try {
                nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
                if (!j.is_discarded() && j.is_object()) {
                    for (auto it = j.begin(); it != j.end(); ++it) {
                        if (it.key() != "_meta" && it.value().is_string()) {
                            s_dict[it.key()] = it.value().get<std::string>();
                        }
                    }
                    break;
                }
            } catch (...) {
            }
        }
    }

    return true;
}

bool I18n::SetLanguageByIndex(int index) {
    if (s_languages.empty()) RefreshLanguages();
    if (index >= 0 && index < static_cast<int>(s_languages.size())) {
        return SetLanguage(s_languages[index].code);
    }
    return false;
}

int I18n::GetCurrentLanguageIndex() {
    if (s_languages.empty()) RefreshLanguages();
    for (size_t i = 0; i < s_languages.size(); ++i) {
        if (s_languages[i].code == s_current_code) {
            return static_cast<int>(i);
        }
    }
    return 0;
}

std::string I18n::GetCurrentLanguageCode() {
    return s_current_code;
}

bool I18n::IsChinese() {
    return s_is_chinese;
}

const char* I18n::Get(const char* key) {
    if (!key) return "";
    auto it = s_dict.find(key);
    if (it != s_dict.end() && !it->second.empty()) {
        return it->second.c_str();
    }
    return key;
}

const char* I18n::Get(const char* key, const char* fallback) {
    if (!key) return fallback ? fallback : "";
    auto it = s_dict.find(key);
    if (it != s_dict.end() && !it->second.empty()) {
        return it->second.c_str();
    }
    return fallback ? fallback : key;
}

const char* I18n::Text(const char* zh, const char* en) {
    if (!en && !zh) return "";

    if (en) {
        auto it = s_dict.find(en);
        if (it != s_dict.end() && !it->second.empty()) {
            return it->second.c_str();
        }
    }
    if (zh) {
        auto it = s_dict.find(zh);
        if (it != s_dict.end() && !it->second.empty()) {
            return it->second.c_str();
        }
    }

    if (s_is_chinese) {
        return (zh && zh[0] != '\0') ? zh : (en ? en : "");
    } else {
        return (en && en[0] != '\0') ? en : (zh ? zh : "");
    }
}

} // namespace sc2
