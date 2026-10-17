#include "App.h"

#include "ILexer.h"
#include "Lexilla.h"
#include "Scintilla.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {

extern "C" Scintilla::ILexer5 *LEXILLA_CALL CreateLexer(const char *name);

std::string fileNameOf(const std::string &path) {
    if (path.empty()) {
        return "未命名";
    }
    return std::filesystem::u8path(path).filename().u8string();
}

bool isValidUtf8(const std::string &text) {
    const auto *s = reinterpret_cast<const unsigned char *>(text.data());
    const size_t n = text.size();
    for (size_t i = 0; i < n;) {
        const unsigned char c = s[i];
        size_t need = 0;
        if (c <= 0x7F) {
            need = 1;
        } else if ((c & 0xE0) == 0xC0 && c >= 0xC2) {
            need = 2;
        } else if ((c & 0xF0) == 0xE0) {
            need = 3;
        } else if ((c & 0xF8) == 0xF0 && c <= 0xF4) {
            need = 4;
        } else {
            return false;
        }
        if (i + need > n) {
            return false;
        }
        for (size_t j = 1; j < need; ++j) {
            if ((s[i + j] & 0xC0) != 0x80) {
                return false;
            }
        }
        if (need == 3) {
            const unsigned code = (c << 12) | (s[i + 1] << 6) | s[i + 2];
            if (code >= 0xD800 && code <= 0xDFFF) {
                return false;
            }
        }
        i += need;
    }
    return true;
}

std::string readFile(const std::string &path, std::string &error) {
    std::ifstream in(std::filesystem::u8path(path), std::ios::binary);
    if (!in) {
        error = "无法打开文件。";
        return {};
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

bool writeFile(const std::string &path, const std::string &data, std::string &error) {
    std::ofstream out(std::filesystem::u8path(path), std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "无法写入文件。";
        return false;
    }
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!out) {
        error = "写入文件失败。";
        return false;
    }
    return true;
}

struct Lang {
    const char *lexer;
    const char *keywords;
};

Lang languageFor(const std::string &path) {
    std::string ext = std::filesystem::u8path(path).extension().u8string();
    for (char &ch : ext) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    if (ext == ".c" || ext == ".h" || ext == ".cc" || ext == ".cpp" || ext == ".cxx" ||
        ext == ".hpp" || ext == ".hh" || ext == ".java" || ext == ".cs") {
        return {"cpp",
                "alignas alignof and and_eq asm auto bitand bitor bool break case catch char "
                "char8_t char16_t char32_t class compl concept const consteval constexpr "
                "constinit continue co_await co_return co_yield decltype default delete do "
                "double dynamic_cast else enum explicit export extern false float for friend "
                "goto if inline int long mutable namespace new noexcept not not_eq nullptr "
                "operator or or_eq private protected public register reinterpret_cast return "
                "short signed sizeof static static_assert static_cast struct switch template "
                "this thread_local throw true try typedef typeid typename union unsigned using "
                "virtual void volatile wchar_t while xor xor_eq"};
    }
    if (ext == ".js" || ext == ".jsx" || ext == ".ts" || ext == ".tsx") {
        return {"cpp",
                "as async await break case catch class const continue debugger default delete "
                "do else export extends false finally for from function if import in instanceof "
                "let new null return static super switch this throw true try typeof var void "
                "while with yield"};
    }
    if (ext == ".py") {
        return {"python",
                "and as assert async await break class continue def del elif else except False "
                "finally for from global if import in is lambda None nonlocal not or pass raise "
                "return True try while with yield"};
    }
    if (ext == ".rs") {
        return {"rust",
                "as async await break const continue crate dyn else enum extern false fn for if "
                "impl in let loop match mod move mut pub ref return self Self static struct super "
                "trait true type unsafe use where while"};
    }
    if (ext == ".json") return {"json", ""};
    if (ext == ".xml" || ext == ".svg") return {"xml", ""};
    if (ext == ".html" || ext == ".htm") return {"hypertext", ""};
    if (ext == ".css") return {"css", ""};
    if (ext == ".md" || ext == ".markdown") return {"markdown", ""};
    if (ext == ".sh" || ext == ".bash") return {"bash", ""};
    if (ext == ".lua") return {"lua", "and break do else elseif end false for function goto if in local nil not or repeat return then true until while"};
    if (ext == ".sql") return {"sql", "select from where insert into update delete create table join left right inner on as and or not null primary key"};
    if (ext == ".yml" || ext == ".yaml") return {"yaml", ""};
    if (ext == ".cmake" || fileNameOf(path) == "CMakeLists.txt") return {"cmake", ""};
    if (ext == ".mak" || fileNameOf(path) == "Makefile" || fileNameOf(path) == "makefile") return {"makefile", ""};
    if (ext == ".rb") return {"ruby", ""};
    if (ext == ".pl" || ext == ".pm") return {"perl", ""};
    if (ext == ".php") return {"phpscript", ""};
    return {"null", ""};
}

}  // namespace

App::App(Host &host) : host_(host) {}

App::Doc *App::active() {
    if (active_ < 0 || active_ >= static_cast<int>(docs_.size())) {
        return nullptr;
    }
    return &docs_[static_cast<size_t>(active_)];
}

App::Doc *App::findDoc(int id) {
    for (Doc &doc : docs_) {
        if (doc.id == id) {
            return &doc;
        }
    }
    return nullptr;
}

intptr_t App::sendTo(int doc, unsigned msg, uintptr_t w, intptr_t l) {
    return host_.send(doc, msg, w, l);
}

intptr_t App::send(unsigned msg, uintptr_t w, intptr_t l) {
    Doc *doc = active();
    if (!doc) {
        return 0;
    }
    return sendTo(doc->id, msg, w, l);
}

std::string App::displayName(const Doc &doc) const {
    std::string name = fileNameOf(doc.path);
    if (doc.modified) {
        name += "*";
    }
    return name;
}

void App::applyTheme(int doc) {
    auto s = [&](unsigned msg, uintptr_t w = 0, intptr_t l = 0) {
        return sendTo(doc, msg, w, l);
    };
    const std::string font = host_.monoFont();
    s(SCI_STYLERESETDEFAULT);
    s(SCI_STYLESETFONT, STYLE_DEFAULT, reinterpret_cast<intptr_t>(font.c_str()));
    s(SCI_STYLESETSIZE, STYLE_DEFAULT, 11);
    s(SCI_STYLESETFORE, STYLE_DEFAULT, 0x202020);
    s(SCI_STYLESETBACK, STYLE_DEFAULT, 0xFFFFFF);
    s(SCI_STYLECLEARALL);
    const int comment = 0x008000;
    const int keyword = 0xFF0000;
    const int string = 0x1515A0;
    const int number = 0x800080;
    const int preproc = 0x800000;
    for (int style : {1, 2, 3, 12, 13, 15, 23}) {
        s(SCI_STYLESETFORE, style, comment);
    }
    for (int style : {4, 14}) {
        s(SCI_STYLESETFORE, style, number);
    }
    s(SCI_STYLESETFORE, 5, keyword);
    s(SCI_STYLESETBOLD, 5, 1);
    for (int style : {6, 7, 10, 11}) {
        s(SCI_STYLESETFORE, style, string);
    }
    s(SCI_STYLESETFORE, 9, preproc);
    s(SCI_STYLESETFORE, STYLE_LINENUMBER, 0x606060);
    s(SCI_STYLESETBACK, STYLE_LINENUMBER, 0xF3F3F3);
    s(SCI_SETCARETFORE, 0x202020);
    s(SCI_SETSELBACK, 1, 0xD8C8B4);
    s(SCI_STYLESETFONT, STYLE_LINENUMBER, reinterpret_cast<intptr_t>(font.c_str()));
}

void App::applyView(int doc) {
    auto s = [&](unsigned msg, uintptr_t w = 0, intptr_t l = 0) {
        return sendTo(doc, msg, w, l);
    };
    s(SCI_SETWRAPMODE, wrap_ ? SC_WRAP_WORD : SC_WRAP_NONE);
    s(SCI_SETVIEWWS, whitespace_ ? SCWS_VISIBLEALWAYS : SCWS_INVISIBLE);
    s(SCI_SETVIEWEOL, eolVisible_ ? 1 : 0);
    s(SCI_SETZOOM, zoom_);
    if (lineNumbers_) {
        const intptr_t width = s(SCI_TEXTWIDTH, STYLE_LINENUMBER, reinterpret_cast<intptr_t>("99999"));
        s(SCI_SETMARGINWIDTHN, 0, width + 8);
    } else {
        s(SCI_SETMARGINWIDTHN, 0, 0);
    }
}

void App::applyLexer(Doc &doc) {
    const Lang lang = languageFor(doc.path);
    doc.lexer = lang.lexer;
    Scintilla::ILexer5 *lexer = CreateLexer(lang.lexer);
    if (!lexer) {
        lexer = CreateLexer("null");
        doc.lexer = "text";
    }
    sendTo(doc.id, SCI_SETILEXER, 0, reinterpret_cast<intptr_t>(lexer));
    if (lang.keywords && lang.keywords[0]) {
        sendTo(doc.id, SCI_SETKEYWORDS, 0, reinterpret_cast<intptr_t>(lang.keywords));
    }
    applyTheme(doc.id);
    sendTo(doc.id, SCI_COLOURISE, 0, -1);
}

void App::configure(Doc &doc) {
    sendTo(doc.id, SCI_SETCODEPAGE, SC_CP_UTF8);
    sendTo(doc.id, SCI_SETEOLMODE, host_.defaultEol());
    sendTo(doc.id, SCI_SETMARGINTYPEN, 0, SC_MARGIN_NUMBER);
    sendTo(doc.id, SCI_SETMARGINWIDTHN, 1, 0);
    sendTo(doc.id, SCI_SETMARGINWIDTHN, 2, 0);
    sendTo(doc.id, SCI_SETTABWIDTH, 4);
    sendTo(doc.id, SCI_SETUSETABS, 0);
    sendTo(doc.id, SCI_SETINDENT, 4);
    sendTo(doc.id, SCI_SETMODEVENTMASK, SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT);
    applyLexer(doc);
    applyView(doc.id);
    sendTo(doc.id, SCI_SETSAVEPOINT);
    doc.modified = false;
}

void App::touchTitles() {
    for (const Doc &doc : docs_) {
        host_.setTabTitle(doc.id, displayName(doc));
    }
    if (const Doc *doc = active()) {
        host_.setWindowTitle(displayName(*doc) + " - NotepadSharp");
    } else {
        host_.setWindowTitle("NotepadSharp");
    }
    updateStatus();
}

void App::updateStatus() {
    Doc *doc = active();
    if (!doc) {
        host_.setStatus("");
        return;
    }
    const intptr_t pos = send(SCI_GETCURRENTPOS);
    const int line = static_cast<int>(send(SCI_LINEFROMPOSITION, pos)) + 1;
    const int col = static_cast<int>(send(SCI_GETCOLUMN, pos));
    const int eol = static_cast<int>(send(SCI_GETEOLMODE));
    const char *eolName = eol == SC_EOL_CRLF ? "CRLF" : eol == SC_EOL_CR ? "CR" : "LF";
    const char *insert = send(SCI_GETOVERTYPE) ? "OVR" : "INS";
    std::ostringstream status;
    status << "行 " << line << "，列 " << col << "    " << doc->encoding << "    " << eolName
           << "    " << doc->lexer << "    " << insert;
    if (wrap_) status << "    换行";
    host_.setStatus(status.str());
}

void App::start() {
    onCommand(CmdNew);
}

void App::openPath(const std::string &path) {
    std::string error;
    std::string data = readFile(path, error);
    if (!error.empty()) {
        host_.alert(error);
        if (docs_.empty()) {
            onCommand(CmdNew);
        }
        return;
    }
    if (docs_.size() == 1 && docs_[0].path.empty() && !docs_[0].modified) {
        // Reuse the initial empty document.
    } else {
        onCommand(CmdNew);
    }
    Doc *doc = active();
    if (!doc) {
        return;
    }
    bool utf8 = true;
    if (data.size() >= 3 &&
        static_cast<unsigned char>(data[0]) == 0xEF &&
        static_cast<unsigned char>(data[1]) == 0xBB &&
        static_cast<unsigned char>(data[2]) == 0xBF) {
        data.erase(0, 3);
    } else if (!isValidUtf8(data)) {
        utf8 = false;
    }
    doc->encoding = utf8 ? "UTF-8" : "Latin-1";
    send(SCI_SETCODEPAGE, utf8 ? SC_CP_UTF8 : 0);
    send(SCI_CLEARALL);
    send(SCI_ADDTEXT, data.size(), reinterpret_cast<intptr_t>(data.data()));
    send(SCI_EMPTYUNDOBUFFER);
    send(SCI_SETSAVEPOINT);
    doc->path = path;
    doc->modified = false;
    if (data.find("\r\n") != std::string::npos) {
        send(SCI_SETEOLMODE, SC_EOL_CRLF);
    } else if (data.find('\r') != std::string::npos) {
        send(SCI_SETEOLMODE, SC_EOL_CR);
    } else if (data.find('\n') != std::string::npos) {
        send(SCI_SETEOLMODE, SC_EOL_LF);
    }
    applyLexer(*doc);
    applyView(doc->id);
    send(SCI_GOTOPOS, 0);
    touchTitles();
}

bool App::save(Doc &doc, bool forceDialog) {
    std::string path = doc.path;
    if (forceDialog || path.empty()) {
        path = host_.promptSavePath(doc.path.empty() ? "未命名.txt" : fileNameOf(doc.path));
        if (path.empty()) {
            return false;
        }
    }
    const intptr_t length = sendTo(doc.id, SCI_GETLENGTH);
    std::string data(static_cast<size_t>(length + 1), '\0');
    sendTo(doc.id, SCI_GETTEXT, static_cast<uintptr_t>(length + 1), reinterpret_cast<intptr_t>(data.data()));
    data.resize(static_cast<size_t>(length));
    std::string error;
    if (!writeFile(path, data, error)) {
        host_.alert(error);
        return false;
    }
    doc.path = path;
    sendTo(doc.id, SCI_SETSAVEPOINT);
    doc.modified = false;
    applyLexer(doc);
    applyView(doc.id);
    touchTitles();
    return true;
}

bool App::confirmClose(Doc &doc) {
    if (!doc.modified) {
        return true;
    }
    const SaveChoice choice = host_.askSave(displayName(doc));
    if (choice == SaveChoice::Cancel) {
        return false;
    }
    if (choice == SaveChoice::Discard) {
        return true;
    }
    return save(doc, false);
}

void App::closeActive() {
    Doc *doc = active();
    if (!doc) {
        return;
    }
    if (!confirmClose(*doc)) {
        return;
    }
    const int id = doc->id;
    docs_.erase(docs_.begin() + active_);
    host_.destroyEditor(id);
    if (docs_.empty()) {
        onCommand(CmdNew);
        return;
    }
    if (active_ >= static_cast<int>(docs_.size())) {
        active_ = static_cast<int>(docs_.size()) - 1;
    }
    host_.showEditor(docs_[static_cast<size_t>(active_)].id);
    touchTitles();
}

void App::findAgain(bool forward) {
    if (find_.find.empty()) {
        onCommand(CmdFind);
        return;
    }
    const int flags = (find_.matchCase ? SCFIND_MATCHCASE : 0) | (find_.wholeWord ? SCFIND_WHOLEWORD : 0);
    send(SCI_SETSEARCHFLAGS, flags);
    const intptr_t length = send(SCI_GETLENGTH);
    const intptr_t anchor = forward ? send(SCI_GETSELECTIONEND) : send(SCI_GETSELECTIONSTART);
    auto search = [&](intptr_t from, intptr_t to) {
        send(SCI_SETTARGETSTART, from);
        send(SCI_SETTARGETEND, to);
        return send(SCI_SEARCHINTARGET, find_.find.size(), reinterpret_cast<intptr_t>(find_.find.c_str()));
    };
    intptr_t found = search(anchor, forward ? length : 0);
    if (found < 0) {
        found = search(forward ? 0 : length, forward ? length : 0);
    }
    if (found < 0) {
        host_.alert("找不到“" + find_.find + "”。");
        return;
    }
    send(SCI_SETSEL, send(SCI_GETTARGETSTART), send(SCI_GETTARGETEND));
    send(SCI_SCROLLCARET);
    updateStatus();
}

void App::replaceAll() {
    if (find_.find.empty()) {
        return;
    }
    const int flags = (find_.matchCase ? SCFIND_MATCHCASE : 0) | (find_.wholeWord ? SCFIND_WHOLEWORD : 0);
    send(SCI_SETSEARCHFLAGS, flags);
    send(SCI_BEGINUNDOACTION);
    send(SCI_SETTARGETSTART, 0);
    send(SCI_SETTARGETEND, send(SCI_GETLENGTH));
    int count = 0;
    while (send(SCI_SEARCHINTARGET, find_.find.size(), reinterpret_cast<intptr_t>(find_.find.c_str())) >= 0) {
        const intptr_t start = send(SCI_GETTARGETSTART);
        send(SCI_REPLACETARGET, find_.replace.size(), reinterpret_cast<intptr_t>(find_.replace.c_str()));
        const intptr_t next = send(SCI_GETTARGETEND);
        if (next <= start) {
            break;
        }
        send(SCI_SETTARGETSTART, next);
        send(SCI_SETTARGETEND, send(SCI_GETLENGTH));
        ++count;
        if (count > 1000000) {
            break;
        }
    }
    send(SCI_ENDUNDOACTION);
    host_.alert("已替换 " + std::to_string(count) + " 处。");
}

void App::onCommand(Command command) {
    switch (command) {
    case CmdNew: {
        Doc doc;
        doc.id = host_.createEditor();
        docs_.push_back(doc);
        active_ = static_cast<int>(docs_.size()) - 1;
        configure(docs_.back());
        host_.showEditor(doc.id);
        touchTitles();
        break;
    }
    case CmdOpen: {
        const std::string path = host_.promptOpenPath();
        if (!path.empty()) {
            openPath(path);
        }
        break;
    }
    case CmdSave:
        if (Doc *doc = active()) {
            save(*doc, false);
        }
        break;
    case CmdSaveAs:
        if (Doc *doc = active()) {
            save(*doc, true);
        }
        break;
    case CmdClose:
        closeActive();
        break;
    case CmdExit:
        if (onCloseRequested()) {
            host_.quit();
        }
        break;
    case CmdUndo: send(SCI_UNDO); break;
    case CmdRedo: send(SCI_REDO); break;
    case CmdCut: send(SCI_CUT); break;
    case CmdCopy: send(SCI_COPY); break;
    case CmdPaste: send(SCI_PASTE); break;
    case CmdDelete: send(SCI_CLEAR); break;
    case CmdSelectAll: send(SCI_SELECTALL); break;
    case CmdFind:
    case CmdReplace: {
        FindRequest request = host_.promptFind(find_, command == CmdReplace);
        if (request.cancelled || request.find.empty()) {
            break;
        }
        find_ = request;
        if (request.action == FindAction::ReplaceAll) {
            replaceAll();
        } else if (request.action == FindAction::ReplaceOne) {
            const intptr_t selStart = send(SCI_GETSELECTIONSTART);
            send(SCI_SETTARGETSTART, selStart);
            send(SCI_SETTARGETEND, send(SCI_GETLENGTH));
            const int flags = (find_.matchCase ? SCFIND_MATCHCASE : 0) | (find_.wholeWord ? SCFIND_WHOLEWORD : 0);
            send(SCI_SETSEARCHFLAGS, flags);
            if (send(SCI_SEARCHINTARGET, find_.find.size(), reinterpret_cast<intptr_t>(find_.find.c_str())) == selStart) {
                send(SCI_REPLACETARGET, find_.replace.size(), reinterpret_cast<intptr_t>(find_.replace.c_str()));
            }
            findAgain(true);
        } else {
            findAgain(true);
        }
        break;
    }
    case CmdFindNext: findAgain(true); break;
    case CmdFindPrev: findAgain(false); break;
    case CmdGoto: {
        const int line = static_cast<int>(send(SCI_LINEFROMPOSITION, send(SCI_GETCURRENTPOS))) + 1;
        const int count = static_cast<int>(send(SCI_GETLINECOUNT));
        const int chosen = host_.promptGoto(line, count);
        if (chosen > 0) {
            send(SCI_GOTOLINE, chosen - 1);
            updateStatus();
        }
        break;
    }
    case CmdWrap:
        wrap_ = !wrap_;
        for (const Doc &doc : docs_) applyView(doc.id);
        updateStatus();
        break;
    case CmdLineNumbers:
        lineNumbers_ = !lineNumbers_;
        for (const Doc &doc : docs_) applyView(doc.id);
        break;
    case CmdWhitespace:
        whitespace_ = !whitespace_;
        for (const Doc &doc : docs_) applyView(doc.id);
        break;
    case CmdEol:
        eolVisible_ = !eolVisible_;
        for (const Doc &doc : docs_) applyView(doc.id);
        break;
    case CmdZoomIn:
        send(SCI_ZOOMIN);
        zoom_ = static_cast<int>(send(SCI_GETZOOM));
        for (const Doc &doc : docs_) applyView(doc.id);
        break;
    case CmdZoomOut:
        send(SCI_ZOOMOUT);
        zoom_ = static_cast<int>(send(SCI_GETZOOM));
        for (const Doc &doc : docs_) applyView(doc.id);
        break;
    case CmdZoomReset:
        zoom_ = 0;
        for (const Doc &doc : docs_) applyView(doc.id);
        break;
    }
}

void App::onNotify(int docId, const SCNotification *notification) {
    Doc *doc = findDoc(docId);
    if (!doc || !notification) {
        return;
    }
    const bool current = active() && active()->id == docId;
    switch (notification->nmhdr.code) {
    case SCN_SAVEPOINTLEFT:
        doc->modified = true;
        if (current) touchTitles();
        break;
    case SCN_SAVEPOINTREACHED:
        doc->modified = false;
        if (current) touchTitles();
        break;
    case SCN_UPDATEUI:
        if (current) {
            if (lineNumbers_) {
                const intptr_t lines = sendTo(docId, SCI_GETLINECOUNT);
                std::string sample(static_cast<size_t>(std::max<intptr_t>(2, std::to_string(lines).size())), '9');
                const intptr_t width = sendTo(docId, SCI_TEXTWIDTH, STYLE_LINENUMBER,
                                               reinterpret_cast<intptr_t>(sample.c_str()));
                sendTo(docId, SCI_SETMARGINWIDTHN, 0, width + 8);
            }
            const intptr_t pos = sendTo(docId, SCI_GETCURRENTPOS);
            intptr_t bracePos = pos;
            intptr_t match = pos > 0 ? sendTo(docId, SCI_BRACEMATCH, static_cast<uintptr_t>(pos - 1), 0) : -1;
            if (match >= 0) {
                bracePos = pos - 1;
            } else {
                match = sendTo(docId, SCI_BRACEMATCH, static_cast<uintptr_t>(pos), 0);
            }
            if (match >= 0) {
                sendTo(docId, SCI_BRACEHIGHLIGHT, static_cast<uintptr_t>(bracePos), match);
            } else {
                sendTo(docId, SCI_BRACEBADLIGHT, static_cast<uintptr_t>(-1));
            }
            updateStatus();
        }
        break;
    default:
        break;
    }
}

void App::onTabChanged(int doc) {
    for (int i = 0; i < static_cast<int>(docs_.size()); ++i) {
        if (docs_[static_cast<size_t>(i)].id == doc) {
            active_ = i;
            host_.showEditor(doc);
            touchTitles();
            return;
        }
    }
}

bool App::onCloseRequested() {
    for (Doc &doc : docs_) {
        active_ = static_cast<int>(&doc - docs_.data());
        host_.showEditor(doc.id);
        if (!confirmClose(doc)) {
            return false;
        }
    }
    return true;
}
