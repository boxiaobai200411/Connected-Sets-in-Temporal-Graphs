#include "TemporalSCCsTestAccess.h"

#if defined(__MINGW32__) && __GNUC__ < 9
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <filesystem>
#endif
#include <fstream>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition,const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

struct OutputCapture {
    std::ostringstream output;
    std::streambuf* previous;

    OutputCapture() : previous(std::cout.rdbuf(output.rdbuf())) {}
    ~OutputCapture() { std::cout.rdbuf(previous); }
};

std::string createDirectory() {
    static ui sequence = 0;
    const auto name = "temporal_v1_query_state_test_" + std::to_string(std::random_device{}()) +
        "_" + std::to_string(++sequence);
#if defined(__MINGW32__) && __GNUC__ < 9
    char base[MAX_PATH];
    const auto length = GetTempPathA(MAX_PATH,base);
    require(length > 0 && length < MAX_PATH,"Cannot locate temporary directory");
    const auto directory = std::string(base) + name;
    require(CreateDirectoryA(directory.c_str(),nullptr) != 0,"Cannot create query test directory");
    return directory;
#else
    const auto directory = std::filesystem::temp_directory_path() / name;
    require(std::filesystem::create_directory(directory),"Cannot create query test directory");
    return directory.generic_string();
#endif
}

void removeDirectory(const std::string& directory) {
#if defined(__MINGW32__) && __GNUC__ < 9
    DeleteFileA((directory + "/graph.txt").c_str());
    DeleteFileA((directory + "/graph_SCCs.txt").c_str());
    RemoveDirectoryA(directory.c_str());
#else
    std::error_code error;
    std::filesystem::remove_all(directory,error);
#endif
}

struct Fixture {
    std::string directory = createDirectory();
    std::vector<VertexID> vertices{0,1,2,3,4,5};
    std::unique_ptr<TemporalSCCsTestAccess> graph;

    Fixture() {
        {
            std::ofstream output(directory + "/graph.txt");
            output << "6 60 4\n";
            for (ui time : {1u,3u})
                for (VertexID u : vertices)
                    for (VertexID v : vertices)
                        if (u != v) output << u << ' ' << v << ' ' << time << '\n';
            require(output.good(),"Cannot write query graph");
        }
        {
            std::ofstream output(directory + "/graph_SCCs.txt");
            output << "1\n6\n0 1 2 3 4 5\n";
            require(output.good(),"Cannot write query SCC input");
        }
        graph = std::make_unique<TemporalSCCsTestAccess>(directory + "/","graph");
    }

    ~Fixture() {
        graph.reset();
        removeDirectory(directory);
    }
};

void requireNoReport(const std::string& output) {
    require(output.empty(), "Queries and standalone enumeration must remain silent");
    require(output.find(" ms") == std::string::npos,
        "Query output must not contain elapsed milliseconds");
}

void testOwnedEnumeration() {
    Fixture fixture;
    auto& graph = *fixture.graph;
    OutputCapture capture;
    graph.buildWindow(1,3,fixture.vertices);
    const std::vector<std::vector<VertexID>> required{{0},{1}};
    const ul before = graph.getAnswerCount();
    graph.enumerateOptimized(fixture.vertices,4,required);
    require(graph.getAnswerCount() - before == 6,
        "Enumeration must individually count C(4,2) owned four-point answers");
    const ul beforeFive = graph.getAnswerCount();
    graph.enumerateOptimized(fixture.vertices,5,required);
    require(graph.getAnswerCount() - beforeFive == 4,
        "Enumeration must preserve C(4,3) owned five-point answers");
    requireNoReport(capture.output.str());
}

void testQueryState() {
    Fixture fixture;
    auto& graph = *fixture.graph;
    {
        OutputCapture capture;
        const auto flags = std::cout.flags();
        const auto precision = std::cout.precision();
        graph.enumerateDec(1,3,4);
        require(graph.getAnswerCount() == 15,"V1 query must preserve answers");
        require(std::cout.flags() == flags && std::cout.precision() == precision,
            "Query output must restore cout formatting");
        requireNoReport(capture.output.str());
    }
    {
        OutputCapture capture;
        graph.enumerateDec(0,0,4);
        require(graph.getAnswerCount() == 0,"Empty window must clear the answer count");
        requireNoReport(capture.output.str());
    }
    {
        OutputCapture capture;
        bool rejected = false;
        try {
            graph.enumerateDec(3,1,4);
        } catch (const std::out_of_range&) {
            rejected = true;
        }
        require(rejected,"Invalid query window must still be rejected");
        requireNoReport(capture.output.str());
    }
    {
        OutputCapture capture;
        bool rejected = false;
        try {
            graph.enumerateDec(1,3,1);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        require(rejected,"V1 must reject an invalid k");
        graph.enumerateDec(1,3,4);
        require(graph.getAnswerCount() == 15,"V1 must recover after invalid window and k queries");
        requireNoReport(capture.output.str());
    }
    {
        OutputCapture capture;
        graph.enumerateCom(1,3,4);
        require(graph.getAnswerCount() == 15,"Baseline fixture answer count changed");
        requireNoReport(capture.output.str());
    }
    {
        OutputCapture capture;
        graph.enumerateDec(1,3,4);
        require(graph.getAnswerCount() == 15,"Repeated V1 query must reset its answer count");
        requireNoReport(capture.output.str());
    }
    {
        OutputCapture capture;
        graph.decompose(fixture.vertices,4,{});
        require(graph.getAnswerCount() == 30,"Standalone V1 search must preserve behavior");
        graph.enumerateOptimized(fixture.vertices,4,{});
        require(graph.getAnswerCount() == 45,"Standalone V1 enumeration must preserve behavior");
        requireNoReport(capture.output.str());
    }
}

}

int main() {
    try {
        testQueryState();
        testOwnedEnumeration();
        std::cout << "V1 query state tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V1 query state test failed: " << error.what() << '\n';
        return 1;
    }
}
