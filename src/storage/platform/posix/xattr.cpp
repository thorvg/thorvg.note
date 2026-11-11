#include <algorithm>
#include <cerrno>
#include <vector>
#include <storage/internal.h>
#include "metadata.h"

namespace storage::detail
{
namespace
{
Error systemError(const char* message, ErrorCode code = ErrorCode::Io)
{
    return failure(code, message, {errno, std::generic_category()});
}

Result<std::vector<std::string>> attributeNames(int file)
{
    auto size = listAttributes(file, nullptr, 0);
    if (size < 0) {
        if (errno == ENOTSUP) return {};
        return {{}, systemError("cannot inspect extended attributes", ErrorCode::UnsupportedMetadata)};
    }
    std::vector<char> names(size_t(size), '\0');
    if (size && listAttributes(file, names.data(), names.size()) != size)
        return {{}, systemError("extended attributes changed", ErrorCode::UnsupportedMetadata)};
    std::vector<std::string> result;
    for (size_t offset = 0; offset < names.size();) {
        auto end = std::find(names.begin() + offset, names.end(), '\0');
        if (end == names.end()) return {{}, failure(ErrorCode::UnsupportedMetadata, "invalid attribute list")};
        result.emplace_back(names.data() + offset, size_t(end - names.begin()) - offset);
        offset = size_t(end - names.begin()) + 1;
    }
    return {std::move(result), {}};
}
}  // namespace

Error copyAttributes(int source, int output)
{
    auto names = attributeNames(source), destinationNames = attributeNames(output);
    if (!names) return names.error;
    if (!destinationNames) return destinationNames.error;
    // Do not retain permissions/attributes inherited by staging but absent from the original.
    for (const auto& name : destinationNames.value)
        if (std::find(names.value.begin(), names.value.end(), name) == names.value.end()) {
            auto status = removeAttribute(output, name.c_str());
            if (status) return systemError("cannot remove inherited attribute", ErrorCode::UnsupportedMetadata);
        }
    for (const auto& name : names.value) {
        auto count = getAttribute(source, name.c_str(), nullptr, 0);
        if (count < 0) return systemError("cannot inspect attribute", ErrorCode::UnsupportedMetadata);
        std::vector<char> value(size_t(count), '\0');
        if (getAttribute(source, name.c_str(), value.data(), value.size()) != count)
            return systemError("cannot read attribute", ErrorCode::UnsupportedMetadata);
        std::vector<char> existing(size_t(count), '\0');
        if (getAttribute(output, name.c_str(), existing.data(), existing.size()) == count && value == existing) continue;
        auto status = setAttribute(output, name.c_str(), value.data(), value.size());
        if (status)
            return systemError("cannot preserve attribute", ErrorCode::UnsupportedMetadata);
    }
    return {};
}
}  // namespace storage::detail
