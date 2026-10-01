#pragma once

#include <string>

#include "dlm/Types.hpp"

namespace dlm::detail {

// Parses a chunked-encoded body (RFC 7230, 4.1) out of the receive buffer.
// Calls onData for each recognized piece of data, removing the processed
// bytes from the front of buffer as it goes — the remainder (a chunk that
// hasn't fully arrived yet) stays in buffer for the next call.
// finished is set to true once the terminating zero-length chunk is
// encountered, along with its closing CRLF/trailers.
// Returns false on malformed chunk framing, or if onData returned false
// (a signal to abort receiving).
bool consumeChunkedBytes(std::string& buffer, const WriteCallback& onData, bool& finished);

} // namespace dlm::detail
