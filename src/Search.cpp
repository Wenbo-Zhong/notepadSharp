#include "App.h"
#include "Scintilla.h"
#include "SearchOptions.h"
#include <algorithm>

bool App::findBounds(intptr_t &low, intptr_t &high) {
    low = 0;
    high = send(SCI_GETLENGTH);
    if (!active())
        return false;
    if (find_.inSelection) {
        if (scopeDoc_ != active()->id || scopeEnd_ <= scopeStart_) {
            host_.findFeedback("请先选中搜索范围，再启用选区搜索。");
            return false;
        }
        low = std::min(scopeStart_, high);
        high = std::min(scopeEnd_, high);
    }
    return true;
}
void App::performFind(const FindRequest &request) {
    if (request.cancelled || !active())
        return;
    if (request.inSelection && (!find_.inSelection || scopeDoc_ != active()->id)) {
        scopeDoc_ = active()->id;
        scopeStart_ = send(SCI_GETSELECTIONSTART);
        scopeEnd_ = send(SCI_GETSELECTIONEND);
    }
    const bool changed =
        request.find != find_.find || request.regex != find_.regex || request.extended != find_.extended ||
        request.matchCase != find_.matchCase || request.wholeWord != find_.wholeWord ||
        request.dotMatchesNewline != find_.dotMatchesNewline || request.unicode != find_.unicode;
    if (changed)
        lastZeroMatch_ = -1;
    find_ = request;
    if (request.action == FindAction::ClearMarks) {
        send(SCI_SETINDICATORCURRENT, 8);
        send(SCI_INDICATORCLEARRANGE, 0, send(SCI_GETLENGTH));
        host_.findFeedback("已清除搜索标记。");
        return;
    }
    if (request.action == FindAction::Count) {
        countMatches();
        return;
    }
    if (request.action == FindAction::FindAllCurrent || request.action == FindAction::FindAllOpen ||
        request.action == FindAction::MarkAll) {
        findAllOpen(request.action == FindAction::MarkAll, true, request.action != FindAction::FindAllOpen);
        return;
    }
    if (request.action == FindAction::ReplaceAllOpen) {
        if (find_.inSelection) {
            host_.findFeedback("跨文档替换请先关闭选区范围。");
            return;
        }
        const int previous = active()->id;
        const bool wrap = find_.wrapAround;
        find_.wrapAround = true;
        intptr_t count = 0;
        int files = 0, skipped = 0;
        std::vector<int> ids;
        for (const auto &doc : docs_)
            if (!doc.transient)
                ids.push_back(doc.id);
        for (int id : ids) {
            onTabChanged(id);
            if (send(SCI_GETREADONLY)) {
                ++skipped;
                continue;
            }
            const auto changed = replaceAll(true);
            if (changed < 0) {
                find_.wrapAround = wrap;
                onTabChanged(previous);
                return;
            }
            count += changed;
            if (changed)
                ++files;
        }
        find_.wrapAround = wrap;
        onTabChanged(previous);
        host_.findFeedback("已在 " + std::to_string(files) + " 个文档替换 " + std::to_string(count) +
                           " 处；跳过只读文档 " + std::to_string(skipped) + " 个。");
        return;
    }
    if (request.action == FindAction::ReplaceAll) {
        replaceAll();
        return;
    }
    if (request.action == FindAction::ReplaceOne) {
        if (send(SCI_GETREADONLY)) {
            host_.findFeedback("文档为只读，无法替换。");
            return;
        }
        PreparedSearch query;
        std::string error;
        if (!prepareSearch(find_, query, error, true)) {
            host_.findFeedback(error);
            return;
        }
        intptr_t low, high;
        if (!findBounds(low, high))
            return;
        const auto start = send(SCI_GETSELECTIONSTART), end = send(SCI_GETSELECTIONEND);
        if (start >= low && end <= high && (end > start || lastZeroMatch_ == start)) {
            send(SCI_SETSTATUS, SC_STATUS_OK);
            send(SCI_SETSEARCHFLAGS, query.flags);
            send(SCI_SETTARGETRANGE, start, high);
            const auto found = searchInTarget(host_, active()->id, query);
            if (found == start && send(SCI_GETTARGETEND) == end) {
                editingSearch_ = true;
                send(SCI_BEGINUNDOACTION);
                const auto added = replaceInTarget(host_, active()->id, query);
                send(SCI_ENDUNDOACTION);
                editingSearch_ = false;
                if (find_.inSelection)
                    scopeEnd_ += added - (end - start);
                send(SCI_SETSEL, start + added, start + added);
                if (start == end) {
                    lastZeroMatch_ = start + added;
                    lastFindDoc_ = active()->id;
                }
            }
        }
        findAgain(!find_.backwards);
        return;
    }
    findAgain(request.action == FindAction::Previous ? find_.backwards : !find_.backwards);
}
void App::findAgain(bool forward) {
    if (find_.find.empty()) {
        onCommand(CmdFind);
        return;
    }
    PreparedSearch query;
    std::string error;
    if (!prepareSearch(find_, query, error)) {
        host_.findFeedback(error);
        return;
    }
    intptr_t low, high;
    if (!findBounds(low, high))
        return;
    send(SCI_SETSEARCHFLAGS, query.flags);
    send(SCI_SETSTATUS, SC_STATUS_OK);
    auto anchor = std::clamp(forward ? send(SCI_GETSELECTIONEND) : send(SCI_GETSELECTIONSTART), low, high);
    bool boundary = false;
    if (query.regex && active()->id == lastFindDoc_ && anchor == lastZeroMatch_ &&
        send(SCI_GETSELECTIONEMPTY)) {
        boundary = forward ? anchor == high : anchor == low;
        if (!boundary)
            anchor = advanceSearchPosition(host_, active()->id, anchor, forward);
    }
    auto search = [&](intptr_t from, intptr_t to) {
        send(SCI_SETTARGETRANGE, from, to);
        return searchInTarget(host_, active()->id, query);
    };
    auto found = boundary ? -1 : search(anchor, forward ? high : low);
    bool wrapped = false;
    if (found < 0 && find_.wrapAround && send(SCI_GETSTATUS) != SC_STATUS_WARN_REGEX) {
        found = search(forward ? low : high, forward ? high : low);
        wrapped = found >= 0;
    }
    if (send(SCI_GETSTATUS) == SC_STATUS_WARN_REGEX) {
        host_.findFeedback("正则表达式无效，请检查语法。");
        return;
    }
    if (found < 0) {
        host_.findFeedback(find_.wrapAround ? "找不到匹配内容。" : "已到搜索范围边界，未找到更多匹配。");
        return;
    }
    const auto end = send(SCI_GETTARGETEND);
    lastZeroMatch_ = query.regex && found == end ? found : -1;
    lastFindDoc_ = active()->id;
    send(SCI_SETSEL, found, end);
    send(SCI_SCROLLCARET);
    updateStatus();
    host_.findFeedback(wrapped ? "已循环到搜索范围另一端。" : "已找到匹配内容。");
}
void App::countMatches() {
    PreparedSearch query;
    std::string error;
    if (!prepareSearch(find_, query, error)) {
        host_.findFeedback(error);
        return;
    }
    intptr_t low, high;
    if (!findBounds(low, high))
        return;
    send(SCI_SETSEARCHFLAGS, query.flags);
    send(SCI_SETSTATUS, SC_STATUS_OK);
    query.allowEmptyAfterMatch = false;
    intptr_t count = 0, from = low;
    while (from <= high) {
        send(SCI_SETTARGETRANGE, from, high);
        auto found = searchInTarget(host_, active()->id, query);
        if (found < 0)
            break;
        auto end = send(SCI_GETTARGETEND);
        ++count;
        if (end > found)
            from = end;
        else {
            if (end == high)
                break;
            from = advanceSearchPosition(host_, active()->id, end);
        }
    }
    if (send(SCI_GETSTATUS) == SC_STATUS_WARN_REGEX)
        host_.findFeedback("正则表达式无效。");
    else
        host_.findFeedback("匹配数量：" + std::to_string(count));
}
intptr_t App::replaceAll(bool quiet) {
    if (send(SCI_GETREADONLY)) {
        host_.findFeedback("文档为只读，无法替换。");
        return -1;
    }
    PreparedSearch query;
    std::string error;
    if (!prepareSearch(find_, query, error, true)) {
        host_.findFeedback(error);
        return -1;
    }
    intptr_t low, high;
    if (!findBounds(low, high))
        return -1;
    if (!find_.inSelection && !find_.wrapAround) {
        if (find_.backwards)
            high = send(SCI_GETSELECTIONSTART);
        else
            low = send(SCI_GETSELECTIONEND);
    }
    send(SCI_SETSEARCHFLAGS, query.flags);
    send(SCI_SETSTATUS, SC_STATUS_OK);
    editingSearch_ = true;
    send(SCI_BEGINUNDOACTION);
    query.allowEmptyAfterMatch = false;
    intptr_t from = low, count = 0;
    while (from <= high && count < 1000000) {
        send(SCI_SETTARGETRANGE, from, high);
        auto found = searchInTarget(host_, active()->id, query);
        if (found < 0)
            break;
        const auto end = send(SCI_GETTARGETEND);
        const bool empty = found == end;
        const auto added = replaceInTarget(host_, active()->id, query);
        if (added < 0)
            break;
        high += added - (end - found);
        from = send(SCI_GETTARGETEND);
        ++count;
        if (empty) {
            if (from >= high)
                break;
            from = advanceSearchPosition(host_, active()->id, from);
        }
    }
    send(SCI_ENDUNDOACTION);
    editingSearch_ = false;
    if (find_.inSelection) {
        scopeEnd_ = high;
        send(SCI_SETSEL, low, high);
    }
    if (send(SCI_GETSTATUS) == SC_STATUS_WARN_REGEX) {
        host_.findFeedback("正则表达式无效，请检查语法。");
        return -1;
    } else if (!quiet)
        host_.findFeedback("已替换 " + std::to_string(count) + " 处。" +
                           (count == 1000000 ? "已达单次替换上限。" : ""));
    return count;
}
