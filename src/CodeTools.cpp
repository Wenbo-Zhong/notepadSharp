#include "App.h"
#include "Scintilla.h"
#include "ScintillaMessages.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>

namespace {
constexpr size_t maxMacroBytes = 4 * 1024 * 1024;
constexpr size_t maxMacroSteps = 10000;
// Only the editing/movement messages emitted by Scintilla macro recording are replayable.
// Pointer-bearing messages below always use owned text, never serialized addresses.
bool allowedMacroMessage(unsigned message) {
    switch (message) {
    case static_cast<unsigned>(Scintilla::Message::Cut):
    case static_cast<unsigned>(Scintilla::Message::Copy):
    case static_cast<unsigned>(Scintilla::Message::Paste):
    case static_cast<unsigned>(Scintilla::Message::Clear):
    case static_cast<unsigned>(Scintilla::Message::ReplaceSel):
    case static_cast<unsigned>(Scintilla::Message::AddText):
    case static_cast<unsigned>(Scintilla::Message::InsertText):
    case static_cast<unsigned>(Scintilla::Message::AppendText):
    case static_cast<unsigned>(Scintilla::Message::ClearAll):
    case static_cast<unsigned>(Scintilla::Message::SelectAll):
    case static_cast<unsigned>(Scintilla::Message::GotoLine):
    case static_cast<unsigned>(Scintilla::Message::GotoPos):
    case static_cast<unsigned>(Scintilla::Message::SearchAnchor):
    case static_cast<unsigned>(Scintilla::Message::SearchNext):
    case static_cast<unsigned>(Scintilla::Message::SearchPrev):
    case static_cast<unsigned>(Scintilla::Message::LineDown):
    case static_cast<unsigned>(Scintilla::Message::LineDownExtend):
    case static_cast<unsigned>(Scintilla::Message::ParaDown):
    case static_cast<unsigned>(Scintilla::Message::ParaDownExtend):
    case static_cast<unsigned>(Scintilla::Message::LineUp):
    case static_cast<unsigned>(Scintilla::Message::LineUpExtend):
    case static_cast<unsigned>(Scintilla::Message::ParaUp):
    case static_cast<unsigned>(Scintilla::Message::ParaUpExtend):
    case static_cast<unsigned>(Scintilla::Message::CharLeft):
    case static_cast<unsigned>(Scintilla::Message::CharLeftExtend):
    case static_cast<unsigned>(Scintilla::Message::CharRight):
    case static_cast<unsigned>(Scintilla::Message::CharRightExtend):
    case static_cast<unsigned>(Scintilla::Message::WordLeft):
    case static_cast<unsigned>(Scintilla::Message::WordLeftExtend):
    case static_cast<unsigned>(Scintilla::Message::WordRight):
    case static_cast<unsigned>(Scintilla::Message::WordRightExtend):
    case static_cast<unsigned>(Scintilla::Message::WordPartLeft):
    case static_cast<unsigned>(Scintilla::Message::WordPartLeftExtend):
    case static_cast<unsigned>(Scintilla::Message::WordPartRight):
    case static_cast<unsigned>(Scintilla::Message::WordPartRightExtend):
    case static_cast<unsigned>(Scintilla::Message::WordLeftEnd):
    case static_cast<unsigned>(Scintilla::Message::WordLeftEndExtend):
    case static_cast<unsigned>(Scintilla::Message::WordRightEnd):
    case static_cast<unsigned>(Scintilla::Message::WordRightEndExtend):
    case static_cast<unsigned>(Scintilla::Message::Home):
    case static_cast<unsigned>(Scintilla::Message::HomeExtend):
    case static_cast<unsigned>(Scintilla::Message::LineEnd):
    case static_cast<unsigned>(Scintilla::Message::LineEndExtend):
    case static_cast<unsigned>(Scintilla::Message::HomeWrap):
    case static_cast<unsigned>(Scintilla::Message::HomeWrapExtend):
    case static_cast<unsigned>(Scintilla::Message::LineEndWrap):
    case static_cast<unsigned>(Scintilla::Message::LineEndWrapExtend):
    case static_cast<unsigned>(Scintilla::Message::DocumentStart):
    case static_cast<unsigned>(Scintilla::Message::DocumentStartExtend):
    case static_cast<unsigned>(Scintilla::Message::DocumentEnd):
    case static_cast<unsigned>(Scintilla::Message::DocumentEndExtend):
    case static_cast<unsigned>(Scintilla::Message::StutteredPageUp):
    case static_cast<unsigned>(Scintilla::Message::StutteredPageUpExtend):
    case static_cast<unsigned>(Scintilla::Message::StutteredPageDown):
    case static_cast<unsigned>(Scintilla::Message::StutteredPageDownExtend):
    case static_cast<unsigned>(Scintilla::Message::PageUp):
    case static_cast<unsigned>(Scintilla::Message::PageUpExtend):
    case static_cast<unsigned>(Scintilla::Message::PageDown):
    case static_cast<unsigned>(Scintilla::Message::PageDownExtend):
    case static_cast<unsigned>(Scintilla::Message::EditToggleOvertype):
    case static_cast<unsigned>(Scintilla::Message::Cancel):
    case static_cast<unsigned>(Scintilla::Message::DeleteBack):
    case static_cast<unsigned>(Scintilla::Message::Tab):
    case static_cast<unsigned>(Scintilla::Message::LineIndent):
    case static_cast<unsigned>(Scintilla::Message::BackTab):
    case static_cast<unsigned>(Scintilla::Message::LineDedent):
    case static_cast<unsigned>(Scintilla::Message::FormFeed):
    case static_cast<unsigned>(Scintilla::Message::VCHome):
    case static_cast<unsigned>(Scintilla::Message::VCHomeExtend):
    case static_cast<unsigned>(Scintilla::Message::VCHomeWrap):
    case static_cast<unsigned>(Scintilla::Message::VCHomeWrapExtend):
    case static_cast<unsigned>(Scintilla::Message::VCHomeDisplay):
    case static_cast<unsigned>(Scintilla::Message::VCHomeDisplayExtend):
    case static_cast<unsigned>(Scintilla::Message::DelWordLeft):
    case static_cast<unsigned>(Scintilla::Message::DelWordRight):
    case static_cast<unsigned>(Scintilla::Message::DelWordRightEnd):
    case static_cast<unsigned>(Scintilla::Message::DelLineLeft):
    case static_cast<unsigned>(Scintilla::Message::DelLineRight):
    case static_cast<unsigned>(Scintilla::Message::LineCopy):
    case static_cast<unsigned>(Scintilla::Message::LineCut):
    case static_cast<unsigned>(Scintilla::Message::LineDelete):
    case static_cast<unsigned>(Scintilla::Message::LineTranspose):
    case static_cast<unsigned>(Scintilla::Message::LineReverse):
    case static_cast<unsigned>(Scintilla::Message::LineDuplicate):
    case static_cast<unsigned>(Scintilla::Message::LowerCase):
    case static_cast<unsigned>(Scintilla::Message::UpperCase):
    case static_cast<unsigned>(Scintilla::Message::LineScrollDown):
    case static_cast<unsigned>(Scintilla::Message::LineScrollUp):
    case static_cast<unsigned>(Scintilla::Message::DeleteBackNotLine):
    case static_cast<unsigned>(Scintilla::Message::HomeDisplay):
    case static_cast<unsigned>(Scintilla::Message::HomeDisplayExtend):
    case static_cast<unsigned>(Scintilla::Message::LineEndDisplay):
    case static_cast<unsigned>(Scintilla::Message::LineEndDisplayExtend):
    case static_cast<unsigned>(Scintilla::Message::SetSelectionMode):
    case static_cast<unsigned>(Scintilla::Message::LineDownRectExtend):
    case static_cast<unsigned>(Scintilla::Message::LineUpRectExtend):
    case static_cast<unsigned>(Scintilla::Message::CharLeftRectExtend):
    case static_cast<unsigned>(Scintilla::Message::CharRightRectExtend):
    case static_cast<unsigned>(Scintilla::Message::HomeRectExtend):
    case static_cast<unsigned>(Scintilla::Message::VCHomeRectExtend):
    case static_cast<unsigned>(Scintilla::Message::LineEndRectExtend):
    case static_cast<unsigned>(Scintilla::Message::PageUpRectExtend):
    case static_cast<unsigned>(Scintilla::Message::PageDownRectExtend):
    case static_cast<unsigned>(Scintilla::Message::SelectionDuplicate):
    case static_cast<unsigned>(Scintilla::Message::CopyAllowLine):
    case static_cast<unsigned>(Scintilla::Message::CutAllowLine):
    case static_cast<unsigned>(Scintilla::Message::VerticalCentreCaret):
    case static_cast<unsigned>(Scintilla::Message::MoveSelectedLinesUp):
    case static_cast<unsigned>(Scintilla::Message::MoveSelectedLinesDown):
    case static_cast<unsigned>(Scintilla::Message::ScrollToStart):
    case static_cast<unsigned>(Scintilla::Message::ScrollToEnd):
        return true;
    default:
        return false;
    }
}
bool countedText(unsigned message) { return message == SCI_ADDTEXT || message == SCI_APPENDTEXT; }
bool pointerText(unsigned message) {
    return countedText(message) || message == SCI_INSERTTEXT || message == SCI_REPLACESEL ||
           message == SCI_SEARCHNEXT || message == SCI_SEARCHPREV;
}
bool wordStart(unsigned char ch) { return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_'; }
bool wordCharacter(unsigned char ch) { return wordStart(ch) || (ch >= '0' && ch <= '9'); }
} // namespace
void App::stopMacro() {
    if (!recordingDoc_)
        return;
    const int doc = recordingDoc_;
    recordingDoc_ = 0;
    sendTo(doc, SCI_STOPRECORD);
    updateStatus();
}
void App::recordMacro(int doc, const SCNotification &notification) {
    if (doc != recordingDoc_ || playingMacro_ || !allowedMacroMessage(notification.message))
        return;
    MacroStep step{static_cast<unsigned>(notification.message), notification.wParam, notification.lParam, {}};
    if (pointerText(step.message)) {
        const char *text = reinterpret_cast<const char *>(notification.lParam);
        if (!text)
            return;
        size_t length = 0;
        if (countedText(step.message))
            length = notification.wParam;
        else
            while (length <= maxMacroBytes && text[length])
                ++length;
        if (length > maxMacroBytes || macroBytes_ + length > maxMacroBytes) {
            stopMacro();
            host_.alert("宏已达到 4 MiB 上限，录制已停止。");
            return;
        }
        step.text.assign(text, length);
        step.l = 0;
        macroBytes_ += length;
    }
    if (macro_.size() >= maxMacroSteps) {
        stopMacro();
        host_.alert("宏已达到 10000 步上限，录制已停止。");
        return;
    }
    macro_.push_back(std::move(step));
}
void App::playMacro(int repetitions, bool untilEnd) {
    stopMacro();
    if (macro_.empty()) {
        host_.alert("没有可回放的宏。");
        return;
    }
    if (!active() || active()->transient || send(SCI_GETREADONLY) || playingMacro_)
        return;
    playingMacro_ = true;
    send(SCI_AUTOCCANCEL);
    send(SCI_BEGINUNDOACTION);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    const auto initialLength = send(SCI_GETLENGTH);
    size_t actions = 0;
    bool stopped = false;
    for (int iteration = 0; iteration < repetitions; ++iteration) {
        const auto before = send(SCI_GETCURRENTPOS), lengthBefore = send(SCI_GETLENGTH);
        if (untilEnd && before >= lengthBefore)
            break;
        for (const auto &step : macro_) {
            if (++actions > 100000 || std::chrono::steady_clock::now() > deadline ||
                send(SCI_GETLENGTH) - initialLength > 16 * 1024 * 1024) {
                stopped = true;
                break;
            }
            send(step.message, step.w,
                 pointerText(step.message) ? reinterpret_cast<intptr_t>(step.text.c_str()) : step.l);
        }
        if (stopped)
            break;
        if (untilEnd && send(SCI_GETCURRENTPOS) <= before && send(SCI_GETLENGTH) >= lengthBefore) {
            stopped = true;
            break;
        }
        if (untilEnd && iteration + 1 == repetitions && send(SCI_GETCURRENTPOS) < send(SCI_GETLENGTH))
            stopped = true;
    }
    send(SCI_ENDUNDOACTION);
    playingMacro_ = false;
    updateStatus();
    if (stopped)
        host_.alert("宏已停止：未向文件尾推进，或已达到运行次数、时间、操作数或文本增长上限。");
}
void App::saveMacro() {
    stopMacro();
    if (macro_.empty()) {
        host_.alert("没有可保存的宏。");
        return;
    }
    const auto path = host_.promptSavePath("macro.nsm");
    if (path.empty())
        return;
    std::ofstream out(std::filesystem::u8path(path), std::ios::binary | std::ios::trunc);
    out << "NotepadSharpMacro1\n" << macro_.size() << '\n';
    for (const auto &step : macro_)
        out << step.message << ' ' << step.w << ' ' << step.l << ' ' << std::quoted(step.text) << '\n';
    out.close();
    if (!out)
        host_.alert("无法保存宏。");
}
void App::loadMacro() {
    const auto path = host_.promptOpenPath();
    if (path.empty())
        return;
    std::error_code ec;
    const auto size = std::filesystem::file_size(std::filesystem::u8path(path), ec);
    if (ec || size > 2 * maxMacroBytes + 1024 * 1024) {
        host_.alert("宏文件无法读取或过大。");
        return;
    }
    std::ifstream in(std::filesystem::u8path(path), std::ios::binary);
    std::string magic;
    std::getline(in, magic);
    size_t count = 0;
    if (magic != "NotepadSharpMacro1" || !(in >> count) || count > maxMacroSteps) {
        host_.alert("无效的宏文件。");
        return;
    }
    std::vector<MacroStep> steps;
    size_t bytes = 0;
    for (size_t i = 0; i < count; ++i) {
        MacroStep step;
        if (!(in >> step.message >> step.w >> step.l >> std::quoted(step.text)) ||
            !allowedMacroMessage(step.message)) {
            host_.alert("宏包含无效或不支持的命令。");
            return;
        }
        bytes += step.text.size();
        if (bytes > maxMacroBytes || (countedText(step.message) && step.w != step.text.size()) ||
            (!pointerText(step.message) && !step.text.empty()) ||
            (pointerText(step.message) && step.l != 0)) {
            host_.alert("宏参数无效。");
            return;
        }
        steps.push_back(std::move(step));
    }
    stopMacro();
    macro_ = std::move(steps);
    macroBytes_ = bytes;
}
void App::completeWord(bool automatic) {
    if (!active() || active()->transient || send(SCI_GETREADONLY) || recordingDoc_ || playingMacro_ ||
        (automatic && !autoComplete_))
        return;
    const auto pos = send(SCI_GETCURRENTPOS), start = send(SCI_WORDSTARTPOSITION, pos, 1);
    const auto prefixLength = pos - start;
    if (prefixLength < (automatic ? 2 : 1) || prefixLength > 128 || !send(SCI_GETSELECTIONEMPTY)) {
        send(SCI_AUTOCCANCEL);
        return;
    }
    const auto length = send(SCI_GETLENGTH);
    const auto from = std::max<intptr_t>(0, pos - 512 * 1024),
               to = std::min<intptr_t>(length, pos + 512 * 1024);
    std::string text(static_cast<size_t>(to - from + 1), '\0');
    Sci_TextRangeFull range{{from, to}, text.data()};
    send(SCI_GETTEXTRANGEFULL, 0, reinterpret_cast<intptr_t>(&range));
    text.resize(text.size() - 1);
    const auto prefix = text.substr(static_cast<size_t>(start - from), static_cast<size_t>(prefixLength));
    if (!wordStart(static_cast<unsigned char>(prefix[0]))) {
        send(SCI_AUTOCCANCEL);
        return;
    }
    const auto currentEnd = send(SCI_WORDENDPOSITION, pos, 1);
    text += " " + active()->keywords;
    std::set<std::string> candidates;
    for (size_t i = 0; i < text.size() && candidates.size() < 200;) {
        if (!wordStart(static_cast<unsigned char>(text[i]))) {
            ++i;
            continue;
        }
        const size_t begin = i++;
        while (i < text.size() && wordCharacter(static_cast<unsigned char>(text[i])))
            ++i;
        const auto count = i - begin;
        if (count <= static_cast<size_t>(prefixLength) || count > 128 ||
            text.compare(begin, prefix.size(), prefix) != 0)
            continue;
        if (from + static_cast<intptr_t>(begin) == start && from + static_cast<intptr_t>(i) == currentEnd)
            continue;
        candidates.insert(text.substr(begin, count));
    }
    if (candidates.empty()) {
        send(SCI_AUTOCCANCEL);
        return;
    }
    std::string list;
    for (const auto &word : candidates) {
        if (!list.empty())
            list += '\n';
        list += word;
    }
    send(SCI_AUTOCSETSEPARATOR, '\n');
    send(SCI_AUTOCSETIGNORECASE, 0);
    send(SCI_AUTOCSETCHOOSESINGLE, 0);
    send(SCI_AUTOCSETDROPRESTOFWORD, 1);
    send(SCI_AUTOCSETMAXHEIGHT, 10);
    send(SCI_AUTOCSHOW, prefixLength, reinterpret_cast<intptr_t>(list.c_str()));
}
