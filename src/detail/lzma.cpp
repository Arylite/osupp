#include "detail/lzma.hpp"

#include <concepts>
#include <cstdint>
#include <format>
#include <string_view>
#include <utility>

#include <lzma.h>

namespace osu::detail::lzma {

namespace {

struct StreamGuard {
    lzma_stream stream = LZMA_STREAM_INIT;

    StreamGuard() = default;
    StreamGuard(const StreamGuard&) = delete;
    StreamGuard& operator=(const StreamGuard&) = delete;
    ~StreamGuard() { lzma_end(&stream); }
};

template <typename Error>
[[nodiscard]] std::unexpected<Error> fail(lzma_ret ret, std::string_view stage) {
    Error error{};
    error.message = std::format("lzma {} failed (code {})", stage, static_cast<int>(ret));
    if constexpr (std::same_as<Error, ParseError>) {
        error.code = ParseErrorCode::lzma_failure;
    } else {
        error.code = WriteErrorCode::lzma_failure;
    }
    return std::unexpected(std::move(error));
}

template <typename Error>
[[nodiscard]] std::expected<std::vector<std::byte>, Error> run(lzma_stream& stream,
                                                               std::span<const std::byte> input,
                                                               std::size_t max_output) {
    stream.next_in = reinterpret_cast<const std::uint8_t*>(input.data());
    stream.avail_in = input.size();

    std::vector<std::byte> output;
    constexpr std::size_t chunk = 1 << 16;
    while (true) {
        const auto used = output.size();
        if (used >= max_output) return fail<Error>(LZMA_MEM_ERROR, "output limit");
        output.resize(used + chunk);
        stream.next_out = reinterpret_cast<std::uint8_t*>(output.data() + used);
        stream.avail_out = chunk;

        const lzma_ret ret = lzma_code(&stream, LZMA_FINISH);
        output.resize(used + (chunk - stream.avail_out));
        if (ret == LZMA_STREAM_END) return output;
        if (ret != LZMA_OK) return fail<Error>(ret, "coding");
    }
}

}  // namespace

std::expected<std::vector<std::byte>, ParseError> decompress(std::span<const std::byte> input,
                                                             std::size_t max_output) {
    StreamGuard guard;
    if (const lzma_ret ret = lzma_alone_decoder(&guard.stream, UINT64_MAX); ret != LZMA_OK) {
        return fail<ParseError>(ret, "decoder init");
    }
    return run<ParseError>(guard.stream, input, max_output);
}

std::expected<std::vector<std::byte>, WriteError> compress(std::span<const std::byte> input) {
    lzma_options_lzma options{};
    if (lzma_lzma_preset(&options, LZMA_PRESET_DEFAULT)) {
        return fail<WriteError>(LZMA_OPTIONS_ERROR, "preset");
    }
    StreamGuard guard;
    if (const lzma_ret ret = lzma_alone_encoder(&guard.stream, &options); ret != LZMA_OK) {
        return fail<WriteError>(ret, "encoder init");
    }
    // Compressed replays are always smaller than frames + slack; cap generously.
    return run<WriteError>(guard.stream, input, input.size() + (1 << 20));
}

}  // namespace osu::detail::lzma
