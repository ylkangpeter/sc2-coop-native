#pragma once

#include <string>
#include <vector>

namespace sc2 {

class Win32Utils {
public:
    // Auto-detect StarCraft II Accounts folder in Documents
    static std::string FindDefaultAccountsFolder();

    // Show native Windows Folder Picker dialog
    static std::string SelectFolderDialog(void* hwnd_parent, const std::string& title);

    // Reveal file in Windows Explorer (selects the file)
    static void RevealFileInExplorer(const std::string& file_path);

    // Open file or URL with default application
    static void OpenPath(const std::string& path);

    // Windows startup registry (HKCU\Software\Microsoft\Windows\CurrentVersion\Run)
    static bool SetAutoStart(bool enable, const std::string& app_name, const std::string& exe_path);
    static bool IsAutoStartEnabled(const std::string& app_name);

    // UTF-8 <-> UTF-16 conversions
    static std::wstring Utf8ToWide(const std::string& utf8);
    static std::string WideToUtf8(const std::wstring& wide);

    // Unescape XML/HTML tags in SC2 player names (e.g. &lt;Clan&gt;<sp/>Name -> <Clan> Name)
    static std::string CleanPlayerName(const std::string& raw_name);
};

} // namespace sc2
