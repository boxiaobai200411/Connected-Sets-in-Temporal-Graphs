#ifndef TCS_ENUM_TYPE_H
#define TCS_ENUM_TYPE_H
#include <cstdint>
#include <vector>
#include <limits>
typedef uint32_t ui;
typedef uint64_t ul;
using VertexID = ui;

constexpr int INF = std::numeric_limits<int>::max();

inline  std::vector<VertexID> flatten(const std::vector<std::vector<VertexID>>& matrix) {
    std::size_t total = 0;
    for (const auto& row : matrix) {
        total += row.size();
    }
    std::vector<VertexID> result;
    result.reserve(total);

    for (const auto& row : matrix) {
        result.insert(result.end(), row.begin(), row.end());
    }
    return result;
}

#endif
