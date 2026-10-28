#include "gateway/fix_framer.h"

namespace gateway {

namespace {

constexpr std::string_view kBeginPrefix = "8=FIX";
constexpr size_t kTrailerLen = 7;          // "10=XXX" + delimiter
constexpr size_t kMaxBeginStringLen = 32;  // "8=FIX.4.4" is 9; generous cap

inline bool is_delim(char c) { return c == '\x01' || c == '|'; }
inline bool is_digit(char c) { return c >= '0' && c <= '9'; }

/// True if buf[pos, pos+7) is "10=ddd<delim>".
bool is_trailer_at(std::string_view buf, size_t pos) {
    return buf.compare(pos, 3, "10=") == 0 && is_digit(buf[pos + 3]) &&
           is_digit(buf[pos + 4]) && is_digit(buf[pos + 5]) && is_delim(buf[pos + 6]);
}

/// No tag 9: delimit by the first "<delim>10=ddd<delim>" at or after `from`.
FrameResult frame_by_trailer(std::string_view buf, size_t from) {
    size_t pos = from;
    while ((pos = buf.find("10=", pos)) != std::string_view::npos) {
        if (pos > 0 && is_delim(buf[pos - 1])) {
            if (pos + kTrailerLen > buf.size()) return {FrameStatus::NeedMore, 0};
            if (is_trailer_at(buf, pos)) {
                return {FrameStatus::Complete, pos + kTrailerLen};
            }
        }
        ++pos;
    }
    if (buf.size() > kFixMaxBodyLength + kMaxBeginStringLen) {
        return {FrameStatus::Error, 0};
    }
    return {FrameStatus::NeedMore, 0};
}

}  // namespace

FrameResult frame_next_message(std::string_view buf) {
    if (buf.empty()) return {FrameStatus::NeedMore, 0};

    // BeginString prefix.
    const size_t cmp = buf.size() < kBeginPrefix.size() ? buf.size() : kBeginPrefix.size();
    if (buf.compare(0, cmp, kBeginPrefix.substr(0, cmp)) != 0) {
        return {FrameStatus::Error, 0};
    }
    if (buf.size() < kBeginPrefix.size()) return {FrameStatus::NeedMore, 0};

    // End of BeginString field.
    size_t i = kBeginPrefix.size();
    while (i < buf.size() && !is_delim(buf[i])) {
        if (i >= kMaxBeginStringLen) return {FrameStatus::Error, 0};
        ++i;
    }
    if (i == buf.size()) return {FrameStatus::NeedMore, 0};

    const size_t p = i + 1;  // first byte of the next field
    if (p >= buf.size()) return {FrameStatus::NeedMore, 0};

    // Expect "9=<BodyLength><delim>".
    if (buf[p] != '9') return frame_by_trailer(buf, i);
    if (p + 1 >= buf.size()) return {FrameStatus::NeedMore, 0};
    if (buf[p + 1] != '=') return frame_by_trailer(buf, i);

    size_t q = p + 2;
    size_t body_len = 0;
    while (q < buf.size() && is_digit(buf[q])) {
        body_len = body_len * 10 + static_cast<size_t>(buf[q] - '0');
        if (body_len > kFixMaxBodyLength) return {FrameStatus::Error, 0};
        ++q;
    }
    if (q == buf.size()) return {FrameStatus::NeedMore, 0};
    if (q == p + 2 || !is_delim(buf[q])) return {FrameStatus::Error, 0};

    const size_t total = q + 1 + body_len + kTrailerLen;
    if (buf.size() < total) return {FrameStatus::NeedMore, 0};

    // BodyLength must land exactly on a checksum trailer.
    if (!is_trailer_at(buf, total - kTrailerLen)) return {FrameStatus::Error, 0};
    return {FrameStatus::Complete, total};
}

FrameStatus FixStreamFramer::next(std::string& out) {
    const FrameResult r = frame_next_message(buf_);
    if (r.status == FrameStatus::Complete) {
        out.assign(buf_, 0, r.length);
        buf_.erase(0, r.length);
    }
    return r.status;
}

}  // namespace gateway
