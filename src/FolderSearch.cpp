#include "App.h"
#include "Scintilla.h"
#include "SearchOptions.h"
#include "TextEncoding.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
namespace fs = std::filesystem;

void App::navigateResult(bool forward) {
    if (searchHits_.empty()) {
        host_.alert("没有有效搜索结果，请重新查找。");
        return;
    }
    if (searchHitIndex_ < 0)
        searchHitIndex_ = forward ? 0 : static_cast<int>(searchHits_.size()) - 1;
    else
        searchHitIndex_ = (searchHitIndex_ + (forward ? 1 : -1) + static_cast<int>(searchHits_.size())) %
                          static_cast<int>(searchHits_.size());
    const auto hit = searchHits_[searchHitIndex_];
    if (hit.path.empty()) {
        if (!findDoc(hit.doc)) {
            searchHits_.clear();
            host_.alert("源文档已关闭，请重新搜索。");
            return;
        }
        onTabChanged(hit.doc);
    } else {
        const auto savedHits = searchHits_;
        const int index = searchHitIndex_;
        openPath(hit.path);
        bool samePath = false;
        if (active() && !active()->path.empty()) {
            std::error_code ec;
            samePath = fs::equivalent(fs::u8path(hit.path), fs::u8path(active()->path), ec) && !ec;
        }
        if (!samePath || std::hash<std::string>{}(documentText(active()->id)) != hit.contentHash) {
            searchHits_.clear();
            searchHitIndex_ = -1;
            host_.alert("源内容已变化或无法打开，请重新搜索。");
            return;
        }
        // Opening a file sends modification notifications; retain the validated result set.
        searchHits_ = savedHits;
        searchHitIndex_ = index;
    }
    send(SCI_SETSEL, hit.start, hit.end);
    send(SCI_SCROLLCARET);
}

void App::findInFolder() {
    stopMacro();
    const auto root = host_.promptDirectory();
    if (root.empty())
        return;
    const auto request = host_.promptFind(find_, false);
    if (request.cancelled || request.find.empty())
        return;
    find_ = request;
    std::error_code ec;
    if (!fs::is_directory(fs::u8path(root), ec) || ec) {
        host_.alert("无法读取搜索文件夹。");
        return;
    }
    std::vector<SearchHit> hits;
    std::ostringstream report;
    report << "文件夹：" << root << "\n搜索：" << find_.find
           << "\nF4 / Shift+F4 跳转；源文件变化后请重新搜索。\n\n";
    size_t files = 0, skipped = 0, totalBytes = 0;
    bool limited = false;
    {
        const int editor = host_.createEditor();
        if (!editor) {
            host_.alert("无法创建搜索编辑器。");
            return;
        }
        struct Scratch {
            Host &host;
            App &app;
            int id;
            int previous;
            ~Scratch() {
                host.destroyEditor(id);
                if (previous)
                    app.onTabChanged(previous);
            }
        } scratch{host_, *this, editor, active() ? active()->id : 0};
        auto s = [&](unsigned msg, uintptr_t w = 0, intptr_t l = 0) { return host_.send(editor, msg, w, l); };
        s(SCI_SETCODEPAGE, SC_CP_UTF8);
        PreparedSearch query;
        std::string error;
        if (!prepareSearch(find_, query, error)) {
            host_.findFeedback(error);
            return;
        }
        query.allowEmptyAfterMatch = false;
        const int flags = query.flags;
        s(SCI_SETSEARCHFLAGS, flags);
        s(SCI_SETSTATUS, SC_STATUS_OK);
        s(SCI_SETTARGETRANGE, 0, 0);
        searchInTarget(host_, editor, query);
        if (s(SCI_GETSTATUS) == SC_STATUS_WARN_REGEX) {
            host_.alert("正则表达式无效。");
            return;
        }
        fs::recursive_directory_iterator iterator(fs::u8path(root),
                                                  fs::directory_options::skip_permission_denied, ec),
            end;
        while (iterator != end && !ec) {
            const auto entry = *iterator;
            const auto path = entry.path();
            std::error_code itemError;
            const bool directory = entry.is_directory(itemError);
            if (directory && (path.filename() == ".git" || iterator.depth() >= 64))
                iterator.disable_recursion_pending();
            if (!directory && !entry.is_symlink(itemError) && entry.is_regular_file(itemError)) {
                if (files >= 2000) {
                    limited = true;
                    break;
                }
                ++files;
                auto bytes = entry.file_size(itemError);
                if (itemError || bytes > 8 * 1024 * 1024)
                    ++skipped;
                else if (totalBytes + bytes > 64 * 1024 * 1024) {
                    limited = true;
                    break;
                } else {
                    totalBytes += static_cast<size_t>(bytes);
                    std::ifstream input(path, std::ios::binary);
                    std::string raw(static_cast<size_t>(bytes), '\0');
                    if (!input.read(raw.data(), static_cast<std::streamsize>(bytes)))
                        ++skipped;
                    else {
                        bool utf16 = raw.size() >= 2 && ((static_cast<unsigned char>(raw[0]) == 0xff &&
                                                          static_cast<unsigned char>(raw[1]) == 0xfe) ||
                                                         (static_cast<unsigned char>(raw[0]) == 0xfe &&
                                                          static_cast<unsigned char>(raw[1]) == 0xff));
                        std::string text, encoding, error;
                        if ((!utf16 && raw.find('\0') != std::string::npos) ||
                            !TextEncoding::decode(raw, text, encoding, error))
                            ++skipped;
                        else {
                            int openDoc = 0;
                            for (const auto &doc : docs_) {
                                std::error_code compareError;
                                if (!doc.path.empty() && !doc.transient &&
                                    fs::equivalent(path, fs::u8path(doc.path), compareError) &&
                                    !compareError) {
                                    text = documentText(doc.id);
                                    openDoc = doc.id;
                                    break;
                                }
                            }
                            const auto hash = std::hash<std::string>{}(text);
                            query.cached = false;
                            s(SCI_CLEARALL);
                            s(SCI_ADDTEXT, text.size(), reinterpret_cast<intptr_t>(text.data()));
                            s(SCI_EMPTYUNDOBUFFER);
                            intptr_t from = 0;
                            const auto length = static_cast<intptr_t>(text.size());
                            while (from <= length) {
                                s(SCI_SETTARGETRANGE, from, length);
                                const auto found = searchInTarget(host_, editor, query);
                                if (found < 0)
                                    break;
                                const auto stop = s(SCI_GETTARGETEND), line = s(SCI_LINEFROMPOSITION, found);
                                hits.push_back({openDoc, found, stop, fs::absolute(path).u8string(), hash});
                                const auto start = s(SCI_POSITIONFROMLINE, line),
                                           lineEnd = s(SCI_GETLINEENDPOSITION, line);
                                auto snippetEnd = std::min<intptr_t>(lineEnd, start + 300);
                                while (snippetEnd < lineEnd && snippetEnd > start &&
                                       (s(SCI_GETCHARAT, snippetEnd) & 0xc0) == 0x80)
                                    --snippetEnd;
                                report << path.u8string() << ":" << (line + 1) << ":"
                                       << (s(SCI_GETCOLUMN, found) + 1) << "  "
                                       << text.substr(static_cast<size_t>(start),
                                                      static_cast<size_t>(snippetEnd - start))
                                       << "\n";
                                if (hits.size() >= 10000) {
                                    limited = true;
                                    break;
                                }
                                if (stop > found)
                                    from = stop;
                                else {
                                    if (stop == length)
                                        break;
                                    from = advanceSearchPosition(host_, editor, stop);
                                }
                            }
                        }
                    }
                }
            }
            if (limited)
                break;
            iterator.increment(ec);
        }
        if (ec)
            report << "\n部分目录无法继续读取。\n";
    }
    report << "\n检查文件：" << files << "；跳过（二进制、过大、编码或读取失败）：" << skipped << "。\n";
    if (limited)
        report << "达到搜索上限（2000 文件 / 64 MiB 总读取量 / 10000 结果），结果不完整。\n";
    showSearchResults(report.str(), std::move(hits));
}
