#ifndef TCS_ENUM_TEMPORAL_TEST_SUPPORT_H
#define TCS_ENUM_TEMPORAL_TEST_SUPPORT_H

#include "TemporalSCCsTestAccess.h"

#include <algorithm>
#include <chrono>
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
#include <limits>
#include <memory>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using Groups = std::vector<std::vector<VertexID>>;

struct Event {
    VertexID source, target;
    ui time;
};

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
    const auto name = "temporal_support_test_" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()) +
        "_" + std::to_string(++sequence);
#if defined(__MINGW32__) && __GNUC__ < 9
    char base[MAX_PATH];
    const auto length = GetTempPathA(MAX_PATH,base);
    require(length > 0 && length < MAX_PATH,"Cannot locate temporary directory");
    const auto directory = std::string(base) + name;
    require(CreateDirectoryA(directory.c_str(),nullptr) != 0,"Cannot create test directory");
    return directory;
#else
    const auto directory = std::filesystem::temp_directory_path() / name;
    require(std::filesystem::create_directory(directory),"Cannot create test directory");
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
    std::string directory;
    std::vector<Event> events;
    std::vector<VertexID> vertices;
    std::unique_ptr<TemporalSCCsTestAccess> graph;

    Fixture(ui count,ui times,std::vector<Event> input)
        : directory(createDirectory()),events(std::move(input)) {
        for (VertexID v = 0; v < count; ++v) vertices.push_back(v);
        std::stable_sort(events.begin(),events.end(),
            [](const Event& a,const Event& b) { return a.time < b.time; });
        {
            std::ofstream output(directory + "/graph.txt");
            output << count << ' ' << events.size() << ' ' << times << '\n';
            for (const auto& edge : events)
                output << edge.source << ' ' << edge.target << ' ' << edge.time << '\n';
            require(output.good(),"Cannot write temporal test graph");
        }
        {
            std::ofstream output(directory + "/graph_SCCs.txt");
            output << "1\n" << vertices.size() << '\n';
            for (VertexID v : vertices) output << v << ' ';
            output << '\n';
            require(output.good(),"Cannot write temporal test SCC file");
        }
        graph = std::make_unique<TemporalSCCsTestAccess>(directory + "/","graph");
        graph->buildWindow(0,times - 1,vertices);
    }

    ~Fixture() {
        graph.reset();
        removeDirectory(directory);
    }
};

bool meetsGroups(const std::vector<VertexID>& chosen,const Groups& groups) {
    for (const auto& group : groups) {
        bool hit = false;
        for (VertexID v : group)
            if (std::find(chosen.begin(),chosen.end(),v) != chosen.end()) hit = true;
        if (!hit) return false;
    }
    return true;
}

bool temporalOracle(const Fixture& fixture,const std::vector<VertexID>& chosen) {
    const auto& window = fixture.graph->getWindow();
    std::vector<std::vector<std::pair<ui,ui>>> outgoing(chosen.size());
    for (const auto& edge : fixture.events) {
        if (edge.time < window.left || edge.time > window.right) continue;
        const auto source = std::find(chosen.begin(),chosen.end(),edge.source);
        const auto target = std::find(chosen.begin(),chosen.end(),edge.target);
        if (source != chosen.end() && target != chosen.end())
            outgoing[source - chosen.begin()].push_back(
                {static_cast<ui>(target - chosen.begin()),edge.time});
    }
    constexpr ui infinity = std::numeric_limits<ui>::max();
    for (ui source = 0; source < chosen.size(); ++source) {
        std::vector<ul> arrival(chosen.size(),std::numeric_limits<ul>::max());
        std::vector<unsigned char> queued(chosen.size(),0);
        std::queue<ui> pending;
        arrival[source] = window.left;
        pending.push(source);
        queued[source] = 1;
        while (!pending.empty()) {
            const ui u = pending.front();
            pending.pop();
            queued[u] = 0;
            for (const auto& edge : outgoing[u]) {
                if ((u != source && edge.second <= arrival[u]) || edge.second >= arrival[edge.first]) continue;
                arrival[edge.first] = edge.second;
                if (!queued[edge.first]) {
                    queued[edge.first] = 1;
                    pending.push(edge.first);
                }
            }
        }
        if (std::find(arrival.begin(),arrival.end(),std::numeric_limits<ul>::max()) != arrival.end()) return false;
    }
    return true;
}

ul oracleCount(const Fixture& fixture,const std::vector<VertexID>& vertices,ui k,
               const Groups& groups = {}) {
    const auto& window = fixture.graph->getWindow();
    std::vector<unsigned char> selected(fixture.graph->getVertexCount(),0),active(selected.size(),0);
    for (VertexID v : vertices) selected[v] = 1;
    for (const auto& edge : fixture.events)
        if (edge.source != edge.target && edge.time >= window.left && edge.time <= window.right &&
            selected[edge.source] && selected[edge.target])
            active[edge.source] = active[edge.target] = 1;
    std::vector<VertexID> candidates,chosen;
    for (VertexID v : vertices)
        if (active[v]) candidates.push_back(v);
    ul count = 0;
    const auto choose = [&](auto&& self,std::size_t start) -> void {
        if (chosen.size() == k) {
            if (meetsGroups(chosen,groups) && temporalOracle(fixture,chosen)) ++count;
            return;
        }
        const auto need = k - chosen.size();
        for (std::size_t i = start; i < candidates.size() && need <= candidates.size() - i; ++i) {
            chosen.push_back(candidates[i]);
            self(self,i + 1);
            chosen.pop_back();
        }
    };
    choose(choose,0);
    return count;
}

}

#endif
