#pragma once

#include <storage/storage.h>
#include <utility>

namespace tn
{
// FileService scans on its worker; poll() applies results on the owner thread.
class NoteBrowser
{
public:
    explicit NoteBrowser(std::function<void()> wake = {}) :
        service(std::move(wake)) {}
    void show(const std::vector<storage::Path>& folders, bool force = false);
    bool poll();
    bool loading() const { return pending != 0; }
    const std::vector<storage::DirectoryEntry>& files() const { return entries; }
    uint64_t revision() const { return filesVersion; }
    const std::string& error() const { return message; }

private:
    std::vector<storage::Path> folders;
    std::vector<storage::DirectoryEntry> entries, incoming;
    std::string message;
    size_t pending = 0;
    uint64_t generation = 0;
    uint64_t filesVersion = 1;
    bool needsSort = false;
    // Join the worker before destroying captured state.
    storage::FileService service;
};

class NoteFileList
{
public:
    struct Row
    {
        std::optional<size_t> file;
        std::string date;
        float top, height;
    };
    bool update(const std::vector<storage::DirectoryEntry>&, uint64_t revision, std::string_view query);
    std::pair<size_t, size_t> visibleRows(float top, float height) const;
    const std::vector<Row>& rows() const { return entries; }
    size_t count() const { return matches; }
    float height() const { return contentHeight; }

private:
    std::optional<uint64_t> revision;
    std::string query;
    std::vector<Row> entries;
    size_t matches = 0;
    float contentHeight = 0;
};

std::string creationDate(const std::optional<int64_t>& milliseconds);
}  // namespace tn
