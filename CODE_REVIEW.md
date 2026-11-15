# StarCraft II Co-op Native — 代码审查报告 (Code Review Report)

> **审查日期**: 2026-11-15  
> **审查范围**: 全部源码 (`src/`, `tests/`, `build.py`, 配置文件与数据头文件)  
> **审查目标**: 仅发现并归纳代码问题（内存安全、并发/竞态、逻辑缺陷、资源泄露、性能瓶颈、架构健壮性），不直接修改代码。

---

## 目录
1. [严重级别定义](#严重级别定义)
2. [重大与严重问题 (Critical & High Severity)](#一重大与严重问题-critical--high-severity)
3. [中等严重问题 (Medium Severity)](#二中等严重问题-medium-severity)
4. [轻微与性能问题 (Low Severity & Performance)](#三轻微与性能问题-low-severity--performance)
5. [架构建议与最佳实践 (Architecture & Best Practices)](#四架构建议与最佳实践-architecture--best-practices)
6. [问题清单速查表 (Summary Table)](#五问题清单速查表-summary-table)

---

## 严重级别定义
- **【严重】(Critical / High)**: 导致崩溃、内存访问违规 (Access Violation)、数据竞争 (Data Race)、未定义行为 (Undefined Behavior)、核心功能严重失效或资源死锁的问题。
- **【中等】(Medium)**: 导致逻辑异常、UI 异常表现、非预期功能行为、资源泄露或性能明显退化的问题。
- **【轻微/建议】(Low / Suggestion)**: 导致代码冗余、风格不一致、可维护性欠佳或微小的边界情况处理不当。

---

## 一、重大与严重问题 (Critical & High Severity)

### 1.1 关键指挥官命名不一致："HanHorner" 与 "Horner" 导致多处核心功能断裂
- **涉及文件**:
  - `src/replay/s2_initdata.h` (第 519-520 行)
  - `src/db/database.cpp` (第 120-121 行)
  - `src/data/sc2_constants.h` (第 47, 68 行)
  - `src/replay/s2_decoder.cpp` (第 210-214, 285-288 行)
  - `src/ui/texture_loader.cpp` (第 102-106 行)
- **问题描述**:
  - 在 `s2_initdata.h` 中，解析到原始名 `"Horner"` 时被强制映射为 `"HanHorner"`：
    ```cpp
    if (raw_cmdr == "Horner") { slot.commander = "HanHorner"; }
    ```
  - 在 `database.cpp` 中执行数据库迁移：
    ```cpp
    sqlite3_exec(m_db, "UPDATE replays SET p1_commander = 'HanHorner' WHERE p1_commander = 'Horner';", nullptr, nullptr, nullptr);
    ```
  - 然而在 `sc2_constants.h` 中，常量表 `kCommanders`、`kPrestiges`、`kCommanderMasteries` 全部使用的是 `"Horner"`，**根本没有 `"HanHorner"` 键**！
- **影响**:
  1. `kPrestiges.find("HanHorner")` 必然失败，导致霍纳与汉的威望名称始终为 `"Default"`，威望无法正确展示。
  2. `kCommanderMasteries.find("HanHorner")` 必然失败，精通技能列表无法匹配。
  3. `S2Decoder::ExtractMasteries` 使用 `data_sv.find(commander)` 在 initData 原始二进制流中查找，而暴雪回放二进制中记录的是 `"Horner"`，传入 `"HanHorner"` 永远找不到，导致精通点数提取失效。
  4. 磁盘图标资源名为 `assets/icons/commanders/Horner.png`，`TextureLoader::GetCommanderIcon("HanHorner")` 尝试加载 `HanHorner.png`，必然失败并在每帧产生磁盘 I/O。

---

### 1.2 分离线程 (`detach`) 导致程序退出时发生 Use-After-Free 和崩溃
- **涉及文件**: `src/app_state.cpp` (第 274-304 行), `src/main.cpp` (第 98-101 行)
- **问题描述**:
  - `AppState::AutoRepairCorruptedReplaysAsync()` 创建了一个后台修复线程并调用 `.detach()`：
    ```cpp
    void AppState::AutoRepairCorruptedReplaysAsync() {
        std::thread([this]() {
            auto corrupted = Database::Instance().GetCorruptedReplayPaths();
            // ...
            Database::Instance().BatchSaveReplays(batch);
            Database::Instance().SanitizeInvalidPrestiges();
        }).detach();
    }
    ```
  - 当程序启动后用户快速关闭，或者修复过程耗时较长时，主线程在 `main.cpp` 中先执行：
    ```cpp
    sc2::Database::Instance().Close();
    ```
    随后 `main.cpp` 退出，全局/单例对象析构。
- **影响**:
  后台分离线程仍在并发执行，访问已经被 `Close()` 并释放的 `sqlite3* m_db`，或者访问已经销毁的单例引用，引发访问违规 (Access Violation / SIGSEGV) 崩溃。

---

### 1.3 `AppState::user_handles` 缺乏互斥锁保护存在数据竞争 (Data Race)
- **涉及文件**:
  - `src/app_state.h` (第 103 行)
  - `src/app_state.cpp` (第 141-180 行)
  - `src/replay/replay_parser.cpp` (第 288-291 行)
- **问题描述**:
  - `AppState::user_handles` 是一个 `std::unordered_set<std::string>`，没有任何锁保护。
  - 主线程在 `AppState::Init()` 或 `AppState::SaveSettings()` 中会调用 `RefreshUserHandles()`，对容器进行 `user_handles.clear()` 和 `user_handles.insert()` 写操作。
  - 同时，后台扫描线程 `m_scan_thread` 和 `AutoRepairCorruptedReplaysAsync` 线程在解析回放时会调用 `ReplayParser::ParseFile()`：
    ```cpp
    const auto& handles = AppState::Instance().user_handles;
    if (!record.p1.handle.empty() && handles.count(record.p1.handle)) p1_is_user = true;
    ```
- **影响**:
  多线程同时读写同一个 `std::unordered_set` 属于标准的 C++ 数据竞争 (Data Race)，具有未定义行为 (UB)，可直接导致哈希桶内部指针错乱、死循环或进程 Crash。

---

### 1.4 主线程未初始化 COM 库即调用 Shell / 文件夹选择对话框
- **涉及文件**: `src/main.cpp` (第 60-80 行), `src/utils/win32_utils.cpp` (第 31-70 行)
- **问题描述**:
  - `wWinMain` 中从未调用 `CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)` 或 `OleInitialize(nullptr)`。
  - 在 `Win32Utils::SelectFolderDialog` 中直接调用了：
    ```cpp
    CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd))
    ```
  - 在 `Win32Utils::FindDefaultAccountsFolder` 中调用了 `SHGetKnownFolderPath(FOLDERID_Documents, ...)`。
- **影响**:
  在未初始化 COM 的线程上调用 `CoCreateInstance` 会直接返回 `CO_E_NOTINITIALIZED` (0x800401F0)，导致用户点击“浏览...”选择回放目录时弹不出选择窗口，功能静默失效。

---

### 1.5 SQLite 查询结果中未检查 `nullptr` 直接构造 `std::string`
- **涉及文件**: `src/db/database.cpp` (第 341-350, 360-365 行)
- **问题描述**:
  - 在 `Database::QueryReplays` 中，存在如下赋值：
    ```cpp
    r.file_path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    r.game_date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    r.map_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    r.raw_map_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
    r.result = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
    r.difficulty = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
    r.p1.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 14));
    ```
  - 同函数后续代码中（如第 351, 353, 368 行）都正确做了 `enemy ? enemy : ""` 判空。
- **影响**:
  如果数据库中历史记录或异常数据某些字段为 `NULL`，`sqlite3_column_text()` 将返回 `nullptr`。将 `nullptr` 传递给 `std::string::operator=` 或构造函数是未定义行为（在 MSVC/libstdc++ 下会直接抛出 `std::logic_error` 或在 `strlen` 崩溃）。

---

### 1.6 SEH 异常崩溃处理函数中使用标准库堆分配与不安全格式化
- **涉及文件**: `src/main.cpp` (第 10-58 行)
- **问题描述**:
  - `CrashHandler` 作为通过 `SetUnhandledExceptionFilter` 注册的系统级异常过滤器，使用了 `std::ofstream f("crash_log.txt", std::ios::app)`、`std::hex`、`std::endl` 等 C++ iostream 操作。
  - 在发生堆损坏 (Heap Corruption)、栈溢出 (Stack Overflow) 或 CRT 锁死引发的崩溃中，在异常处理程序内尝试再次分配堆内存或加锁会导致二次崩溃 (Double Fault) 或死锁，使诊断日志完全丢失。
  - 第 54 行使用 `wsprintfW(buf, L"... at 0x%p", ...)`，但在 Win32 `wsprintfW` 规范中不支持 64 位 `%p` 指针占位符（且有 1024 字节截断限制）。
- **影响**:
  致命崩溃时无法成功落盘 `crash_log.txt`，调试信息丢失，弹窗提示失败。

---

## 二、中等严重问题 (Medium Severity)

### 2.1 最小化到托盘后窗口隐藏但仍以 60 FPS 渲染并占用 GPU
- **涉及文件**: `src/ui/ui_manager.cpp` (第 42-45, 108-111, 467-470 行)
- **问题描述**:
  - 当用户在设置中开启“关闭时最小化到系统托盘”并点击窗口叉号时，`WndProc` 处理 `WM_CLOSE`：
    ```cpp
    if (AppState::Instance().minimize_to_tray) {
        ShowWindow(hWnd, SW_HIDE);
        return 0;
    }
    ```
  - 但是 `m_minimized` 标志**仅在 `WM_SIZE` 的 `wParam == SIZE_MINIMIZED` 时设置**。使用 `ShowWindow(hWnd, SW_HIDE)` 隐藏窗口并不会触发 `SIZE_MINIMIZED`。
  - 因此，主循环中的 `if (m_minimized) { Sleep(10); continue; }` 永远不会命中。
- **影响**:
  应用在完全隐藏在系统托盘的状态下，主循环依然以 60 FPS 持续执行 `ImGui::NewFrame()`、D3D11 渲染和 `m_pSwapChain->Present(1, 0)`，造成无意义的 GPU/CPU 功耗和笔记本电量浪费。应检查 `!IsWindowVisible(m_hwnd)`。

---

### 2.2 `build.py` 增量编译未检查头文件修改时间
- **涉及文件**: `build.py` (第 90-100 行)
- **问题描述**:
  - `build.py` 在第 96 行计算了 `MAX_HEADER_MTIME = get_max_header_mtime()`。
  - 但在第 99 行 `compile_file` 中，仅判断了：
    ```python
    if obj_path.exists() and obj_path.stat().st_mtime >= src_path.stat().st_mtime:
        return
    ```
  - 完全没有比对 `obj_path.stat().st_mtime` 与 `MAX_HEADER_MTIME`。
- **影响**:
  当开发者修改了 `app_state.h`、`replay_types.h` 或 `database.h` 中的结构体定义时，由于对应的 `.cpp` 文件时间戳未改变，增量编译会跳过编译。链接后各个编译单元持有的结构体内存布局不一致，产生严重的 ODR (One Definition Rule) 违规及偶发诡异内存崩溃。

---

### 2.3 `ReplayParser` 回放难度提取逻辑有误
- **涉及文件**: `src/replay/replay_parser.cpp` (第 170-186 行)
- **问题描述**:
  - 代码提取难度的逻辑如下：
    ```cpp
    std::string d1, d2;
    if (slots.size() > 2) d1 = DifficultyFromInt(slots[2].difficulty);
    if (slots.size() > 3) d2 = DifficultyFromInt(slots[3].difficulty);
    ```
  - 星际争霸 2 合作任务的两个人类玩家位于 `slots[0]` 和 `slots[1]`。`slots[0].difficulty` 和 `slots[1].difficulty` 才是玩家选择的难度。
  - 此外，如果走的是后备字符串启发式扫描器 (`DecodeInitDataSlots` 分支 2)，`slots.size()` 最大就是 2。此时 `slots.size() > 2` 为假，导致 `d1` 和 `d2` 恒为空，所有普通游戏都被无条件降级回退为 `"Brutal"`。
- **影响**:
  玩家在休闲、普通、困难难度下进行的游戏，在回放解析时可能被错误标记为残酷难度。

---

### 2.4 全局键盘钩子 (Low-Level Keyboard Hook) 吞噬游戏内 ESC 键
- **涉及文件**: `src/utils/hotkey_manager.cpp` (第 179-189 行)
- **问题描述**:
  - `HotkeyManager` 注册了全局底层键盘钩子 `WH_KEYBOARD_LL`。
  - 在 `HandleLowLevelKey` 中：
    ```cpp
    if (vk == VK_ESCAPE) {
        if (SummaryOverlay::Instance().IsActive()) {
            SummaryOverlay::Instance().Hide();
            return true;
        }
        if (AllyToastWindow::Instance().IsActive()) {
            AllyToastWindow::Instance().Hide();
            return true;
        }
        return false;
    }
    ```
  - 当钩子返回 `true` 时，`LowLevelKeyboardProc` 直接返回 `1`，阻止该按键传递给系统后续处理。
- **影响**:
  如果玩家在游戏开局看到了队友信息 Toast，或者结算时悬浮窗显示期间，玩家在星际争霸 2 游戏内按下 ESC（例如取消建筑建造、取消选兵、打开游戏菜单），ESC 键会被该程序截断并拦截，导致星际争霸 2 收不到 ESC 键响应。

---

### 2.5 `QueryPlayers` 在有搜索关键字时返回错误的总记录数
- **涉及文件**: `src/db/database.cpp` (第 405-412 行)
- **问题描述**:
  - 在 `Database::QueryPlayers` 中：
    ```cpp
    std::string count_sql = "SELECT COUNT(DISTINCT p2_name) FROM replays WHERE p2_name IS NOT NULL AND p2_name != '' ...;";
    ```
  - 无论 `search_filter` 是否为空，`count_sql` 都不包含搜索过滤条件。
- **影响**:
  在“队友”标签页中输入搜索关键字后，虽然表格列表正确展示了匹配的少数玩家，但底部的分页指示器和总数依然显示为全库玩家总数，导致翻页栏计算出错误的页数，用户翻到第 2 页后会显示空白。

---

### 2.6 `QueryPlayers` 缺少对 `total_games` 字段的排序支持
- **涉及文件**: `src/db/database.cpp` (第 414-420 行)
- **问题描述**:
  - `order_sql` 匹配了 `"wins"`, `"losses"`, `"winrate"`, `"apm"`, `"name"`：
    ```cpp
    std::string order_sql = "wins DESC";
    if (sort_column == "wins") ...
    else if (sort_column == "losses") ...
    // 缺少 else if (sort_column == "total_games")
    ```
  - 而在 `AppState::OpenPlayerNoteModal`（第 317 行）中：
    ```cpp
    auto players = Database::Instance().QueryPlayers(note_target_player, "total_games", false, 1, 0, dummy_total);
    ```
- **影响**:
  按总场次排序的需求无法正常生效，默认直接回退为按胜场 (`wins DESC`) 排序。

---

### 2.7 `I18n` 字典无锁并发读写与悬垂指针风险
- **涉及文件**: `src/i18n/i18n.cpp` (第 135-165, 194-201 行)
- **问题描述**:
  - 全局静态变量 `s_dict` (`std::unordered_map<std::string, std::string>`) 无互斥锁保护。
  - `I18n::Get()` 直接返回内部字符串指针：
    ```cpp
    const char* I18n::Get(const char* key) {
        auto it = s_dict.find(key);
        if (it != s_dict.end()) return it->second.c_str();
        return key;
    }
    ```
- **影响**:
  如果主线程调用 `SetLanguage()` 清空并重新加载 `s_dict`，而后台扫描线程或 HUD 正在并发调用 `TR()`，会导致容器并发修改崩溃；此外，如果其他地方保留了 `Get()` 返回的 `const char*` 指针，字典重建后该指针将成为野指针 (Dangling Pointer)。

---

### 2.8 `Database::CalculateStats` 采用字符串拼接构建 SQL
- **涉及文件**: `src/db/database.cpp` (第 603-630 行)
- **问题描述**:
  - `CalculateStats` 中对过滤条件的拼接采用了手工转义：
    ```cpp
    auto escape_sql = [](const std::string& input) { ... };
    if (!filter.date_from.empty()) where_clause += " AND game_date >= '" + escape_sql(filter.date_from) + "'";
    if (!filter.ally_name_filter.empty()) where_clause += " AND p2_name LIKE '%" + escape_sql(filter.ally_name_filter) + "%'";
    ```
- **影响**:
  SQL 拼接不仅违反了数据库参数化查询的最佳安全实践，而且手工转义未处理通配符 `%`、`_` 等特殊字符，一旦用户输入包含特殊符号，可能导致查询语法解析错误或结果异常。

---

### 2.9 在 SQLite 事务持有期间执行大量同步磁盘文件 I/O
- **涉及文件**: `src/db/database.cpp` (第 1010-1028 行)
- **问题描述**:
  - 在 `Database::RepairWeeklyMutationsIfNeeded` 中：
    ```cpp
    sqlite3_exec(m_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    for (const auto& path : paths) {
        std::vector<uint8_t> initdata;
        if (MpqReader::ReadFile(path, "replay.initData", initdata)) { ... }
    }
    sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr);
    ```
- **影响**:
  在已开启写事务（占用 SQLite 排他写锁）的状态下，循环打开并解压读取大量回放文件。当回放库中有上千场回放时，磁盘 I/O 耗时数秒至数十秒，导致数据库被长时间锁死，UI 主线程查询该数据库时全部卡死。

---

## 三、轻微与性能问题 (Low Severity & Performance)

### 3.1 `TabGames` 与 `TabPlayers` 每帧重复执行数据库全量查询
- **涉及文件**:
  - `src/ui/tabs/tab_games.cpp` (第 50 行)
  - `src/ui/tabs/tab_players.cpp` (第 59-62 行)
  - `src/ui/tabs/tab_settings.cpp` (第 85 行)
- **问题描述**:
  - `TabStats` 和 `TabWeeklies` 都设计了精巧的数据缓存机制 (`s_cache_valid`)，只在过滤条件改变时重新查询。
  - 但 `TabGames::Render` 和 `TabPlayers::Render` 却在**每一帧 (60 FPS)** 都调用 `Database::Instance().QueryReplays` 和 `QueryPlayers`。
  - `TabSettings::Render` 也在每帧调用 `Database::Instance().GetTotalReplayCount()` (`SELECT COUNT(*) FROM replays`)。
- **影响**:
  空闲时每秒执行 120+ 次复杂 SQL 语句（包括 `GROUP BY` 和聚合），增加 CPU 占用与数据库锁竞争。

---

### 3.2 缺失贴图失败未缓存，导致每帧重复触发文件系统属性检查
- **涉及文件**: `src/ui/texture_loader.cpp` (第 46-55 行)
- **问题描述**:
  - 当通过 `GetTexture()` 请求一个不存在的图片路径时：
    ```cpp
    if (GetFileAttributesW(wpath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return (ImTextureID)0;
    }
    ```
  - 函数直接返回 `0`，而**没有将该失败结果记录在 `m_cache` 中**。
- **影响**:
  若某个突变因子、指挥官（如 `HanHorner.png`）或地图图片缺失，UI 每次渲染该元素时都会调用 Windows 内核函数 `GetFileAttributesW` 访问磁盘，每秒重复数十次。

---

### 3.3 `SummaryOverlay` 字体资源泄露
- **涉及文件**: `src/ui/hud/summary_overlay.cpp` (第 78-81 行)
- **问题描述**:
  - `SummaryOverlay::Init` 中两次调用了 `AddFontResourceExW(..., FR_PRIVATE, nullptr)` 加载字体资源。
  - 在 `SummaryOverlay::Shutdown` 中从未调用 `RemoveFontResourceExW`。
- **影响**:
  进程退出前虽然由系统清理，但在模块生命周期内未遵循资源对称释放规范。

---

### 3.4 `ReplayWatcher` 轮询使用遍历整树而非事件通知
- **涉及文件**: `src/replay/replay_watcher.cpp` (第 64-85 行)
- **问题描述**:
  - `WatchLoop` 每隔 1.5 秒通过 `recursive_directory_iterator` 遍历一次星际争霸 2 整个 Accounts 文件夹下的所有文件和子目录。
- **影响**:
  当回放目录累计数万场比赛时，高频遍历整个目录树会导致持续磁盘 I/O。建议改用 Windows 提供的 `ReadDirectoryChangesW` 或仅检查账号根目录修改时间。

---

### 3.5 切换语言后已打开模态弹窗的 ID 失效
- **涉及文件**:
  - `src/ui/modals/modal_game_detail.cpp` (第 15, 22 行)
  - `src/ui/modals/modal_chat_view.cpp` (第 14, 21 行)
  - `src/ui/modals/modal_player_note.cpp` (第 15, 25 行)
- **问题描述**:
  - 模态弹窗通过 `ImGui::OpenPopup(TR("Match Details"))` 打开，并以 `TR("Match Details")` 作为弹窗标题和 ID。
  - 如果弹窗处于打开状态时语言被切换，`TR()` 返回的字符串发生改变，导致弹窗 ID 变化，弹窗可能突然消失或状态无法正常关闭。

---

### 3.6 设置界面每次键盘输入立即触发磁盘保存与注册表写入
- **涉及文件**: `src/ui/tabs/tab_settings.cpp` (第 54-57 行)
- **问题描述**:
  - 在“账号目录”输入框中：
    ```cpp
    if (ImGui::InputText("##AccountFolder", folder_buf, sizeof(folder_buf))) {
        state.account_folder = folder_buf;
        state.SaveSettings();
    }
    ```
- **影响**:
  用户在文本框中每键入一个字符，就会立即调用 `SaveSettings()`，触发 SQLite 数据库写入并打开 Windows 注册表写入自启动项，应改为失焦或回车确认后保存。

---

### 3.7 `ExtractMasteries` 中无符号数下溢与越界风险
- **涉及文件**: `src/replay/s2_decoder.cpp` (第 289 行)
- **问题描述**:
  - `size_t scan_end = std::min(c_idx + 120, initdata_bytes.size() - 25);`
  - 如果传入的损坏数据 `initdata_bytes.size() < 25`，由于 `size_t` 为无符号类型，`initdata_bytes.size() - 25` 将发生向下溢出，产生一个极大的正数（如 `18446744073709551600`）。
- **影响**:
  可能导致指针越界访问损坏数据。

---

### 3.8 Explorer 定位文件路径斜杠格式问题
- **涉及文件**: `src/utils/win32_utils.cpp` (第 74-75 行)
- **问题描述**:
  - `Win32Utils::RevealFileInExplorer` 构建的参数为：
    ```cpp
    std::wstring param = L"/select,\"" + wpath + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", param.c_str(), nullptr, SW_SHOWNORMAL);
    ```
  - Windows `explorer.exe /select,` 要求路径必须使用反斜杠 `\`。如果数据库或回放路径中包含正斜杠 `/`，资源管理器将无法选中对应文件，仅会打开文档根目录或报错。

---

## 四、架构建议与最佳实践 (Architecture & Best Practices)

1. **测试套件解耦与隔离**:
   - `tests/test_full_e2e.cpp` 硬编码依赖 `dist/sc2_coop/coop_data.db` 必须存在且有记录 (`assert(total > 0)`)，在全新构建环境无法独立运行。
   - `tests/test_real_replays.cpp` 强依赖特定中文回放文件名（如 `[CM] 湮灭快车 - P.SC2Replay`）。
   - 测试运行生成的 `.db` 测试文件没有在退出时清理。
   - 建议引入轻量级单元测试框架（如 Catch2 / doctest），并在 `build.py` 中增加 `--test` 指令自动化运行。

2. **图元渲染通道与透明通道混合 (Premultiplied Alpha)**:
   - `AllyToastWindow` 和 `SummaryOverlay` 使用 GDI 在 32 位 DIBSection 上绘制文本和线条，GDI ClearType 并不支持原生 Premultiplied Alpha，代码中使用了手工根据颜色亮度重算 Alpha 通道的 Hack 方案：
     ```cpp
     uint8_t a = (max_c < 180) ? (uint8_t)std::min(255, (int)max_c + 120) : 255;
     ```
   - 这会导致文字边缘在半透明层下产生明显的黑边或杂色光晕。未来建议使用 Direct2D / DirectWrite 替换 GDI，以获得原生的硬件加速与高质量抗锯齿透明图层支持。

---

## 五、问题清单速查表 (Summary Table)

| 编号 | 类别 | 问题简述 | 所在文件及位置 | 严重等级 |
|---|---|---|---|---|
| 1 | 数据/解析 | "HanHorner" 与 "Horner" 命名不一致导致威望、精通、图标全部失效 | `s2_initdata.h:519`, `sc2_constants.h:47` 等 | **严重** |
| 2 | 并发/生命周期 | `AutoRepairCorruptedReplaysAsync` 使用 detach，退出时产生 UAF 崩溃 | `app_state.cpp:275-304` | **严重** |
| 3 | 并发/线程安全 | `AppState::user_handles` 无锁并发读写产生数据竞争 | `app_state.h:103`, `replay_parser.cpp:288` | **严重** |
| 4 | 系统 API | 主线程缺少 `CoInitializeEx`，导致文件选择对话框无法呼出 | `main.cpp:60`, `win32_utils.cpp:48` | **严重** |
| 5 | 数据库/崩溃 | `sqlite3_column_text` 缺少 nullptr 校验直接赋值给 `std::string` | `database.cpp:341-350, 360-365` | **严重** |
| 6 | 异常安全 | 崩溃回调 `CrashHandler` 中使用不安全的 C++ 堆流操作 | `main.cpp:11-52` | **严重** |
| 7 | UI/功耗 | 最小化到托盘窗口隐藏后仍以 60 FPS 持续渲染与呈现 | `ui_manager.cpp:108-111, 467-470` | **中等** |
| 8 | 构建系统 | `build.py` 增量编译未检查头文件时间戳，存在 ODR 违规隐患 | `build.py:96-100` | **中等** |
| 9 | 回放解析 | 回放难度提取读取了错误槽位 (`slots[2]/[3]`)，普通游戏易被误判为残酷 | `replay_parser.cpp:171-172` | **中等** |
| 10 | 系统输入 | 全局底层键盘钩子无条件拦截游戏内 ESC 键 | `hotkey_manager.cpp:179-189` | **中等** |
| 11 | 数据库/UI | `QueryPlayers` 搜索时总记录数未加过滤，导致分页页码错误 | `database.cpp:405-412` | **中等** |
| 12 | 数据库 | `QueryPlayers` 缺少 `total_games` 排序分支支持 | `database.cpp:414-420` | **中等** |
| 13 | 国际化 | `I18n` 语言切换存在无锁并发访问与内部指针失效风险 | `i18n.cpp:137, 194-201` | **中等** |
| 14 | 数据库/安全 | `CalculateStats` 采用手工字符串转义拼接 SQL | `database.cpp:603-630` | **中等** |
| 15 | 数据库/性能 | `RepairWeeklyMutationsIfNeeded` 在事务中执行磁盘文件读取引发长时间锁库 | `database.cpp:1010-1028` | **中等** |
| 16 | UI/性能 | `TabGames` 和 `TabPlayers` 每一帧都在重复执行全表聚合 SQL 查询 | `tab_games.cpp:50`, `tab_players.cpp:59` | **轻微/性能** |
| 17 | 资源加载 | 缺失贴图失败未缓存，导致每帧重复调用文件系统 API | `texture_loader.cpp:47-49` | **轻微/性能** |
| 18 | 资源泄露 | `SummaryOverlay` 中的 GDI 字体资源在退出时未卸载 | `summary_overlay.cpp:79-81` | **轻微** |
| 19 | 磁盘 I/O | `ReplayWatcher` 采用 1.5s 全树递归扫描，开销较大 | `replay_watcher.cpp:64-85` | **轻微/性能** |
| 20 | UI 交互 | 设置界面文本框每输入一个字符即刻写库写注册表 | `tab_settings.cpp:54-57` | **轻微** |
| 21 | 二进制解析 | `ExtractMasteries` 在数据长度小于 25 字节时发生 `size_t` 减法下溢 | `s2_decoder.cpp:289` | **轻微** |
| 22 | 系统集成 | Explorer 文件定位未将正斜杠统一转换为反斜杠 | `win32_utils.cpp:74` | **轻微** |
| 23 | 测试质量 | 测试用例硬编码外部数据与特定本地中文回放文件名，测试后未清理 DB | `tests/*.cpp` | **轻微** |
