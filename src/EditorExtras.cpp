#include "App.h"
#include "Scintilla.h"
#include "SearchOptions.h"
#include <algorithm>
#include <filesystem>
#include <sstream>
#include <unordered_set>

bool App::extraCommand(Command command) {
    if (referenceCommand(command))
        return true;
    const char *encoding = nullptr;
    switch (command) {
    case CmdEncodingUtf8:
        encoding = "UTF-8";
        break;
    case CmdEncodingUtf8Bom:
        encoding = "UTF-8 BOM";
        break;
    case CmdEncodingUtf16LE:
        encoding = "UTF-16 LE";
        break;
    case CmdEncodingUtf16BE:
        encoding = "UTF-16 BE";
        break;
    case CmdEncodingLatin1:
        encoding = "Latin-1";
        break;
    case CmdSaveSession: {
        auto path = host_.promptSavePath("session.nss");
        if (!path.empty() && !writeState(path, true))
            host_.alert("无法保存会话（最多 64 MiB）。");
        return true;
    }
    case CmdLoadSession: {
        auto path = host_.promptOpenPath();
        if (!path.empty() && !readState(path, true))
            host_.alert("无法读取会话文件，当前文档保持不变。");
        return true;
    }
    case CmdFindInFolder:
        findInFolder();
        return true;
    case CmdMacroStart:
        if (recordingDoc_) {
            stopMacro();
            return true;
        }
        if (active() && !active()->transient && !send(SCI_GETREADONLY)) {
            macro_.clear();
            macroBytes_ = 0;
            recordingDoc_ = active()->id;
            send(SCI_STARTRECORD);
            updateStatus();
        }
        return true;
    case CmdMacroStop:
        stopMacro();
        return true;
    case CmdMacroRepeat: {
        const int count = host_.promptNumber("重复运行宏（次数）", 1, 10000);
        if (count > 0 && count <= 10000)
            playMacro(count);
        return true;
    }
    case CmdMacroToEnd:
        playMacro(10000, true);
        return true;
    case CmdMacroPlay:
        playMacro();
        return true;
    case CmdMacroSave:
        saveMacro();
        return true;
    case CmdMacroLoad:
        loadMacro();
        return true;
    case CmdCompleteWord:
        completeWord(false);
        return true;
    case CmdAutoComplete:
        autoComplete_ = !autoComplete_;
        if (!autoComplete_)
            send(SCI_AUTOCCANCEL);
        return true;
    case CmdFindAllOpen:
        findAllOpen(false);
        return true;
    case CmdMarkAll:
        findAllOpen(true);
        return true;
    case CmdClearMarks:
        send(SCI_SETINDICATORCURRENT, 8);
        send(SCI_INDICATORCLEARRANGE, 0, send(SCI_GETLENGTH));
        return true;
    case CmdNextResult:
    case CmdPrevResult:
        navigateResult(command == CmdNextResult);
        return true;
    case CmdEolsToSpaces:
    case CmdTrimAndJoin:
    case CmdSpacesToTabs:
    case CmdIndentToTabs:
    case CmdRemoveEmptyLines:
    case CmdRemoveBlankLines:
    case CmdUniqueAdjacent:
    case CmdReverseLines:
    case CmdSortLengthAsc:
    case CmdSortLengthDesc:
    case CmdSortIgnoreCaseAsc:
    case CmdSortIgnoreCaseDesc:
    case CmdTrimLeading:
    case CmdTrimBoth:
    case CmdSortAsc:
    case CmdSortDesc:
    case CmdUniqueLines:
    case CmdToggleComment:
    case CmdTabsToSpaces:
        transformLines(command);
        return true;
    default:
        return false;
    }
    if (auto *doc = active()) {
        if (doc->transient)
            return true;
        if (doc->encoding != encoding) {
            doc->encoding = encoding;
            doc->modified = send(SCI_GETMODIFY) || doc->encoding != doc->savedEncoding;
            touchTitles();
        }
    }
    return true;
}

void App::findAllOpen(bool markOnly, bool useRequest, bool currentOnly) {
    auto request = useRequest ? find_ : host_.promptFind(find_, false);
    if (request.cancelled || request.find.empty())
        return;
    find_ = request;
    PreparedSearch query;
    std::string error;
    if (!prepareSearch(find_, query, error)) {
        host_.findFeedback(error);
        return;
    }
    query.allowEmptyAfterMatch = false;
    const int flags = query.flags;
    std::vector<SearchHit> hits;
    std::ostringstream report;
    report << "搜索：" << find_.find << "\nF4 / Shift+F4 跳转结果；编辑源文档后请重新搜索。\n\n";
    bool truncated = false;
    for (const auto &doc : docs_) {
        if (doc.transient ||
            ((markOnly || currentOnly || find_.inSelection) && active() && doc.id != active()->id))
            continue;
        auto s = [&](unsigned msg, uintptr_t w = 0, intptr_t l = 0) { return sendTo(doc.id, msg, w, l); };
        auto length = s(SCI_GETLENGTH);
        intptr_t first = 0;
        if (find_.inSelection) {
            if (!findBounds(first, length))
                return;
        }
        if (markOnly) {
            s(SCI_SETINDICATORCURRENT, 8);
            s(SCI_INDICATORCLEARRANGE, 0, length);
        }
        s(SCI_SETSEARCHFLAGS, flags);
        s(SCI_SETSTATUS, SC_STATUS_OK);
        intptr_t from = first;
        while (from <= length) {
            s(SCI_SETTARGETSTART, from);
            s(SCI_SETTARGETEND, length);
            const auto found = searchInTarget(host_, doc.id, query);
            if (s(SCI_GETSTATUS) == SC_STATUS_WARN_REGEX) {
                host_.findFeedback("正则表达式无效。");
                return;
            }
            if (found < 0)
                break;
            const auto end = s(SCI_GETTARGETEND);
            if (markOnly) {
                s(SCI_SETINDICATORCURRENT, 8);
                s(SCI_INDICATORFILLRANGE, found, end - found);
            } else {
                hits.push_back({doc.id, found, end, {}, 0});
                const auto line = s(SCI_LINEFROMPOSITION, found);
                const auto start = s(SCI_POSITIONFROMLINE, line), stop = s(SCI_GETLINEENDPOSITION, line);
                auto snippetEnd = std::min<intptr_t>(stop, start + 300);
                while (snippetEnd < stop && snippetEnd > start &&
                       (s(SCI_GETCHARAT, snippetEnd) & 0xc0) == 0x80)
                    --snippetEnd;
                Sci_TextRangeFull range{{start, snippetEnd}, nullptr};
                std::string text(static_cast<size_t>(range.chrg.cpMax - start + 1), '\0');
                range.lpstrText = text.data();
                s(SCI_GETTEXTRANGEFULL, 0, reinterpret_cast<intptr_t>(&range));
                text.resize(text.size() - 1);
                report << (doc.path.empty() ? displayName(doc) : doc.path) << ":" << (line + 1) << ":"
                       << (s(SCI_GETCOLUMN, found) + 1) << "  " << text << "\n";
            }
            if (!markOnly && hits.size() >= 10000) {
                truncated = true;
                break;
            }
            if (end > found)
                from = end;
            else {
                if (end == length)
                    break;
                from = advanceSearchPosition(host_, doc.id, end);
            }
        }
        if (truncated)
            break;
    }
    if (markOnly) {
        host_.findFeedback("已标记所有非空匹配。");
        return;
    }
    showSearchResults(report.str() + (truncated ? "\n结果已达 10000 条上限。\n" : ""), std::move(hits));
}

void App::showSearchResults(const std::string &report, std::vector<SearchHit> hits) {
    stopMacro();
    onCommand(CmdNew);
    active()->transient = true;
    const auto text = report + "\n结果数：" + std::to_string(hits.size()) + "\n";
    send(SCI_ADDTEXT, text.size(), reinterpret_cast<intptr_t>(text.data()));
    send(SCI_EMPTYUNDOBUFFER);
    send(SCI_SETSAVEPOINT);
    send(SCI_SETREADONLY, 1);
    active()->modified = false;
    searchHits_ = std::move(hits);
    searchHitIndex_ = -1;
    send(SCI_GOTOPOS, 0);
    touchTitles();
}

void App::transformLines(Command command) {
    if (!active() || active()->transient || send(SCI_GETREADONLY))
        return;
    const auto begin = send(SCI_GETSELECTIONSTART), end = send(SCI_GETSELECTIONEND);
    const bool currentLineOnly = begin == end && command == CmdToggleComment;
    const auto first = begin == end && !currentLineOnly ? 0 : send(SCI_LINEFROMPOSITION, begin);
    auto last =
        begin == end && !currentLineOnly ? send(SCI_GETLINECOUNT) - 1 : send(SCI_LINEFROMPOSITION, end);
    if (begin != end && last > first && send(SCI_POSITIONFROMLINE, last) == end)
        --last;
    const auto from = send(SCI_POSITIONFROMLINE, first);
    const auto to =
        last + 1 < send(SCI_GETLINECOUNT) ? send(SCI_POSITIONFROMLINE, last + 1) : send(SCI_GETLENGTH);
    const auto all = documentText(active()->id);
    const auto source = all.substr(static_cast<size_t>(from), static_cast<size_t>(to - from));
    std::vector<std::string> lines, endings;
    size_t pos = 0;
    bool finalEol = false;
    while (pos < source.size()) {
        auto stop = source.find_first_of("\r\n", pos);
        if (stop == std::string::npos) {
            lines.push_back(source.substr(pos));
            endings.emplace_back();
            finalEol = false;
            break;
        }
        lines.push_back(source.substr(pos, stop - pos));
        pos = stop + 1;
        if (source[stop] == '\r' && pos < source.size() && source[pos] == '\n')
            ++pos;
        endings.push_back(source.substr(stop, pos - stop));
        finalEol = pos == source.size();
    }
    if (lines.empty())
        return;
    auto characters = [](const std::string &line) {
        return std::count_if(line.begin(), line.end(), [](unsigned char c) { return (c & 0xc0) != 0x80; });
    };
    auto folded = [](std::string line) {
        for (char &c : line)
            if (c >= 'A' && c <= 'Z')
                c += 'a' - 'A';
        return line;
    };
    if (command == CmdSpacesToTabs || command == CmdIndentToTabs) {
        const auto width = std::max<intptr_t>(1, send(SCI_GETTABWIDTH));
        for (auto &line : lines) {
            std::string converted;
            intptr_t col = 0;
            bool indent = true;
            for (size_t i = 0; i < line.size();) {
                if (line[i] == ' ' && (command == CmdSpacesToTabs || indent)) {
                    size_t end = i;
                    while (end < line.size() && line[end] == ' ')
                        ++end;
                    while (i < end) {
                        const auto stop = width - col % width;
                        if (stop > 1 && end - i >= static_cast<size_t>(stop)) {
                            converted += '\t';
                            i += static_cast<size_t>(stop);
                            col += stop;
                        } else {
                            converted += ' ';
                            ++i;
                            ++col;
                        }
                    }
                } else {
                    unsigned char ch = line[i++];
                    converted += static_cast<char>(ch);
                    if (ch == '\t')
                        col += width - col % width;
                    else {
                        if ((ch & 0xc0) != 0x80)
                            ++col;
                        if (ch != ' ')
                            indent = false;
                    }
                }
            }
            line = std::move(converted);
        }
    } else if (command == CmdRemoveEmptyLines || command == CmdRemoveBlankLines) {
        lines.erase(std::remove_if(lines.begin(), lines.end(),
                                   [&](const auto &line) {
                                       return command == CmdRemoveEmptyLines
                                                  ? line.empty()
                                                  : line.find_first_not_of(" \t") == std::string::npos;
                                   }),
                    lines.end());
    } else if (command == CmdUniqueAdjacent)
        lines.erase(std::unique(lines.begin(), lines.end()), lines.end());
    else if (command == CmdReverseLines)
        std::reverse(lines.begin(), lines.end());
    else if (command == CmdSortLengthAsc || command == CmdSortLengthDesc)
        std::stable_sort(lines.begin(), lines.end(), [&](const auto &a, const auto &b) {
            return command == CmdSortLengthAsc ? characters(a) < characters(b)
                                               : characters(a) > characters(b);
        });
    else if (command == CmdSortIgnoreCaseAsc || command == CmdSortIgnoreCaseDesc)
        std::stable_sort(lines.begin(), lines.end(), [&](const auto &a, const auto &b) {
            return command == CmdSortIgnoreCaseAsc ? folded(a) < folded(b) : folded(a) > folded(b);
        });
    else if (command == CmdTrimLeading || command == CmdTrimBoth || command == CmdTrimAndJoin) {
        for (auto &line : lines) {
            const auto first = line.find_first_not_of(" \t");
            line.erase(0, first == std::string::npos ? line.size() : first);
            if (command == CmdTrimBoth || command == CmdTrimAndJoin) {
                const auto last = line.find_last_not_of(" \t");
                line.erase(last == std::string::npos ? 0 : last + 1);
            }
        }
    } else if (command == CmdSortAsc)
        std::stable_sort(lines.begin(), lines.end());
    else if (command == CmdSortDesc)
        std::stable_sort(lines.begin(), lines.end(), std::greater<std::string>());
    else if (command == CmdUniqueLines) {
        std::unordered_set<std::string> seen;
        lines.erase(std::remove_if(lines.begin(), lines.end(),
                                   [&](const auto &line) { return !seen.insert(line).second; }),
                    lines.end());
    } else if (command == CmdToggleComment) {
        const auto lexer = active()->lexer;
        const std::string prefix =
            (lexer == "python" || lexer == "bash" || lexer == "ruby" || lexer == "perl" || lexer == "yaml" ||
             lexer == "cmake" || lexer == "makefile")
                ? "#"
            : (lexer == "sql" || lexer == "lua")                          ? "--"
            : (lexer == "cpp" || lexer == "rust" || lexer == "phpscript") ? "//"
                                                                          : "";
        if (prefix.empty()) {
            host_.alert("当前语言未定义行注释符号。");
            return;
        }
        bool uncomment = true;
        for (const auto &line : lines) {
            auto i = line.find_first_not_of(" \t");
            if (i != std::string::npos && line.compare(i, prefix.size(), prefix) != 0)
                uncomment = false;
        }
        for (auto &line : lines) {
            auto i = line.find_first_not_of(" \t");
            if (i == std::string::npos)
                continue;
            if (uncomment) {
                line.erase(i, prefix.size());
                if (i < line.size() && line[i] == ' ')
                    line.erase(i, 1);
            } else
                line.insert(i, prefix + " ");
        }
    } else if (command == CmdTabsToSpaces) {
        const auto width = std::max<intptr_t>(1, send(SCI_GETTABWIDTH));
        for (auto &line : lines) {
            std::string expanded;
            intptr_t col = 0;
            for (unsigned char ch : line) {
                if (ch == '\t') {
                    auto n = width - col % width;
                    expanded.append(static_cast<size_t>(n), ' ');
                    col += n;
                } else {
                    expanded += static_cast<char>(ch);
                    if ((ch & 0xc0) != 0x80)
                        ++col;
                }
            }
            line = std::move(expanded);
        }
    }
    if (command == CmdEolsToSpaces || command == CmdTrimAndJoin) {
        std::string joined;
        for (size_t i = 0; i < lines.size(); ++i) {
            joined += lines[i];
            if (i + 1 < lines.size() || finalEol)
                joined += ' ';
        }
        if (joined != source) {
            send(SCI_BEGINUNDOACTION);
            send(SCI_SETTARGETRANGE, from, to);
            send(SCI_REPLACETARGET, joined.size(), reinterpret_cast<intptr_t>(joined.data()));
            send(SCI_ENDUNDOACTION);
            if (begin != end)
                send(SCI_SETSEL, from, from + joined.size());
        }
        return;
    }
    const auto mode = send(SCI_GETEOLMODE);
    const std::string eol = mode == SC_EOL_CRLF ? "\r\n" : mode == SC_EOL_CR ? "\r" : "\n";
    std::string replacement;
    for (size_t i = 0; i < lines.size(); ++i) {
        replacement += lines[i];
        if (i + 1 < lines.size() || finalEol)
            replacement +=
                (command == CmdToggleComment || command == CmdTabsToSpaces || command == CmdTrimLeading ||
                 command == CmdTrimBoth || command == CmdSpacesToTabs || command == CmdIndentToTabs)
                    ? endings[i]
                    : eol;
    }
    if (replacement == source)
        return;
    send(SCI_BEGINUNDOACTION);
    send(SCI_SETTARGETRANGE, from, to);
    send(SCI_REPLACETARGET, replacement.size(), reinterpret_cast<intptr_t>(replacement.data()));
    send(SCI_ENDUNDOACTION);
    if (begin != end)
        send(SCI_SETSEL, from, from + replacement.size());
}
