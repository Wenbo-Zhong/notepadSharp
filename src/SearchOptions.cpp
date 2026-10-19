#include "SearchOptions.h"
#include "UnicodeRegex.h"
#include "IcuRegex.h"
#include "Scintilla.h"
#include <algorithm>
#include <boost/regex.hpp>
#ifdef _WIN32
// Boost disables native traits by default under MinGW; select them explicitly.
#include <boost/regex/v5/w32_regex_traits.hpp>
#endif
#include <stdexcept>
#include <climits>
namespace {
int hex(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}
bool expand(const std::string &source, std::string &out, std::string &error) {
    out.clear();
    for (size_t i = 0; i < source.size(); ++i) {
        if (source[i] != '\\') {
            out += source[i];
            continue;
        }
        if (++i == source.size()) {
            error = "扩展模式末尾不能是单独的反斜杠。";
            return false;
        }
        switch (source[i]) {
        case 'n':
            out += '\n';
            break;
        case 'r':
            out += '\r';
            break;
        case 't':
            out += '\t';
            break;
        case '0':
            out += '\0';
            break;
        case '\\':
            out += '\\';
            break;
        case 'x': {
            if (i + 2 >= source.size() || hex(source[i + 1]) < 0 || hex(source[i + 2]) < 0) {
                error = "\\x 后需要两个十六进制数字。";
                return false;
            }
            out += static_cast<char>(hex(source[i + 1]) * 16 + hex(source[i + 2]));
            i += 2;
            break;
        }
        default:
            error = "不支持的扩展转义；支持 \\n、\\r、\\t、\\0、\\xHH、\\\\。";
            return false;
        }
    }
    return true;
}
// Decode once, recording editor byte offsets at each wide-character boundary.
// Reject invalid UTF-8 rather than silently returning positions in the wrong encoding.
std::wstring wide(const std::string &text, std::vector<size_t> *bytes = nullptr) {
    std::wstring out;
    if (bytes)
        bytes->clear();
    for (size_t i = 0; i < text.size();) {
        const auto start = i;
        auto first = static_cast<unsigned char>(text[i++]);
        uint32_t cp = first;
        unsigned count = 0;
        if (first >= 0xC2 && first <= 0xDF) {
            cp &= 31;
            count = 1;
        } else if (first >= 0xE0 && first <= 0xEF) {
            cp &= 15;
            count = 2;
        } else if (first >= 0xF0 && first <= 0xF4) {
            cp &= 7;
            count = 3;
        } else if (first >= 0x80)
            throw std::runtime_error("Invalid UTF-8");
        if (i + count > text.size())
            throw std::runtime_error("Truncated UTF-8");
        for (unsigned n = 0; n < count; ++n) {
            const auto next = static_cast<unsigned char>(text[i++]);
            if ((next & 0xC0) != 0x80)
                throw std::runtime_error("Invalid UTF-8 continuation");
            cp = (cp << 6) | (next & 63);
        }
        if ((count == 1 && cp < 0x80) || (count == 2 && cp < 0x800) || (count == 3 && cp < 0x10000) ||
            cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
            throw std::runtime_error("Invalid Unicode scalar");
        if (bytes)
            bytes->push_back(start);
        if (sizeof(wchar_t) == 2 && cp >= 0x10000) {
            cp -= 0x10000;
            out += static_cast<wchar_t>(0xD800 + (cp >> 10));
            if (bytes)
                bytes->push_back(start);
            out += static_cast<wchar_t>(0xDC00 + (cp & 1023));
        } else
            out += static_cast<wchar_t>(cp);
    }
    if (bytes)
        bytes->push_back(text.size());
    return out;
}
std::string utf8(const std::wstring &text) {
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        uint32_t cp = static_cast<uint32_t>(text[i]);
        if (sizeof(wchar_t) == 2 && cp >= 0xD800 && cp <= 0xDBFF) {
            if (i + 1 == text.size() || text[i + 1] < 0xDC00 || text[i + 1] > 0xDFFF)
                throw std::runtime_error("Unpaired UTF-16 surrogate");
            cp = 0x10000 + ((cp - 0xD800) << 10) + (text[++i] - 0xDC00);
        } else if (cp >= 0xDC00 && cp <= 0xDFFF)
            throw std::runtime_error("Unpaired UTF-16 surrogate");
        if (cp < 0x80)
            out += static_cast<char>(cp);
        else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 63));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 63));
            out += static_cast<char>(0x80 | (cp & 63));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 63));
            out += static_cast<char>(0x80 | ((cp >> 6) & 63));
            out += static_cast<char>(0x80 | (cp & 63));
        }
    }
    return out;
}
} // namespace
struct RegexState {
#ifdef _WIN32
    // Explicitly use the same native classification/case tables as Notepad++.
    boost::basic_regex<wchar_t, boost::regex_traits<wchar_t, boost::w32_regex_traits<wchar_t>>> expression;
#else
    boost::wregex expression;
#endif
    std::unique_ptr<IcuRegex> unicode;
    std::shared_ptr<boost::wsmatch::named_sub_type> names;
    std::wstring document, replacement;
    std::vector<size_t> bytes;
    std::string formatted;
    bool dotAll = false;
    int previousDoc = 0;
    intptr_t previousEnd = -1;
    bool previousNonempty = false;
    void load(const std::string &text) {
        document = wide(text, &bytes);
        if (unicode)
            unicode->load(text);
    }
};
bool prepareSearch(const FindRequest &request, PreparedSearch &query, std::string &error, bool replacement) {
    query = {};
    error.clear();
    query.find = request.find;
    query.replace = request.replace;
    query.regex = request.regex;

    if (request.regex && request.extended) {
        error = "请选择一种搜索模式。";
        return false;
    }
    if (request.extended && (!expand(request.find, query.find, error) ||
                             (replacement && !expand(request.replace, query.replace, error))))
        return false;
    if (query.find.empty()) {
        error = "请输入查找内容。";
        return false;
    }
    if (request.regex) {
        try {
            query.engine = std::make_shared<RegexState>();
            query.engine->dotAll = request.dotMatchesNewline;
            boost::regex::flag_type options = boost::regex::normal;
            if (!request.matchCase)
                options |= boost::regex::icase;
            if (request.unicode) {
                query.engine->unicode =
                    std::make_unique<IcuRegex>(query.find, request.matchCase, request.dotMatchesNewline);
                query.engine->names = std::make_shared<boost::wsmatch::named_sub_type>();
                for (const auto &entry : query.engine->unicode->names())
                    query.engine->names->set_name(entry.first.data(), entry.first.data() + entry.first.size(),
                                                  entry.second);
            } else
                query.engine->expression.assign(prepareUnicodeRegex(wide(query.find)), options);
            query.engine->replacement = replacement ? wide(query.replace) : std::wstring{};
        } catch (const std::exception &e) {
            error = std::string("正则表达式无效：") + e.what();
            return false;
        }
    }
    query.flags = (request.matchCase ? SCFIND_MATCHCASE : 0) | (request.wholeWord ? SCFIND_WHOLEWORD : 0) |
                  (request.regex ? SCFIND_REGEXP : 0);
    return true;
}

intptr_t searchInTarget(Host &host, int doc, PreparedSearch &query) {
    auto s = [&](unsigned msg, uintptr_t w = 0, intptr_t l = 0) { return host.send(doc, msg, w, l); };
    if (!query.regex)
        return s(SCI_SEARCHINTARGET, query.find.size(), reinterpret_cast<intptr_t>(query.find.data()));
    try {
        auto &engine = *query.engine;
        if (!query.cached || query.cachedDoc != doc) {
            const auto length = s(SCI_GETLENGTH);
            query.document.assign(length + 1, '\0');
            s(SCI_GETTEXT, length + 1, reinterpret_cast<intptr_t>(query.document.data()));
            query.document.resize(length);
            engine.load(query.document);
            engine.previousEnd = -1;
            engine.previousNonempty = false;
            query.cached = true;
            query.cachedDoc = doc;
        }
        const auto from = s(SCI_GETTARGETSTART), to = s(SCI_GETTARGETEND);
        const auto low = std::min(from, to), high = std::max(from, to);
        const auto begin = engine.document.cbegin();
        auto position =
            begin + (std::lower_bound(engine.bytes.begin(), engine.bytes.end(), low) - engine.bytes.begin());
        const auto end =
            begin + (std::lower_bound(engine.bytes.begin(), engine.bytes.end(), high) - engine.bytes.begin());
        auto flags = boost::match_default;
        if (!engine.dotAll)
            flags |= boost::match_not_dot_newline;
        // Like the original, the upper target bound terminates the regex input:
        // $, \z and lookahead see this end, while lookbehind can inspect the full prefix.
        boost::wsmatch match, last;
        bool matched = false;
        const bool suppressInitialEmpty = !query.allowEmptyAfterMatch && engine.previousDoc == doc &&
                                          engine.previousEnd == low && engine.previousNonempty;
        while (position <= end) {
            auto currentFlags = flags;
            if (position != begin)
                currentFlags |= boost::match_prev_avail;
            if (engine.unicode) {
                std::vector<std::pair<int64_t, int64_t>> groups;
                if (!engine.unicode->find(engine.bytes[position - begin], engine.bytes[end - begin], groups))
                    break;
                auto iterator = [&](int64_t offset) {
                    if (offset < 0)
                        return end;
                    return begin + (std::lower_bound(engine.bytes.begin(), engine.bytes.end(), offset) -
                                    engine.bytes.begin());
                };
                match.set_size(groups.size(), begin, end);
                match.set_base(begin);
                match.set_first(iterator(groups[0].first));
                match.set_second(iterator(groups[0].second), 0);
                for (size_t n = 1; n < groups.size(); ++n) {
                    match.set_first(iterator(groups[n].first), n);
                    match.set_second(iterator(groups[n].second), n, groups[n].first >= 0);
                }
                match.set_named_subs(engine.names);
            } else if (!boost::regex_search(position, end, match, engine.expression, currentFlags, begin))
                break;
            const auto start = engine.bytes[match[0].first - begin];
            const auto finish = engine.bytes[match[0].second - begin];
            bool accepted =
                !(start == finish && ((suppressInitialEmpty && start == static_cast<size_t>(low)) ||
                                      (start > 0 && start < query.document.size() &&
                                       query.document[start - 1] == '\r' && query.document[start] == '\n')));
            if (query.flags & SCFIND_WHOLEWORD)
                accepted = accepted && (engine.unicode ? engine.unicode->wholeWord(start, finish)
                                                       : s(SCI_ISRANGEWORD, start, finish) != 0);
            // Reverse search uses the farthest end, retaining the longest match on ties.
            if (accepted && (!matched || finish > engine.bytes[last[0].second - begin])) {
                last = match;
                matched = true;
                if (from <= to)
                    break;
            }
            // Restart at the next character after the candidate START, so reverse
            // search also sees overlapping matches and rejected whole-word candidates.
            const auto next = advanceSearchPosition(host, doc, start);
            if (next <= static_cast<intptr_t>(start) || next > high)
                break;
            position = begin + (std::lower_bound(engine.bytes.begin(), engine.bytes.end(), next) -
                                engine.bytes.begin());
        }
        if (!matched)
            return -1;
        const auto start = engine.bytes[last[0].first - begin];
        const auto finish = engine.bytes[last[0].second - begin];
        engine.formatted = utf8(last.format(engine.replacement, boost::format_all));
        engine.previousDoc = doc;
        engine.previousEnd = finish;
        engine.previousNonempty = finish > start;
        s(SCI_SETTARGETRANGE, start, finish);
        return start;
    } catch (const std::exception &) {
        s(SCI_SETSTATUS, SC_STATUS_WARN_REGEX);
        return -1;
    }
}
intptr_t replaceInTarget(Host &host, int doc, PreparedSearch &query) {
    auto s = [&](unsigned msg, uintptr_t w = 0, intptr_t l = 0) { return host.send(doc, msg, w, l); };
    const std::string &replacement = query.regex ? query.engine->formatted : query.replace;
    const auto start = s(SCI_GETTARGETSTART), end = s(SCI_GETTARGETEND);
    const auto added =
        s(SCI_REPLACETARGET, replacement.size(), reinterpret_cast<intptr_t>(replacement.data()));
    if (added >= 0 && query.cached) {
        query.document.replace(static_cast<size_t>(start), static_cast<size_t>(end - start), replacement);
        query.engine->load(query.document);
        query.engine->previousEnd = start + added;
    }
    return added;
}

intptr_t advanceSearchPosition(Host &host, int doc, intptr_t position, bool forward) {
    auto s = [&](unsigned msg, uintptr_t w = 0) { return host.send(doc, msg, w, 0); };
    const auto length = s(SCI_GETLENGTH);
    if (forward) {
        if (position >= length)
            return length + 1;
        if (s(SCI_GETCHARAT, position) == '\r' && s(SCI_GETCHARAT, position + 1) == '\n')
            return position + 2;
        return s(SCI_POSITIONAFTER, position);
    }
    if (position <= 0)
        return -1;
    if (position >= 2 && s(SCI_GETCHARAT, position - 2) == '\r' && s(SCI_GETCHARAT, position - 1) == '\n')
        return position - 2;
    return s(SCI_POSITIONBEFORE, position);
}
