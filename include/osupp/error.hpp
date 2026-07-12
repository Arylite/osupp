#pragma once

#include <cstddef>
#include <string>

namespace osu {

enum class ParseErrorCode {
    io_failure,
    bad_header,
    bad_field,
    bad_number,
    invalid_value,
    truncated,
    lzma_failure,
};

struct ParseError {
    ParseErrorCode code{};
    std::string message;
    std::size_t line = 0;    // 1-based line in text input; 0 when not applicable
    std::size_t offset = 0;  // byte offset in binary input; 0 when not applicable
};

enum class WriteErrorCode {
    io_failure,
    lzma_failure,
    invalid_model,
};

struct WriteError {
    WriteErrorCode code{};
    std::string message;
};

}  // namespace osu
