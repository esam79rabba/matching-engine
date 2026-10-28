#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace gateway {

/// Maximum accepted FIX BodyLength (tag 9) and total frame size.
inline constexpr size_t kFixMaxBodyLength = 1024 * 1024;  // 1 MiB

enum class FrameStatus {
    Complete,  ///< A full message is available; `length` bytes at buf start.
    NeedMore,  ///< Buffer holds a valid prefix of a message; read more bytes.
    Error,     ///< Stream is corrupt (bad BeginString/BodyLength/trailer).
};

struct FrameResult {
    FrameStatus status = FrameStatus::NeedMore;
    size_t      length = 0;  ///< Valid only when status == Complete.
};

/// Inspect the front of `buf` and report whether it holds one complete FIX
/// message. Pure function; does not consume anything.
///
/// Framing rules:
///   - Message must start with "8=FIX".
///   - If the field after BeginString is "9=<N>", the message ends exactly
///     N bytes after that field's delimiter plus the 7-byte trailer
///     "10=XXX<delim>". The trailer is verified. N must be all digits
///     and <= kFixMaxBodyLength, otherwise Error.
///   - If tag 9 is absent (non-conforming peers / legacy test fixtures), the
///     message is delimited by the first "<delim>10=XXX<delim>" trailer.
///   - Delimiter is SOH (0x01); '|' is also accepted for diagnostics.
FrameResult frame_next_message(std::string_view buf);

/// Per-connection stream reassembler. Feed raw bytes with append(); pull
/// complete messages with next().
class FixStreamFramer {
public:
    void append(const char* data, size_t len) { buf_.append(data, len); }

    /// Extract the next complete message into `out` and consume its bytes.
    /// Returns Complete, NeedMore (nothing consumed), or Error (stream is
    /// unrecoverable; caller should close the connection).
    FrameStatus next(std::string& out);

    [[nodiscard]] size_t buffered() const { return buf_.size(); }

private:
    std::string buf_;
};

}  // namespace gateway
