#include "App.h"
#include "Scintilla.h"
#include <algorithm>
#include <filesystem>
namespace fs = std::filesystem;
bool App::monitoring() const {
    return active_ >= 0 && active_ < static_cast<int>(docs_.size()) && docs_[active_].monitoring;
}
std::vector<std::string> App::documentNames() const {
    std::vector<std::string> names;
    for (const auto &doc : docs_)
        names.push_back(displayName(doc) + (doc.path.empty() ? "" : " — " + doc.path));
    return names;
}
void App::rememberFile(const std::string &path) {
    if (restoring_ || path.empty())
        return;
    std::error_code ec;
    const auto normalized = fs::absolute(fs::u8path(path), ec).lexically_normal().u8string();
    if (ec)
        return;
    recentFiles_.erase(std::remove_if(recentFiles_.begin(), recentFiles_.end(),
                                      [&](const auto &item) {
                                          std::error_code compare;
                                          return item == normalized ||
                                                 fs::equivalent(fs::u8path(item), fs::u8path(normalized),
                                                                compare);
                                      }),
                       recentFiles_.end());
    recentFiles_.insert(recentFiles_.begin(), normalized);
    if (recentFiles_.size() > 20)
        recentFiles_.resize(20);
}
void App::observeDisk(Doc &doc) {
    if (doc.path.empty())
        return;
    std::error_code ec;
    auto path = fs::u8path(doc.path);
    auto time = fs::last_write_time(path, ec);
    if (ec)
        return;
    auto size = fs::file_size(path, ec);
    if (ec)
        return;
    doc.diskTime = static_cast<int64_t>(time.time_since_epoch().count());
    doc.diskSize = size;
    doc.diskKnown = true;
    doc.diskChanged = false;
}
bool App::diskChanged(const Doc &doc) const {
    if (!doc.diskKnown || doc.path.empty())
        return false;
    std::error_code ec;
    auto path = fs::u8path(doc.path);
    auto time = fs::last_write_time(path, ec);
    if (ec)
        return true;
    auto size = fs::file_size(path, ec);
    return ec || size != doc.diskSize ||
           static_cast<int64_t>(time.time_since_epoch().count()) != doc.diskTime;
}
void App::toggleMonitoring() {
    auto *doc = active();
    if (!doc || doc->transient)
        return;
    if (doc->monitoring) {
        doc->monitoring = false;
        send(SCI_SETREADONLY, doc->monitorReadOnly);
        updateStatus();
        return;
    }
    if (doc->path.empty() || doc->modified || send(SCI_GETMODIFY)) {
        host_.alert("请先保存文档，再启用文件监视。");
        return;
    }
    doc->monitorReadOnly = send(SCI_GETREADONLY) != 0;
    doc->monitoring = true;
    send(SCI_SETREADONLY, 1);
    send(SCI_DOCUMENTEND);
    updateStatus();
}
void App::pollFiles() {
    if (pollingFiles_ || restoring_ || macroRecording())
        return;
    pollingFiles_ = true;
    struct Guard {
        bool &flag;
        ~Guard() { flag = false; }
    } guard{pollingFiles_};
    const int previous = active() ? active()->id : 0;
    std::vector<int> ids;
    for (const auto &doc : docs_)
        if (!doc.transient && !doc.path.empty())
            ids.push_back(doc.id);
    for (int id : ids) {
        auto *doc = findDoc(id);
        if (!doc || !diskChanged(*doc))
            continue;
        std::error_code ec;
        const bool exists = fs::is_regular_file(fs::u8path(doc->path), ec) && !ec;
        // Remember this version even when the user keeps the buffer; do not prompt every tick.
        observeDisk(*doc);
        doc->diskChanged = true;
        if (!exists) {
            if (!doc->diskKnown)
                continue;
            doc->diskKnown = false;
            if (doc->monitoring) {
                doc->monitoring = false;
                sendTo(id, SCI_SETREADONLY, doc->monitorReadOnly);
            }
            host_.alert("文件已从磁盘移除或无法访问，编辑器内容仍保留。\n" + doc->path);
            continue;
        }
        const bool monitor = doc->monitoring;
        onTabChanged(id);
        const bool reload = monitor || host_.askReloadExternal(
                                           "磁盘文件已变化。是否重新载入？" +
                                           std::string(doc->modified ? "\n当前未保存的修改将被丢弃。" : "") +
                                           "\n" + doc->path);
        if (reload && findDoc(id)) {
            reloadActive(true);
            if (monitor)
                send(SCI_DOCUMENTEND);
        }
    }
    if (previous && findDoc(previous))
        onTabChanged(previous);
    updateStatus();
}
