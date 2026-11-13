#ifndef CLIQUE_OUTPUT_HPP
#define CLIQUE_OUTPUT_HPP

#include "types.hpp"
#include <algorithm>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

// One clique per line: size vertexID... . IDs are those in the input graph.
// A shared writer serializes complete lines from OpenMP workers.
class CliqueOutput {
    std::ofstream file;
    std::mutex mutex;
    ull written = 0;

public:
    explicit CliqueOutput(const std::string &path) : file(path) {
        if (!file) throw std::runtime_error("Cannot open clique output: " + path);
    }

    void emit(const std::vector<ui> &vertices) {
        std::vector<ui> sorted(vertices);
        std::sort(sorted.begin(), sorted.end());
        std::string line = std::to_string(sorted.size());
        for (ui v : sorted) {
            line += ' ';
            line += std::to_string(v);
        }
        line += '\n';
        std::lock_guard<std::mutex> lock(mutex);
        file << line;
        ++written;
    }

    // Check buffered writes after all workers finish (do not throw in OpenMP).
    ull finish() {
        std::lock_guard<std::mutex> lock(mutex);
        file.flush();
        if (!file) throw std::runtime_error("Failed to write clique output");
        return written;
    }
};

#endif
