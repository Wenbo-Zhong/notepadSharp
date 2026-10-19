#include "App.h"
#include "TextEncoding.h"

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
    std::string name = doc.transient ? "搜索结果" : fileNameOf(doc.path);
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
    s(SCI_STYLESETSIZE, STYLE_DEFAULT, fontSize_);
    s(SCI_STYLESETWEIGHT, STYLE_DEFAULT, fontWeight_);
    s(SCI_STYLESETITALIC, STYLE_DEFAULT, fontItalic_);
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
    s(SCI_STYLESETWEIGHT, 5, std::max(fontWeight_, 700));
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
    const intptr_t unit = s(SCI_TEXTWIDTH, STYLE_LINENUMBER, reinterpret_cast<intptr_t>("9"));
    s(SCI_SETMARGINWIDTHN, 1, unit + 8);
    s(SCI_SETMARGINWIDTHN, 2, unit + 8);
    if (lineNumbers_) {
        const intptr_t width = s(SCI_TEXTWIDTH, STYLE_LINENUMBER, reinterpret_cast<intptr_t>("99999"));
        s(SCI_SETMARGINWIDTHN, 0, width + 8);
    } else {
        s(SCI_SETMARGINWIDTHN, 0, 0);
    }
}

void App::applyLexer(Doc &doc) {
    const Lang lang = languageFor(doc.language >= 0 ? languageChoices[doc.language].example : doc.path);
    doc.lexer = lang.lexer;
    doc.keywords = lang.keywords ? lang.keywords : "";
    Scintilla::ILexer5 *lexer = CreateLexer(lang.lexer);
    if (!lexer) {
        lexer = CreateLexer("null");
        doc.lexer = "text";
    }
    sendTo(doc.id, SCI_CLEARDOCUMENTSTYLE);
    sendTo(doc.id, SCI_SETILEXER, 0, reinterpret_cast<intptr_t>(lexer));
    if (lang.keywords && lang.keywords[0]) {
        sendTo(doc.id, SCI_SETKEYWORDS, 0, reinterpret_cast<intptr_t>(lang.keywords));
    }
    sendTo(doc.id, SCI_SETPROPERTY, reinterpret_cast<uintptr_t>("fold"), reinterpret_cast<intptr_t>("1"));
    applyTheme(doc.id);
    sendTo(doc.id, SCI_COLOURISE, 0, -1);
}

void App::configure(Doc &doc) {
    sendTo(doc.id, SCI_INDICSETSTYLE, 8, INDIC_ROUNDBOX);
    sendTo(doc.id, SCI_INDICSETFORE, 8, 0x00BFFF);
    sendTo(doc.id, SCI_SETCODEPAGE, SC_CP_UTF8);
    sendTo(doc.id, SCI_SETEOLMODE, host_.defaultEol());
    sendTo(doc.id, SCI_SETMARGINTYPEN, 0, SC_MARGIN_NUMBER);
    sendTo(doc.id, SCI_SETMARGINTYPEN, 1, SC_MARGIN_SYMBOL);
    sendTo(doc.id, SCI_SETMARGINMASKN, 1, 1);
    sendTo(doc.id, SCI_SETMARGINSENSITIVEN, 1, 1);
    sendTo(doc.id, SCI_MARKERDEFINE, 0, SC_MARK_BOOKMARK);
    sendTo(doc.id, SCI_MARKERSETBACK, 0, 0xD77800);
    sendTo(doc.id, SCI_SETMULTIPLESELECTION, 1);
    sendTo(doc.id, SCI_SETADDITIONALSELECTIONTYPING, 1);
    sendTo(doc.id, SCI_SETMULTIPASTE, SC_MULTIPASTE_EACH);
    sendTo(doc.id, SCI_SETMARGINTYPEN, 2, SC_MARGIN_SYMBOL);
    sendTo(doc.id, SCI_SETMARGINMASKN, 2, SC_MASK_FOLDERS);
    sendTo(doc.id, SCI_SETMARGINSENSITIVEN, 2, 1);
    for (int marker = SC_MARKNUM_FOLDEREND; marker <= SC_MARKNUM_FOLDEROPEN; ++marker)
        sendTo(doc.id, SCI_MARKERDEFINE, marker, marker == SC_MARKNUM_FOLDER || marker == SC_MARKNUM_FOLDEREND ? SC_MARK_PLUS : SC_MARK_MINUS);
    sendTo(doc.id, SCI_SETAUTOMATICFOLD, SC_AUTOMATICFOLD_SHOW | SC_AUTOMATICFOLD_CHANGE);
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
    if (doc->monitoring) status << "    监视中";
    if (doc->diskChanged) status << "    磁盘已变更";
    if (send(SCI_GETREADONLY)) status << "    只读";
    if (macroRecording()) status << "    宏录制中";
    if (wrap_) status << "    换行";
    host_.setStatus(status.str());
}

void App::start() {
    const auto directory = host_.stateDirectory();
    if (!directory.empty() && acquireStateLock()) {
        if (readState((std::filesystem::u8path(directory) / "recovery.nss").u8string(), false)) return;
        if (readState((std::filesystem::u8path(directory) / "session.nss").u8string(), false)) return;
    }
    onCommand(CmdNew);
}

void App::openPath(const std::string &path) {
    for (const Doc &doc : docs_) {
        std::error_code ec;
        if (!doc.path.empty() && std::filesystem::equivalent(std::filesystem::u8path(path), std::filesystem::u8path(doc.path), ec) && !ec) { rememberFile(path); onTabChanged(doc.id); return; }
    }
    std::string error;
    std::string data = readFile(path, error);
    if (!error.empty()) {
        host_.alert(error);
        if (docs_.empty()) {
            onCommand(CmdNew);
        }
        return;
    }
    std::string decoded, encoding;
    if (!TextEncoding::decode(data, decoded, encoding, error)) { host_.alert(error); return; }
    data = std::move(decoded);
    if (docs_.size() == 1 && docs_[0].path.empty() && !docs_[0].modified && !docs_[0].transient && sendTo(docs_[0].id, SCI_GETLENGTH) == 0) {
        // Reuse the initial empty document.
    } else {
        onCommand(CmdNew);
    }
    Doc *doc = active();
    if (!doc) {
        return;
    }
    doc->encoding = encoding;
    doc->savedEncoding = encoding;
    send(SCI_SETCODEPAGE, SC_CP_UTF8);
    send(SCI_CLEARALL);
    send(SCI_ADDTEXT, data.size(), reinterpret_cast<intptr_t>(data.data()));
    send(SCI_EMPTYUNDOBUFFER);
    send(SCI_SETSAVEPOINT);
    doc->path = path;
    observeDisk(*doc); rememberFile(path);
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
    std::error_code compareError;
    const bool samePath = !doc.path.empty() && std::filesystem::equivalent(std::filesystem::u8path(path), std::filesystem::u8path(doc.path), compareError);
    if ((path == doc.path || (samePath && !compareError)) && (doc.diskChanged || diskChanged(doc))) {
        if (!host_.askOverwriteExternal("磁盘上的文件已被修改或移除。是否仍用编辑器内容覆盖？\n" + path)) return false;
    }
    std::string encoded;
    if (!TextEncoding::encode(data, doc.encoding, encoded, error)) { host_.alert(error); return false; }
    if (!writeFile(path, encoded, error)) {
        host_.alert(error);
        return false;
    }
    doc.path = path;
    observeDisk(doc); rememberFile(path);
    doc.savedEncoding = doc.encoding;
    sendTo(doc.id, SCI_SETSAVEPOINT);
    doc.modified = false;
    applyLexer(doc);
    applyView(doc.id);
    touchTitles();
    checkpoint();
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
    if (!doc->path.empty() && !doc->transient) { closedFiles_.push_back(doc->path); if (closedFiles_.size() > 20) closedFiles_.erase(closedFiles_.begin()); }
    searchHits_.clear(); searchHitIndex_ = -1;
    docs_.erase(docs_.begin() + active_);
    host_.destroyEditor(id);
    if (docs_.empty()) {
        onCommand(CmdNew);
        checkpoint();
        return;
    }
    if (active_ >= static_cast<int>(docs_.size())) {
        active_ = static_cast<int>(docs_.size()) - 1;
    }
    host_.showEditor(docs_[static_cast<size_t>(active_)].id);
    touchTitles();
    checkpoint();
}

void App::refreshView() {
    for (const Doc &doc : docs_) { applyTheme(doc.id); applyView(doc.id); }
}

int App::languageSelection() const {
    return active_ >= 0 && active_ < static_cast<int>(docs_.size()) ? std::max(0, docs_[active_].language) : 0;
}

void App::onCommand(Command command) {
    if (command >= CmdRecentBase && command < CmdRecentBase + 20) {
        const size_t i = command - CmdRecentBase;
        if (i < recentFiles_.size()) { const auto path = recentFiles_[i]; openPath(path); }
        return;
    }
    if (command >= CmdDocumentBase && command < CmdDocumentBase + 1000) {
        const size_t i = command - CmdDocumentBase;
        if (i < docs_.size()) onTabChanged(docs_[i].id);
        return;
    }
    if (command >= CmdLanguageBase && command < CmdLanguageBase + languageChoiceCount) {
        if (auto *doc = active(); doc && !doc->transient) {
            stopMacro();
            doc->language = command == CmdLanguageBase ? -1 : command - CmdLanguageBase;
            applyLexer(*doc); applyView(doc->id); updateStatus();
        }
        return;
    }
    if (recordingDoc_ && (command == CmdNew || command == CmdOpen || command == CmdClose || command == CmdCloseAll || command == CmdLoadSession || command == CmdFindAllOpen || command == CmdFindInFolder)) stopMacro();
    if (extraCommand(command)) return;
    if (command >= CmdFontSizeBase + 6 && command <= CmdFontSizeBase + 72) {
        fontSize_ = command - CmdFontSizeBase; refreshView(); return;
    }
    if (command >= CmdFontWeightBase && command < CmdFontWeightBase + 8) {
        fontWeight_ = 100 * (command - CmdFontWeightBase + 1); refreshView(); return;
    }
    switch (command) {
    default: break; // Additional commands are handled by extraCommand().
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
    case CmdExit: host_.quit(); break;
    case CmdSaveAll:
        for (Doc &doc : docs_) if (!doc.transient && (doc.modified || doc.path.empty()) && !save(doc, false)) break;
        break;
    case CmdCloseAll:
        if (onCloseRequested()) {
            for (const Doc &doc : docs_) if (!doc.path.empty() && !doc.transient) { closedFiles_.push_back(doc.path); if (closedFiles_.size() > 20) closedFiles_.erase(closedFiles_.begin()); }
            for (const Doc &doc : docs_) host_.destroyEditor(doc.id);
            docs_.clear(); active_ = -1; searchHits_.clear(); searchHitIndex_ = -1; onCommand(CmdNew); checkpoint();
        }
        break;
    case CmdFontItalic: fontItalic_ = !fontItalic_; refreshView(); break;
    case CmdDuplicateLine: send(SCI_SELECTIONDUPLICATE); break;
    case CmdDeleteLine: send(SCI_LINEDELETE); break;
    case CmdMoveLineUp: send(SCI_MOVESELECTEDLINESUP); break;
    case CmdMoveLineDown: send(SCI_MOVESELECTEDLINESDOWN); break;
    case CmdUpperCase: send(SCI_UPPERCASE); break;
    case CmdLowerCase: send(SCI_LOWERCASE); break;
    case CmdTrimTrailing: {
        send(SCI_BEGINUNDOACTION);
        for (intptr_t line = send(SCI_GETLINECOUNT); line-- > 0;) {
            const intptr_t start = send(SCI_POSITIONFROMLINE, line);
            const intptr_t end = send(SCI_GETLINEENDPOSITION, line);
            intptr_t trim = end;
            while (trim > start) {
                const auto ch = send(SCI_GETCHARAT, trim - 1);
                if (ch != ' ' && ch != '\t') break;
                --trim;
            }
            if (trim != end) send(SCI_DELETERANGE, trim, end - trim);
        }
        send(SCI_ENDUNDOACTION); break;
    }
    case CmdEolCRLF: case CmdEolLF: case CmdEolCR: {
        const int mode = command == CmdEolCRLF ? SC_EOL_CRLF : command == CmdEolLF ? SC_EOL_LF : SC_EOL_CR;
        send(SCI_BEGINUNDOACTION); send(SCI_CONVERTEOLS, mode); send(SCI_ENDUNDOACTION);
        send(SCI_SETEOLMODE, mode); updateStatus(); break;
    }
    case CmdToggleBookmark: {
        const auto line = send(SCI_LINEFROMPOSITION, send(SCI_GETCURRENTPOS));
        send((send(SCI_MARKERGET, line) & 1) ? SCI_MARKERDELETE : SCI_MARKERADD, line, 0); break;
    }
    case CmdNextBookmark: case CmdPrevBookmark: {
        const auto line = send(SCI_LINEFROMPOSITION, send(SCI_GETCURRENTPOS));
        const bool next = command == CmdNextBookmark;
        intptr_t found = !next && line == 0 ? -1 : send(next ? SCI_MARKERNEXT : SCI_MARKERPREVIOUS, next ? line + 1 : line - 1, 1);
        if (found < 0) found = send(next ? SCI_MARKERNEXT : SCI_MARKERPREVIOUS, next ? 0 : send(SCI_GETLINECOUNT) - 1, 1);
        if (found >= 0) send(SCI_GOTOLINE, found);
        break;
    }
    case CmdClearBookmarks: send(SCI_MARKERDELETEALL, 0); break;
    case CmdFoldAll: send(SCI_FOLDALL, SC_FOLDACTION_CONTRACT); break;
    case CmdUnfoldAll: send(SCI_FOLDALL, SC_FOLDACTION_EXPAND); break;
    case CmdFontSizeBase: case CmdFontWeightBase: break;
    case CmdUndo: send(SCI_UNDO); break;
    case CmdRedo: send(SCI_REDO); break;
    case CmdCut: send(SCI_CUT); break;
    case CmdCopy: send(SCI_COPY); break;
    case CmdPaste: send(SCI_PASTE); break;
    case CmdDelete: send(SCI_CLEAR); break;
    case CmdSelectAll: send(SCI_SELECTALL); break;
    case CmdFind:
    case CmdReplace: {
        FindRequest seed = find_;
        const auto selectionStart = send(SCI_GETSELECTIONSTART), selectionEnd = send(SCI_GETSELECTIONEND);
        if (!seed.inSelection && selectionEnd > selectionStart && selectionEnd-selectionStart <= 1024) {
            const auto selected = documentText(active()->id).substr(static_cast<size_t>(selectionStart), static_cast<size_t>(selectionEnd-selectionStart));
            if (selected.find_first_of("\r\n") == std::string::npos && selected.find('\0') == std::string::npos) seed.find = selected;
        }
        if (!host_.showFindDialog(seed, command == CmdReplace, [this](const FindRequest &request) { performFind(request); })) {
            const auto request = host_.promptFind(seed, command == CmdReplace);
            if (!request.cancelled) performFind(request);
        }
        break;
    }
    case CmdFindNext: findAgain(!find_.backwards); break;
    case CmdFindPrev: findAgain(find_.backwards); break;
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
    case SCN_MACRORECORD: recordMacro(docId, *notification); break;
    case SCN_CHARADDED:
        if (current && !playingMacro_ && !restoring_ && notification->ch < 128) completeWord(true);
        break;
    case SCN_MODIFIED:
        if (!doc->transient && (notification->modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT))) {
            if (!editingSearch_ && scopeDoc_ == docId) scopeDoc_ = 0;
            searchHits_.clear(); searchHitIndex_ = -1; lastZeroMatch_ = -1;
            sendTo(docId, SCI_SETINDICATORCURRENT, 8);
            sendTo(docId, SCI_INDICATORCLEARRANGE, 0, sendTo(docId, SCI_GETLENGTH));
        }
        break;
    case SCN_SAVEPOINTLEFT:
        doc->modified = true;
        if (current) touchTitles();
        break;
    case SCN_SAVEPOINTREACHED:
        doc->modified = doc->encoding != doc->savedEncoding;
        if (current) touchTitles();
        break;
    case SCN_MARGINCLICK:
        if (current) {
            const auto line = send(SCI_LINEFROMPOSITION, notification->position);
            if (notification->margin == 1) send((send(SCI_MARKERGET, line) & 1) ? SCI_MARKERDELETE : SCI_MARKERADD, line, 0);
            else if (notification->margin == 2) send(SCI_TOGGLEFOLD, line);
        }
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
    if (recordingDoc_ && recordingDoc_ != doc) stopMacro();
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
    const int previous = active_ ;
    for (Doc &doc : docs_) {
        active_ = static_cast<int>(&doc - docs_.data());
        host_.showEditor(doc.id);
        if (!confirmClose(doc)) {
            touchTitles(); return false;
        }
    }
    active_ = previous;
    if (active()) host_.showEditor(active()->id);
    persistCleanState();
    return true;
}
