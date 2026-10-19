#include "UnicodeRegex.h"
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace {
const std::wstring scalar = LR"((?:[\x{D800}-\x{DBFF}][\x{DC00}-\x{DFFF}]|[^\x{D800}-\x{DFFF}]))";
std::wstring scalarAtom(const std::wstring &atom) { return L"(?:(?=" + atom + L")" + scalar + L")"; }
std::wstring hex(uint32_t value) {
    const wchar_t digits[] = L"0123456789ABCDEF";
    std::wstring out;
    do {
        out.insert(out.begin(), digits[value & 15]);
        value >>= 4;
    } while (value);
    return L"\\x{" + out + L"}";
}
std::wstring unitRange(uint32_t low, uint32_t high) {
    return low == high ? hex(low) : L"[" + hex(low) + L"-" + hex(high) + L"]";
}
std::wstring pairRange(uint32_t low, uint32_t high) {
    low -= 0x10000;
    high -= 0x10000;
    const auto first = 0xD800 + (low >> 10), last = 0xD800 + (high >> 10);
    if (first == last)
        return hex(first) + unitRange(0xDC00 + (low & 1023), 0xDC00 + (high & 1023));
    std::wstring out = hex(first) + unitRange(0xDC00 + (low & 1023), 0xDFFF);
    if (first + 1 < last)
        out += L"|" + unitRange(first + 1, last - 1) + LR"([\x{DC00}-\x{DFFF}])";
    out += L"|" + hex(last) + unitRange(0xDC00, 0xDC00 + (high & 1023));
    return L"(?:" + out + L")";
}
int digit(wchar_t c) {
    if (c >= L'0' && c <= L'9')
        return c - L'0';
    if (c >= L'a' && c <= L'f')
        return c - L'a' + 10;
    if (c >= L'A' && c <= L'F')
        return c - L'A' + 10;
    return -1;
}
struct Token {
    std::wstring text;
    int64_t cp = -1;
};
// Read one character-set operand, keeping POSIX classes and escaped syntax intact.
Token readToken(const std::wstring &source, size_t &i) {
    const auto start = i;
    int64_t cp = source[i++];
    if (cp == L'[' && i < source.size() && (source[i] == L':' || source[i] == L'.' || source[i] == L'=')) {
        const auto end = source.find(std::wstring{source[i], L']'}, i + 1);
        if (end != std::wstring::npos) {
            i = end + 2;
            cp = -1;
        }
    } else if (cp == L'\\' && i < source.size()) {
        const auto escape = source[i++];
        cp = -1;
        if (escape == L'x' && i < source.size() && source[i] == L'{') {
            const auto end = source.find(L'}', i + 1);
            if (end != std::wstring::npos) {
                uint32_t value = 0;
                bool valid = end > i + 1;
                for (size_t n = i + 1; n < end; ++n) {
                    const int d = digit(source[n]);
                    if (d < 0 || value > 0x10FFFF / 16) {
                        valid = false;
                        break;
                    }
                    value = value * 16 + d;
                }
                if (valid && value <= 0x10FFFF) {
                    cp = value;
                    i = end + 1;
                } else
                    throw std::runtime_error("Invalid Unicode code point in hexadecimal escape");
            }
        } else if (escape == L'x' && i + 1 < source.size() && digit(source[i]) >= 0 &&
                   digit(source[i + 1]) >= 0) {
            cp = digit(source[i]) * 16 + digit(source[i + 1]);
            i += 2;
        } else if ((escape == L'p' || escape == L'P') && i < source.size()) {
            if (source[i] == L'{') {
                const auto end = source.find(L'}', i + 1);
                if (end != std::wstring::npos)
                    i = end + 1;
            } else
                ++i; // Boost also accepts one-letter properties, e.g. \pd.
        } else if (std::wstring(L"\\[]-^.#(){}+*?|$").find(escape) != std::wstring::npos)
            cp = escape;
    } else if (cp >= 0xD800 && cp <= 0xDBFF && i < source.size() && source[i] >= 0xDC00 &&
               source[i] <= 0xDFFF) {
        cp = 0x10000 + ((cp - 0xD800) << 10) + (source[i++] - 0xDC00);
    }
    return {source.substr(start, i - start), cp};
}
std::wstring classAtom(const std::wstring &source, bool inLookbehind) {
    const bool negate = source.size() > 1 && source[1] == L'^';
    size_t i = negate ? 2 : 1;
    std::vector<Token> tokens;
    while (i + 1 < source.size())
        tokens.push_back(readToken(source, i));
    bool supplementary = false;
    for (const auto &token : tokens)
        supplementary = supplementary || token.cp >= 0x10000;
    for (size_t n = 0; n + 2 < tokens.size(); ++n)
        if (tokens[n + 1].text == L"-" && tokens[n].cp >= 0 && tokens[n].cp <= 0xDFFF &&
            tokens[n + 2].cp >= 0xD800)
            supplementary = true;
    if (!supplementary)
        return inLookbehind ? source : scalarAtom(source);
    std::wstring bmp, pairs;
    auto add = [&](uint32_t low, uint32_t high) {
        if (low > high || high > 0x10FFFF)
            throw std::runtime_error("Invalid Unicode character range");
        auto bmpRange = [&](uint32_t first, uint32_t last) {
            bmp += hex(first);
            if (first != last)
                bmp += L"-" + hex(last);
        };
        // Surrogates are encoding units, never standalone members of a scalar range.
        if (low < 0xD800)
            bmpRange(low, high < 0xD800 ? high : 0xD7FF);
        if (low < 0x10000 && high >= 0xE000)
            bmpRange(low < 0xE000 ? 0xE000 : low, high < 0x10000 ? high : 0xFFFF);
        if (high >= 0x10000) {
            if (!pairs.empty())
                pairs += L"|";
            pairs += pairRange(low < 0x10000 ? 0x10000 : low, high);
        }
    };
    for (size_t n = 0; n < tokens.size(); ++n) {
        if (n + 2 < tokens.size() && tokens[n + 1].text == L"-" && tokens[n].cp >= 0 &&
            tokens[n + 2].cp >= 0 && (tokens[n].cp >= 0x10000 || tokens[n + 2].cp >= 0xD800)) {
            add(static_cast<uint32_t>(tokens[n].cp), static_cast<uint32_t>(tokens[n + 2].cp));
            n += 2;
        } else if (tokens[n].cp >= 0x10000)
            add(static_cast<uint32_t>(tokens[n].cp), static_cast<uint32_t>(tokens[n].cp));
        else if (tokens[n].cp == L'^' || tokens[n].cp == L']' ||
                 (tokens[n].cp == L'-' && (n == 0 || n + 1 == tokens.size())))
            bmp += hex(static_cast<uint32_t>(tokens[n].cp));
        else
            bmp += tokens[n].text;
    }
    std::wstring positive = pairs;
    if (!bmp.empty()) {
        if (!positive.empty())
            positive += L"|";
        positive += inLookbehind ? L"[" + bmp + L"]" : scalarAtom(L"[" + bmp + L"]");
    }
    if (positive.empty())
        positive = LR"(\b\B)";
    if (negate)
        return L"(?:(?!(?:" + positive + L"))" + scalar + L")";
    return L"(?:" + positive + L")";
}
size_t classEnd(const std::wstring &source, size_t start) {
    auto i = start + 1;
    if (i < source.size() && source[i] == L'^')
        ++i;
    if (i < source.size() && source[i] == L']')
        ++i;
    while (i < source.size()) {
        if (source[i] == L']')
            return i;
        readToken(source, i);
    }
    return std::wstring::npos;
}
struct Mode {
    bool extended = false, lookbehind = false;
};
} // namespace

std::wstring prepareUnicodeRegex(const std::wstring &source) {
    if (sizeof(wchar_t) != 2)
        return source;
    std::wstring out;
    bool quoted = false;
    Mode mode;
    std::vector<Mode> groups;
    for (size_t i = 0; i < source.size();) {
        const auto c = source[i];
        if (quoted) {
            if (source.compare(i, 2, L"\\E") == 0) {
                quoted = false;
                i += 2;
                continue;
            }
            if (c >= 0xD800 && c <= 0xDBFF && i + 1 < source.size() && source[i + 1] >= 0xDC00 &&
                source[i + 1] <= 0xDFFF) {
                auto token = readToken(source, i);
                out += L"(?:" + token.text + L")";
            } else {
                out += hex(static_cast<uint32_t>(c));
                ++i;
            }
            continue;
        }
        if (!quoted && mode.extended && c == L'#') {
            auto end = source.find_first_of(L"\r\n\x85\u2028\u2029", i);
            if (end == std::wstring::npos)
                end = source.size();
            out += source.substr(i, end - i);
            i = end;
            continue;
        }
        if (!quoted && c == L'(') {
            if (source.compare(i, 3, L"(?#") == 0) {
                auto end = source.find(L')', i + 3);
                if (end == std::wstring::npos)
                    end = source.size() - 1;
                out += source.substr(i, end - i + 1);
                i = end + 1;
                continue;
            }
            if (source.compare(i, 2, L"(?") == 0) {
                auto end = i + 2;
                Mode changed = mode;
                bool enable = true, option = false;
                while (end < source.size() &&
                       std::wstring(L"imsx-").find(source[end]) != std::wstring::npos) {
                    if (source[end] == L'-')
                        enable = false;
                    else {
                        option = true;
                        if (source[end] == L'x')
                            changed.extended = enable;
                    }
                    ++end;
                }
                if (option && end < source.size() && (source[end] == L')' || source[end] == L':')) {
                    if (source[end] == L':')
                        groups.push_back(mode);
                    mode = changed;
                    out += source.substr(i, end - i + 1);
                    i = end + 1;
                    continue;
                }
            }
            groups.push_back(mode);
            if (source.compare(i, 4, L"(?<=") == 0 || source.compare(i, 4, L"(?<!") == 0)
                mode.lookbehind = true;
        }
        if (!quoted && c == L'[') {
            const auto end = classEnd(source, i);
            if (end != std::wstring::npos) {
                out += classAtom(source.substr(i, end - i + 1), mode.lookbehind);
                i = end + 1;
                continue;
            }
        }
        if (c == L'\\' && i + 1 < source.size()) {
            const auto escape = source[i + 1];
            if (escape == L'Q') {
                quoted = true;
                i += 2;
                continue;
            }
            auto token = readToken(source, i);
            if (token.cp >= 0x10000) {
                out += L"(?:" + pairRange(static_cast<uint32_t>(token.cp), static_cast<uint32_t>(token.cp)) +
                       L")";
            } else if (!mode.lookbehind &&
                       std::wstring(L"wWdDsShHvVlLuUpP").find(escape) != std::wstring::npos)
                out += scalarAtom(token.text);
            else
                out += token.text;
            continue;
        }
        if (!quoted && c >= 0xD800 && c <= 0xDBFF && i + 1 < source.size() && source[i + 1] >= 0xDC00 &&
            source[i + 1] <= 0xDFFF) {
            auto token = readToken(source, i);
            out += L"(?:" + token.text + L")";
            continue;
        }
        if (!quoted && c == L'.' && !mode.lookbehind)
            out += scalarAtom(L".");
        else
            out += c;
        ++i;
        if (!quoted && c == L')' && !groups.empty()) {
            mode = groups.back();
            groups.pop_back();
        }
    }
    return out;
}
