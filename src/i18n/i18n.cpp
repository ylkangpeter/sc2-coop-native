#include "i18n.h"

namespace sc2 {

Language I18n::s_language = Language::Chinese;

static const std::unordered_map<std::string_view, const char*> s_translations = {
    // Navigation / Tabs
    {"Games", "对局历史"},
    {"Players", "玩家统计"},
    {"Weeklies", "每周突变"},
    {"Statistics", "数据大盘"},
    {"Settings", "系统设置"},
    {"Summary Overlay", "赛后战报浮窗"},

    // Games Tab
    {"Map", "地图"},
    {"Mode", "模式"},
    {"Result", "结果"},
    {"Player 1", "玩家1"},
    {"Player 2 (Ally)", "盟友"},
    {"Enemy", "敌人"},
    {"Length", "时长"},
    {"Difficulty", "难度"},
    {"Date", "日期"},
    {"Actions", "操作"},
    {"Victory", "胜利"},
    {"Defeat", "失败"},
    {"Tie", "平局"},
    {"Unknown", "未知"},
    {"Weekly", "周突变"},
    {"Custom Mut", "自定义突变"},
    {"Regular", "普通对局"},
    {"Detail", "详情"},
    {"Chat", "聊天"},
    {"File", "文件"},
    {"Clear", "清除"},
    {"Refresh", "刷新"},

    // Settings Tab
    {"Interface Language", "界面语言"},
    {"Preferences & System Integration", "首选项与系统集成"},
    {"Global System Hotkeys", "全局快捷键 (游戏内与桌面)"},
    {"Reset Default Hotkeys", "恢复默认快捷键"},
    {"Change Hotkey", "修改快捷键"},
    {"Press key combo... (Esc to cancel)", "请按下快捷键组合... (按 Esc 取消)"},
};

const char* I18n::Get(const char* key) {
    if (s_language != Language::Chinese || !key) {
        return key;
    }
    auto it = s_translations.find(key);
    if (it != s_translations.end()) {
        return it->second;
    }
    return key;
}

} // namespace sc2
