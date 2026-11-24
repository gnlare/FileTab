#pragma once
#include <algorithm>

namespace ft {
// Clamp the row before applying the current column, including partial final rows.
inline int GridPageTarget(int count, int columns, int current, int pageRows, bool down) {
    if (count <= 0) return -1;
    if (current < 0) return 0;
    columns = std::max(1, columns);
    current = std::min(current, count - 1);
    const int row = current / columns;
    const int lastRow = (count - 1) / columns;
    pageRows = std::max(1, pageRows);
    const int destRow = down ? row + std::min(pageRows, lastRow - row)
                             : row - std::min(pageRows, row);
    const int start = destRow * columns;
    return start + std::min(current % columns, count - 1 - start);
}
}  // namespace ft
