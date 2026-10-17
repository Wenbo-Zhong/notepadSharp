#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct SCNotification;

enum Command {
    CmdNew = 1,
    CmdOpen,
    CmdSave,
    CmdSaveAs,
    CmdClose,
    CmdExit,
    CmdUndo,
    CmdRedo,
    CmdCut,
    CmdCopy,
    CmdPaste,
    CmdDelete,
    CmdSelectAll,
    CmdFind,
    CmdFindNext,
    CmdFindPrev,
    CmdReplace,
    CmdGoto,
    CmdWrap,
    CmdLineNumbers,
    CmdWhitespace,
    CmdEol,
    CmdZoomIn,
    CmdZoomOut,
    CmdZoomReset
};

enum class SaveChoice { Save, Discard, Cancel };

enum class FindAction { Next, ReplaceOne, ReplaceAll };

struct FindRequest {
    std::string find;
    std::string replace;
    bool matchCase = false;
    bool wholeWord = false;
    FindAction action = FindAction::Next;
    bool cancelled = true;
};

// Platform window. The same App drives every backend.
class Host {
public:
    virtual ~Host() = default;
    virtual intptr_t send(int doc, unsigned msg, uintptr_t wParam, intptr_t lParam) = 0;
    virtual int createEditor() = 0;
    virtual void destroyEditor(int doc) = 0;
    virtual void showEditor(int doc) = 0;
    virtual void setWindowTitle(const std::string &title) = 0;
    virtual void setStatus(const std::string &text) = 0;
    virtual void setTabTitle(int doc, const std::string &title) = 0;
    virtual std::string promptOpenPath() = 0;
    virtual std::string promptSavePath(const std::string &suggested) = 0;
    virtual SaveChoice askSave(const std::string &name) = 0;
    virtual void alert(const std::string &message) = 0;
    virtual FindRequest promptFind(const FindRequest &seed, bool replaceMode) = 0;
    virtual int promptGoto(int currentLine, int lineCount) = 0;
    virtual std::string monoFont() const = 0;
    virtual int defaultEol() const = 0;
    virtual void quit() = 0;
};

class App {
public:
    explicit App(Host &host);

    void start();
    void openPath(const std::string &path);
    void onCommand(Command command);
    void onNotify(int doc, const SCNotification *notification);
    void onTabChanged(int doc);
    bool onCloseRequested();

    bool wrap() const { return wrap_; }
    bool lineNumbers() const { return lineNumbers_; }
    bool whitespace() const { return whitespace_; }
    bool eolVisible() const { return eolVisible_; }

private:
    struct Doc {
        int id = 0;
        std::string path;
        bool modified = false;
        std::string lexer = "text";
        std::string encoding = "UTF-8";
    };

    Host &host_;
    std::vector<Doc> docs_;
    int active_ = -1;
    FindRequest find_{};
    bool wrap_ = false;
    bool lineNumbers_ = true;
    bool whitespace_ = false;
    bool eolVisible_ = false;
    int zoom_ = 0;

    Doc *active();
    Doc *findDoc(int id);
    intptr_t send(unsigned msg, uintptr_t w = 0, intptr_t l = 0);
    intptr_t sendTo(int doc, unsigned msg, uintptr_t w = 0, intptr_t l = 0);
    void configure(Doc &doc);
    void applyView(int doc);
    void applyTheme(int doc);
    void applyLexer(Doc &doc);
    void touchTitles();
    void updateStatus();
    bool save(Doc &doc, bool forceDialog);
    bool confirmClose(Doc &doc);
    void closeActive();
    void findAgain(bool forward);
    void replaceAll();
    std::string displayName(const Doc &doc) const;
};
