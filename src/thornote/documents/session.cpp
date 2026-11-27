#include "session.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#ifdef __EMSCRIPTEN__
#include <thornote/platform/emscripten/browser_files.h>
#endif

namespace tn
{
namespace
{
constexpr size_t manifestLimit = 1024 * 1024;
bool sameContent(const SessionDocument& a, const SessionDocument& b)
{
    return a.content.revision == b.content.revision && a.content.originalPath == b.content.originalPath && a.content.title == b.content.title && a.content.format.ending == b.content.format.ending && a.content.format.bom == b.content.format.bom && a.hadFile == b.hadFile && a.diskDigest == b.diskDigest && a.dirty == b.dirty && a.uncertain == b.uncertain;
}
SessionDocument metadata(const SessionDocument& value)
{
    SessionDocument result;
    result.id = value.id;
    result.content.id = value.content.id;
    result.content.title = value.content.title;
    result.content.originalPath = value.content.originalPath;
    result.content.revision = value.content.revision;
    result.content.format = value.content.format;
    result.diskDigest = value.diskDigest;
    result.hadFile = value.hadFile;
    result.dirty = value.dirty;
    result.uncertain = value.uncertain;
    return result;
}
}  // namespace

std::string encodeSession(const SessionState& value)
{
    std::ostringstream out;
    out << std::setprecision(std::numeric_limits<double>::max_digits10);
    out << "thornote-session 1\nselections";
    for (auto id : value.selections)
        out << ' ' << id;
    out << "\nwindow " << value.width << ' ' << value.height
        << "\nsettings " << value.settingsOpen << ' ' << value.settingsActive << ' ' << value.settingsCategory
        << "\nnotes " << value.commands << ' ' << std::quoted(value.collection) << ' ' << std::quoted(value.noteQuery) << '\n';
    if (value.folderWorkspace) out << "folders 1\n";
    for (const auto& doc : value.documents) {
        out << "document " << doc.id << ' ' << std::quoted(doc.content.id) << ' ' << std::quoted(utf8(doc.content.originalPath)) << ' '
            << std::quoted(doc.content.title) << ' ' << doc.content.revision << ' ' << int(doc.content.format.ending) << ' ' << doc.content.format.bom << ' '
            << doc.hadFile << ' ' << doc.dirty << ' ' << doc.uncertain << ' ' << doc.favorite << ' ' << unsigned(doc.layouts) << ' '
            << doc.anchor << ' ' << doc.caret << ' ' << doc.scrollX << ' ' << doc.scrollY;
        for (auto byte : doc.diskDigest)
            out << ' ' << unsigned(byte);
        out << '\n';
    }
    return out.str();
}

bool decodeSession(std::string_view text, SessionState& result)
{
    std::istringstream in{std::string(text)};
    SessionState value;
    std::string key;
    unsigned version = 0;
    if (!(in >> key >> version) || key != "thornote-session" || version != 1) return false;
    std::set<uint64_t> ids;
    std::set<std::string> snapshots;
    std::set<storage::Path> paths;
    while (in >> key) {
        if (key == "active") {
            // Sessions before layouts selected one document for every layout.
            uint64_t id;
            if (!(in >> id)) return false;
            value.selections.fill(id);
        } else if (key == "selections") {
            for (auto& id : value.selections)
                if (!(in >> id)) return false;
        } else if (key == "window") {
            if (!(in >> value.width >> value.height) || !std::isfinite(value.width) || !std::isfinite(value.height) || value.width < 1 || value.height < 1 || value.width > 32768 || value.height > 32768) return false;
        } else if (key == "settings") {
            if (!(in >> value.settingsOpen >> value.settingsActive >> value.settingsCategory) || value.settingsCategory < 0 || value.settingsCategory > 2 || (value.settingsActive && !value.settingsOpen)) return false;
        } else if (key == "notes") {
            if (!(in >> value.commands >> std::quoted(value.collection) >> std::quoted(value.noteQuery))) return false;
            if (value.collection != "all" && value.collection != "favorites" && value.collection != "unsaved" && value.collection.compare(0, 7, "folder:") != 0) return false;
        } else if (key == "folders") {
            if (!(in >> value.folderWorkspace)) return false;
        } else if (key == "document") {
            SessionDocument doc;
            std::string path;
            int ending;
            unsigned layouts;
            if (!(in >> doc.id >> std::quoted(doc.content.id) >> std::quoted(path) >> std::quoted(doc.content.title) >> doc.content.revision >> ending >> doc.content.format.bom
                  >> doc.hadFile >> doc.dirty >> doc.uncertain >> doc.favorite >> layouts >> doc.anchor >> doc.caret >> doc.scrollX >> doc.scrollY)) return false;
            if (!doc.id || doc.id >= std::numeric_limits<uint64_t>::max() - 1 || !ids.insert(doc.id).second || doc.content.id.size() != 32 || doc.content.id.find_first_not_of("0123456789abcdef") != std::string::npos || !snapshots.insert(doc.content.id).second || ending < 0 || ending > 3 || layouts >> layoutCount || value.documents.size() >= 2048 || !std::isfinite(doc.scrollX) || !std::isfinite(doc.scrollY) || doc.scrollX < 0 || doc.scrollY < 0) return false;
            doc.layouts = uint8_t(layouts);
            doc.content.originalPath = utf8Path(path);
            if (!path.empty() && (!doc.content.originalPath.is_absolute() || !paths.insert(doc.content.originalPath.lexically_normal()).second)) return false;
            doc.content.format.ending = storage::LineEnding(ending);
            for (auto& byte : doc.diskDigest) {
                unsigned number;
                if (!(in >> number) || number > 255) return false;
                byte = uint8_t(number);
            }
            value.documents.push_back(std::move(doc));
        } else return false;
    }
    if (!in.eof() || std::any_of(value.selections.begin(), value.selections.end(), [&](auto id) { return id && !ids.count(id); })) return false;
    result = std::move(value);
    return true;
}

SessionStore::SessionStore(std::function<void()> wake) :
    queue(std::move(wake)) {}

SessionState SessionStore::load(const storage::Path& path)
{
#ifdef __EMSCRIPTEN__
    if (browserPending) {
        message = "Cannot load session while recovery is being saved";
        return {};
    }
#endif
    finish();
    blobs.clear();
    message.clear();
    writable = autosave.enabled = false;
    lastSucceeded = false;
    SessionState result;
    if (path.empty()) {
        message = "Session folder is unavailable.";
        return result;
    }
    std::error_code ec;
    directory = std::filesystem::absolute(path, ec);
    if (!ec) std::filesystem::create_directories(directory, ec);
    if (ec) {
        message = "Cannot create session folder: " + ec.message();
        return result;
    }
    buffers = storage::RecoveryStore(directory / "buffers");
    auto manifest = storage::load(directory / "session.conf", {true, manifestLimit});
    if (!manifest) {
        message = "Cannot read session: " + manifest.error.message;
        return result;
    }
    stamp = manifest.value.stamp;
    if (stamp.exists() && !decodeSession(manifest.value.text, result)) {
        message = "Invalid session.conf; the saved session has been left untouched.";
        return {};
    }
    writable = true;
    auto documents = std::move(result.documents);
    for (auto& doc : documents) {
        auto recovery = buffers.load(doc.content.id);
        if (!recovery || recovery.value.originalPath != doc.content.originalPath || recovery.value.title != doc.content.title || recovery.value.revision != doc.content.revision || recovery.value.format.ending != doc.content.format.ending || recovery.value.format.bom != doc.content.format.bom) {
            message = "Some session buffers could not be restored; the saved session has been left untouched.";
            writable = false;
            continue;
        }
        doc.content = std::move(recovery.value);
        blobs.emplace(doc.id, metadata(doc));
        result.documents.push_back(std::move(doc));
    }
    for (auto& id : result.selections)
        if (std::none_of(result.documents.begin(), result.documents.end(), [&](const auto& doc) { return doc.id == id; })) id = 0;
    lastSucceeded = autosave.enabled = writable;
    return result;
}

std::string SessionStore::write(SessionState state)
{
    // Buffers are written only while the manifest still matches its baseline.
    auto manifest = storage::load(directory / "session.conf", {true, manifestLimit});
    if (!manifest) return "Cannot save session: " + manifest.error.message;
    if (manifest.value.stamp.exists() != stamp.exists() || manifest.value.stamp.digest() != stamp.digest()) return "Cannot save session: session.conf changed on disk";
    std::map<uint64_t, SessionDocument> next;
    for (auto& doc : state.documents) {
        auto found = blobs.find(doc.id);
        if (found != blobs.end() && sameContent(found->second, doc)) {
            doc.content.id = found->second.content.id;
            next.emplace(doc.id, found->second);
        } else {
            // A new record keeps the previous buffer intact until the manifest commits.
            doc.content.id.clear();
            auto saved = buffers.save(doc.content);
            if (!saved) return "Cannot save session buffer: " + saved.error.message;
            next.emplace(doc.id, metadata(doc));
        }
    }
    auto saved = storage::save(directory / "session.conf", encodeSession(state), {{}, stamp, manifestLimit});
    if (!saved) return "Cannot save session: " + saved.error.message;
    stamp = saved.stamp;
    // Only remove old buffers after the new manifest has committed successfully.
    for (const auto& entry : blobs) {
        auto found = next.find(entry.first);
        if (found == next.end() || found->second.content.id != entry.second.content.id) buffers.remove(entry.second.content.id);
    }
    blobs = std::move(next);
    return {};
}

bool SessionStore::submit(SessionState state)
{
    if (!writable || writing) return false;
#ifdef __EMSCRIPTEN__
    auto next = std::make_shared<SessionState>(std::move(state));
    auto lifetime = std::weak_ptr<bool>(browserLifetime);
    BrowserRecoveryCallbacks callbacks;
    callbacks.prepare = [this, lifetime] {
        if (lifetime.expired()) return false;
        auto manifest = storage::load(directory / "session.conf", {true, manifestLimit});
        if (!manifest) message = "Cannot save session: " + manifest.error.message;
        else if (manifest.value.stamp.exists() != stamp.exists() || manifest.value.stamp.digest() != stamp.digest()) message = "Cannot save session: session.conf changed on disk";
        else return true;
        return false;
    };
    callbacks.commit = [this, lifetime, next] {
        if (lifetime.expired()) return false;
        auto saved = storage::save(directory / "session.conf", encodeSession(*next), {{}, stamp, manifestLimit});
        if (!saved) {
            message = "Cannot save session: " + saved.error.message;
            return false;
        }
        stamp = saved.stamp;
        blobs.clear();
        for (const auto& doc : next->documents)
            blobs.emplace(doc.id, metadata(doc));
        return true;
    };
    callbacks.complete = [this, lifetime](std::string error) {
        if (lifetime.expired()) return;
        writing = false;
        browserPending = false;
        browserCompleted = true;
        lastSucceeded = error.empty();
        if (lastSucceeded) message.clear();
        else if (message.empty()) message = "Cannot save session buffer: " + error;
    };
    auto pending = browserRecoveryStart(directory, std::move(callbacks));
    if (pending) {
        writing = true;
        browserPending = true;
        message.clear();
        std::set<std::string> retained;
        for (auto& doc : next->documents) {
            auto found = blobs.find(doc.id);
            if (found != blobs.end() && sameContent(found->second, doc)) doc.content.id = found->second.content.id;
            else doc.content.id = browserRecoveryAdd(pending, doc.content);
            retained.insert(doc.content.id);
            std::string{}.swap(doc.content.text);
        }
        std::string obsolete;
        for (const auto& [id, doc] : blobs)
            if (!retained.count(doc.content.id)) obsolete += doc.content.id + '\n';
        browserRecoverySubmit(pending, obsolete);
        return true;
    }
    state = std::move(*next);
#endif
    writing = true;
    queue.submit([this, state = std::move(state)](task::TaskContext&) mutable -> task::TaskQueue::Completion {
        return [this, error = write(std::move(state))]() mutable {
            writing = false;
            lastSucceeded = error.empty();
            message = std::move(error);
        };
    });
    return true;
}

bool SessionStore::checkpoint(const std::function<SessionState(bool contents)>& capture, uint64_t now, bool force, std::string& error)
{
    if (!autosave.enabled || syncing) return false;
    if (force && writing) {
        if (finish()) autosave.saved = submitted;
        else error = message;
    }
    auto signature = encodeSession(capture(false));
    if (!autosave.due(signature, now, force) || (!force && writing)) return true;
    if (!submit(capture(true))) {
        error = message;
        return false;
    }
    submitted = std::move(signature);
    autosave.at = 0;
    if (!force) return true;
    if (!finish()) {
        error = message;
        return false;
    }
    autosave.saved = submitted;
    return true;
}

bool SessionStore::poll(const Persist& persist, std::string& error)
{
#ifdef __EMSCRIPTEN__
    auto completed = browserCompleted;
    browserCompleted = false;
    if (queue.poll() == 0 && !completed) return false;
#else
    if (queue.poll() == 0) return false;
#endif
    if (!message.empty()) error = message;
    else if (persist) {
        syncing = true;
        persist([this, &error, signature = submitted](const std::string& failure) {
            syncing = false;
            if (failure.empty()) autosave.saved = signature;
            else error = "Recovery could not be saved: " + failure;
        });
    } else autosave.saved = submitted;
    return true;
}

bool SessionStore::finish()
{
#ifdef __EMSCRIPTEN__
    if (browserPending) return false;
#endif
    queue.wait();
    queue.poll();
    return lastSucceeded;
}
}  // namespace tn
