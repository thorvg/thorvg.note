#pragma once

#include <chrono>
#include <fstream>
#include <iterator>
#include <random>
#include <stdexcept>
#include <thread>
#include <common/doctest/doctest.h>
#include <storage/storage.h>

namespace storage::test
{
struct Directory
{
    Path path;
    Directory()
    {
        std::random_device random;
        for (unsigned attempt = 0; attempt < 32; ++attempt) {
            auto candidate = std::filesystem::temp_directory_path() / ("thornote-storage-test-" + std::to_string(random()) + "-" + std::to_string(random()));
            if (std::filesystem::create_directory(candidate)) {
                path = std::filesystem::canonical(candidate);
                return;
            }
        }
        throw std::runtime_error("cannot create test directory");
    }
    ~Directory()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
    Directory(const Directory&) = delete;
    Directory& operator=(const Directory&) = delete;
    Path file(const char* name = "sample.txt") const { return path / name; }
    size_t entries() const { return size_t(std::distance(std::filesystem::directory_iterator(path), std::filesystem::directory_iterator{})); }
};

inline void write(const Path& path, std::string_view bytes)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(bytes.data(), std::streamsize(bytes.size()));
    file.close();
    if (!file) throw std::runtime_error("cannot write fixture");
}

inline std::string read(const Path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("cannot read fixture");
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

inline SaveOptions options(const LoadedFile& loaded)
{
    return {loaded.format, loaded.stamp};
}

inline bool drain(FileService& service, const std::function<bool()>& complete)
{
    auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!complete() && std::chrono::steady_clock::now() < until) {
        service.poll();
        if (!complete()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return complete();
}
}  // namespace storage::test
