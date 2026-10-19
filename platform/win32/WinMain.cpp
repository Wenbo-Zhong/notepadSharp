#ifndef WINVER
#define WINVER 0x0601
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>

#include "App.h"
#include "ToolbarIcons.h"
#include "SearchDialog.h"
#include "Scintilla.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifdef _MSC_VER
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#endif

namespace {

constexpr wchar_t kClassName[] = L"NotepadSharpFrame";
constexpr int kTabId = 100;
constexpr int kStatusId = 101;

UINT windowDpi(HWND hwnd) {
    using GetDpi = UINT (WINAPI *)(HWND);
    GetDpi getDpi = nullptr;
    FARPROC address = GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    static_assert(sizeof(getDpi) == sizeof(address));
    std::memcpy(&getDpi, &address, sizeof(getDpi));
    if (getDpi && hwnd) return getDpi(hwnd);
    HDC dc = GetDC(hwnd);
    const UINT dpi = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc) ReleaseDC(hwnd, dc);
    return dpi;
}

// Scale dialogs created with logical 96-DPI coordinates and give them a readable UI font.
HFONT scaleDialog(HWND hwnd) {
    const UINT dpi = windowDpi(hwnd);
    HFONT font = CreateFontW(-MulDiv(9, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    for (HWND child = GetWindow(hwnd, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        RECT r{}; GetWindowRect(child, &r); MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT *>(&r), 2);
        MoveWindow(child, MulDiv(r.left, dpi, 96), MulDiv(r.top, dpi, 96),
            MulDiv(r.right-r.left, dpi, 96), MulDiv(r.bottom-r.top, dpi, 96), TRUE);
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
    RECT r{}; GetClientRect(hwnd, &r);
    RECT outer{0, 0, MulDiv(r.right, dpi, 96), MulDiv(r.bottom, dpi, 96)};
    AdjustWindowRectEx(&outer, GetWindowLongW(hwnd, GWL_STYLE), FALSE, GetWindowLongW(hwnd, GWL_EXSTYLE));
    SetWindowPos(hwnd, nullptr, 0, 0, outer.right-outer.left, outer.bottom-outer.top, SWP_NOMOVE | SWP_NOZORDER);
    return font;
}

struct EmbeddedFonts {
    std::vector<HANDLE> handles;
    explicit EmbeddedFonts(HINSTANCE instance) {
        for (int id = 500; id < 516; ++id) {
            HRSRC resource = FindResourceW(instance, MAKEINTRESOURCEW(id), RT_RCDATA);
            if (!resource) continue;
            HGLOBAL data = LoadResource(instance, resource);
            DWORD count = 0;
            HANDLE font = AddFontMemResourceEx(LockResource(data), SizeofResource(instance, resource), nullptr, &count);
            if (font) handles.push_back(font);
        }
    }
    ~EmbeddedFonts() { for (HANDLE font : handles) RemoveFontMemResourceEx(font); }
};

std::wstring widen(const std::string &text) {
    if (text.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
    return wide;
}

std::string narrow(const std::wstring &text) {
    if (text.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), size, nullptr, nullptr);
    return utf8;
}

HMENU makeMenu() {
    HMENU bar = CreateMenu();
    HMENU file = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, CmdNew, L"新建(&N)\tCtrl+N");
    AppendMenuW(file, MF_STRING, CmdOpen, L"打开(&O)...\tCtrl+O");
    HMENU recent = CreatePopupMenu();
    MENUINFO recentInfo{}; recentInfo.cbSize = sizeof(recentInfo); recentInfo.fMask = MIM_MENUDATA; recentInfo.dwMenuData = 1;
    SetMenuInfo(recent, &recentInfo);
    AppendMenuW(file, MF_POPUP, reinterpret_cast<UINT_PTR>(recent), L"最近文件");
    AppendMenuW(file, MF_STRING, CmdSave, L"保存(&S)\tCtrl+S");
    AppendMenuW(file, MF_STRING, CmdSaveAs, L"另存为(&A)...");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, CmdClose, L"关闭(&W)\tCtrl+W");
    AppendMenuW(file, MF_STRING, CmdExit, L"退出(&X)");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"文件(&F)");

    HMENU edit = CreatePopupMenu();
    AppendMenuW(edit, MF_STRING, CmdUndo, L"撤销(&U)\tCtrl+Z");
    AppendMenuW(edit, MF_STRING, CmdRedo, L"重做(&R)\tCtrl+Y");
    AppendMenuW(edit, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(edit, MF_STRING, CmdCut, L"剪切(&T)\tCtrl+X");
    AppendMenuW(edit, MF_STRING, CmdCopy, L"复制(&C)\tCtrl+C");
    AppendMenuW(edit, MF_STRING, CmdPaste, L"粘贴(&P)\tCtrl+V");
    AppendMenuW(edit, MF_STRING, CmdDelete, L"删除(&D)\tDel");
    AppendMenuW(edit, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(edit, MF_STRING, CmdSelectAll, L"全选(&A)\tCtrl+A");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(edit), L"编辑(&E)");

    HMENU search = CreatePopupMenu();
    AppendMenuW(search, MF_STRING, CmdFind, L"查找(&F)...\tCtrl+F");
    AppendMenuW(search, MF_STRING, CmdFindNext, L"查找下一个(&N)\tF3");
    AppendMenuW(search, MF_STRING, CmdFindPrev, L"查找上一个(&P)\tShift+F3");
    AppendMenuW(search, MF_STRING, CmdReplace, L"替换(&R)...\tCtrl+H");
    AppendMenuW(search, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(search, MF_STRING, CmdGoto, L"转到行(&G)...\tCtrl+G");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(search), L"搜索(&S)");

    HMENU view = CreatePopupMenu();
    AppendMenuW(view, MF_STRING, CmdWrap, L"自动换行(&W)");
    AppendMenuW(view, MF_STRING, CmdLineNumbers, L"行号(&L)");
    AppendMenuW(view, MF_STRING, CmdWhitespace, L"空白字符(&S)");
    AppendMenuW(view, MF_STRING, CmdEol, L"换行符(&E)");
    AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(view, MF_STRING, CmdZoomIn, L"放大(&I)\tCtrl+=");
    AppendMenuW(view, MF_STRING, CmdZoomOut, L"缩小(&O)\tCtrl+-");
    AppendMenuW(view, MF_STRING, CmdZoomReset, L"重置缩放(&R)\tCtrl+0");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(view), L"视图(&V)");
    HMENU tools = CreatePopupMenu();
    for (const char *group : {"会话与文件", "编码", "搜索结果", "文本处理", "书签与折叠", "宏", "代码补全", "视图"}) {
        HMENU submenu = CreatePopupMenu();
        for (const auto &entry : extraCommands) if (std::strcmp(extraCommandGroup(entry.command), group) == 0)
            AppendMenuW(submenu, MF_STRING, entry.command, widen(entry.label).c_str());
        AppendMenuW(tools, MF_POPUP, reinterpret_cast<UINT_PTR>(submenu), widen(group).c_str());
    }
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(tools), L"代码工具(&T)");
    HMENU languages = CreatePopupMenu();
    for (int i=0;i<languageChoiceCount;++i) AppendMenuW(languages, MF_STRING, CmdLanguageBase + i, widen(languageChoices[i].label).c_str());
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(languages), L"语言(&L)");
    HMENU font = CreatePopupMenu();
    HMENU sizes = CreatePopupMenu();
    for (int size = 6; size <= 72; ++size) AppendMenuW(sizes, MF_STRING, CmdFontSizeBase + size, std::to_wstring(size).c_str());
    AppendMenuW(font, MF_POPUP, reinterpret_cast<UINT_PTR>(sizes), L"字号（pt）");
    const wchar_t *weights[] = {L"Thin 100", L"ExtraLight 200", L"Light 300", L"Regular 400", L"Medium 500", L"SemiBold 600", L"Bold 700", L"ExtraBold 800"};
    for (int i = 0; i < 8; ++i) AppendMenuW(font, MF_STRING, CmdFontWeightBase + i, weights[i]);
    AppendMenuW(font, MF_STRING, CmdFontItalic, L"切换斜体");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(font), L"字体(&P)");
    HMENU documents = CreatePopupMenu();
    MENUINFO documentsInfo{}; documentsInfo.cbSize = sizeof(documentsInfo); documentsInfo.fMask = MIM_MENUDATA; documentsInfo.dwMenuData = 2;
    SetMenuInfo(documents, &documentsInfo);
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(documents), L"窗口(&W)");
    return bar;
}

struct PromptState {
    FindRequest *request = nullptr;
    bool replaceMode = false;
    HWND find = nullptr;
    HWND repl = nullptr;
    HWND matchCase = nullptr;
    HWND wholeWord = nullptr;
    HWND regex = nullptr;
};

PromptState *promptState(HWND hwnd) {
    return reinterpret_cast<PromptState *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

void finishPrompt(HWND hwnd, FindAction action, bool cancelled) {
    PromptState *state = promptState(hwnd);
    if (!cancelled && state && state->request) {
        wchar_t buffer[2048];
        GetWindowTextW(state->find, buffer, 2048);
        state->request->find = narrow(buffer);
        GetWindowTextW(state->repl, buffer, 2048);
        state->request->replace = narrow(buffer);
        state->request->matchCase = SendMessageW(state->matchCase, BM_GETCHECK, 0, 0) == BST_CHECKED;
        state->request->wholeWord = SendMessageW(state->wholeWord, BM_GETCHECK, 0, 0) == BST_CHECKED;
        state->request->regex = SendMessageW(state->regex, BM_GETCHECK, 0, 0) == BST_CHECKED;
        state->request->action = action;
        state->request->cancelled = state->request->find.empty();
    } else if (state && state->request) {
        state->request->cancelled = true;
    }
    DestroyWindow(hwnd);
}

LRESULT CALLBACK promptProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_CREATE) {
        auto *state = reinterpret_cast<PromptState *>(reinterpret_cast<CREATESTRUCTW *>(lParam)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        CreateWindowW(L"STATIC", L"查找", WS_CHILD | WS_VISIBLE, 12, 14, 60, 20, hwnd, nullptr, nullptr, nullptr);
        state->find = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", widen(state->request->find).c_str(),
                                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 80, 12, 280, 24,
                                       hwnd, reinterpret_cast<HMENU>(1), nullptr, nullptr);
        if (state->replaceMode) {
            CreateWindowW(L"STATIC", L"替换", WS_CHILD | WS_VISIBLE, 12, 46, 60, 20, hwnd, nullptr, nullptr, nullptr);
            state->repl = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", widen(state->request->replace).c_str(),
                                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 80, 44, 280, 24,
                                           hwnd, reinterpret_cast<HMENU>(2), nullptr, nullptr);
        }
        state->matchCase = CreateWindowW(L"BUTTON", L"区分大小写", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                          12, state->replaceMode ? 80 : 48, 140, 22, hwnd, nullptr, nullptr, nullptr);
        state->wholeWord = CreateWindowW(L"BUTTON", L"全字匹配", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                          170, state->replaceMode ? 80 : 48, 140, 22, hwnd, nullptr, nullptr, nullptr);
        SendMessageW(state->matchCase, BM_SETCHECK, state->request->matchCase ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(state->wholeWord, BM_SETCHECK, state->request->wholeWord ? BST_CHECKED : BST_UNCHECKED, 0);
        state->regex = CreateWindowW(L"BUTTON", L"正则表达式（替换引用：\\1）", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            12, state->replaceMode ? 106 : 74, 340, 22, hwnd, nullptr, nullptr, nullptr);
        SendMessageW(state->regex, BM_SETCHECK, state->request->regex ? BST_CHECKED : BST_UNCHECKED, 0);
        const int y = state->replaceMode ? 140 : 108;
        CreateWindowW(L"BUTTON", L"查找下一个", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                      12, y, 100, 26, hwnd, reinterpret_cast<HMENU>(IDOK), nullptr, nullptr);
        if (state->replaceMode) {
            CreateWindowW(L"BUTTON", L"替换", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 120, y, 70, 26, hwnd,
                          reinterpret_cast<HMENU>(3), nullptr, nullptr);
            CreateWindowW(L"BUTTON", L"全部替换", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 198, y, 80, 26, hwnd,
                          reinterpret_cast<HMENU>(4), nullptr, nullptr);
        }
        CreateWindowW(L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 286, y, 74, 26, hwnd,
                      reinterpret_cast<HMENU>(IDCANCEL), nullptr, nullptr);
        return 0;
    }
    if (msg == WM_COMMAND) {
        switch (LOWORD(wParam)) {
        case IDOK: finishPrompt(hwnd, FindAction::Next, false); return 0;
        case 3: finishPrompt(hwnd, FindAction::ReplaceOne, false); return 0;
        case 4: finishPrompt(hwnd, FindAction::ReplaceAll, false); return 0;
        case IDCANCEL: finishPrompt(hwnd, FindAction::Next, true); return 0;
        }
    }
    if (msg == WM_CLOSE) {
        finishPrompt(hwnd, FindAction::Next, true);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

class WinHost : public Host {
public:
    HINSTANCE instance = nullptr;
    HWND frame = nullptr;
    HWND tabs = nullptr;
    HWND toolbar = nullptr;
    HWND status = nullptr;
    App *app = nullptr;
    SearchDialog searchDialog;
    int nextId = 1;
    struct Slot {
        int id;
        HWND editor;
    };
    std::vector<Slot> slots;
    bool closing = false;
    WINDOWPLACEMENT normalPlacement{};
    LONG normalStyle = 0;
    void setFullScreen(bool enabled) override {
        if (enabled) {
            normalPlacement.length = sizeof(normalPlacement);
            normalStyle = GetWindowLongW(frame, GWL_STYLE); GetWindowPlacement(frame, &normalPlacement);
            MONITORINFO monitor{}; monitor.cbSize = sizeof(monitor);
            GetMonitorInfoW(MonitorFromWindow(frame, MONITOR_DEFAULTTONEAREST), &monitor);
            SetWindowLongW(frame, GWL_STYLE, normalStyle & ~WS_OVERLAPPEDWINDOW);
            SetWindowPos(frame, nullptr, monitor.rcMonitor.left, monitor.rcMonitor.top,
                monitor.rcMonitor.right-monitor.rcMonitor.left, monitor.rcMonitor.bottom-monitor.rcMonitor.top,
                SWP_NOZORDER | SWP_FRAMECHANGED);
        } else {
            SetWindowLongW(frame, GWL_STYLE, normalStyle); SetWindowPlacement(frame, &normalPlacement);
            SetWindowPos(frame, nullptr, 0,0,0,0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        }
    }
    void setAlwaysOnTop(bool enabled) override {
        SetWindowPos(frame, enabled ? HWND_TOPMOST : HWND_NOTOPMOST, 0,0,0,0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    HFONT uiFont = nullptr;
    HFONT toolbarFont = nullptr;
    HIMAGELIST toolbarIcons = nullptr;
    ~WinHost() override { if (uiFont) DeleteObject(uiFont); if (toolbarFont) DeleteObject(toolbarFont); if (toolbarIcons) ImageList_Destroy(toolbarIcons); }
    void updateDpi() {
        HFONT old = uiFont;
        uiFont = CreateFontW(-MulDiv(9, windowDpi(frame), 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        SendMessageW(tabs, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont), TRUE);
        HFONT oldToolbarFont = toolbarFont;
        toolbarFont = CreateFontW(-MulDiv(8, windowDpi(frame), 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        SendMessageW(toolbar, WM_SETFONT, reinterpret_cast<WPARAM>(toolbarFont), TRUE);
        if (oldToolbarFont) DeleteObject(oldToolbarFont);
        HIMAGELIST icons = createToolbarIcons(windowDpi(frame));
        if (icons) {
            SendMessageW(toolbar, TB_SETIMAGELIST, 0, reinterpret_cast<LPARAM>(icons));
            if (toolbarIcons) ImageList_Destroy(toolbarIcons);
            toolbarIcons = icons;
        }
        SendMessageW(toolbar, TB_SETBITMAPSIZE, 0, MAKELPARAM(MulDiv(22, windowDpi(frame), 96), MulDiv(22, windowDpi(frame), 96)));
        SendMessageW(toolbar, TB_SETBUTTONSIZE, 0, MAKELPARAM(MulDiv(64, windowDpi(frame), 96), MulDiv(46, windowDpi(frame), 96)));
        SendMessageW(status, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont), TRUE);
        SendMessageW(status, SB_SETMINHEIGHT, MulDiv(22, windowDpi(frame), 96), 0);
        if (old) DeleteObject(old);
        if (app) app->refreshView();
        layout();
    }

    explicit WinHost(HINSTANCE inst) : instance(inst) {}

    Slot *slot(int id) {
        for (Slot &item : slots) {
            if (item.id == id) {
                return &item;
            }
        }
        return nullptr;
    }

    intptr_t send(int doc, unsigned msg, uintptr_t wParam, intptr_t lParam) override {
        Slot *item = slot(doc);
        if (!item) {
            return 0;
        }
        return static_cast<intptr_t>(SendMessageW(item->editor, msg, static_cast<WPARAM>(wParam), static_cast<LPARAM>(lParam)));
    }

    int createEditor() override {
        Slot item;
        item.id = nextId++;
        item.editor = CreateWindowExW(0, L"Scintilla", L"", WS_CHILD | WS_TABSTOP | WS_VSCROLL | WS_HSCROLL,
                                       0, 0, 100, 100, frame, nullptr, instance, nullptr);
        if (!item.editor) {
            wchar_t buffer[160];
            swprintf(buffer, 160, L"无法创建编辑器，错误 %lu", GetLastError());
            MessageBoxW(frame, buffer, L"NotepadSharp", MB_OK | MB_ICONERROR);
            return 0;
        }
        SendMessageW(item.editor, SCI_SETMODEVENTMASK, SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT, 0);
        // Memory-private fonts are visible to GDI; DirectWrite's system font
        // collection does not include AddFontMemResourceEx registrations.
        SendMessageW(item.editor, SCI_SETFONTQUALITY, SC_EFF_QUALITY_LCD_OPTIMIZED, 0);
        slots.push_back(item);
        TCITEMW tab{};
        tab.mask = TCIF_TEXT | TCIF_PARAM;
        std::wstring title = L"未命名";
        tab.pszText = title.data();
        tab.lParam = item.id;
        const int index = TabCtrl_InsertItem(tabs, TabCtrl_GetItemCount(tabs), &tab);
        TabCtrl_SetCurSel(tabs, index);
        layout();
        return item.id;
    }

    void destroyEditor(int doc) override {
        const int count = TabCtrl_GetItemCount(tabs);
        for (int i = 0; i < count; ++i) {
            TCITEMW tab{};
            tab.mask = TCIF_PARAM;
            TabCtrl_GetItem(tabs, i, &tab);
            if (tab.lParam == doc) {
                TabCtrl_DeleteItem(tabs, i);
                break;
            }
        }
        Slot *item = slot(doc);
        if (item) {
            DestroyWindow(item->editor);
            slots.erase(std::remove_if(slots.begin(), slots.end(), [&](const Slot &slot) { return slot.id == doc; }), slots.end());
        }
    }

    void showEditor(int doc) override {
        for (Slot &item : slots) {
            ShowWindow(item.editor, item.id == doc ? SW_SHOW : SW_HIDE);
        }
        const int count = TabCtrl_GetItemCount(tabs);
        for (int i = 0; i < count; ++i) {
            TCITEMW tab{};
            tab.mask = TCIF_PARAM;
            TabCtrl_GetItem(tabs, i, &tab);
            if (tab.lParam == doc) {
                TabCtrl_SetCurSel(tabs, i);
                break;
            }
        }
        layout();
        if (Slot *item = slot(doc)) {
            SetFocus(item->editor);
        }
    }

    void setWindowTitle(const std::string &title) override {
        SetWindowTextW(frame, widen(title).c_str());
    }

    void setStatus(const std::string &text) override {
        const std::wstring wide = widen(text);
        SendMessageW(status, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(wide.c_str()));
    }

    void setTabTitle(int doc, const std::string &title) override {
        const int count = TabCtrl_GetItemCount(tabs);
        for (int i = 0; i < count; ++i) {
            TCITEMW tab{};
            tab.mask = TCIF_PARAM;
            TabCtrl_GetItem(tabs, i, &tab);
            if (tab.lParam == doc) {
                std::wstring wide = widen(title);
                TCITEMW updated{};
                updated.mask = TCIF_TEXT;
                updated.pszText = wide.data();
                TabCtrl_SetItem(tabs, i, &updated);
                break;
            }
        }
    }

    std::string promptDirectory() override {
        BROWSEINFOW info{}; info.hwndOwner = frame; info.lpszTitle = L"选择搜索文件夹";
        info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
        PIDLIST_ABSOLUTE selected = SHBrowseForFolderW(&info);
        if (!selected) return {};
        wchar_t path[MAX_PATH]{};
        const BOOL ok = SHGetPathFromIDListW(selected, path);
        CoTaskMemFree(selected);
        return ok ? narrow(path) : std::string();
    }
    std::string promptOpenPath() override {
        wchar_t buffer[MAX_PATH] = {};
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = frame;
        ofn.lpstrFilter = L"所有文件\0*.*\0文本\0*.txt\0";
        ofn.lpstrFile = buffer;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        if (!GetOpenFileNameW(&ofn)) {
            return {};
        }
        return narrow(buffer);
    }

    std::string promptSavePath(const std::string &suggested) override {
        wchar_t buffer[MAX_PATH] = {};
        const std::wstring suggestedWide = widen(suggested);
        wcsncpy(buffer, suggestedWide.c_str(), MAX_PATH - 1);
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = frame;
        ofn.lpstrFilter = L"所有文件\0*.*\0文本\0*.txt\0";
        ofn.lpstrFile = buffer;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
        if (!GetSaveFileNameW(&ofn)) {
            return {};
        }
        return narrow(buffer);
    }

    SaveChoice askSave(const std::string &name) override {
        const std::wstring text = L"是否保存对“" + widen(name) + L"”的更改？";
        const int answer = MessageBoxW(frame, text.c_str(), L"NotepadSharp", MB_YESNOCANCEL | MB_ICONWARNING);
        if (answer == IDYES) return SaveChoice::Save;
        if (answer == IDNO) return SaveChoice::Discard;
        return SaveChoice::Cancel;
    }

    void alert(const std::string &message) override {
        MessageBoxW(frame, widen(message).c_str(), L"NotepadSharp", MB_OK | MB_ICONINFORMATION);
    }

    bool showFindDialog(const FindRequest &seed, bool replaceMode, const std::function<void(const FindRequest &)> &perform) override {
        searchDialog.show(instance, frame, seed, replaceMode, perform); return true;
    }
    void findFeedback(const std::string &message) override { if (!searchDialog.feedback(message)) setStatus(message); }
    FindRequest promptFind(const FindRequest &seed, bool replaceMode) override {
        FindRequest request = seed;
        request.cancelled = true;
        PromptState state;
        state.request = &request;
        state.replaceMode = replaceMode;
        WNDCLASSW wc{};
        wc.lpfnWndProc = promptProc;
        wc.hInstance = instance;
        wc.lpszClassName = L"NotepadSharpPrompt";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        RegisterClassW(&wc);
        const int height = replaceMode ? 214 : 180;
        HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME, wc.lpszClassName, replaceMode ? L"替换" : L"查找",
                                       WS_POPUP | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 390, height,
                                       frame, nullptr, instance, &state);
        RECT frameRect{};
        GetWindowRect(frame, &frameRect);
        SetWindowPos(dialog, HWND_TOP, frameRect.left + 80, frameRect.top + 80, 0, 0, SWP_NOSIZE);
        HFONT dialogFont = scaleDialog(dialog);
        ShowWindow(dialog, SW_SHOW);
        EnableWindow(frame, FALSE);
        MSG msg;
        while (IsWindow(dialog) && GetMessageW(&msg, nullptr, 0, 0)) {
            if (!IsDialogMessageW(dialog, &msg)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
        DeleteObject(dialogFont);
        EnableWindow(frame, TRUE);
        SetForegroundWindow(frame);
        return request;
    }

    int promptGoto(int currentLine, int lineCount) override { return promptNumber("转到行", currentLine, lineCount); }
    int promptNumber(const std::string &title, int currentLine, int lineCount) override {
        wchar_t buffer[32];
        swprintf(buffer, 32, L"%d", currentLine);
        std::wstring caption = widen(title) + L" (1-" + std::to_wstring(lineCount) + L")";
        // Use a tiny owned dialog built on the prompt window class with one edit.
        struct GotoState {
            std::wstring text;
            bool ok = false;
        } gotoState;
        gotoState.text = buffer;
        WNDCLASSW wc{};
        wc.lpfnWndProc = [](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) -> LRESULT {
            if (msg == WM_CREATE) {
                auto *state = reinterpret_cast<GotoState *>(reinterpret_cast<CREATESTRUCTW *>(lParam)->lpCreateParams);
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
                CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", state->text.c_str(),
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL,
                                12, 12, 220, 24, hwnd, reinterpret_cast<HMENU>(1), nullptr, nullptr);
                CreateWindowW(L"BUTTON", L"确定", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                              12, 48, 90, 26, hwnd, reinterpret_cast<HMENU>(IDOK), nullptr, nullptr);
                CreateWindowW(L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                              142, 48, 90, 26, hwnd, reinterpret_cast<HMENU>(IDCANCEL), nullptr, nullptr);
                return 0;
            }
            auto *state = reinterpret_cast<GotoState *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (msg == WM_COMMAND && (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)) {
                if (LOWORD(wParam) == IDOK && state) {
                    wchar_t text[32];
                    GetWindowTextW(GetDlgItem(hwnd, 1), text, 32);
                    state->text = text;
                    state->ok = true;
                }
                DestroyWindow(hwnd);
                return 0;
            }
            if (msg == WM_CLOSE) {
                DestroyWindow(hwnd);
                return 0;
            }
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        };
        wc.hInstance = instance;
        wc.lpszClassName = L"NotepadSharpGoto";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        RegisterClassW(&wc);
        HWND box = CreateWindowExW(WS_EX_DLGMODALFRAME, wc.lpszClassName, caption.c_str(),
                                    WS_POPUP | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 260, 120,
                                    frame, nullptr, instance, &gotoState);
        HFONT dialogFont = scaleDialog(box);
        ShowWindow(box, SW_SHOW);
        EnableWindow(frame, FALSE);
        MSG msg;
        while (IsWindow(box) && GetMessageW(&msg, nullptr, 0, 0)) {
            if (!IsDialogMessageW(box, &msg)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
        EnableWindow(frame, TRUE);
        DeleteObject(dialogFont);
        if (!gotoState.ok) {
            return 0;
        }
        return _wtoi(gotoState.text.c_str());
    }

    std::string stateDirectory() const override {
        wchar_t path[32768]{};
        const DWORD length = GetEnvironmentVariableW(L"APPDATA", path, 32768);
        return length > 0 && length < 32768 ? narrow(std::wstring(path) + L"\\NotepadSharp") : std::string();
    }
    bool askReloadExternal(const std::string &message) override {
        return MessageBoxW(frame, widen(message).c_str(), L"磁盘文件已变化", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES;
    }
    bool askOverwriteExternal(const std::string &message) override {
        return MessageBoxW(frame, widen(message).c_str(), L"确认覆盖外部修改", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES;
    }
    std::string monoFont() const override { return "JetBrains Mono"; }
    int defaultEol() const override { return SC_EOL_CRLF; }

    void quit() override {
        SendMessageW(frame, WM_CLOSE, 0, 0);
    }

    void layout() {
        RECT rect{};
        GetClientRect(frame, &rect);
        SendMessageW(status, WM_SIZE, 0, 0);
        RECT statusRect{};
        GetWindowRect(status, &statusRect);
        const int statusHeight = statusRect.bottom - statusRect.top;
        SendMessageW(toolbar, TB_AUTOSIZE, 0, 0);
        RECT toolbarRect{}; GetWindowRect(toolbar, &toolbarRect);
        const int toolbarHeight = toolbar ? toolbarRect.bottom - toolbarRect.top : 0;
        const int tabHeight = MulDiv(30, windowDpi(frame), 96);
        MoveWindow(tabs, 0, toolbarHeight, rect.right, tabHeight, TRUE);
        for (Slot &item : slots) {
            MoveWindow(item.editor, 0, toolbarHeight + tabHeight, rect.right, std::max(0L, rect.bottom - toolbarHeight - tabHeight - statusHeight), TRUE);
        }
    }

    void showTabMenu(POINT screen) {
        int index = TabCtrl_GetCurSel(tabs);
        if (screen.x == -1 && screen.y == -1) {
            RECT rect{}; if (index >= 0) TabCtrl_GetItemRect(tabs, index, &rect);
            screen = {rect.left + 12, rect.bottom}; ClientToScreen(tabs, &screen);
        } else {
            TCHITTESTINFO hit{}; hit.pt = screen; ScreenToClient(tabs, &hit.pt);
            index = TabCtrl_HitTest(tabs, &hit);
        }
        if (index < 0 || !app) return;
        TCITEMW item{}; item.mask = TCIF_PARAM; TabCtrl_GetItem(tabs, index, &item);
        app->onTabChanged(static_cast<int>(item.lParam));
        HMENU menu = CreatePopupMenu();
        const Command commands[] = {CmdSave, CmdSaveAs, CmdSaveCopy, CmdReload, CmdReadOnly, CmdClose, CmdCloseOthers, CmdCloseLeft, CmdCloseRight, CmdCopyFullPath, CmdCopyFileName, CmdCopyDirectory};
        const wchar_t *labels[] = {L"保存", L"另存为...", L"拷贝另存为...", L"重新读取文件", L"编辑器只读", L"关闭", L"关闭其他文档", L"关闭左侧文档", L"关闭右侧文档", L"复制完整文件路径", L"复制文件名", L"复制所在目录路径"};
        for (size_t i=0; i<sizeof(commands)/sizeof(commands[0]); ++i) {
            if (i == 5 || i == 9) AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, commands[i], labels[i]);
        }
        if (Slot *selected = slot(static_cast<int>(item.lParam))) CheckMenuItem(menu, CmdReadOnly, MF_BYCOMMAND | (SendMessageW(selected->editor, SCI_GETREADONLY, 0, 0) ? MF_CHECKED : MF_UNCHECKED));
        const UINT chosen = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y, 0, frame, nullptr);
        DestroyMenu(menu); if (chosen) app->onCommand(static_cast<Command>(chosen));
    }

    int docFromTab() const {
        const int index = TabCtrl_GetCurSel(tabs);
        if (index < 0) {
            return 0;
        }
        TCITEMW tab{};
        tab.mask = TCIF_PARAM;
        TabCtrl_GetItem(tabs, index, &tab);
        return static_cast<int>(tab.lParam);
    }
};

LRESULT CALLBACK tabContextProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
    if (msg == WM_CONTEXTMENU) {
        auto *host = reinterpret_cast<WinHost *>(data);
        host->showTabMenu({static_cast<short>(LOWORD(l)), static_cast<short>(HIWORD(l))});
        return 0;
    }
    return DefSubclassProc(hwnd, msg, w, l);
}

WinHost *hostFrom(HWND hwnd) {
    return reinterpret_cast<WinHost *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

LRESULT CALLBACK frameProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    WinHost *host = hostFrom(hwnd);
    switch (msg) {
    case WM_CREATE: {
        auto *created = reinterpret_cast<WinHost *>(reinterpret_cast<CREATESTRUCTW *>(lParam)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(created));
        return 0;
    }
    case WM_TIMER:
        if (host && host->app && wParam == 1) host->app->checkpoint();
        if (host && host->app && wParam == 2) host->app->pollFiles();
        return 0;
    case WM_DPICHANGED:
        if (host) {
            const auto *rect = reinterpret_cast<const RECT *>(lParam);
            SetWindowPos(hwnd, nullptr, rect->left, rect->top, rect->right-rect->left, rect->bottom-rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
            host->updateDpi();
        }
        return 0;
    case WM_SIZE:
        if (host) host->layout();
        return 0;
    case WM_COMMAND:
        if (host && host->app && HIWORD(wParam) <= 1) {
            host->app->onCommand(static_cast<Command>(LOWORD(wParam)));
        }
        return 0;
    case WM_NOTIFY: {
        auto *header = reinterpret_cast<NMHDR *>(lParam);
        if (host && header->hwndFrom == host->tabs && header->code == TCN_SELCHANGE) {
            host->app->onTabChanged(host->docFromTab());
            return 0;
        }
        if (host && host->app) {
            for (const WinHost::Slot &item : host->slots) {
                if (header->hwndFrom == item.editor) {
                    host->app->onNotify(item.id, reinterpret_cast<SCNotification *>(lParam));
                    break;
                }
            }
        }
        return 0;
    }
    case WM_INITMENUPOPUP:
        if (host && host->app) {
            HMENU menu = reinterpret_cast<HMENU>(wParam);
            MENUINFO info{}; info.cbSize = sizeof(info); info.fMask = MIM_MENUDATA; GetMenuInfo(menu, &info);
            if (info.dwMenuData == 1 || info.dwMenuData == 2) {
                while (GetMenuItemCount(menu) > 0) DeleteMenu(menu, 0, MF_BYPOSITION);
                const auto labels = info.dwMenuData == 1 ? host->app->recentFiles() : host->app->documentNames();
                const int base = info.dwMenuData == 1 ? CmdRecentBase : CmdDocumentBase;
                for (size_t i=0; i<labels.size() && i<1000; ++i) {
                    auto label = std::to_wstring(i+1) + L"  " + widen(labels[i]);
                    for (size_t pos=0; (pos=label.find(L'&',pos))!=std::wstring::npos; pos+=2) label.insert(pos,1,L'&');
                    AppendMenuW(menu, MF_STRING, base+i, label.c_str());
                }
                if (labels.empty()) AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"（无）");
            }
            CheckMenuItem(menu, CmdMonitorFile, MF_BYCOMMAND | (host->app->monitoring() ? MF_CHECKED : MF_UNCHECKED));
            CheckMenuRadioItem(menu, CmdLanguageBase, CmdLanguageBase + languageChoiceCount - 1, CmdLanguageBase + host->app->languageSelection(), MF_BYCOMMAND);
            CheckMenuItem(menu, CmdFullScreen, MF_BYCOMMAND | (host->app->fullScreen() ? MF_CHECKED : MF_UNCHECKED));
            CheckMenuItem(menu, CmdAlwaysOnTop, MF_BYCOMMAND | (host->app->alwaysOnTop() ? MF_CHECKED : MF_UNCHECKED));
            CheckMenuItem(menu, CmdAutoComplete, MF_BYCOMMAND | (host->app->autoComplete() ? MF_CHECKED : MF_UNCHECKED));
            CheckMenuItem(menu, CmdMacroStart, MF_BYCOMMAND | (host->app->macroRecording() ? MF_CHECKED : MF_UNCHECKED));
            CheckMenuRadioItem(menu, CmdFontSizeBase + 6, CmdFontSizeBase + 72, CmdFontSizeBase + host->app->fontSize(), MF_BYCOMMAND);
            CheckMenuRadioItem(menu, CmdFontWeightBase, CmdFontWeightBase + 7, CmdFontWeightBase + host->app->fontWeight() / 100 - 1, MF_BYCOMMAND);
            CheckMenuItem(menu, CmdFontItalic, MF_BYCOMMAND | (host->app->fontItalic() ? MF_CHECKED : MF_UNCHECKED));
            CheckMenuItem(reinterpret_cast<HMENU>(wParam), CmdWrap, MF_BYCOMMAND | (host->app->wrap() ? MF_CHECKED : MF_UNCHECKED));
            CheckMenuItem(reinterpret_cast<HMENU>(wParam), CmdLineNumbers, MF_BYCOMMAND | (host->app->lineNumbers() ? MF_CHECKED : MF_UNCHECKED));
            CheckMenuItem(reinterpret_cast<HMENU>(wParam), CmdWhitespace, MF_BYCOMMAND | (host->app->whitespace() ? MF_CHECKED : MF_UNCHECKED));
            CheckMenuItem(reinterpret_cast<HMENU>(wParam), CmdEol, MF_BYCOMMAND | (host->app->eolVisible() ? MF_CHECKED : MF_UNCHECKED));
        }
        return 0;
    case WM_CLOSE:
        if (host && host->app && !host->closing) {
            host->closing = true;
            const bool ok = host->app->onCloseRequested();
            host->closing = false;
            if (!ok) {
                return 0;
            }
        }
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    const HRESULT comResult = OleInitialize(nullptr);
    struct ComScope { HRESULT result; ~ComScope() { if (SUCCEEDED(result)) OleUninitialize(); } } comScope{comResult};
    EmbeddedFonts fonts(instance);
    Scintilla_RegisterClasses(instance);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_TAB_CLASSES | ICC_BAR_CLASSES | ICC_COOL_CLASSES};
    InitCommonControlsEx(&controls);

    WNDCLASSW wc{};
    wc.lpfnWndProc = frameProc;
    wc.hInstance = instance;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    WinHost host(instance);
    host.frame = CreateWindowExW(0, kClassName, L"NotepadSharp", WS_OVERLAPPEDWINDOW,
                                  CW_USEDEFAULT, CW_USEDEFAULT, 960, 640, nullptr, makeMenu(), instance, &host);
    host.tabs = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                                 0, 0, 100, 28, host.frame, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTabId)), instance, nullptr);
    SetWindowSubclass(host.tabs, tabContextProc, 1, reinterpret_cast<DWORD_PTR>(&host));
    host.status = CreateWindowExW(0, STATUSCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
                                   0, 0, 100, 20, host.frame, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStatusId)), instance, nullptr);
    host.toolbar = CreateWindowExW(0, TOOLBARCLASSNAMEW, L"常用操作", WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | CCS_TOP | CCS_NODIVIDER,
        0, 0, 0, 0, host.frame, nullptr, instance, nullptr);
    SendMessageW(host.toolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    const Command toolCommands[] = {CmdNew, CmdOpen, CmdSave, CmdSaveAll, CmdUndo, CmdRedo, CmdFind, CmdReplace, CmdFindInFolder, CmdReadOnly};
    const wchar_t *toolLabels[] = {L"新建", L"打开", L"保存", L"全部保存", L"撤销", L"重做", L"查找", L"替换", L"文件夹搜索", L"只读"};
    for (size_t i = 0; i < sizeof(toolCommands)/sizeof(toolCommands[0]); ++i) {
        TBBUTTON button{}; button.iBitmap = static_cast<int>(i); button.idCommand = toolCommands[i]; button.fsState = TBSTATE_ENABLED;
        button.fsStyle = BTNS_BUTTON | BTNS_SHOWTEXT; button.iString = reinterpret_cast<INT_PTR>(toolLabels[i]);
        SendMessageW(host.toolbar, TB_ADDBUTTONSW, 1, reinterpret_cast<LPARAM>(&button));
    }
    SetWindowPos(host.frame, nullptr, 0, 0, MulDiv(960, windowDpi(host.frame), 96), MulDiv(640, windowDpi(host.frame), 96), SWP_NOMOVE | SWP_NOZORDER);
    SendMessageW(host.tabs, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    App app(host);
    host.app = &app;

    ACCEL accels[] = {
        {FVIRTKEY, VK_F11, CmdFullScreen},
        {FCONTROL | FALT | FVIRTKEY, 'F', CmdFoldCurrent},
        {FCONTROL | FALT | FSHIFT | FVIRTKEY, 'F', CmdUnfoldCurrent},
        {FCONTROL | FSHIFT | FVIRTKEY, 'T', CmdReopenClosed},
        {FCONTROL | FVIRTKEY, VK_TAB, CmdNextTab},
        {FCONTROL | FSHIFT | FVIRTKEY, VK_TAB, CmdPrevTab},
        {FCONTROL | FSHIFT | FVIRTKEY, 'S', CmdSaveAll},
        {FCONTROL | FSHIFT | FVIRTKEY, 'W', CmdCloseAll},
        {FCONTROL | FVIRTKEY, 'R', CmdReload},
        {FCONTROL | FVIRTKEY, 'B', CmdGotoBrace},
        {FCONTROL | FALT | FVIRTKEY, 'B', CmdSelectBrace},
        {FCONTROL | FSHIFT | FVIRTKEY, 'F', CmdFindInFolder},
        {FCONTROL | FSHIFT | FVIRTKEY, VK_UP, CmdMoveLineUp},
        {FCONTROL | FSHIFT | FVIRTKEY, VK_DOWN, CmdMoveLineDown},
        {FCONTROL | FVIRTKEY, 'D', CmdDuplicateLine},
        {FCONTROL | FVIRTKEY, VK_F2, CmdToggleBookmark},
        {FCONTROL | FVIRTKEY, VK_SPACE, CmdCompleteWord},
        {FCONTROL | FSHIFT | FVIRTKEY, 'R', CmdMacroStart},
        {FCONTROL | FSHIFT | FVIRTKEY, 'P', CmdMacroPlay},
        {FVIRTKEY, VK_F4, CmdNextResult},
        {FSHIFT | FVIRTKEY, VK_F4, CmdPrevResult},
        {FVIRTKEY, VK_F2, CmdNextBookmark},
        {FSHIFT | FVIRTKEY, VK_F2, CmdPrevBookmark},
        {FCONTROL | FVIRTKEY, 'N', CmdNew},
        {FCONTROL | FVIRTKEY, 'O', CmdOpen},
        {FCONTROL | FVIRTKEY, 'S', CmdSave},
        {FCONTROL | FVIRTKEY, 'W', CmdClose},
        {FCONTROL | FVIRTKEY, 'Z', CmdUndo},
        {FCONTROL | FVIRTKEY, 'Y', CmdRedo},
        {FCONTROL | FVIRTKEY, 'X', CmdCut},
        {FCONTROL | FVIRTKEY, 'C', CmdCopy},
        {FCONTROL | FVIRTKEY, 'V', CmdPaste},
        {FCONTROL | FVIRTKEY, 'A', CmdSelectAll},
        {FCONTROL | FVIRTKEY, 'F', CmdFind},
        {FCONTROL | FVIRTKEY, 'H', CmdReplace},
        {FCONTROL | FVIRTKEY, 'G', CmdGoto},
        {FVIRTKEY, VK_F3, CmdFindNext},
        {FSHIFT | FVIRTKEY, VK_F3, CmdFindPrev},
        {FCONTROL | FVIRTKEY, VK_OEM_PLUS, CmdZoomIn},
        {FCONTROL | FVIRTKEY, VK_ADD, CmdZoomIn},
        {FCONTROL | FVIRTKEY, VK_OEM_MINUS, CmdZoomOut},
        {FCONTROL | FVIRTKEY, VK_SUBTRACT, CmdZoomOut},
        {FCONTROL | FVIRTKEY, '0', CmdZoomReset},
    };
    HACCEL table = CreateAcceleratorTableW(accels, static_cast<int>(sizeof(accels) / sizeof(accels[0])));

    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    host.updateDpi();
    app.start();
    SetTimer(host.frame, 1, 30000, nullptr);
    SetTimer(host.frame, 2, 2000, nullptr);
    if (argc > 1) {
        for (int i = 1; i < argc; ++i) app.openPath(narrow(argv[i]));
    }
    LocalFree(argv);

    if (!host.frame || !host.tabs || !host.status) {
        wchar_t buffer[256];
        swprintf(buffer, 256, L"窗口创建失败 frame=%p tabs=%p status=%p err=%lu",
                 host.frame, host.tabs, host.status, GetLastError());
        MessageBoxW(nullptr, buffer, L"NotepadSharp", MB_OK | MB_ICONERROR);
        return 1;
    }
    ShowWindow(host.frame, show == 0 ? SW_SHOW : show);
    UpdateWindow(host.frame);
    host.layout();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!host.searchDialog.translate(msg) && !TranslateAcceleratorW(host.frame, table, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return static_cast<int>(msg.wParam);
}
