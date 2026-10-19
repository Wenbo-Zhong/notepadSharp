#include "App.h"
#include "Scintilla.h"
#include "TextEncoding.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif
namespace fs = std::filesystem;
bool App::acquireStateLock() {
    if (host_.stateDirectory().empty())
        return false;
    const auto directory = fs::u8path(host_.stateDirectory());
    std::error_code ec;
    fs::create_directories(directory, ec);
    if (ec)
        return false;
#ifdef _WIN32
    HANDLE lock = CreateFileW((directory / "session.lock").c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (lock == INVALID_HANDLE_VALUE)
        return false;
    stateLock_ = reinterpret_cast<intptr_t>(lock);
#else
    int lock = open((directory / "session.lock").c_str(), O_CREAT | O_RDWR, 0600);
    if (lock < 0)
        return false;
    if (flock(lock, LOCK_EX | LOCK_NB) != 0) {
        close(lock);
        return false;
    }
    stateLock_ = lock;
#endif
    return true;
}
App::~App() {
    if (stateLock_ == -1)
        return;
#ifdef _WIN32
    CloseHandle(reinterpret_cast<HANDLE>(stateLock_));
#else
    close(static_cast<int>(stateLock_));
#endif
}
std::string App::documentText(int doc) {
    auto length = sendTo(doc, SCI_GETLENGTH);
    std::string text(static_cast<size_t>(length + 1), '\0');
    sendTo(doc, SCI_GETTEXT, length + 1, reinterpret_cast<intptr_t>(text.data()));
    text.resize(static_cast<size_t>(length));
    return text;
}
bool App::writeState(const std::string &path, bool snapshot) {
    if (path.empty())
        return false;
    if (snapshot) {
        intptr_t total = 0;
        for (const auto &doc : docs_) {
            if (!doc.transient)
                total += sendTo(doc.id, SCI_GETLENGTH);
            if (total > 64 * 1024 * 1024)
                return false;
        }
    }
    std::ostringstream out;
    out << "NotepadSharpSession3\n"
        << fontSize_ << ' ' << fontWeight_ << ' ' << fontItalic_ << ' ' << zoom_ << ' ' << wrap_ << ' '
        << lineNumbers_ << ' ' << whitespace_ << ' ' << eolVisible_ << '\n';
    std::vector<const Doc *> saved;
    for (const auto &doc : docs_)
        if (!doc.transient && (snapshot || !doc.path.empty()))
            saved.push_back(&doc);
    size_t selected = 0;
    for (size_t i = 0; i < saved.size(); ++i)
        if (active() && active()->id == saved[i]->id)
            selected = i;
    out << snapshot << ' ' << saved.size() << ' ' << selected << '\n';
    for (const auto *doc : saved)
        out << std::quoted(doc->path) << ' ' << std::quoted(doc->encoding) << ' ' << doc->modified << ' '
            << sendTo(doc->id, SCI_GETCURRENTPOS) << ' ' << sendTo(doc->id, SCI_GETEOLMODE) << ' '
            << doc->language << ' '
            << (doc->monitoring ? doc->monitorReadOnly : (sendTo(doc->id, SCI_GETREADONLY) != 0)) << ' '
            << std::quoted(snapshot ? documentText(doc->id) : std::string()) << '\n';
    out << "AutoComplete " << autoComplete_ << '\n';
    out << "RecentFiles " << recentFiles_.size() << '\n';
    for (const auto &path : recentFiles_)
        out << std::quoted(path) << '\n';
    const auto contents = out.str();
    if (contents.size() > 64 * 1024 * 1024)
        return false;
    auto target = fs::u8path(path);
    auto temporary = target;
    temporary += ".tmp";
    std::error_code ec;
    if (!target.parent_path().empty())
        fs::create_directories(target.parent_path(), ec);
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    if (!file)
        return false;
    file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    file.close();
    if (!file)
        return false;
#ifdef _WIN32
    return MoveFileExW(temporary.c_str(), target.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
#else
    fs::rename(temporary, target, ec);
    return !ec;
#endif
}
bool App::readState(const std::string &path, bool confirm) {
    std::error_code ec;
    const auto size = fs::file_size(fs::u8path(path), ec);
    if (ec || size > 64 * 1024 * 1024)
        return false;
    std::ifstream in(fs::u8path(path), std::ios::binary);
    std::string magic;
    std::getline(in, magic);
    if (magic != "NotepadSharpSession2" && magic != "NotepadSharpSession3")
        return false;
    int fontSize, weight, zoom;
    bool italic, wrap, numbers, whitespace, eolVisible, snapshot;
    size_t count, selected;
    if (!(in >> fontSize >> weight >> italic >> zoom >> wrap >> numbers >> whitespace >> eolVisible >>
          snapshot >> count >> selected) ||
        count > 1000 || fontSize < 6 || fontSize > 72 || weight < 100 || weight > 800 || zoom < -10 ||
        zoom > 100)
        return false;
    struct Saved {
        std::string path, encoding, text;
        bool modified;
        intptr_t pos;
        int eol;
        int language = -1;
        bool readOnly = false;
    };
    std::vector<Saved> saved;
    for (size_t i = 0; i < count; ++i) {
        Saved item;
        if (!(in >> std::quoted(item.path) >> std::quoted(item.encoding) >> item.modified >> item.pos >>
              item.eol))
            return false;
        if (magic == "NotepadSharpSession3" && !(in >> item.language >> item.readOnly))
            return false;
        if (!(in >> std::quoted(item.text)) || item.language < -1 || item.language >= languageChoiceCount)
            return false;
        if (item.eol < 0 || item.eol > 2 || item.pos < 0 ||
            (item.encoding != "UTF-8" && item.encoding != "UTF-8 BOM" && item.encoding != "UTF-16 LE" &&
             item.encoding != "UTF-16 BE" && item.encoding != "Latin-1"))
            return false;
        std::string checked, error;
        if (snapshot && !TextEncoding::encode(item.text, "UTF-8", checked, error))
            return false;
        saved.push_back(std::move(item));
    }
    bool complete = true;
    std::string feature;
    if (in >> feature) {
        if (feature != "AutoComplete" || !(in >> complete))
            return false;
    }
    std::vector<std::string> recent;
    if (in >> feature) {
        size_t count = 0;
        if (feature != "RecentFiles" || !(in >> count) || count > 20)
            return false;
        for (size_t i = 0; i < count; ++i) {
            std::string path;
            if (!(in >> std::quoted(path)) || path.size() > 32768)
                return false;
            recent.push_back(std::move(path));
        }
    }
    // Parse the whole file before asking to close existing work.
    if (confirm && !onCloseRequested())
        return true;
    restoring_ = true;
    auto old = std::move(docs_);
    docs_.clear();
    active_ = -1;
    for (const auto &doc : old)
        host_.destroyEditor(doc.id);
    autoComplete_ = complete;
    fontSize_ = fontSize;
    fontWeight_ = weight;
    fontItalic_ = italic;
    zoom_ = zoom;
    wrap_ = wrap;
    lineNumbers_ = numbers;
    whitespace_ = whitespace;
    eolVisible_ = eolVisible;
    int selectedId = 0;
    for (size_t i = 0; i < saved.size(); ++i) {
        const auto &item = saved[i];
        if (snapshot) {
            onCommand(CmdNew);
            auto *doc = active();
            doc->path = item.path;
            doc->encoding = item.encoding;
            doc->savedEncoding = item.encoding;
            send(SCI_ADDTEXT, item.text.size(), reinterpret_cast<intptr_t>(item.text.data()));
            send(SCI_EMPTYUNDOBUFFER);
            if (!item.modified)
                send(SCI_SETSAVEPOINT);
            doc->modified = item.modified;
            applyLexer(*doc);
            applyView(doc->id);
            send(SCI_SETEOLMODE, item.eol);
        } else {
            if (item.path.empty() || !fs::is_regular_file(fs::u8path(item.path), ec))
                continue;
            openPath(item.path);
        }
        if (active()) {
            observeDisk(*active());
            if (snapshot && !active()->path.empty())
                active()->diskChanged = true;
            active()->language = item.language;
            applyLexer(*active());
            applyView(active()->id);
            send(SCI_SETREADONLY, item.readOnly);
            send(SCI_GOTOPOS, std::min<intptr_t>(item.pos, send(SCI_GETLENGTH)));
            if (i == selected)
                selectedId = active()->id;
        }
    }
    if (docs_.empty())
        onCommand(CmdNew);
    if (selectedId)
        onTabChanged(selectedId);
    if (!recent.empty())
        recentFiles_ = std::move(recent);
    restoring_ = false;
    touchTitles();
    return true;
}
void App::checkpoint() {
    if (restoring_ || stateLock_ == -1 || host_.stateDirectory().empty())
        return;
    const bool success = writeState((fs::u8path(host_.stateDirectory()) / "recovery.nss").u8string(), true);
    if (!success && !checkpointFailed_)
        host_.alert("恢复快照写入失败：请检查配置目录是否可写，或会话是否超过 64 MiB。");
    checkpointFailed_ = !success;
}
void App::persistCleanState() {
    if (restoring_ || stateLock_ == -1 || host_.stateDirectory().empty())
        return;
    const auto directory = fs::u8path(host_.stateDirectory());
    if (writeState((directory / "session.nss").u8string(), false)) {
        std::error_code ec;
        fs::remove(directory / "recovery.nss", ec);
    }
}
