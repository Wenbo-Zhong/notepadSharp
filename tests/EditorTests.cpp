#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "App.h"
#include "Scintilla.h"
#include "TextEncoding.h"
#include "SearchOptions.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <windows.h>

struct TestHost : Host {
    std::vector<HWND> editors;
    HWND parent = nullptr;
    App *observer = nullptr;
    int number = 3;
    bool reloadExternal = false, overwriteExternal = false;
    int reloadQuestions = 0, overwriteQuestions = 0;
    bool askReloadExternal(const std::string &) override {
        ++reloadQuestions;
        return reloadExternal;
    }
    bool askOverwriteExternal(const std::string &) override {
        ++overwriteQuestions;
        return overwriteExternal;
    }
    bool fullScreenState = false, topState = false;
    void setFullScreen(bool value) override { fullScreenState = value; }
    void setAlwaysOnTop(bool value) override { topState = value; }
    int promptNumber(const std::string &, int, int) override { return number; }
    int current = 0;
    FindRequest request;
    SaveChoice choice = SaveChoice::Discard;
    std::string openPath, savePath, configPath, lastAlert, folder;
    ~TestHost() {
        observer = nullptr;
        for (auto hwnd : editors)
            if (hwnd)
                DestroyWindow(hwnd);
        if (parent)
            DestroyWindow(parent);
    }
    intptr_t send(int doc, unsigned msg, uintptr_t w, intptr_t l) override {
        return SendMessageW(editors.at(doc - 1), msg, w, l);
    }
    int createEditor() override {
        if (!parent) {
            WNDCLASSW wc{};
            wc.lpszClassName = L"NotepadSharpTestParent";
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpfnWndProc = [](HWND hwnd, UINT msg, WPARAM w, LPARAM l) -> LRESULT {
                if (msg == WM_NCCREATE)
                    SetWindowLongPtrW(
                        hwnd, GWLP_USERDATA,
                        reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams));
                auto *self = reinterpret_cast<TestHost *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
                if (msg == WM_NOTIFY && self && self->observer) {
                    const auto *notification = reinterpret_cast<SCNotification *>(l);
                    for (size_t i = 0; i < self->editors.size(); ++i)
                        if (self->editors[i] == notification->nmhdr.hwndFrom) {
                            self->observer->onNotify(static_cast<int>(i + 1), notification);
                            break;
                        }
                }
                return DefWindowProcW(hwnd, msg, w, l);
            };
            RegisterClassW(&wc);
            parent = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 800, 600, nullptr,
                                     nullptr, wc.hInstance, this);
        }
        editors.push_back(CreateWindowExW(0, L"Scintilla", L"", WS_CHILD, 0, 0, 800, 600, parent, nullptr,
                                          GetModuleHandleW(nullptr), nullptr));
        if (!editors.back())
            throw std::runtime_error("Create Scintilla failed");
        return static_cast<int>(editors.size());
    }
    void destroyEditor(int doc) override {
        DestroyWindow(editors.at(doc - 1));
        editors.at(doc - 1) = nullptr;
    }
    void showEditor(int doc) override { current = doc; }
    void setWindowTitle(const std::string &) override {}
    void setStatus(const std::string &) override {}
    void setTabTitle(int, const std::string &) override {}
    std::string promptDirectory() override { return folder; }
    std::string promptOpenPath() override { return openPath; }
    std::string promptSavePath(const std::string &) override { return savePath; }
    SaveChoice askSave(const std::string &) override { return choice; }
    void alert(const std::string &message) override { lastAlert = message; }
    std::string stateDirectory() const override { return configPath; }
    FindRequest promptFind(const FindRequest &, bool) override { return request; }
    int promptGoto(int, int) override { return 1; }
    std::string monoFont() const override { return "Consolas"; }
    int defaultEol() const override { return SC_EOL_CRLF; }
    void quit() override {}
    intptr_t sci(unsigned msg, uintptr_t w = 0, intptr_t l = 0) { return send(current, msg, w, l); }
    void text(const char *value) { sci(SCI_SETTEXT, 0, reinterpret_cast<intptr_t>(value)); }
    std::string text() {
        const auto length = sci(SCI_GETLENGTH);
        std::string value(length + 1, '\0');
        sci(SCI_GETTEXT, length + 1, reinterpret_cast<intptr_t>(value.data()));
        value.resize(length);
        return value;
    }
};
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
int main() {
    try {
        Scintilla_RegisterClasses(GetModuleHandleW(nullptr));
        wchar_t executable[32768]{};
        GetModuleFileNameW(nullptr, executable, 32768);
        const auto appPath = std::filesystem::path(executable).parent_path() / L"notepadSharp.exe";
        HMODULE module = LoadLibraryExW(appPath.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE);
        require(module != nullptr, "load application resources");
        HRSRC manifestResource = FindResourceW(module, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(24));
        require(manifestResource != nullptr, "embedded manifest");
        const auto manifestData =
            static_cast<const char *>(LockResource(LoadResource(module, manifestResource)));
        std::string manifest(manifestData, SizeofResource(module, manifestResource));
        require(manifest.find("PerMonitorV2") != std::string::npos,
                "PerMonitorV2 manifest embedded correctly");
        std::vector<HANDLE> fonts;
        for (int id = 500; id < 516; ++id) {
            HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(id), RT_RCDATA);
            require(resource != nullptr, "all 16 font faces embedded");
            DWORD count = 0;
            HANDLE font = AddFontMemResourceEx(LockResource(LoadResource(module, resource)),
                                               SizeofResource(module, resource), nullptr, &count);
            require(font && count > 0, "register embedded font");
            fonts.push_back(font);
        }
        HDC dc = CreateCompatibleDC(nullptr);
        HFONT probe = CreateFontW(-20, 0, 0, 0, 500, TRUE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, 0, 0,
                                  L"JetBrains Mono");
        auto oldFont = SelectObject(dc, probe);
        wchar_t face[128]{};
        GetTextFaceW(dc, 128, face);
        require(std::wstring(face) == L"JetBrains Mono", "embedded font resolves without installation");
        SelectObject(dc, oldFont);
        DeleteObject(probe);
        DeleteDC(dc);
        for (HANDLE font : fonts)
            RemoveFontMemResourceEx(font);
        FreeLibrary(module);
        TestHost host;
        App app(host);
        app.start();
        host.text("a  \r\nb\t\r\nlast ");
        app.onCommand(CmdTrimTrailing);
        require(host.text() == "a\r\nb\r\nlast", "trim trailing whitespace");
        app.onCommand(CmdUndo);
        require(host.text() == "a  \r\nb\t\r\nlast ", "trim is one undo action");
        app.onCommand(CmdEolLF);
        require(host.text() == "a  \nb\t\nlast ", "LF conversion");
        app.onCommand(CmdEolCRLF);
        require(host.text() == "a  \r\nb\t\r\nlast ", "CRLF conversion");
        host.text("abc ABC abc");
        host.request.find = "abc";
        host.request.replace = "";
        host.request.action = FindAction::ReplaceAll;
        host.request.cancelled = false;
        app.onCommand(CmdReplace);
        require(host.text() == "  ", "replace all with empty string");
        app.onCommand(CmdUndo);
        require(host.text() == "abc ABC abc", "replace all undo");
        host.text("a1 b22 c333");
        host.request.regex = true;
        host.request.find = "([a-z])([0-9]+)";
        host.request.replace = "\\2-\\1";
        app.onCommand(CmdReplace);
        require(host.text() == "1-a 22-b 333-c", "regex capture replacement");
        host.text("ab");
        host.request.find = "(?=.)";
        host.request.replace = "";
        app.onCommand(CmdReplace);
        require(host.text() == "ab", "zero width regex terminates");
        host.sci(SCI_GOTOPOS, 0);
        host.request.action = FindAction::Next;
        app.onCommand(CmdFind);
        require(host.sci(SCI_GETCURRENTPOS) == 0, "zero width first match");
        app.onCommand(CmdFindNext);
        require(host.sci(SCI_GETCURRENTPOS) == 1, "zero width next match advances");
        host.request.find = "[";
        host.request.action = FindAction::ReplaceAll;
        app.onCommand(CmdReplace);
        require(host.text() == "ab", "invalid regex leaves text intact");
        host.text("abc ABC abc");
        host.sci(SCI_SETSEL, 0, 3);
        app.onCommand(CmdUpperCase);
        require(host.text() == "ABC ABC abc", "uppercase");
        app.onCommand(CmdLowerCase);
        require(host.text() == "abc ABC abc", "lowercase");
        host.text("one\ntwo\nthree");
        host.sci(SCI_GOTOLINE, 0);
        app.onCommand(CmdToggleBookmark);
        host.sci(SCI_GOTOLINE, 2);
        app.onCommand(CmdToggleBookmark);
        app.onCommand(CmdNextBookmark);
        require(host.sci(SCI_GETCURRENTPOS) == 0, "bookmark wraps forward");
        app.onCommand(CmdPrevBookmark);
        require(host.sci(SCI_LINEFROMPOSITION, host.sci(SCI_GETCURRENTPOS)) == 2, "bookmark wraps backward");
        app.onCommand(CmdClearBookmarks);
        require(host.sci(SCI_MARKERNEXT, 0, 1) == -1, "clear bookmarks");
        app.onCommand(static_cast<Command>(CmdFontSizeBase + 24));
        app.onCommand(static_cast<Command>(CmdFontWeightBase + 4));
        app.onCommand(CmdFontItalic);
        app.onCommand(CmdNew);
        require(host.sci(SCI_STYLEGETSIZE, STYLE_DEFAULT) == 24, "new tab inherits font size");
        require(host.sci(SCI_STYLEGETWEIGHT, STYLE_DEFAULT) == 500, "new tab inherits weight");
        require(host.sci(SCI_STYLEGETITALIC, STYLE_DEFAULT) == 1, "new tab inherits italic");
        require(host.sci(SCI_GETMULTIPLESELECTION) == 1, "multiple selections enabled");
        app.onCommand(CmdCloseAll);
        require(host.editors.size() == 3, "close all creates one empty tab");
        host.sci(SCI_SETEOLMODE, SC_EOL_LF);
        host.text("z\nb\na\nb\n");
        app.onCommand(CmdSortAsc);
        require(host.text() == "a\nb\nb\nz\n", "sort ascending preserves final newline");
        app.onCommand(CmdUniqueLines);
        require(host.text() == "a\nb\nz\n", "deduplicate keeps first occurrence");
        app.onCommand(CmdUndo);
        require(host.text() == "a\nb\nb\nz\n", "deduplicate undo");
        host.text("a\tb\n");
        app.onCommand(CmdTabsToSpaces);
        require(host.text() == "a   b\n", "tab stops expanded");
        const auto temp = std::filesystem::temp_directory_path() /
                          ("notepadsharp-tests-" + std::to_string(GetCurrentProcessId()));
        std::filesystem::create_directories(temp);
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() {
                std::error_code ec;
                std::filesystem::remove_all(path, ec);
            }
        } cleanup{temp};
        const std::string unicode = "ASCII \xe4\xb8\xad\xe6\x96\x87 \xf0\x9f\x98\x80\r\n";
        for (const auto &encoding : {"UTF-8", "UTF-8 BOM", "UTF-16 LE", "UTF-16 BE"}) {
            std::string bytes, error, text, detected;
            require(TextEncoding::encode(unicode, encoding, bytes, error), "encode Unicode");
            require(TextEncoding::decode(bytes, text, detected, error) && text == unicode &&
                        detected == encoding,
                    "encoding round trip");
            auto file = temp / (std::string(encoding) + ".txt");
            {
                std::ofstream out(file, std::ios::binary);
                out.write(bytes.data(), bytes.size());
            }
            app.openPath(file.u8string());
            require(host.text() == unicode, "App opens Unicode");
            const auto before = host.editors.size();
            app.openPath(file.u8string());
            require(host.editors.size() == before, "opening same file activates existing tab");
            app.onCommand(CmdSave);
            std::ifstream in(file, std::ios::binary);
            std::string saved((std::istreambuf_iterator<char>(in)), {});
            require(saved == bytes, "save preserves encoding and BOM");
        }
        std::string bytes, error, text, detected;
        require(!TextEncoding::decode(std::string("\xff\xfe\x00\xd8", 4), text, detected, error),
                "reject unpaired UTF16 surrogate");
        require(!TextEncoding::encode(unicode, "Latin-1", bytes, error), "block lossy Latin1 conversion");
        require(TextEncoding::decode(std::string("\xc0\xaf", 2), text, detected, error) &&
                    detected == "Latin-1",
                "reject overlong UTF8 as UTF8");
        auto pythonFile = temp / "test.py";
        {
            std::ofstream out(pythonFile, std::ios::binary);
            out << "  x = 1\n  y = 2\n";
        }
        app.openPath(pythonFile.u8string());
        host.sci(SCI_SELECTALL);
        app.onCommand(CmdToggleComment);
        require(host.text() == "  # x = 1\n  # y = 2\n", "comment Python");
        app.onCommand(CmdToggleComment);
        require(host.text() == "  x = 1\n  y = 2\n", "uncomment Python");
        host.sci(SCI_GOTOPOS, 0);
        host.request.find = "x";
        host.request.regex = false;
        host.request.cancelled = false;
        app.onCommand(CmdMarkAll);
        require(host.sci(SCI_INDICATORVALUEAT, 8, 2) != 0, "mark all");
        app.onCommand(CmdClearMarks);
        require(host.sci(SCI_INDICATORVALUEAT, 8, 2) == 0, "clear marks");
        const int sourceDoc = host.current;
        app.onCommand(CmdFindAllOpen);
        require(host.sci(SCI_GETREADONLY) == 1, "search results read only");
        require(host.text().find("test.py:1:3") != std::string::npos,
                "search report has filename line column");
        app.onCommand(CmdNextResult);
        require(host.current == sourceDoc && host.sci(SCI_GETSELECTIONSTART) == 2, "result navigation");
        host.savePath = (temp / "manual.nss").u8string();
        app.onCommand(CmdSaveSession);
        app.onCommand(CmdNew);
        host.text("throwaway");
        host.openPath = host.savePath;
        app.onCommand(CmdLoadSession);
        require(host.text() == "  x = 1\n  y = 2\n", "manual session restores active document");
        {
            TestHost first;
            first.configPath = (temp / "state").u8string();
            App a(first);
            a.start();
            first.text("unsaved recovery \xe4\xb8\xad");
            SCNotification changed{};
            changed.nmhdr.code = SCN_SAVEPOINTLEFT;
            a.onNotify(first.current, &changed);
            a.onCommand(static_cast<Command>(CmdFontSizeBase + 18));
            a.checkpoint();
            require(std::filesystem::exists(temp / "state/recovery.nss"), "checkpoint written");
            TestHost second;
            second.configPath = first.configPath;
            App b(second);
            b.start();
            second.text("second instance");
            b.checkpoint();
        }
        {
            TestHost recovered;
            recovered.configPath = (temp / "state").u8string();
            App a(recovered);
            a.start();
            require(recovered.text() == "unsaved recovery \xe4\xb8\xad",
                    "crash recovery restores unsaved content; second instance cannot overwrite");
            require(a.fontSize() == 18, "font preferences restored");
            recovered.choice = SaveChoice::Cancel;
            require(!a.onCloseRequested(), "cancel closing recovered dirty buffer");
            recovered.choice = SaveChoice::Discard;
            require(a.onCloseRequested(), "discard recovered dirty buffer");
        }
        {
            TestHost clean;
            clean.configPath = (temp / "state").u8string();
            App a(clean);
            a.start();
            require(clean.text().empty(), "discarded buffer does not return");
        }
        {
            TestHost fileHost;
            App a(fileHost);
            a.start();
            fileHost.text("protected bytes");
            const auto target = temp / "protected.txt";
            fileHost.savePath = target.u8string();
            a.onCommand(CmdSave);
            fileHost.text(unicode.c_str());
            a.onCommand(CmdEncodingLatin1);
            a.onCommand(CmdSave);
            std::ifstream in(target, std::ios::binary);
            std::string value((std::istreambuf_iterator<char>(in)), {});
            require(value == "protected bytes", "failed encoding conversion never truncates original file");
            a.onCommand(CmdEncodingUtf8);
            a.onCommand(CmdSave);
            fileHost.openPath = (temp / "corrupt.nss").u8string();
            {
                std::ofstream out(std::filesystem::u8path(fileHost.openPath));
                out << "NotepadSharpSession2\n12 400 0";
            }
            a.onCommand(CmdLoadSession);
            require(fileHost.text() == unicode, "malformed session leaves current work intact");
            const auto fileCount = fileHost.editors.size();
            auto malformed = temp / "bad-utf16.txt";
            {
                std::ofstream out(malformed, std::ios::binary);
                out.write("\xff\xfe\x00\xd8", 4);
            }
            a.openPath(malformed.u8string());
            require(fileHost.editors.size() == fileCount && fileHost.text() == unicode,
                    "invalid input encoding does not create or overwrite a tab");
        }
        {
            TestHost persisted;
            persisted.configPath = (temp / "clean-state").u8string();
            App a(persisted);
            a.start();
            a.openPath(pythonFile.u8string());
            persisted.sci(SCI_GOTOPOS, 5);
            a.onCommand(CmdWrap);
            a.onCommand(CmdAutoComplete);
            a.onCommand(static_cast<Command>(CmdFontSizeBase + 16));
            require(a.onCloseRequested(), "normal exit persists session");
        }
        {
            TestHost persisted;
            persisted.configPath = (temp / "clean-state").u8string();
            App a(persisted);
            a.start();
            require(persisted.text() == "  x = 1\n  y = 2\n" && persisted.sci(SCI_GETCURRENTPOS) == 5,
                    "normal session restores disk file and cursor");
            require(a.wrap() && a.fontSize() == 16 && !a.autoComplete(),
                    "view settings survive normal restart");
            persisted.text("one\ttwo\r\nthree\tfour\n");
            a.onCommand(CmdTabsToSpaces);
            require(persisted.text() == "one two\r\nthree   four\n",
                    "tab conversion preserves mixed line endings");
            persisted.text("x = 1\ny = 2\n");
            persisted.sci(SCI_GOTOPOS, 0);
            a.onCommand(CmdToggleComment);
            require(persisted.text() == "# x = 1\ny = 2\n",
                    "comment without selection affects only current line");
        }
        {
            TestHost macroHost;
            App a(macroHost);
            macroHost.observer = &a;
            a.start();
            a.onCommand(CmdMacroStart);
            require(a.macroRecording(), "macro recording starts");
            SendMessageW(macroHost.editors[macroHost.current - 1], WM_CHAR, 'x', 0);
            macroHost.sci(SCI_LINEEND);
            SendMessageW(macroHost.editors[macroHost.current - 1], WM_CHAR, 'y', 0);
            a.onCommand(CmdMacroStop);
            require(!a.macroRecording() && macroHost.text() == "xy", "real typing is recorded");
            a.onCommand(CmdMacroPlay);
            require(macroHost.text() == "xyxy", "macro playback repeats typed text");
            a.onCommand(CmdUndo);
            require(macroHost.text() == "xy", "macro playback is one undo action");
            macroHost.savePath = (temp / "typed.nsm").u8string();
            a.onCommand(CmdMacroSave);
            a.onCommand(CmdMacroStart);
            a.onCommand(CmdMacroStop);
            macroHost.openPath = macroHost.savePath;
            a.onCommand(CmdMacroLoad);
            a.onCommand(CmdMacroPlay);
            require(macroHost.text() == "xyxy", "saved macro reloads and plays");
            macroHost.openPath = (temp / "bad.nsm").u8string();
            {
                std::ofstream out(std::filesystem::u8path(macroHost.openPath));
                out << "NotepadSharpMacro1\n1\n2181 0 123 \"\"\n";
            }
            a.onCommand(CmdMacroLoad);
            a.onCommand(CmdMacroPlay);
            require(macroHost.text() == "xyxyxy",
                    "invalid macro preserves previous macro and cannot execute arbitrary messages");
            a.onCommand(CmdMacroStart);
            a.onCommand(CmdNew);
            require(!a.macroRecording(), "tab creation stops recording");
            macroHost.text("alpha alphabet beta\nal");
            macroHost.sci(SCI_DOCUMENTEND);
            a.onCommand(CmdCompleteWord);
            require(macroHost.sci(SCI_AUTOCACTIVE) == 1, "word completion opens candidates");
            char candidate[256]{};
            macroHost.sci(SCI_AUTOCGETCURRENTTEXT, 0, reinterpret_cast<intptr_t>(candidate));
            require(std::string(candidate) == "alpha", "completion candidates are sorted");
            macroHost.sci(SCI_AUTOCCOMPLETE);
            require(macroHost.text() == "alpha alphabet beta\nalpha", "completion replaces prefix");
            a.onCommand(CmdAutoComplete);
            macroHost.text("alpha alphabet\nal");
            macroHost.sci(SCI_DOCUMENTEND);
            SCNotification added{};
            added.nmhdr.code = SCN_CHARADDED;
            added.ch = 'l';
            a.onNotify(macroHost.current, &added);
            require(macroHost.sci(SCI_AUTOCACTIVE) == 0, "automatic completion can be disabled");
            a.onCommand(CmdCompleteWord);
            require(macroHost.sci(SCI_AUTOCACTIVE) == 1, "manual completion remains available");
        }
        {
            const auto root = temp / "folder-search";
            std::filesystem::create_directories(root / "nested");
            std::filesystem::create_directories(root / ".git");
            {
                std::ofstream out(root / "a.txt", std::ios::binary);
                out << "needle one\n";
            }
            {
                std::ofstream out(root / "nested/b.txt", std::ios::binary);
                out << "second needle\n";
            }
            {
                std::ofstream out(root / ".git/ignored", std::ios::binary);
                out << "needle ignored";
            }
            {
                std::ofstream out(root / "binary.bin", std::ios::binary);
                out.write("needle\0binary", 13);
            }
            std::string encoded, error;
            TextEncoding::encode("UTF16 needle\n", "UTF-16 LE", encoded, error);
            {
                std::ofstream out(root / "utf16.txt", std::ios::binary);
                out.write(encoded.data(), encoded.size());
            }
            TestHost folderHost;
            App a(folderHost);
            folderHost.observer = &a;
            a.start();
            folderHost.folder = root.u8string();
            folderHost.request.find = "needle";
            folderHost.request.cancelled = false;
            a.onCommand(CmdFindInFolder);
            const auto report = folderHost.text();
            require(report.find("a.txt:1:1") != std::string::npos &&
                        report.find("b.txt:1:8") != std::string::npos &&
                        report.find("utf16.txt:1:7") != std::string::npos,
                    "folder search recursively finds UTF8 and UTF16");
            require(report.find("needle ignored") == std::string::npos &&
                        report.find("binary.bin:") == std::string::npos,
                    "folder search skips git and binary data");
            a.onCommand(CmdPrevResult);
            require(folderHost.sci(SCI_GETSELECTIONSTART) >= 0 &&
                        folderHost.text().find("needle") != std::string::npos,
                    "folder result opens file");
            a.onCommand(CmdNextResult);
            require(folderHost.sci(SCI_GETREADONLY) == 0,
                    "result navigation survives document load notifications");
            SCNotification modified{};
            modified.nmhdr.code = SCN_MODIFIED;
            modified.modificationType = SC_MOD_INSERTTEXT;
            a.onNotify(folderHost.current, &modified);
            folderHost.lastAlert.clear();
            a.onCommand(CmdNextResult);
            require(!folderHost.lastAlert.empty(), "source editing invalidates folder results");
        }
        {
            const auto root = temp / "changed-search";
            std::filesystem::create_directories(root);
            const auto file = root / "changed.txt";
            {
                std::ofstream out(file, std::ios::binary);
                out << "needle";
            }
            TestHost h;
            App a(h);
            h.observer = &a;
            a.start();
            h.folder = root.u8string();
            h.request.find = "needle";
            h.request.cancelled = false;
            a.onCommand(CmdFindInFolder);
            {
                std::ofstream out(file, std::ios::binary);
                out << "new contents";
            }
            h.lastAlert.clear();
            a.onCommand(CmdNextResult);
            require(!h.lastAlert.empty() && h.text() == "new contents",
                    "changed unopened file invalidates stale search positions");
            h.text("unsaved needle in memory");
            a.onCommand(CmdFindInFolder);
            require(h.text().find("unsaved needle in memory") != std::string::npos,
                    "folder search uses dirty open document content");
            a.onCommand(CmdNextResult);
            require(h.text() == "unsaved needle in memory" && h.sci(SCI_GETSELECTIONSTART) == 8,
                    "folder navigation preserves unsaved edits");
            h.request.regex = true;
            h.request.find = "[";
            a.onCommand(CmdFindInFolder);
            require(h.text() == "unsaved needle in memory", "invalid folder regex leaves work intact");
            const auto cpp = temp / "completion.cpp";
            {
                std::ofstream out(cpp, std::ios::binary);
                out << "ret";
            }
            a.openPath(cpp.u8string());
            h.sci(SCI_DOCUMENTEND);
            a.onCommand(CmdCompleteWord);
            char word[256]{};
            h.sci(SCI_AUTOCGETCURRENTTEXT, 0, reinterpret_cast<intptr_t>(word));
            require(std::string(word) == "return", "completion includes lexer keywords");
            a.onCommand(CmdMacroStart);
            a.onCommand(CmdMacroStart);
            require(!a.macroRecording(), "record shortcut toggles without clearing completed macro");
        }
        {
            TestHost h;
            App a(h);
            h.observer = &a;
            a.start();
            h.sci(SCI_SETEOLMODE, SC_EOL_LF);
            h.text("a\n\n \t\nb\na\na\n");
            a.onCommand(CmdRemoveEmptyLines);
            require(h.text() == "a\n \t\nb\na\na\n", "remove only empty lines");
            a.onCommand(CmdRemoveBlankLines);
            require(h.text() == "a\nb\na\na\n", "remove whitespace-only lines");
            a.onCommand(CmdUniqueAdjacent);
            require(h.text() == "a\nb\na\n", "remove only adjacent duplicates");
            a.onCommand(CmdReverseLines);
            require(h.text() == "a\nb\na\n", "reverse line ordering");
            h.text("bbbb\n\xe4\xb8\xad\nzz\n");
            a.onCommand(CmdSortLengthAsc);
            require(h.text() == "\xe4\xb8\xad\nzz\nbbbb\n", "line length sorting counts Unicode codepoints");
            h.text("z\nB\na\n");
            a.onCommand(CmdSortIgnoreCaseAsc);
            require(h.text() == "a\nB\nz\n", "ASCII ignore-case line sort");
            h.text("  a \r\n\tb\t\n");
            a.onCommand(CmdTrimBoth);
            require(h.text() == "a\r\nb\n", "trim both keeps mixed EOLs");
            a.onCommand(CmdUndo);
            require(h.text() == "  a \r\n\tb\t\n", "trim both one undo");
            h.text("(value)");
            h.sci(SCI_GOTOPOS, 0);
            a.onCommand(CmdSelectBrace);
            require(h.sci(SCI_GETSELECTIONSTART) == 1 && h.sci(SCI_GETSELECTIONEND) == 6,
                    "select brace contents");
            h.sci(SCI_GOTOPOS, 0);
            a.onCommand(CmdGotoBrace);
            require(h.sci(SCI_GETCURRENTPOS) == 6, "jump matching brace");
            h.text("foo foo foo");
            h.sci(SCI_SETSEL, 0, 3);
            a.onCommand(CmdSelectNextOccurrence);
            require(h.sci(SCI_GETSELECTIONS) == 2, "select next occurrence");
            h.sci(SCI_SETSEL, 0, 3);
            a.onCommand(CmdSelectAllOccurrences);
            require(h.sci(SCI_GETSELECTIONS) == 3, "select all occurrences");
            h.sci(SCI_SETSEL, 0, 0);
            a.onCommand(CmdReadOnly);
            h.sci(SCI_REPLACESEL, 0, reinterpret_cast<intptr_t>("blocked"));
            require(h.text() == "foo foo foo", "read-only prevents editing");
            a.onCommand(CmdReadOnly);
            require(h.sci(SCI_GETREADONLY) == 0, "read-only can be disabled");
            a.onCommand(CmdStatistics);
            require(h.lastAlert.find("11") != std::string::npos, "document statistics report bytes");
            auto file = temp / "reload.txt";
            {
                std::ofstream out(file, std::ios::binary);
                out << "original";
            }
            a.openPath(file.u8string());
            h.text("edited");
            h.savePath = (temp / "copy.txt").u8string();
            a.onCommand(CmdSaveCopy);
            {
                std::ifstream in(std::filesystem::u8path(h.savePath), std::ios::binary);
                std::string value((std::istreambuf_iterator<char>(in)), {});
                require(value == "edited", "save copy writes current contents");
            }
            require(h.sci(SCI_GETMODIFY) != 0, "save copy preserves original dirty state");
            {
                std::ofstream out(file, std::ios::binary);
                out << "external";
            }
            h.choice = SaveChoice::Cancel;
            a.onCommand(CmdReload);
            require(h.text() == "edited", "reload cancellation preserves edits");
            h.choice = SaveChoice::Discard;
            a.onCommand(CmdReload);
            require(h.text() == "external" && h.sci(SCI_GETMODIFY) == 0, "reload reads external content");
            h.text("saved to original");
            a.onCommand(CmdSave);
            {
                std::ifstream in(file, std::ios::binary);
                std::string value((std::istreambuf_iterator<char>(in)), {});
                require(value == "saved to original", "save copy did not change original path");
            }
        }
        {
            TestHost h;
            App a(h);
            h.observer = &a;
            a.start();
            const int left = h.current;
            h.text("dirty left");
            a.onCommand(CmdNew);
            const int middle = h.current;
            a.onCommand(CmdNew);
            const int right = h.current;
            a.onTabChanged(middle);
            h.choice = SaveChoice::Cancel;
            a.onCommand(CmdCloseOthers);
            require(h.editors[left - 1] && h.editors[right - 1], "cancel batch close keeps all tabs");
            a.onTabChanged(middle);
            h.choice = SaveChoice::Discard;
            a.onCommand(CmdCloseLeft);
            require(!h.editors[left - 1] && h.editors[middle - 1] && h.editors[right - 1],
                    "close left keeps active and right tabs");
            a.onCommand(CmdCloseRight);
            require(!h.editors[right - 1] && h.current == middle, "close right keeps active tab");
        }
        {
            TestHost h;
            App a(h);
            h.observer = &a;
            a.start();
            h.sci(SCI_SETEOLMODE, SC_EOL_LF);
            h.text("a  b\r\nc\rd\n");
            a.onCommand(CmdEolsToSpaces);
            require(h.text() == "a  b c d ", "EOL to space handles CRLF CR LF exactly once");
            a.onCommand(CmdUndo);
            require(h.text() == "a  b\r\nc\rd\n", "join is one undo action");
            h.text("  one  \r\n\ttwo\t\n");
            a.onCommand(CmdTrimAndJoin);
            require(h.text() == "one two ", "trim and join combines whitespace transforms");
            h.text("    a    b\r\n  x   y\n");
            a.onCommand(CmdIndentToTabs);
            require(h.text() == "\ta    b\r\n  x   y\n",
                    "indent conversion leaves interior spaces and mixed EOLs intact");
            a.onCommand(CmdUndo);
            a.onCommand(CmdSpacesToTabs);
            require(h.text() == "\ta\t b\r\n  x   y\n", "space conversion respects tab stops");
            h.text("");
            a.onCommand(CmdMacroStart);
            SendMessageW(h.editors[h.current - 1], WM_CHAR, 'x', 0);
            a.onCommand(CmdMacroStop);
            h.number = 3;
            a.onCommand(CmdMacroRepeat);
            require(h.text() == "xxxx", "repeat macro executes requested count");
            a.onCommand(CmdUndo);
            require(h.text() == "x", "all repetitions undo together");
            h.number = 0;
            a.onCommand(CmdMacroRepeat);
            require(h.text() == "x", "repeat cancellation leaves document intact");
            h.text("a\nb\nc");
            h.sci(SCI_GOTOPOS, 0);
            a.onCommand(CmdMacroStart);
            h.sci(SCI_LINEDOWN);
            a.onCommand(CmdMacroStop);
            h.sci(SCI_GOTOPOS, 0);
            a.onCommand(CmdMacroToEnd);
            require(h.sci(SCI_GETCURRENTPOS) == h.sci(SCI_GETLENGTH),
                    "run macro to EOF reaches last position");
            h.sci(SCI_GOTOPOS, 0);
            a.onCommand(CmdMacroStart);
            h.sci(SCI_HOME);
            a.onCommand(CmdMacroStop);
            h.lastAlert.clear();
            a.onCommand(CmdMacroToEnd);
            require(!h.lastAlert.empty(), "non advancing macro stops without an infinite loop");
            const int first = h.current;
            a.onCommand(CmdNew);
            const int second = h.current;
            a.onCommand(CmdPrevTab);
            require(h.current == first, "previous tab navigation");
            a.onCommand(CmdNextTab);
            require(h.current == second, "next tab navigation");
        }
        {
            TestHost h;
            App a(h);
            h.observer = &a;
            a.start();
            h.text("def example():\n    return 1\n\nprint(example())\n");
            a.onCommand(static_cast<Command>(CmdLanguageBase + 3));
            h.sci(SCI_COLOURISE, 0, -1);
            require(a.languageSelection() == 3 && h.sci(SCI_GETSTYLEAT, 0) == 5,
                    "manual Python language styles untitled code");
            h.sci(SCI_GOTOPOS, 0);
            a.onCommand(CmdFoldCurrent);
            require(h.sci(SCI_GETFOLDEXPANDED, 0) == 0, "fold current Python block");
            a.onCommand(CmdUnfoldCurrent);
            require(h.sci(SCI_GETFOLDEXPANDED, 0) != 0, "unfold current block");
            h.sci(SCI_SETSEL, h.sci(SCI_POSITIONFROMLINE, 1), h.sci(SCI_POSITIONFROMLINE, 3));
            a.onCommand(CmdHideLines);
            require(h.sci(SCI_GETLINEVISIBLE, 1) == 0 && h.sci(SCI_GETLINEVISIBLE, 0) != 0,
                    "hide selected lines preserves first line");
            a.onCommand(CmdShowAllLines);
            require(h.sci(SCI_GETLINEVISIBLE, 1) != 0, "show hidden lines");
            a.onCommand(CmdFullScreen);
            require(h.fullScreenState && a.fullScreen(), "fullscreen command delegated");
            a.onCommand(CmdFullScreen);
            require(!h.fullScreenState, "fullscreen restores");
            a.onCommand(CmdAlwaysOnTop);
            require(h.topState && a.alwaysOnTop(), "topmost delegated");
            a.onCommand(CmdAlwaysOnTop);
            require(!h.topState, "topmost toggles off");
            a.onCommand(CmdReadOnly);
            h.savePath = (temp / "language.nss").u8string();
            a.onCommand(CmdSaveSession);
            a.onCommand(CmdNew);
            h.openPath = h.savePath;
            a.onCommand(CmdLoadSession);
            require(a.languageSelection() == 3 && h.sci(SCI_GETREADONLY) == 1,
                    "language override and read-only survive snapshot session");
            a.onCommand(CmdReadOnly);
            a.onCommand(static_cast<Command>(CmdLanguageBase + 1));
            h.sci(SCI_COLOURISE, 0, -1);
            require(h.sci(SCI_GETSTYLEAT, 0) == 0, "plain text language clears highlighting");
            h.openPath = (temp / "legacy2.nss").u8string();
            {
                std::ofstream out(std::filesystem::u8path(h.openPath), std::ios::binary);
                out << "NotepadSharpSession2\n12 400 0 0 0 1 0 0\n1 1 0\n\"\" \"UTF-8\" 1 0 2 \"legacy "
                       "buffer\"\nAutoComplete 1\n";
            }
            a.onCommand(CmdLoadSession);
            require(h.text() == "legacy buffer" && a.languageSelection() == 0,
                    "previous session format remains readable");
        }
        {
            TestHost h;
            App a(h);
            h.observer = &a;
            a.start();
            const auto watched = temp / "watch.txt";
            {
                std::ofstream out(watched, std::ios::binary);
                out << "initial";
            }
            a.openPath(watched.u8string());
            h.text("unsaved draft");
            {
                std::ofstream out(watched, std::ios::binary);
                out << "external version changed";
            }
            a.pollFiles();
            require(h.text() == "unsaved draft" && h.reloadQuestions == 1,
                    "external change keeps unsaved work until reload confirmed");
            a.pollFiles();
            require(h.reloadQuestions == 1, "same disk version does not repeatedly prompt");
            a.onCommand(CmdSave);
            {
                std::ifstream in(watched, std::ios::binary);
                std::string text((std::istreambuf_iterator<char>(in)), {});
                require(text == "external version changed" && h.overwriteQuestions == 1,
                        "save cannot silently overwrite external content");
            }
            h.overwriteExternal = true;
            a.onCommand(CmdSave);
            require(h.sci(SCI_GETMODIFY) == 0, "explicit overwrite saves and resets change tracking");
            {
                std::ofstream out(watched, std::ios::binary);
                out << "accepted external reload";
            }
            h.reloadExternal = true;
            a.pollFiles();
            require(h.text() == "accepted external reload", "confirmed external reload updates buffer");
            const int questions = h.reloadQuestions;
            a.onCommand(CmdMonitorFile);
            require(a.monitoring() && h.sci(SCI_GETREADONLY) == 1, "monitoring locks buffer read only");
            {
                std::ofstream out(watched, std::ios::binary | std::ios::app);
                out << "\nnew log line";
            }
            a.pollFiles();
            require(h.text() == "accepted external reload\nnew log line" &&
                        h.sci(SCI_GETCURRENTPOS) == h.sci(SCI_GETLENGTH) && h.reloadQuestions == questions,
                    "monitoring reloads and follows EOF without prompting");
            a.onCommand(CmdMonitorFile);
            require(!a.monitoring() && h.sci(SCI_GETREADONLY) == 0,
                    "stopping monitoring restores read-only state");
            const auto second = temp / "second-watch.txt";
            {
                std::ofstream out(second);
                out << "second";
            }
            a.openPath(second.u8string());
            require(!a.recentFiles().empty() && a.recentFiles()[0] == second.u8string(),
                    "recent files are newest first");
            a.onCommand(CmdClose);
            a.onCommand(CmdReopenClosed);
            require(h.text() == "second", "reopen closed file restores its path");
            a.onCommand(static_cast<Command>(CmdRecentBase + 1));
            require(h.text().find("new log line") != std::string::npos,
                    "recent command activates matching file");
            require(a.documentNames().size() >= 2, "window list includes open documents");
            h.savePath = (temp / "recent-session.nss").u8string();
            a.onCommand(CmdSaveSession);
            h.openPath = h.savePath;
            a.onCommand(CmdLoadSession);
            require(a.recentFiles().size() >= 2, "recent list survives session round trip");
        }
        {
            TestHost h;
            App a(h);
            h.observer = &a;
            a.start();
            const auto missing = temp / "removed-watch.txt";
            {
                std::ofstream out(missing, std::ios::binary);
                out << "keep this text";
            }
            a.openPath(missing.u8string());
            a.onCommand(CmdMonitorFile);
            std::filesystem::remove(missing);
            a.pollFiles();
            require(h.text() == "keep this text" && !a.monitoring() && !h.lastAlert.empty(),
                    "removed monitored file preserves text and stops monitoring");
            a.onCommand(CmdSave);
            require(!std::filesystem::exists(missing), "recreating removed file requires confirmation");
            h.overwriteExternal = true;
            a.onCommand(CmdSave);
            require(std::filesystem::exists(missing), "confirmed recreation writes kept buffer");
            h.text("not yet saved");
            a.onCommand(CmdMonitorFile);
            require(!a.monitoring(), "dirty buffer cannot enter monitoring mode");
        }
        {
            TestHost h;
            App a(h);
            h.observer = &a;
            a.start();
            auto run = [&](FindAction action) {
                h.request.action = action;
                h.request.cancelled = false;
                a.onCommand(CmdFind);
            };
            h.text("foo FOO foobar foo");
            h.sci(SCI_GOTOPOS, 0);
            h.request = {};
            h.request.find = "foo";
            h.request.matchCase = true;
            h.request.wholeWord = true;
            run(FindAction::Count);
            require(h.lastAlert == "匹配数量：2", "count honors case and whole-word without moving caret");
            require(h.sci(SCI_GETCURRENTPOS) == 0, "count preserves caret");
            run(FindAction::Next);
            require(h.sci(SCI_GETSELECTIONSTART) == 0, "find first exact word");
            a.onCommand(CmdFindNext);
            require(h.sci(SCI_GETSELECTIONSTART) == 15, "find skips other case and partial words");
            h.request.wrapAround = false;
            run(FindAction::Next);
            require(h.sci(SCI_GETSELECTIONSTART) == 15, "no-wrap stops at boundary");
            h.request.wrapAround = true;
            run(FindAction::Next);
            require(h.sci(SCI_GETSELECTIONSTART) == 0, "wrap returns to beginning");
            h.sci(SCI_DOCUMENTEND);
            h.request.backwards = true;
            run(FindAction::Next);
            require(h.sci(SCI_GETSELECTIONSTART) == 15, "backward checkbox reverses next");
            h.request = {};
            h.request.find = "\\r\\n";
            h.request.replace = "\\t";
            h.request.extended = true;
            h.text("a\r\nb\r\n");
            run(FindAction::ReplaceAll);
            require(h.text() == "a\tb\t", "extended replacements decode escaped newlines and tab");
            a.onCommand(CmdUndo);
            require(h.text() == "a\r\nb\r\n", "extended replace all is one undo action");
            h.request.find = "\\xGG";
            run(FindAction::ReplaceAll);
            require(h.text() == "a\r\nb\r\n", "invalid extended escape is non-destructive");
            h.request = {};
            h.request.find = "foo";
            h.request.replace = "longer";
            h.request.inSelection = true;
            h.text("foo | foo foo | foo");
            h.sci(SCI_SETSEL, 6, 13);
            run(FindAction::ReplaceAll);
            require(h.text() == "foo | longer longer | foo",
                    "selection replace tracks growing range and does not touch outside");
            h.request.replace = "";
            run(FindAction::ReplaceAll);
            require(h.text() == "foo | longer longer | foo", "selection search remains within saved bounds");
            h.request.inSelection = false;
            h.request.find = "longer";
            h.request.replace = "";
            run(FindAction::ReplaceAll);
            require(h.text() == "foo |   | foo", "deletion replacement makes progress");
            h.text("a\nb a.b");
            h.sci(SCI_GOTOPOS, 0);
            h.request = {};
            h.request.find = "a.b";
            h.request.regex = true;
            run(FindAction::Count);
            require(h.lastAlert == "匹配数量：1", "regex dot does not match newline by default");
            h.request.dotMatchesNewline = true;
            run(FindAction::Count);
            require(h.lastAlert == "匹配数量：2", "dot-all regex matches across newline");
            h.request.find = "a[.]b";
            run(FindAction::Count);
            require(h.lastAlert == "匹配数量：1", "dot-all preserves literal dots in classes");
            h.request = {};
            h.request.find = "foo";
            h.request.replace = "bar";
            h.request.wrapAround = false;
            h.text("foo foo");
            h.sci(SCI_GOTOPOS, 4);
            run(FindAction::ReplaceAll);
            require(h.text() == "foo bar", "no-wrap replace all starts at caret");
            h.text("foo foo");
            h.sci(SCI_GOTOPOS, 0);
            h.request.wrapAround = true;
            run(FindAction::ReplaceOne);
            require(h.text() == "foo foo" && h.sci(SCI_GETSELECTIONEND) == 3,
                    "replace one first finds text when no selection matches");
            run(FindAction::ReplaceOne);
            require(h.text() == "bar foo", "replace one replaces selected match then finds next");
            h.request = {};
            h.request.regex = true;
            h.request.find = "(?=.)";
            h.request.replace = "";
            h.text("ab");
            run(FindAction::Count);
            require(h.lastAlert == "匹配数量：2", "zero-width count terminates");
            a.onCommand(CmdReadOnly);
            run(FindAction::ReplaceAll);
            require(h.text() == "ab", "read-only replace all leaves contents intact");
            a.onCommand(CmdReadOnly);
            h.request = {};
            h.request.find = "a";
            h.text("alpha beta alpha");
            run(FindAction::FindAllCurrent);
            require(h.sci(SCI_GETREADONLY) && h.text().find("结果数：5") != std::string::npos,
                    "find-current action creates results without another prompt");
        }
        {
            TestHost h;
            App a(h);
            h.observer = &a;
            a.start();
            auto run = [&](FindAction action) {
                h.request.action = action;
                h.request.cancelled = false;
                a.onCommand(CmdFind);
            };
            h.request.find = "(a.b)";
            h.request.replace = "\\1!";
            h.request.regex = true;
            h.request.dotMatchesNewline = true;
            h.text("a\nb a.b");
            run(FindAction::ReplaceAll);
            require(h.text() == "a\nb! a.b!", "multiline regex capture replacement updates cached text");
            a.onCommand(CmdUndo);
            require(h.text() == "a\nb a.b", "multiline regex all replacements undo together");
            h.request.find = "a.b";
            h.request.replace = "OK";
            h.text("a\xf0\x9f\x98\x80"
                   "b");
            run(FindAction::ReplaceAll);
            require(h.text() == "OK", "dot-all wildcard consumes one complete UTF8 scalar");

            h.request.dotMatchesNewline = false;
            h.request.find = R"((?<=前缀)(?<word>中文)\R\k<word>)";
            h.request.replace = R"($+{word}!)";
            h.text("前缀中文\r\n中文");
            run(FindAction::ReplaceAll);
            require(h.text() == "前缀中文!", "Boost lookbehind named captures linebreak and replacement");
            h.request.find = R"(foo\Kbar)";
            h.request.replace = "X";
            h.text("foobar foobar");
            run(FindAction::ReplaceAll);
            require(h.text() == "fooX fooX", "Boost reset match start preserves prefix");
            h.request.find = "(?s)a.b";
            h.request.replace = "X";
            h.text("a\nb");
            run(FindAction::ReplaceAll);
            require(h.text() == "X", "Boost inline dotall overrides checkbox default");
            h.request.find = "(?-s)a.b";
            h.request.dotMatchesNewline = true;
            h.text("a\nb a.b");
            run(FindAction::ReplaceAll);
            require(h.text() == "a\nb X", "Boost inline dotall off overrides checkbox");
            h.request.find = "中文";
            h.request.dotMatchesNewline = false;
            h.request.backwards = true;
            h.text("中文 abc 中文");
            h.sci(SCI_GOTOPOS, h.sci(SCI_GETLENGTH));
            run(FindAction::Next);
            require(h.sci(SCI_GETSELECTIONSTART) == 11 && h.sci(SCI_GETSELECTIONEND) == 17,
                    "Boost reverse search maps Unicode match to UTF8 byte positions");
            h.request.backwards = false;
            h.request.wholeWord = true;
            h.request.find = "cat";
            h.text("cat cats cat");
            run(FindAction::Count);
            require(h.lastAlert == "匹配数量：2", "Boost whole word honors editor boundaries");
            h.request = {};
            h.request.find = "foo";
            h.request.replace = "bar";
            h.text("foo one");
            const int first = h.current;
            a.onCommand(CmdNew);
            h.text("foo two");
            const int second = h.current;
            a.onCommand(CmdNew);
            h.text("foo locked");
            const int locked = h.current;
            a.onCommand(CmdReadOnly);
            run(FindAction::ReplaceAllOpen);
            a.onTabChanged(first);
            require(h.text() == "bar one", "all-open replaces first document");
            a.onTabChanged(second);
            require(h.text() == "bar two", "all-open replaces second document");
            a.onTabChanged(locked);
            require(h.text() == "foo locked" && h.sci(SCI_GETREADONLY), "all-open skips readonly documents");
            a.onTabChanged(second);
            h.request = {};
            h.request.find = "bar";
            h.request.inSelection = true;
            h.sci(SCI_SELECTALL);
            run(FindAction::Count);
            require(h.lastAlert == "匹配数量：1", "selection-only count");
            h.sci(SCI_GOTOPOS, 0);
            h.sci(SCI_REPLACESEL, 0, reinterpret_cast<intptr_t>("edit "));
            run(FindAction::Count);
            require(h.lastAlert.find("请先选中") != std::string::npos,
                    "manual edits invalidate old search scope");
            h.request = {};
            h.request.extended = true;
            h.request.find = "\\0";
            h.request.replace = "-";
            h.sci(SCI_CLEARALL);
            const std::string nul("ab\0cd", 5);
            h.sci(SCI_ADDTEXT, nul.size(), reinterpret_cast<intptr_t>(nul.data()));
            run(FindAction::ReplaceAll);
            require(h.text() == "ab-cd", "extended mode can replace NUL bytes");
        }
        {
            TestHost h;App a(h);h.observer=&a;a.start();h.text("foo");h.sci(SCI_GOTOPOS,1);h.request.find="foo";h.request.cancelled=false;
            a.onCommand(CmdFind);require(h.sci(SCI_GETSELECTIONSTART)==0&&h.sci(SCI_GETSELECTIONEND)==3,"wrapped search can match across original caret");
            h.sci(SCI_GOTOPOS,2);h.request.backwards=true;a.onCommand(CmdFind);require(h.sci(SCI_GETSELECTIONSTART)==0&&h.sci(SCI_GETSELECTIONEND)==3,"backward wrap can match across original caret");
        }

        {
            TestHost h;
            App a(h);
            h.observer = &a;
            a.start();
            auto search = [&](const std::string &pattern, const std::string &text, intptr_t from,
                              intptr_t to, bool matchCase = true) {
                h.text(text.c_str());
                FindRequest request;
                request.find = pattern;
                request.regex = true;
                request.matchCase = matchCase;
                PreparedSearch query;
                std::string error;
                require(prepareSearch(request, query, error), "Unicode/boundary test compiles");
                h.sci(SCI_SETSTATUS, SC_STATUS_OK);
                h.sci(SCI_SETSEARCHFLAGS, query.flags);
                h.sci(SCI_SETTARGETRANGE, from, to);
                auto found = searchInTarget(h, h.current, query);
                require(h.sci(SCI_GETSTATUS) != SC_STATUS_WARN_REGEX, "Unicode/boundary search has no runtime error");
                return std::pair<intptr_t, intptr_t>{found, found < 0 ? -1 : h.sci(SCI_GETTARGETEND)};
            };
            // Independent oracle: Windows Unicode classification used by the original engine.
            const std::wstring samples = L"Aaz09_中éΩЖ\u0663\uFF15\u00A0\u3000\u0301!\t";
            struct Category { const char *pattern; WORD mask; bool underscore; };
            const Category categories[] = {{R"(\w)", C1_ALPHA | C1_DIGIT, true},
                {R"(\d)", C1_DIGIT, false}, {R"(\s)", C1_SPACE, false},
                {R"([[:alpha:]])", C1_ALPHA, false}, {R"([[:upper:]])", C1_UPPER, false},
                {R"([[:lower:]])", C1_LOWER, false}};
            for (wchar_t sample : samples) {
                WORD type = 0;
                require(GetStringTypeW(CT_CTYPE1, &sample, 1, &type) != 0, "native Unicode category oracle");
                char encoded[8]{};
                const int size = WideCharToMultiByte(CP_UTF8, 0, &sample, 1, encoded, sizeof encoded, nullptr, nullptr);
                for (const auto &category : categories) {
                    const bool expected = (type & category.mask) != 0 || (category.underscore && sample == L'_');
                    const auto result = search(category.pattern, std::string(encoded, size), 0, size);
                    require((result.first == 0 && result.second == size) == expected,
                            "Unicode regex classification equals native Windows tables");
                }
            }
            require(search("éωж", "ÉΩЖ", 0, 6, false) == std::make_pair<intptr_t,intptr_t>(0,6),
                    "native non-ASCII case-insensitive matching");
            const std::string emoji = "\xF0\x9F\x98\x80";
            require(search(R"(\W)", emoji, 0, 4) == std::make_pair<intptr_t,intptr_t>(0,4),
                    "negated Unicode word class consumes supplementary scalar");
            require(search(R"([^a])", emoji, 0, 4) == std::make_pair<intptr_t,intptr_t>(0,4),
                    "negated bracket class never splits a surrogate pair");

            const std::string grin = "\xF0\x9F\x98\x81";
            require(search("[" + emoji + "]", grin, 0, 4).first == -1,
                    "supplementary literal class distinguishes shared high surrogate");
            require(search("[^" + emoji + "]", grin, 0, 4).second == 4,
                    "negated supplementary class retains different emoji");
            require(search("[^" + emoji + "]", emoji, 0, 4).first == -1,
                    "negated supplementary class excludes exact scalar");
            require(search(emoji + "+", emoji + emoji, 0, 8).second == 8,
                    "literal supplementary quantifier repeats whole scalar");
            require(search("[a" + emoji + "]+", "a" + emoji + grin, 0, 9).second == 5,
                    "mixed BMP and supplementary class preserves alternatives");
            require(search("[" + emoji + "^]+", emoji + "^", 0, 5).second == 5,
                    "class literal caret does not become negation after scalar extraction");
            require(search(R"([\x{1F600}-\x{1F64F}]+)", emoji + grin, 0, 8).second == 8,
                    "supplementary hexadecimal range matches complete scalars");
            require(search(R"([\x{1F600}-\x{1F64F}])", "\xF0\x9F\x99\x90", 0, 4).first == -1,
                    "supplementary range excludes adjacent codepoint");
            require(search(R"([\x{103FF}-\x{10401}])", "\xF0\x90\x90\x80", 0, 4).second == 4,
                    "supplementary range crosses surrogate block boundary");
            require(search(R"([a-\x{1F600}])", emoji, 0, 4).second == 4,
                    "range can cross BMP and supplementary planes");

            require(search(R"([a-\x{1F600}])", grin, 0, 4).first == -1,
                    "cross-plane range does not leak through surrogate code units");
            require(search(R"([\x{0000}-\x{FFFF}])", emoji, 0, 4).first == -1,
                    "BMP-only scalar range excludes supplementary characters");
            require(search(R"([^\x{0000}-\x{FFFF}])", emoji, 0, 4).second == 4,
                    "complement of BMP range matches supplementary scalar");
            require(search(R"([\x61-\x{1F600}])", grin, 0, 4).first == -1,
                    "short hexadecimal range endpoint preserves scalar bound");
            require(search("\\Q" + emoji + "\\E+", emoji + emoji, 0, 8).second == 8,
                    "quantifier after quote repeats entire supplementary literal");
            require(search("(?x)\\Q[ # " + emoji + "\\E", "[ # " + emoji, 0, 8).second == 8,
                    "quoted spaces hash and brackets stay literal in extended mode");
            require(search("(?x)# [ ( ignored\r[" + emoji + "]", emoji, 0, 4).second == 4,
                    "extended comment terminates at CR as well as LF");
            require(search(R"(\x{1F600}{2})", emoji + emoji, 0, 8).second == 8,
                    "hexadecimal supplementary atom supports quantifier");
            require(search("(?<=[" + emoji + "])文", emoji + "文", 0, 7).first == 4,
                    "fixed-width supplementary class lookbehind");
            require(search("(?<![" + emoji + "])文", grin + "文", 0, 7).first == 4,
                    "negative supplementary class lookbehind distinguishes emoji");
            require(search("(?x)# [ ( . ignored\n" + emoji + "+", emoji + emoji, 0, 8).second == 8,
                    "extended-mode comment cannot consume later Unicode atoms");
            require(search("(?x:(?-x:[#]))[" + emoji + "]", "#" + emoji, 0, 5).second == 5,
                    "nested scoped extended mode restores enclosing mode");
            require(search("(?x: a # ] ( ignored\n ) [" + emoji + "]", "a " + emoji, 0, 6).second == 6,
                    "scoped comment does not alter group nesting");
            require(search("(?x)\\#" + emoji + "+", "#" + emoji + emoji, 0, 9).second == 9,
                    "escaped hash is literal under extended mode");
            require(search(R"(\Pd)", emoji, 0, 4).second == 4,
                    "one-letter negated Unicode property consumes scalar");
            require(search(R"((?<u>[^a])\k<u>)", emoji + emoji, 0, 8).second == 8,
                    "named backreference retains supplementary capture");
            require(search("e\xCC\x81", "é", 0, 2).first == -1,
                    "regex does not silently normalize canonical equivalents");
            require(search(R"(\Q.[]\E)", ".[]", 0, 3).first == 0, "quoted regex atoms stay literal");
            require(search(R"([]a.]+)", "]a.", 0, 3).second == 3, "leading closing bracket stays in class");

            require(search(R"((?<=\w)文)", "中文", 0, 6).first == 3,
                    "Unicode class in fixed-width lookbehind remains valid");
            require(search(R"((?<!\d)文)", "中文", 0, 6).first == 3,
                    "negated fixed-width lookbehind remains valid");
            require(search(R"((?# [. ignored)中文)", "中文", 0, 6).first == 0,
                    "regex comment does not alter scalar atom parsing");
            require(search("aba", "ababa", 5, 0) == std::make_pair<intptr_t,intptr_t>(2,5),
                    "reverse search finds overlapping rightmost match");
            require(search("a+", "aaa", 3, 0) == std::make_pair<intptr_t,intptr_t>(0,3),
                    "reverse tie retains longest match");
            require(search("abcd|bc", "abcd", 4, 0) == std::make_pair<intptr_t,intptr_t>(0,4),
                    "reverse chooses farthest end rather than latest start");
            require(search("^中文", "x中文\n中文", 1, 14).first == 8,
                    "caret in line does not create false start-of-line anchor");
            require(search(R"(\A中文)", "x中文", 1, 7).first == -1,
                    "range start is not document start");
            require(search(R"((?<=x)中文)", "x中文y", 1, 7).first == 1,
                    "lookbehind can inspect prefix before target range");
            require(search(R"(中文(?=y))", "x中文y", 1, 7).first == -1,
                    "lookahead stops at upper target boundary like original");
            require(search(R"(中文$)", "x中文y", 1, 7).second == 7,
                    "upper target boundary terminates line anchor like original");
            require(search(R"(中文\z)", "x中文y", 1, 7).second == 7,
                    "upper target boundary terminates buffer anchor like original");
            require(search(R"(\b中文)", "x中文", 1, 7).first == -1,
                    "word boundary sees preceding Unicode word character");
            require(search(R"($)", "a\r\nb", 0, 4).first == 1,
                    "line end anchor is before CRLF");
            auto run = [&](FindAction action) {
                h.request.action = action;
                h.request.cancelled = false;
                a.onCommand(CmdFind);
            };
            h.request = {};
            h.request.regex = true;
            h.request.find = "(?s)(?=.|$)";
            h.request.wrapAround = false;
            h.text("a\r\nb");
            run(FindAction::Count);
            require(h.lastAlert == "匹配数量：4", "zero-width batch skips interior CRLF boundary");
            h.sci(SCI_GOTOPOS, 1);
            run(FindAction::Next);
            require(h.sci(SCI_GETSELECTIONSTART) == 1, "zero-width interactive finds CRLF start");
            run(FindAction::Next);
            require(h.sci(SCI_GETSELECTIONSTART) == 3, "next zero-width match skips whole CRLF");
            h.request.backwards = true;
            run(FindAction::Next);
            require(h.sci(SCI_GETSELECTIONSTART) == 1, "previous zero-width match skips whole CRLF");
            h.request.backwards = false;

            h.request.find = "(?<face>[" + emoji + "]+)";
            h.request.replace = "$+{face}!";
            h.request.wrapAround = true;
            h.text((emoji + emoji + grin).c_str());
            run(FindAction::ReplaceAll);
            require(h.text() == emoji + emoji + "!" + grin,
                    "supplementary named capture replacement preserves neighboring emoji");
            a.onCommand(CmdUndo);
            require(h.text() == emoji + emoji + grin, "complex Unicode replacements undo as one action");
            h.request.find = R"([\x{110000}])";
            run(FindAction::ReplaceAll);
            require(h.text() == emoji + emoji + grin && h.lastAlert.find("无效") != std::string::npos,
                    "invalid out-of-range codepoint is rejected before editing");

            h.request.find = "(?<=[a" + emoji + "])文";
            h.text("a文");
            run(FindAction::ReplaceAll);
            require(h.text() == "a文" && h.lastAlert.find("无效") != std::string::npos,
                    "mixed UTF16-width lookbehind is rejected without editing");
            h.request.find = "a*";
            h.text("ab");
            run(FindAction::Count);
            require(h.lastAlert == "匹配数量：2", "batch suppresses empty match immediately after nonempty match");
            h.request.wrapAround = true;
            h.request.replace = "X";
            run(FindAction::ReplaceAll);
            require(h.text() == "XbX", "batch replace shares empty continuation policy");
        }

        {
            TestHost h;
            App a(h);
            h.observer = &a;
            a.start();
            auto run = [&](FindAction action) {
                h.request.action = action;
                h.request.cancelled = false;
                a.onCommand(CmdFind);
            };
            auto count = [&](const std::string &pattern, const std::string &text, int expected, bool matchCase = true) {
                h.text(text.c_str());
                h.request = {};
                h.request.regex = true;
                h.request.unicode = true;
                h.request.matchCase = matchCase;
                h.request.find = pattern;
                run(FindAction::Count);
                const auto wanted = "匹配数量：" + std::to_string(expected);
                if (h.lastAlert != wanted)
                    std::fprintf(stderr, "ICU pattern %s: expected %s, got %s\n", pattern.c_str(), wanted.c_str(), h.lastAlert.c_str());
                require(h.lastAlert == wanted, "ICU Unicode properties/folding/grapheme count");
            };
            count(R"(\p{L}+)", "中文 ΩЖ \xF0\x90\x90\x80 123", 3);
            count(R"(\p{Script=Han}+)", "中文ΩЖ", 1);
            count(R"(\p{scx=Hira}+)", "あーA", 1);
            count(R"(\p{Block=Basic_Latin}+)", "abc中文", 1);
            count(R"(\p{Extended_Pictographic})", "😀👩A", 2);
            count(R"(\p{White_Space}+)", "a\xC2\xA0\xE3\x80\x80"
                                            "b", 1);
            count(R"(\p{M}+)", "e\xCC\x81", 1);
            count(R"([\p{L}&&[^\p{Script=Latin}]]+)", "abc中文Ω", 1);
            count(R"(\p{L})", "\xF0\x90\x90\x80", 1);
            count("strasse", "Straße STRASSE straße", 3, false);
            count("straße", "STRASSE Straße", 2, false);
            count("ffi", "ﬃ FFI ffi", 3, false);
            count("σ", "Σ σ ς", 3, false);
            count("\xF0\x90\x90\xA8", "\xF0\x90\x90\x80", 1, false);
            count("strasse", "Straße", 0, true);
            count(R"((?-i:strasse))", "Straße", 0, false);
            count(R"((?i:strasse))", "Straße", 1, true);
            for (const auto &cluster : std::vector<std::string>{"e\xCC\x81", "👨‍👩‍👧‍👦", "👍🏽", "🇨🇳", "1️⃣", "\r\n", "가", "क्ष"})
                count(R"(\X)", cluster, 1);
            count(R"(\X{2})", "e\xCC\x81🇨🇳", 1);
            count(R"(\X)", "e\xCC\x81🇨🇳", 2);
            count(R"(e\x{301})", "é", 0);
            h.request = {};
            h.request.regex = h.request.unicode = true;
            h.request.find = "(?<word>strasse)";
            h.request.replace = "[$+{word}]";
            h.text("Straße STRASSE");
            run(FindAction::ReplaceAll);
            require(h.text() == "[Straße] [STRASSE]", "ICU full folding retains original capture and UTF8 positions");
            a.onCommand(CmdUndo);
            require(h.text() == "Straße STRASSE", "ICU full-fold replacement is one undo action");
            h.request.find = R"((?<cluster>\X))";
            h.request.replace = "<$+{cluster}>";
            h.text("e\xCC\x81🇨🇳");
            run(FindAction::ReplaceAll);
            require(h.text() == "<e\xCC\x81><🇨🇳>", "ICU grapheme named capture replacement retains original bytes");
            h.request.find = "strasse";
            h.request.backwards = true;
            h.request.wrapAround = false;
            h.text("Straße Straße");
            h.sci(SCI_GOTOPOS, h.sci(SCI_GETLENGTH));
            run(FindAction::Next);
            require(h.sci(SCI_GETSELECTIONSTART) == 8 && h.sci(SCI_GETSELECTIONEND) == 15,
                    "ICU reverse full folding returns original UTF8 byte range");
            h.request.backwards = false;
            h.request.inSelection = true;
            h.request.find = R"(\X)";
            h.sci(SCI_SETSEL, 8, 15);
            run(FindAction::Count);
            require(h.lastAlert == "匹配数量：6", "ICU grapheme count honors saved selection range");
            h.request.inSelection = false;
            h.request.wholeWord = true;
            h.request.find = "\xF0\x90\x90\xA8";
            h.text("\xF0\x90\x90\x80 \xF0\x90\x90\x80"
                   "a");
            run(FindAction::Count);
            require(h.lastAlert == "匹配数量：1", "ICU whole-word uses Unicode supplementary letter boundaries");
            h.request.wholeWord = false;
            h.request.find = R"(\p{NoSuchUnicodeProperty})";
            h.request.replace = "X";
            const auto original = h.text();
            run(FindAction::ReplaceAll);
            require(h.text() == original && h.lastAlert.find("无效") != std::string::npos,
                    "ICU invalid Unicode property leaves document untouched");
            h.request.find = R"(foo\Kbar)";
            run(FindAction::ReplaceAll);
            require(h.text() == original && h.lastAlert.find("无效") != std::string::npos,
                    "ICU mode rejects unsupported Boost escapes rather than matching literals");
            // Turning ICU off still preserves Boost-only syntax.
            h.request.unicode = false;
            h.request.find = R"(foo\Kbar)";
            h.text("foobar");
            h.request.wrapAround = true;
            run(FindAction::ReplaceAll);
            require(h.text() == "fooX", "Boost compatibility mode survives ICU integration");
        }
        std::puts("Editor behavior tests passed");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
