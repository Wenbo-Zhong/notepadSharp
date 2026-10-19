#include "IcuRegex.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <CoreFoundation/CoreFoundation.h>
#else
#include <unistd.h>
#endif
#include <unicode/uregex.h>
#include <unicode/ustring.h>
#include <unicode/utext.h>
#include <unicode/udata.h>
#include <unicode/uchar.h>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <stdexcept>

namespace {
void check(UErrorCode status) {
    if (U_FAILURE(status))
        throw std::runtime_error(u_errorName(status));
}
void initializeData() {
    static std::once_flag once;
    std::call_once(once, [] {
        // ICU requires aligned, process-lifetime data. It is embedded on Windows.
        static std::vector<std::max_align_t> storage;
        const void *data = nullptr;
        size_t size = 0;
#ifdef _WIN32
        const auto module = GetModuleHandleW(nullptr);
        const auto resource = FindResourceW(module, MAKEINTRESOURCEW(901), RT_RCDATA);
        if (!resource)
            throw std::runtime_error("Embedded ICU Unicode data is missing");
        size = SizeofResource(module, resource);
        data = LockResource(LoadResource(module, resource));
#else
        std::filesystem::path executable;
#ifdef __APPLE__
        uint32_t length = 0; _NSGetExecutablePath(nullptr, &length);
        std::string path(length, '\0');
        if (_NSGetExecutablePath(path.data(), &length)) throw std::runtime_error("Cannot locate ICU data");
        executable = path.c_str();
#else
        std::vector<char> path(4096);
        const auto length = readlink("/proc/self/exe", path.data(), path.size());
        if (length < 0 || length == static_cast<ssize_t>(path.size())) throw std::runtime_error("Cannot locate ICU data");
        executable = std::string(path.data(), length);
#endif
        std::filesystem::path dataDirectory = executable.parent_path();
#ifdef __APPLE__
        if (CFURLRef resources = CFBundleCopyResourcesDirectoryURL(CFBundleGetMainBundle())) {
            UInt8 resourcePath[PATH_MAX]{};
            if (CFURLGetFileSystemRepresentation(resources, true, resourcePath, sizeof(resourcePath)))
                dataDirectory = reinterpret_cast<const char *>(resourcePath);
            CFRelease(resources);
        }
#endif
        std::ifstream stream(dataDirectory / "unicode" / "icudt78l.dat", std::ios::binary);
        std::string bytes((std::istreambuf_iterator<char>(stream)), {});
        size = bytes.size(); data = bytes.data();
#endif
        if (!data || size < 32)
            throw std::runtime_error("ICU Unicode data is missing or invalid");
        storage.resize((size + sizeof(std::max_align_t) - 1) / sizeof(std::max_align_t));
        std::memcpy(storage.data(), data, size);
        UErrorCode status = U_ZERO_ERROR;
        udata_setCommonData(storage.data(), &status);
        check(status);
    });
}
std::u16string utf16(const std::string &text) {
    UErrorCode status = U_ZERO_ERROR;
    int32_t length = 0;
    u_strFromUTF8(nullptr, 0, &length, text.data(), static_cast<int32_t>(text.size()), &status);
    if (status != U_BUFFER_OVERFLOW_ERROR)
        check(status);
    status = U_ZERO_ERROR;
    std::u16string result(length, 0);
    u_strFromUTF8(reinterpret_cast<UChar *>(result.data()), length, nullptr, text.data(),
                  static_cast<int32_t>(text.size()), &status);
    check(status);
    return result;
}
bool word(UChar32 cp) {
    if (cp < 0)
        return false;
    const auto type = u_charType(cp);
    return u_hasBinaryProperty(cp, UCHAR_ALPHABETIC) || u_hasBinaryProperty(cp, UCHAR_JOIN_CONTROL) ||
           type == U_NON_SPACING_MARK || type == U_COMBINING_SPACING_MARK || type == U_ENCLOSING_MARK ||
           type == U_DECIMAL_DIGIT_NUMBER || type == U_CONNECTOR_PUNCTUATION;
}
} // namespace
struct IcuRegex::Impl {
    URegularExpression *regex = nullptr;
    std::string text;
    int64_t bound = -1;
    std::vector<std::pair<std::wstring, int>> names;
    ~Impl() {
        if (regex)
            uregex_close(regex);
    }
};
IcuRegex::IcuRegex(const std::string &pattern, bool matchCase, bool dotAll)
    : impl_(std::make_unique<Impl>()) {
    initializeData();
    auto source = utf16(pattern);
    UErrorCode status = U_ZERO_ERROR;
    UParseError error{};
    const uint32_t flags = UREGEX_MULTILINE | UREGEX_ERROR_ON_UNKNOWN_ESCAPES |
                           (matchCase ? 0 : UREGEX_CASE_INSENSITIVE) | (dotAll ? UREGEX_DOTALL : 0);
    impl_->regex = uregex_open(reinterpret_cast<const UChar *>(source.data()),
                               static_cast<int32_t>(source.size()), flags, &error, &status);
    if (U_FAILURE(status))
        throw std::runtime_error(std::string(u_errorName(status)) + " at UTF-16 offset " +
                                 std::to_string(error.offset));
    uregex_setTimeLimit(impl_->regex, 2000, &status);
    uregex_setStackLimit(impl_->regex, 8 * 1024 * 1024, &status);
    check(status);
    for (size_t i = 0; (i = source.find(u"(?<", i)) != std::u16string::npos; ++i) {
        const auto end = source.find(u'>', i + 3);
        if (end == std::u16string::npos)
            break;
        const auto name = source.substr(i + 3, end - i - 3);
        UErrorCode local = U_ZERO_ERROR;
        const int group =
            uregex_groupNumberFromName(impl_->regex, reinterpret_cast<const UChar *>(name.data()),
                                       static_cast<int32_t>(name.size()), &local);
        if (U_SUCCESS(local) && group > 0)
            impl_->names.emplace_back(std::wstring(name.begin(), name.end()), group);
    }
}
IcuRegex::~IcuRegex() = default;
void IcuRegex::load(const std::string &text) {
    impl_->text = text;
    impl_->bound = -1;
}
bool IcuRegex::find(int64_t start, int64_t end, std::vector<std::pair<int64_t, int64_t>> &groups) {
    UErrorCode status = U_ZERO_ERROR;
    if (impl_->bound != end) {
        UText input = UTEXT_INITIALIZER;
        utext_openUTF8(&input, impl_->text.data(), end, &status);
        uregex_setUText(impl_->regex, &input, &status);
        utext_close(&input);
        check(status);
        impl_->bound = end;
    }
    const auto found = uregex_find64(impl_->regex, start, &status);
    check(status);
    if (!found)
        return false;
    groups.clear();
    const auto count = uregex_groupCount(impl_->regex, &status);
    for (int n = 0; n <= count; ++n) {
        const auto first = uregex_start64(impl_->regex, n, &status);
        const auto last = uregex_end64(impl_->regex, n, &status);
        groups.emplace_back(first, last);
    }
    check(status);
    return true;
}
bool IcuRegex::wholeWord(int64_t start, int64_t end) const {
    if (start == end)
        return false;
    UErrorCode status = U_ZERO_ERROR;
    UText text = UTEXT_INITIALIZER;
    utext_openUTF8(&text, impl_->text.data(), static_cast<int64_t>(impl_->text.size()), &status);
    const bool result = word(utext_next32From(&text, start)) && word(utext_previous32From(&text, end)) &&
                        !word(utext_previous32From(&text, start)) && !word(utext_next32From(&text, end));
    utext_close(&text);
    check(status);
    return result;
}
const std::vector<std::pair<std::wstring, int>> &IcuRegex::names() const { return impl_->names; }
