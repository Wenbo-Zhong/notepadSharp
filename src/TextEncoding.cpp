#include "TextEncoding.h"
#include <cstdint>
#include <stdexcept>
#include <vector>
namespace {
void append(std::string &out, uint32_t cp) {
    if (cp < 0x80)
        out += static_cast<char>(cp);
    else if (cp < 0x800) {
        out += static_cast<char>(0xc0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 63));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xe0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 63));
        out += static_cast<char>(0x80 | (cp & 63));
    } else {
        out += static_cast<char>(0xf0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 63));
        out += static_cast<char>(0x80 | ((cp >> 6) & 63));
        out += static_cast<char>(0x80 | (cp & 63));
    }
}
std::vector<uint32_t> codepoints(const std::string &text) {
    std::vector<uint32_t> out;
    for (size_t i = 0; i < text.size();) {
        unsigned char c = text[i++];
        uint32_t cp;
        unsigned n;
        if (c < 0x80) {
            cp = c;
            n = 0;
        } else if (c >= 0xc2 && c <= 0xdf) {
            cp = c & 31;
            n = 1;
        } else if (c >= 0xe0 && c <= 0xef) {
            cp = c & 15;
            n = 2;
        } else if (c >= 0xf0 && c <= 0xf4) {
            cp = c & 7;
            n = 3;
        } else
            throw std::runtime_error("Invalid UTF-8");
        if (i + n > text.size())
            throw std::runtime_error("Truncated UTF-8");
        for (unsigned j = 0; j < n; ++j) {
            unsigned char d = text[i++];
            if ((d & 0xc0) != 0x80)
                throw std::runtime_error("Invalid UTF-8 continuation");
            cp = (cp << 6) | (d & 63);
        }
        if ((n == 1 && cp < 0x80) || (n == 2 && cp < 0x800) || (n == 3 && cp < 0x10000) || cp > 0x10ffff ||
            (cp >= 0xd800 && cp <= 0xdfff))
            throw std::runtime_error("Invalid Unicode scalar");
        out.push_back(cp);
    }
    return out;
}
} // namespace
namespace TextEncoding {
bool decode(const std::string &bytes, std::string &text, std::string &encoding, std::string &error) {
    text.clear();
    error.clear();
    try {
        const auto byte = [&](size_t i) { return static_cast<unsigned char>(bytes[i]); };
        if (bytes.size() >= 2 &&
            ((byte(0) == 0xff && byte(1) == 0xfe) || (byte(0) == 0xfe && byte(1) == 0xff))) {
            const bool le = byte(0) == 0xff;
            encoding = le ? "UTF-16 LE" : "UTF-16 BE";
            if (bytes.size() % 2)
                throw std::runtime_error("Odd UTF-16 byte length");
            auto unit = [&](size_t i) -> uint32_t {
                return le ? byte(i) | (byte(i + 1) << 8) : (byte(i) << 8) | byte(i + 1);
            };
            for (size_t i = 2; i < bytes.size(); i += 2) {
                uint32_t cp = unit(i);
                if (cp >= 0xd800 && cp <= 0xdbff) {
                    if (i + 3 >= bytes.size())
                        throw std::runtime_error("Truncated UTF-16 surrogate");
                    uint32_t lo = unit(i + 2);
                    if (lo < 0xdc00 || lo > 0xdfff)
                        throw std::runtime_error("Invalid UTF-16 surrogate");
                    cp = 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00);
                    i += 2;
                } else if (cp >= 0xdc00 && cp <= 0xdfff)
                    throw std::runtime_error("Unpaired UTF-16 surrogate");
                append(text, cp);
            }
            return true;
        }
        const bool bom = bytes.size() >= 3 && byte(0) == 0xef && byte(1) == 0xbb && byte(2) == 0xbf;
        text = bom ? bytes.substr(3) : bytes;
        encoding = bom ? "UTF-8 BOM" : "UTF-8";
        try {
            codepoints(text);
        } catch (...) {
            if (bom)
                throw;
            encoding = "Latin-1";
            text.clear();
            for (unsigned char c : bytes)
                append(text, c);
        }
        return true;
    } catch (const std::exception &e) {
        error = e.what();
        text.clear();
        return false;
    }
}
bool encode(const std::string &text, const std::string &encoding, std::string &bytes, std::string &error) {
    bytes.clear();
    error.clear();
    try {
        auto cps = codepoints(text);
        if (encoding == "UTF-8" || encoding == "UTF-8 BOM") {
            bytes = (encoding == "UTF-8 BOM" ? "\xef\xbb\xbf" : "") + text;
            return true;
        }
        if (encoding == "Latin-1") {
            for (auto cp : cps) {
                if (cp > 255)
                    throw std::runtime_error(
                        "Character cannot be represented in Latin-1; choose UTF-8 or UTF-16");
                bytes += static_cast<char>(cp);
            }
            return true;
        }
        if (encoding != "UTF-16 LE" && encoding != "UTF-16 BE")
            throw std::runtime_error("Unsupported encoding");
        const bool le = encoding == "UTF-16 LE";
        bytes = le ? "\xff\xfe" : "\xfe\xff";
        auto unit = [&](uint32_t cp) {
            bytes += static_cast<char>(le ? cp & 255 : cp >> 8);
            bytes += static_cast<char>(le ? cp >> 8 : cp & 255);
        };
        for (auto cp : cps) {
            if (cp < 0x10000)
                unit(cp);
            else {
                cp -= 0x10000;
                unit(0xd800 + (cp >> 10));
                unit(0xdc00 + (cp & 1023));
            }
        }
        return true;
    } catch (const std::exception &e) {
        error = e.what();
        bytes.clear();
        return false;
    }
}
} // namespace TextEncoding
