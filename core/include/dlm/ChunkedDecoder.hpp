#pragma once

#include <string>

#include "dlm/Types.hpp"

namespace dlm::detail {

// Разбирает chunked-encoded тело (RFC 7230, 4.1) из приёмного буфера.
// Вызывает onData для каждого распознанного куска данных, удаляя
// обработанные байты из начала buffer по ходу разбора — остаток (ещё не
// пришедший чанк) остаётся в buffer для следующего вызова.
// finished выставляется в true, когда встречен завершающий чанк нулевой
// длины вместе с его завершающими CRLF/трейлерами.
// Возвращает false при повреждённом формате чанков или если onData вернул
// false (сигнал прервать приём).
bool consumeChunkedBytes(std::string& buffer, const WriteCallback& onData, bool& finished);

} // namespace dlm::detail
