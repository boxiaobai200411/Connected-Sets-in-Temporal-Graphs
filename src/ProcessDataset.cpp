#include "TemporalSCCs.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#ifndef TCS_ENUM_DATASET_DIR
#define TCS_ENUM_DATASET_DIR "../dataset"
#endif

int main(int argc,char** argv) try {
    std::string inputPath,dataid;
    std::string directory = TCS_ENUM_DATASET_DIR;
    if (argc % 2 == 0) {
        std::cerr << "Usage: ProcessDataset -i <raw-file> -d <dataid> [-o <dataset-directory>]\n";
        return 1;
    }
    for (int i = 1; i < argc; i += 2) {
        const std::string option = argv[i];
        if (option == "-i") inputPath = argv[i + 1];
        else if (option == "-d") dataid = argv[i + 1];
        else if (option == "-o") directory = argv[i + 1];
        else throw std::invalid_argument("Unknown option: " + option);
    }
    if (inputPath.empty() || dataid.empty() || directory.empty() || dataid.find_first_of("/\\") != std::string::npos)
        throw std::invalid_argument("Specify -i <raw-file>, -d <dataid>, and an existing output directory");
    if (directory.back() != '/' && directory.back() != '\\') directory += '/';
    const std::string outputPath = directory + dataid + ".txt";
    const std::string sccPath = directory + dataid + "_SCCs.txt";
    if (std::ifstream(outputPath) || std::ifstream(sccPath))
        throw std::runtime_error("Output dataset or SCC file already exists: " + dataid);

    std::ifstream input(inputPath);
    if (!input) throw std::runtime_error("Cannot open raw dataset: " + inputPath);
    using Integer = std::int64_t;
    std::vector<Integer> vertices;
    std::vector<std::array<Integer,3>> edges;
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(input,line)) {
        ++lineNumber;
        const auto comment = line.find_first_of("#%");
        if (comment != std::string::npos) line.erase(comment);
        if (line.find_first_not_of(" \t\r\n") == std::string::npos) continue;
        std::istringstream record(line);
        Integer u,v,t;
        std::string extra;
        if (!(record >> u >> v >> t) || record >> extra)
            throw std::runtime_error("Expected u v t at line " + std::to_string(lineNumber));
        if (u == v) continue;
        edges.push_back({u,v,t});
        vertices.push_back(u);
        vertices.push_back(v);
    }
    if (input.bad()) throw std::runtime_error("Failed to read raw dataset: " + inputPath);
    std::sort(vertices.begin(),vertices.end());
    vertices.erase(std::unique(vertices.begin(),vertices.end()),vertices.end());
    std::sort(edges.begin(),edges.end(),[](const auto& a,const auto& b) {
        return std::tie(a[2],a[0],a[1]) < std::tie(b[2],b[0],b[1]);
    });

    constexpr auto maximum = std::numeric_limits<ui>::max();
    if (vertices.size() > maximum || edges.size() > maximum)
        throw std::overflow_error("Dataset exceeds 32-bit vertex or edge limits");
    const Integer first = edges.empty() ? 0 : edges.front()[2];
    ul timeCount = 0;
    if (!edges.empty()) {
        const ul span = static_cast<ul>(edges.back()[2]) - static_cast<ul>(first);
        if (span >= maximum) throw std::overflow_error("Timestamp range exceeds 32-bit limits");
        timeCount = span + 1;
    }

    std::ofstream output(outputPath);
    if (!output) throw std::runtime_error("Cannot create dataset: " + outputPath);
    output << vertices.size() << ' ' << edges.size() << ' ' << timeCount << '\n';
    for (const auto& edge : edges) {
        const auto u = std::lower_bound(vertices.begin(),vertices.end(),edge[0]) - vertices.begin();
        const auto v = std::lower_bound(vertices.begin(),vertices.end(),edge[1]) - vertices.begin();
        output << u << ' ' << v << ' ' << static_cast<ul>(edge[2]) - static_cast<ul>(first) << '\n';
    }
    output.close();
    if (!output) throw std::runtime_error("Failed to write dataset: " + outputPath);

    edges.clear();
    edges.shrink_to_fit();
    vertices.clear();
    vertices.shrink_to_fit();
    TemporalSCCs graph(directory,dataid);
    std::cout << "Dataset: " << outputPath << '\n'
              << "SCCs: " << sccPath << '\n'
              << "Vertices: " << graph.getVertexCount() << '\n'
              << "Components: " << graph.getGlobalSCCs().size() << '\n';
    return 0;
}
catch (const std::exception& error) {
    std::cerr << "Error: " << error.what() << '\n';
    return 1;
}
