#include <algorithm>
#include "internal.h"

namespace storage::detail
{
namespace
{
size_t utf8Length(unsigned char first)
{
    return first < 0x80 ? 1 : first >= 0xc2 && first <= 0xdf ? 2
        : first >= 0xe0 && first <= 0xef                     ? 3
        : first >= 0xf0 && first <= 0xf4                     ? 4
                                                             : 0;
}

bool validSequence(std::string_view bytes)
{
    auto first = static_cast<unsigned char>(bytes[0]);
    for (size_t i = 1; i < bytes.size(); ++i)
        if ((static_cast<unsigned char>(bytes[i]) & 0xc0) != 0x80) return false;
    if (bytes.size() < 3) return true;
    auto second = static_cast<unsigned char>(bytes[1]);
    return !((first == 0xe0 && second < 0xa0) || (first == 0xed && second >= 0xa0) || (first == 0xf0 && second < 0x90) || (first == 0xf4 && second >= 0x90));
}

Error invalidUtf8()
{
    return failure(ErrorCode::NotText, "invalid UTF-8");
}
}  // namespace

Error Decoder::output(std::string_view bytes)
{
    if (limit && bytes.size() > *limit - result.text.size()) return failure(ErrorCode::TooLarge, "decoded text exceeds limit");
    result.text.append(bytes);
    return {};
}

Error Decoder::character(std::string_view bytes)
{
    if (bytes[0] == 0) return failure(ErrorCode::NotText, "NUL byte in text file");
    if (validationOnly) return {};
    if (cr) {
        cr = false;
        if (auto error = output("\n")) return error;
        if (bytes == "\n") {
            crlfSeen = true;
            return {};
        }
        crSeen = true;
    }
    if (bytes == "\r") {
        cr = true;
        return {};
    }
    if (bytes == "\n") lfSeen = true;
    return output(bytes);
}

Error Decoder::consume(std::string_view bytes)
{
    for (size_t i = 0; i < bytes.size(); ++i) {
        if (validationOnly && sequence.empty()) {
            while (i < bytes.size() && bytes[i] > 0 && static_cast<unsigned char>(bytes[i]) < 0x80)
                ++i;
            if (i == bytes.size()) break;
        }
        auto byte = static_cast<unsigned char>(bytes[i]);
        if (sequence.empty()) {
            auto size = utf8Length(byte);
            if (!size) return invalidUtf8();
            if (size == 1) {
                char ch = char(byte);
                if (auto error = character({&ch, 1})) return error;
                continue;
            }
            remaining = unsigned(size - 1);
        } else if ((byte & 0xc0) != 0x80) return invalidUtf8();
        else --remaining;
        sequence += char(byte);
        if (remaining == 0) {
            if (!validSequence(sequence)) return invalidUtf8();
            if (auto error = character(sequence)) return error;
            sequence.clear();
        }
    }
    return {};
}

Error Decoder::headerReady()
{
    started = true;
    std::string_view bytes = header;
    if (bytes.substr(0, 2) == "\xff\xfe" || bytes.substr(0, 2) == "\xfe\xff" || bytes.substr(0, 4) == std::string_view("\0\0\xfe\xff", 4))
        return failure(ErrorCode::NotText, "UTF-16/32 is not supported");
    if (bytes.substr(0, 3) == "\xef\xbb\xbf") {
        result.format.bom = true;
        bytes.remove_prefix(3);
    }
    return consume(bytes);
}

Error Decoder::append(std::string_view bytes)
{
    if (error) return error;
    if (validationOnly && limit) {
        if (bytes.size() > *limit - inspected) return error = failure(ErrorCode::TooLarge, "text exceeds limit");
        inspected += bytes.size();
    }
    if (!started) {
        auto count = std::min(size_t(4) - header.size(), bytes.size());
        header.append(bytes.substr(0, count));
        bytes.remove_prefix(count);
        if (header.size() < 4) return {};
        error = headerReady();
    }
    if (!error) error = consume(bytes);
    return error;
}

Result<Text> Decoder::finish()
{
    if (!started && !error) error = headerReady();
    if (!error && !sequence.empty()) error = invalidUtf8();
    if (!error && cr) {
        crSeen = true;
        error = output("\n");
    }
    if (error) return {{}, error};
    auto kinds = unsigned(lfSeen) + unsigned(crSeen) + unsigned(crlfSeen);
    result.format.ending = kinds > 1 ? LineEnding::Mixed : crlfSeen ? LineEnding::Crlf
        : crSeen                                                    ? LineEnding::Cr
                                                                    : LineEnding::Lf;
    return {std::move(result), {}};
}

Error validateOutput(std::string_view text, FileFormat format, std::optional<size_t> limit)
{
    if (format.ending == LineEnding::Mixed) return failure(ErrorCode::InvalidText, "select a uniform line ending before saving");
    auto available = limit;
    if (available) {
        size_t prefix = format.bom ? 3 : 0;
        if (prefix > *available || text.size() > *available - prefix) return failure(ErrorCode::TooLarge, "encoded text exceeds limit");
        *available -= prefix + text.size();
    }
    for (size_t i = 0; i < text.size();) {
        auto length = utf8Length(static_cast<unsigned char>(text[i]));
        if (!length || length > text.size() - i || !validSequence(text.substr(i, length))) return failure(ErrorCode::InvalidText, "invalid UTF-8");
        if (text[i] == 0) return failure(ErrorCode::InvalidText, "NUL byte in text");
        if (text[i] == '\r') return failure(ErrorCode::InvalidText, "save requires LF-normalized text");
        if (text[i] == '\n' && format.ending == LineEnding::Crlf && available) {
            if (!*available) return failure(ErrorCode::TooLarge, "encoded text exceeds limit");
            --*available;
        }
        i += length;
    }
    return {};
}

Error encodeChunks(std::string_view text, FileFormat format, const Sink& sink)
{
    if (format.bom)
        if (auto error = sink("\xef\xbb\xbf")) return error;
    std::string output;
    output.reserve(64 * 1024);
    for (char ch : text) {
        if (ch == '\n' && format.ending == LineEnding::Crlf) output += '\r';
        output += ch == '\n' && format.ending == LineEnding::Cr ? '\r' : ch;
        if (output.size() >= 64 * 1024) {
            if (auto error = sink(output)) return error;
            output.clear();
        }
    }
    return output.empty() ? Error{} : sink(output);
}
}  // namespace storage::detail
