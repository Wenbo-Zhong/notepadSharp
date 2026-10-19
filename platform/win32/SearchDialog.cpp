#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// clang-format off
#include <windows.h>
#include <commctrl.h>
#include <cstring>
#include "SearchDialog.h"
// clang-format on
namespace {
std::wstring wide(const std::string &text) {
    const auto n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n);
    return out;
}
std::string value(HWND control) {
    const auto n = GetWindowTextLengthW(control);
    std::wstring text(n + 1, 0);
    GetWindowTextW(control, text.data(), n + 1);
    const auto size = WideCharToMultiByte(CP_UTF8, 0, text.data(), n, nullptr, 0, nullptr, nullptr);
    std::string out(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, text.data(), n, out.data(), size, nullptr, nullptr);
    return out;
}
UINT dpiOf(HWND owner) {
    using Fn = UINT(WINAPI *)(HWND);
    Fn fn = nullptr;
    auto address = GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    std::memcpy(&fn, &address, sizeof(fn));
    if (fn)
        return fn(owner);
    HDC dc = GetDC(owner);
    const UINT dpi = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(owner, dc);
    return dpi;
}
bool checked(HWND control) { return SendMessageW(control, BM_GETCHECK, 0, 0) == BST_CHECKED; }
void check(HWND control, bool yes) {
    SendMessageW(control, BM_SETCHECK, yes ? BST_CHECKED : BST_UNCHECKED, 0);
}
void history(HWND combo, const std::string &text) {
    auto w = wide(text);
    if (SendMessageW(combo, CB_FINDSTRINGEXACT, -1, reinterpret_cast<LPARAM>(w.c_str())) == CB_ERR) {
        SendMessageW(combo, CB_INSERTSTRING, 0, reinterpret_cast<LPARAM>(w.c_str()));
        if (SendMessageW(combo, CB_GETCOUNT, 0, 0) > 20)
            SendMessageW(combo, CB_DELETESTRING, 20, 0);
    }
}
} // namespace
SearchDialog::~SearchDialog() {
    if (IsWindow(window_))
        DestroyWindow(window_);
    if (font_)
        DeleteObject(font_);
}
void SearchDialog::show(HINSTANCE instance, HWND owner, const FindRequest &seed, bool replaceMode,
                        const std::function<void(const FindRequest &)> &perform) {
    perform_ = perform;
    const bool visible = window_ && IsWindowVisible(window_);
    const bool created = window_ == nullptr;
    if (!window_) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = proc;
        wc.hInstance = instance;
        wc.lpszClassName = L"NotepadSharpFindWindow";
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        RegisterClassW(&wc);
        request_ = seed;
        window_ = CreateWindowExW(WS_EX_CONTROLPARENT | WS_EX_DLGMODALFRAME, wc.lpszClassName, L"查找与替换",
                                  WS_CAPTION | WS_SYSMENU | WS_POPUP, CW_USEDEFAULT, CW_USEDEFAULT, 680, 390,
                                  owner, nullptr, instance, this);
    }
    if (!visible && (created || seed.find != request_.find)) {
        request_ = seed;
        SetWindowTextW(find_, wide(seed.find).c_str());
        SetWindowTextW(replace_, wide(seed.replace).c_str());
        check(matchCase_, seed.matchCase);
        check(wholeWord_, seed.wholeWord);
        check(wrap_, seed.wrapAround);
        check(backwards_, seed.backwards);
        check(selection_, seed.inSelection);
        check(dot_, seed.dotMatchesNewline);
        check(unicode_, seed.unicode);
        check(ordinary_, !seed.regex && !seed.extended);
        check(extended_, seed.extended);
        check(regex_, seed.regex);
    }
    TabCtrl_SetCurSel(tabs_, replaceMode ? 1 : 0);
    pageChanged();
    ShowWindow(window_, SW_SHOW);
    SetForegroundWindow(window_);
    SetFocus(find_);
    SendMessageW(find_, CB_SETEDITSEL, 0, MAKELPARAM(0, -1));
}
bool SearchDialog::translate(MSG &message) {
    return window_ && IsWindowVisible(window_) && IsDialogMessageW(window_, &message);
}
bool SearchDialog::feedback(const std::string &message) {
    if (!window_ || !IsWindowVisible(window_))
        return false;
    SetWindowTextW(status_, wide(message).c_str());
    return true;
}
void SearchDialog::pageChanged() {
    const int page = TabCtrl_GetCurSel(tabs_);
    EnableWindow(replace_, page == 1);
    ShowWindow(buttons_[5], page == 1 ? SW_SHOW : SW_HIDE);
    ShowWindow(buttons_[6], page == 1 ? SW_SHOW : SW_HIDE);
    ShowWindow(buttons_[10], page == 1 ? SW_SHOW : SW_HIDE);
    ShowWindow(buttons_[7], page == 2 ? SW_SHOW : SW_HIDE);
    ShowWindow(buttons_[8], page == 2 ? SW_SHOW : SW_HIDE);
    EnableWindow(dot_, checked(regex_));
    EnableWindow(unicode_, checked(regex_));
}
void SearchDialog::action(FindAction action) {
    request_.find = value(find_);
    request_.replace = value(replace_);
    request_.matchCase = checked(matchCase_);
    request_.wholeWord = checked(wholeWord_);
    request_.wrapAround = checked(wrap_);
    request_.backwards = checked(backwards_);
    request_.inSelection = checked(selection_);
    request_.dotMatchesNewline = checked(dot_);
    request_.unicode = checked(unicode_);
    request_.regex = checked(regex_);
    request_.extended = checked(extended_);
    request_.cancelled = false;
    request_.action = action;
    history(find_, request_.find);
    if (action == FindAction::ReplaceOne || action == FindAction::ReplaceAll)
        history(replace_, request_.replace);
    if (perform_)
        perform_(request_);
}
LRESULT CALLBACK SearchDialog::proc(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
    auto *self = reinterpret_cast<SearchDialog *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_CREATE) {
        self = static_cast<SearchDialog *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->window_ = hwnd;
        const UINT dpi = dpiOf(GetParent(hwnd));
        self->dpi_ = dpi;
        auto scale = [&](int n) { return MulDiv(n, dpi, 96); };
        self->font_ = CreateFontW(-MulDiv(9, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        auto control = [&](const wchar_t *cls, const wchar_t *text, DWORD style, int x, int y, int width,
                           int height, int id = 0) {
            HWND child = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, scale(x), scale(y),
                                         scale(width), scale(height), hwnd,
                                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
            SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(self->font_), TRUE);
            return child;
        };
        self->tabs_ = control(WC_TABCONTROLW, L"", WS_TABSTOP, 12, 8, 636, 27, 100);
        for (const wchar_t *label : {L"查找", L"替换", L"标记"}) {
            TCITEMW item{};
            item.mask = TCIF_TEXT;
            item.pszText = const_cast<wchar_t *>(label);
            TabCtrl_InsertItem(self->tabs_, TabCtrl_GetItemCount(self->tabs_), &item);
        }
        control(L"STATIC", L"查找目标：", 0, 16, 52, 74, 22);
        self->find_ =
            control(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWN | CBS_AUTOHSCROLL, 92, 48, 330, 160, 101);
        SendMessageW(self->find_, CB_LIMITTEXT, 32768, 0);
        control(L"STATIC", L"替换为：", 0, 16, 88, 74, 22);
        self->replace_ =
            control(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWN | CBS_AUTOHSCROLL, 92, 84, 330, 160, 102);
        SendMessageW(self->replace_, CB_LIMITTEXT, 32768, 0);
        auto box = [&](const wchar_t *label, int x, int y, int width, int id) {
            return control(L"BUTTON", label, BS_AUTOCHECKBOX | WS_TABSTOP, x, y, width, 24, id);
        };
        self->matchCase_ = box(L"匹配大小写", 16, 128, 132, 110);
        self->wholeWord_ = box(L"全词匹配", 158, 128, 120, 111);
        self->wrap_ = box(L"循环查找", 290, 128, 132, 112);
        self->backwards_ = box(L"反向查找", 16, 157, 132, 113);
        self->selection_ = box(L"选取范围内", 158, 157, 132, 114);
        self->dot_ = box(L". 匹配换行", 290, 157, 132, 115);
        control(L"BUTTON", L"搜索模式", BS_GROUPBOX, 16, 194, 406, 60);
        self->ordinary_ =
            control(L"BUTTON", L"普通", BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, 28, 218, 82, 24, 120);
        self->extended_ = control(L"BUTTON", L"扩展（\\n \\t...）", BS_AUTORADIOBUTTON | WS_TABSTOP, 116, 218,
                                  168, 24, 121);
        self->regex_ =
            control(L"BUTTON", L"正则表达式", BS_AUTORADIOBUTTON | WS_TABSTOP, 290, 218, 120, 24, 122);
        self->unicode_ = box(L"完整 Unicode（ICU：属性、折叠、字素簇）", 16, 264, 406, 116);
        self->status_ =
            control(L"STATIC", L"正则替换捕获引用：\\1、$1、$+{name}。", SS_LEFT, 16, 296, 406, 58, 130);
        const wchar_t *labels[] = {L"查找下一个",       L"查找上一个", L"计数",     L"在当前文档中查找",
                                   L"查找所有打开文档", L"替换",       L"全部替换", L"标记全部",
                                   L"清除标记",         L"关闭"};
        const int ys[] = {48, 82, 116, 150, 184, 218, 252, 218, 252, 326};
        for (int i = 0; i < 10; ++i)
            self->buttons_[i] =
                control(L"BUTTON", labels[i], WS_TABSTOP | (i == 0 ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON), 450,
                        ys[i], 180, 28,
                        i == 0   ? IDOK
                        : i == 9 ? IDCANCEL
                                 : 200 + i);
        self->buttons_[10] =
            control(L"BUTTON", L"全部替换打开的文档", WS_TABSTOP | BS_PUSHBUTTON, 450, 286, 180, 28, 210);
        RECT bounds{0, 0, scale(660), scale(370)};
        AdjustWindowRectEx(&bounds, GetWindowLongW(hwnd, GWL_STYLE), FALSE,
                           GetWindowLongW(hwnd, GWL_EXSTYLE));
        RECT owner{};
        GetWindowRect(GetParent(hwnd), &owner);
        SetWindowPos(hwnd, nullptr, owner.left + scale(48), owner.top + scale(72), bounds.right - bounds.left,
                     bounds.bottom - bounds.top, SWP_NOZORDER);
        return 0;
    }
    if (!self)
        return DefWindowProcW(hwnd, message, w, l);
    if (message == WM_DPICHANGED) {
        const UINT dpi = HIWORD(w), oldDpi = self->dpi_;
        HFONT oldFont = self->font_;
        self->font_ = CreateFontW(-MulDiv(9, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        for (HWND child = GetWindow(hwnd, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
            RECT r{};
            GetWindowRect(child, &r);
            MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT *>(&r), 2);
            MoveWindow(child, MulDiv(r.left, dpi, oldDpi), MulDiv(r.top, dpi, oldDpi),
                       MulDiv(r.right - r.left, dpi, oldDpi), MulDiv(r.bottom - r.top, dpi, oldDpi), TRUE);
            SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(self->font_), TRUE);
        }
        if (oldFont)
            DeleteObject(oldFont);
        self->dpi_ = dpi;
        const auto *r = reinterpret_cast<RECT *>(l);
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    if (message == WM_NOTIFY && reinterpret_cast<NMHDR *>(l)->hwndFrom == self->tabs_ &&
        reinterpret_cast<NMHDR *>(l)->code == TCN_SELCHANGE) {
        self->pageChanged();
        return 0;
    }
    if (message == WM_COMMAND) {
        const int id = LOWORD(w);
        if (id == IDCANCEL) {
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        if (id == 120 || id == 121 || id == 122) {
            self->pageChanged();
            return 0;
        }
        if (id == 210) {
            self->action(FindAction::ReplaceAllOpen);
            return 0;
        }
        if (id == IDOK) {
            self->action(FindAction::Next);
            return 0;
        }
        if (id >= 201 && id <= 208) {
            const FindAction actions[] = {FindAction::Previous,       FindAction::Count,
                                          FindAction::FindAllCurrent, FindAction::FindAllOpen,
                                          FindAction::ReplaceOne,     FindAction::ReplaceAll,
                                          FindAction::MarkAll,        FindAction::ClearMarks};
            self->action(actions[id - 201]);
            return 0;
        }
    }
    if (message == WM_NCDESTROY)
        self->window_ = nullptr;
    if (message == WM_CLOSE) {
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    }
    return DefWindowProcW(hwnd, message, w, l);
}
