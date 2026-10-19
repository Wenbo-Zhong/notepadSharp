#include "App.h"
#include "Scintilla.h"
#include "TextEncoding.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
namespace fs = std::filesystem;

bool App::referenceCommand(Command command) {
    switch (command) {
    case CmdMonitorFile:
        toggleMonitoring();
        return true;
    case CmdReopenClosed:
        if (!closedFiles_.empty()) {
            const auto path = closedFiles_.back();
            closedFiles_.pop_back();
            openPath(path);
        }
        return true;
    case CmdFullScreen:
        fullScreen_ = !fullScreen_;
        host_.setFullScreen(fullScreen_);
        return true;
    case CmdAlwaysOnTop:
        alwaysOnTop_ = !alwaysOnTop_;
        host_.setAlwaysOnTop(alwaysOnTop_);
        return true;
    case CmdFoldCurrent:
    case CmdUnfoldCurrent: {
        auto line = send(SCI_LINEFROMPOSITION, send(SCI_GETCURRENTPOS));
        if (!(send(SCI_GETFOLDLEVEL, line) & SC_FOLDLEVELHEADERFLAG))
            line = send(SCI_GETFOLDPARENT, line);
        if (line >= 0)
            send(SCI_FOLDLINE, line,
                 command == CmdFoldCurrent ? SC_FOLDACTION_CONTRACT : SC_FOLDACTION_EXPAND);
        return true;
    }
    case CmdHideLines: {
        auto start = send(SCI_LINEFROMPOSITION, send(SCI_GETSELECTIONSTART));
        auto last = send(SCI_LINEFROMPOSITION, send(SCI_GETSELECTIONEND));
        if (last > start && send(SCI_POSITIONFROMLINE, last) == send(SCI_GETSELECTIONEND))
            --last;
        start = std::max<intptr_t>(1, start);
        if (last >= start) {
            send(SCI_HIDELINES, start, last);
            send(SCI_GOTOLINE, start - 1);
        }
        return true;
    }
    case CmdShowAllLines:
        send(SCI_SHOWLINES, 0, send(SCI_GETLINECOUNT) - 1);
        return true;
    case CmdNextTab:
    case CmdPrevTab:
        if (!docs_.empty()) {
            const int index = (active_ + (command == CmdNextTab ? 1 : -1) + static_cast<int>(docs_.size())) %
                              static_cast<int>(docs_.size());
            onTabChanged(docs_[index].id);
        }
        return true;
    case CmdCopyFullPath:
    case CmdCopyFileName:
    case CmdCopyDirectory:
        if (active() && !active()->path.empty()) {
            const auto path = fs::absolute(fs::u8path(active()->path));
            const auto text = command == CmdCopyFileName    ? path.filename().u8string()
                              : command == CmdCopyDirectory ? path.parent_path().u8string()
                                                            : path.u8string();
            send(SCI_COPYTEXT, text.size(), reinterpret_cast<intptr_t>(text.data()));
        }
        return true;
    case CmdReload:
        stopMacro();
        reloadActive();
        return true;
    case CmdSaveCopy:
        saveCopy();
        return true;
    case CmdCloseOthers:
    case CmdCloseLeft:
    case CmdCloseRight:
        stopMacro();
        closeGroup(command);
        return true;
    case CmdReadOnly:
        if (monitoring()) {
            host_.alert("请先停止文件监视，再调整只读状态。");
            return true;
        }
        if (active() && !active()->transient)
            send(SCI_SETREADONLY, !send(SCI_GETREADONLY));
        updateStatus();
        return true;
    case CmdStatistics:
        documentStatistics();
        return true;
    case CmdSelectNextOccurrence:
    case CmdSelectAllOccurrences:
        send(SCI_SETSEARCHFLAGS, SCFIND_MATCHCASE);
        send(SCI_SETTARGETRANGE, 0, send(SCI_GETLENGTH));
        send(command == CmdSelectNextOccurrence ? SCI_MULTIPLESELECTADDNEXT : SCI_MULTIPLESELECTADDEACH);
        return true;
    case CmdGotoBrace:
    case CmdSelectBrace: {
        const auto pos = send(SCI_GETCURRENTPOS);
        auto brace = pos;
        auto match = send(SCI_BRACEMATCH, pos, 0);
        if (match < 0 && pos > 0) {
            brace = pos - 1;
            match = send(SCI_BRACEMATCH, brace, 0);
        }
        if (match >= 0) {
            if (command == CmdGotoBrace)
                send(SCI_GOTOPOS, match);
            else
                send(SCI_SETSEL, std::min(brace, match) + 1, std::max(brace, match));
            send(SCI_SCROLLCARET);
        }
        return true;
    }
    default:
        return false;
    }
}
void App::saveCopy() {
    auto *doc = active();
    if (!doc)
        return;
    auto path = host_.promptSavePath("copy.txt");
    if (path.empty())
        return;
    std::error_code ec;
    if (!doc->path.empty() && fs::equivalent(fs::u8path(path), fs::u8path(doc->path), ec) && !ec) {
        host_.alert("拷贝另存为请选择不同的文件路径。");
        return;
    }
    std::string bytes, error;
    if (!TextEncoding::encode(documentText(doc->id), doc->encoding, bytes, error)) {
        host_.alert(error);
        return;
    }
    std::ofstream out(fs::u8path(path), std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.close();
    if (!out)
        host_.alert("无法写入副本文件。");
}
void App::reloadActive(bool discardConfirmed) {
    auto *doc = active();
    if (!doc || doc->path.empty() || doc->transient)
        return;
    // Save/Discard/Cancel uses the same unsaved-work policy as closing a document.
    if (!discardConfirmed && !confirmClose(*doc))
        return;
    std::ifstream in(fs::u8path(doc->path), std::ios::binary);
    if (!in) {
        host_.alert("无法重新读取文件，当前内容保留。");
        return;
    }
    std::string bytes((std::istreambuf_iterator<char>(in)), {}), text, encoding, error;
    if (in.bad() || !TextEncoding::decode(bytes, text, encoding, error)) {
        host_.alert(error.empty() ? "文件读取失败。" : error);
        return;
    }
    const auto pos = send(SCI_GETCURRENTPOS), readOnly = send(SCI_GETREADONLY);
    send(SCI_SETREADONLY, 0);
    doc->encoding = encoding;
    doc->savedEncoding = encoding;
    send(SCI_CLEARALL);
    send(SCI_ADDTEXT, text.size(), reinterpret_cast<intptr_t>(text.data()));
    send(SCI_EMPTYUNDOBUFFER);
    send(SCI_SETSAVEPOINT);
    doc->modified = false;
    observeDisk(*doc);
    send(SCI_SETEOLMODE, text.find("\r\n") != std::string::npos ? SC_EOL_CRLF
                         : text.find('\n') != std::string::npos ? SC_EOL_LF
                         : text.find('\r') != std::string::npos ? SC_EOL_CR
                                                                : host_.defaultEol());
    applyLexer(*doc);
    applyView(doc->id);
    send(SCI_GOTOPOS, std::min<intptr_t>(pos, text.size()));
    send(SCI_SETREADONLY, readOnly);
    touchTitles();
    checkpoint();
}
void App::closeGroup(Command command) {
    if (!active())
        return;
    const int keep = active()->id, index = active_;
    std::vector<int> remove;
    for (int i = 0; i < static_cast<int>(docs_.size()); ++i)
        if (i != index && (command == CmdCloseOthers || (command == CmdCloseLeft && i < index) ||
                           (command == CmdCloseRight && i > index)))
            remove.push_back(docs_[i].id);
    // Complete confirmations first: cancellation leaves the tab set intact.
    for (int id : remove) {
        onTabChanged(id);
        if (auto *doc = findDoc(id); doc && !confirmClose(*doc)) {
            touchTitles();
            return;
        }
    }
    onTabChanged(keep);
    for (int id : remove) {
        if (auto *doc = findDoc(id); doc && !doc->path.empty() && !doc->transient) {
            closedFiles_.push_back(doc->path);
            if (closedFiles_.size() > 20)
                closedFiles_.erase(closedFiles_.begin());
        }
        docs_.erase(std::remove_if(docs_.begin(), docs_.end(), [&](const Doc &doc) { return doc.id == id; }),
                    docs_.end());
        // Keep active_ valid during backend tab-change callbacks.
        for (size_t i = 0; i < docs_.size(); ++i)
            if (docs_[i].id == keep)
                active_ = static_cast<int>(i);
        host_.destroyEditor(id);
    }
    searchHits_.clear();
    searchHitIndex_ = -1;
    onTabChanged(keep);
    checkpoint();
}
void App::documentStatistics() {
    if (!active())
        return;
    const auto text = documentText(active()->id);
    size_t characters = 0, words = 0;
    bool inWord = false;
    for (unsigned char c : text) {
        if ((c & 0xc0) != 0x80)
            ++characters;
        const bool space = c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
        if (!space && !inWord)
            ++words;
        inWord = !space;
    }
    std::ostringstream message;
    message << "文档：" << displayName(*active()) << "\nUTF-8 字节数：" << text.size() << "\nUnicode 码点数："
            << characters << "\n行数：" << send(SCI_GETLINECOUNT) << "\n单词数（空白分隔）：" << words
            << "\n选中字节数：" << send(SCI_GETSELECTIONEND) - send(SCI_GETSELECTIONSTART);
    host_.alert(message.str());
}
