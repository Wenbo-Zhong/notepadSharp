#pragma once
// clang-format off
#include <windows.h>
#include "App.h"
// clang-format on
class SearchDialog {
  public:
    ~SearchDialog();
    void show(HINSTANCE instance, HWND owner, const FindRequest &seed, bool replaceMode,
              const std::function<void(const FindRequest &)> &perform);
    bool translate(MSG &message);
    bool feedback(const std::string &message);

  private:
    HWND window_ = nullptr, tabs_ = nullptr, find_ = nullptr, replace_ = nullptr, status_ = nullptr;
    HWND matchCase_ = nullptr, wholeWord_ = nullptr, wrap_ = nullptr, backwards_ = nullptr,
         selection_ = nullptr, dot_ = nullptr, unicode_ = nullptr;
    HWND ordinary_ = nullptr, extended_ = nullptr, regex_ = nullptr;
    HWND buttons_[11]{};
    HFONT font_ = nullptr;
    UINT dpi_ = 96;
    FindRequest request_;
    std::function<void(const FindRequest &)> perform_;
    void pageChanged();
    void action(FindAction action);
    static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
};
