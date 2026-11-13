#include "win32_utils.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <filesystem>
#include <sstream>
#include <iostream>

namespace sc2 {

std::wstring Win32Utils::Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return L"";
    int count = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (count <= 0) return L"";
    std::wstring wide(count - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wide[0], count);
    return wide;
}

std::string Win32Utils::WideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return "";
    int count = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (count <= 0) return "";
    std::string utf8(count - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, &utf8[0], count, nullptr, nullptr);
    return utf8;
}

std::string Win32Utils::FindDefaultAccountsFolder() {
    PWSTR docs_path = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs_path))) {
        std::wstring wpath = docs_path;
        CoTaskMemFree(docs_path);

        std::filesystem::path candidate = std::filesystem::path(wpath) / L"StarCraft II" / L"Accounts";
        if (std::filesystem::exists(candidate)) {
            return WideToUtf8(candidate.wstring());
        }
    }
    return "";
}

std::string Win32Utils::SelectFolderDialog(void* hwnd_parent, const std::string& title) {
    std::string result;
    IFileDialog* pfd = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd)))) {
        DWORD dwOptions;
        if (SUCCEEDED(pfd->GetOptions(&dwOptions))) {
            pfd->SetOptions(dwOptions | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        }
        std::wstring wtitle = Utf8ToWide(title);
        pfd->SetTitle(wtitle.c_str());

        if (SUCCEEDED(pfd->Show(reinterpret_cast<HWND>(hwnd_parent)))) {
            IShellItem* psi = nullptr;
            if (SUCCEEDED(pfd->GetResult(&psi))) {
                PWSTR pszPath = nullptr;
                if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &pszPath))) {
                    result = WideToUtf8(pszPath);
                    CoTaskMemFree(pszPath);
                }
                psi->Release();
            }
        }
        pfd->Release();
    }
    return result;
}

void Win32Utils::RevealFileInExplorer(const std::string& file_path) {
    std::wstring wpath = Utf8ToWide(file_path);
    std::wstring param = L"/select,\"" + wpath + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", param.c_str(), nullptr, SW_SHOWNORMAL);
}

void Win32Utils::OpenPath(const std::string& path) {
    std::wstring wpath = Utf8ToWide(path);
    ShellExecuteW(nullptr, L"open", wpath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

bool Win32Utils::SetAutoStart(bool enable, const std::string& app_name, const std::string& exe_path) {
    HKEY hKey = nullptr;
    const wchar_t* subkey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    if (RegOpenKeyExW(HKEY_CURRENT_USER, subkey, 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS) {
        return false;
    }

    std::wstring wname = Utf8ToWide(app_name);
    bool ok = false;
    if (enable) {
        std::wstring wexe = Utf8ToWide(exe_path);
        ok = (RegSetValueExW(hKey, wname.c_str(), 0, REG_SZ,
                             reinterpret_cast<const BYTE*>(wexe.c_str()),
                             static_cast<DWORD>((wexe.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS);
    } else {
        ok = (RegDeleteValueW(hKey, wname.c_str()) == ERROR_SUCCESS);
    }

    RegCloseKey(hKey);
    return ok;
}

bool Win32Utils::IsAutoStartEnabled(const std::string& app_name) {
    HKEY hKey = nullptr;
    const wchar_t* subkey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    if (RegOpenKeyExW(HKEY_CURRENT_USER, subkey, 0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS) {
        return false;
    }

    std::wstring wname = Utf8ToWide(app_name);
    bool enabled = (RegQueryValueExW(hKey, wname.c_str(), nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS);
    RegCloseKey(hKey);
    return enabled;
}

static void ReplaceAll(std::string& str, const std::string& from, const std::string& to) {
    if (from.empty()) return;
    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length();
    }
}

std::string Win32Utils::CleanPlayerName(const std::string& raw_name) {
    std::string s = raw_name;
    ReplaceAll(s, "&lt;", "<");
    ReplaceAll(s, "&gt;", ">");
    ReplaceAll(s, "&amp;", "&");
    ReplaceAll(s, "&apos;", "'");
    ReplaceAll(s, "&quot;", "\"");
    ReplaceAll(s, "<sp/>", " ");
    ReplaceAll(s, "<sp>", " ");

    // If the name starts with a clan tag e.g. "<Clan> Name", strip the clan tag
    if (s.rfind("<", 0) == 0) {
        size_t close_pos = s.find(">");
        if (close_pos != std::string::npos && close_pos + 1 < s.size()) {
            s = s.substr(close_pos + 1);
            size_t first = s.find_first_not_of(" \t\r\n");
            if (first != std::string::npos) {
                s = s.substr(first);
            }
        }
    }
    return s;
}

} // namespace sc2
