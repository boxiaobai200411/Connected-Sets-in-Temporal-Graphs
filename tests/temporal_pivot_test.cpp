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
#include <numeric>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Event {
    VertexID source, target;
    ui time;
};

void require(bool condition,const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

struct CaptureOutput {
    std::ostringstream output;
    std::streambuf* previous;
    CaptureOutput() : previous(std::cout.rdbuf(output.rdbuf())) {}
    ~CaptureOutput() { std::cout.rdbuf(previous); }
};

std::string createDirectory() {
    static ui sequence = 0;
    const auto name = "temporal_pivot_test_" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()) + "_" + std::to_string(++sequence);
#if defined(__MINGW32__) && __GNUC__ < 9
    char base[MAX_PATH];
    const auto length = GetTempPathA(MAX_PATH,base);
    require(length > 0 && length < MAX_PATH,"Cannot find test temporary directory");
    const auto directory = std::string(base) + name;
    require(CreateDirectoryA(directory.c_str(),nullptr) != 0,"Cannot create test temporary directory");
    return directory;
#else
    const auto directory = std::filesystem::temp_directory_path() / name;
    require(std::filesystem::create_directory(directory),"Cannot create test temporary directory");
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

    Fixture(ui count,std::vector<Event> input)
        : directory(createDirectory()),events(std::move(input)),vertices(count) {
        std::iota(vertices.begin(),vertices.end(),VertexID{0});
        std::stable_sort(events.begin(),events.end(),
            [](const Event& a,const Event& b) { return a.time < b.time; });
        {
            std::ofstream output(directory + "/graph.txt");
            output << count << ' ' << events.size() << " 8\n";
            for (const auto& edge : events)
                output << edge.source << ' ' << edge.target << ' ' << edge.time << '\n';
            require(output.good(),"Cannot write pivot test graph");
        }
        {
            std::ofstream output(directory + "/graph_SCCs.txt");
            output << "1\n" << vertices.size() << '\n';
            for (VertexID vertex : vertices) output << vertex << ' ';
            output << '\n';
            require(output.good(),"Cannot write pivot test SCC file");
        }
        graph = std::make_unique<TemporalSCCsTestAccess>(directory + "/","graph");
        graph->buildWindow(0,7,vertices);
    }

    ~Fixture() {
        graph.reset();
        removeDirectory(directory);
    }
};

std::vector<Event> triangleRing() {
    std::vector<Event> events;
    for (VertexID block = 0; block < 44; ++block) {
        const VertexID first = block * 3;
        for (VertexID u = first; u < first + 3; ++u)
            for (VertexID v = first; v < first + 3; ++v)
                if (u != v) events.push_back({u,v,2});
        events.push_back({first + 2,((block + 1) % 44) * 3,3});
    }
    return events;
}

std::vector<VertexID> candidates(Fixture& fixture,const std::vector<VertexID>& vertices) {
    VertexID degree, neighbor;
    fixture.graph->buildPivotIndex(vertices,degree,neighbor);
    const VertexID temporal = fixture.graph->selectPivot(vertices);
    std::vector<VertexID> result;
    for (VertexID vertex : {degree,temporal,neighbor,vertices.front(),
            vertices[vertices.size() / 2],vertices.back()})
        if (result.size() < 2 && std::find(result.begin(),result.end(),vertex) == result.end())
            result.push_back(vertex);
    return result;
}

struct Plan {
    bool decompose=false;
    VertexID pivot=0;
    TemporalSCCsTestAccess::SplitResult split;
    std::vector<ui> arrival;
    long double cost=0;
};

Plan selectPlan(Fixture& fixture,const std::vector<VertexID>& vertices) {
    const auto selected=fixture.graph->buildSplitPlan(vertices,3);
    Plan result;result.decompose=selected.decompose;
    if(result.decompose) {result.pivot=selected.pivot;result.split=selected.forward;result.arrival=selected.arrival;}
    return result;
}

void requireSamePlan(const Plan& first,const Plan& second,const std::string& message) {
    require(first.decompose == second.decompose && first.pivot == second.pivot && first.arrival == second.arrival &&
        first.split.R == second.split.R && first.split.U == second.split.U && first.cost == second.cost,message);
}

void testCandidateState() {
    Fixture fixture(132,triangleRing());
    const auto selected = selectPlan(fixture,fixture.vertices);
    requireSamePlan(selectPlan(fixture,fixture.vertices),selected,"Repeated pivot calls changed the plan");
    fixture.graph->prepareSubsetIndex({2,5,8});
    requireSamePlan(selectPlan(fixture,fixture.vertices),selected,"A previous local index contaminated pivot selection");

    fixture.graph->buildWindow(2,2,fixture.vertices);
    const auto narrow = selectPlan(fixture,fixture.vertices);
    std::vector<Event> filtered;
    for (const auto& edge : fixture.events)
        if (edge.time == 2) filtered.push_back(edge);
    Fixture onlyNarrow(132,std::move(filtered));
    onlyNarrow.graph->buildWindow(2,2,onlyNarrow.vertices);
    requireSamePlan(narrow,selectPlan(onlyNarrow,onlyNarrow.vertices),
        "Out-of-window events affected a rebuilt one-timestamp pivot plan");
    fixture.graph->buildWindow(0,7,fixture.vertices);
    requireSamePlan(selectPlan(fixture,fixture.vertices),selected,"Window switching retained stale pivot state");
}

void testInducedSetIsolation() {
    const auto base = triangleRing();
    auto noisy = base;
    for (ui repeat = 0; repeat < 80; ++repeat)
        for (VertexID vertex = 0; vertex < 128; ++vertex) {
            noisy.push_back({128 + repeat % 4,vertex,repeat % 8});
            noisy.push_back({vertex,128 + repeat % 4,(repeat + 3) % 8});
        }
    Fixture clean(132,base), dirty(132,std::move(noisy));
    std::vector<VertexID> subset(clean.vertices.begin(),clean.vertices.begin() + 128);
    const auto expected = selectPlan(clean,subset);
    const auto actual = selectPlan(dirty,subset);
    requireSamePlan(actual,expected,"Edges with an endpoint outside the induced set changed the pivot plan");
    require(!actual.decompose || std::find(subset.begin(),subset.end(),actual.pivot) != subset.end(),"Pivot is outside the induced set");
}

std::vector<ui> earliestOracle(const Fixture& fixture,const std::vector<VertexID>& vertices,VertexID root) {
    constexpr ui infinity = std::numeric_limits<ui>::max();
    const auto& window = fixture.graph->getWindow();
    std::vector<ui> arrival(fixture.vertices.size(),infinity);
    std::vector<unsigned char> included(fixture.vertices.size(),0);
    for (VertexID vertex : vertices) included[vertex] = 1;
    arrival[root] = window.left;
    bool changed;
    do {
        changed = false;
        for (const auto& edge : fixture.events) {
            if (edge.time < window.left || edge.time > window.right ||
                !included[edge.source] || !included[edge.target]) continue;
            if ((edge.source == root || arrival[edge.source] < edge.time) && arrival[edge.target] > edge.time) {
                arrival[edge.target] = edge.time;
                changed = true;
            }
        }
    } while (changed);
    std::vector<ui> local;
    for (VertexID vertex : vertices) local.push_back(arrival[vertex]);
    return local;
}

ul staticCombinationCost(const Fixture& fixture,const std::vector<VertexID>& vertices) {
    const ui n = static_cast<ui>(vertices.size());
    std::vector<ui> position(fixture.vertices.size(),n);
    for (ui i = 0; i < n; ++i) position[vertices[i]] = i;
    std::vector<std::vector<unsigned char>> reach(n,std::vector<unsigned char>(n,0));
    for (ui i = 0; i < n; ++i) reach[i][i] = 1;
    const auto& window = fixture.graph->getWindow();
    for (const auto& edge : fixture.events)
        if (edge.time >= window.left && edge.time <= window.right &&
            position[edge.source] != n && position[edge.target] != n)
            reach[position[edge.source]][position[edge.target]] = 1;
    for (ui middle = 0; middle < n; ++middle)
        for (ui source = 0; source < n; ++source)
            if (reach[source][middle])
                for (ui target = 0; target < n; ++target)
                    reach[source][target] |= reach[middle][target];
    std::vector<unsigned char> assigned(n,0);
    ul cost = 0;
    for (ui source = 0; source < n; ++source) {
        if (assigned[source]) continue;
        ul size = 0;
        for (ui target = 0; target < n; ++target)
            if (reach[source][target] && reach[target][source]) {
                assigned[target] = 1;
                ++size;
            }
        if (size >= 3) cost += size * (size - 1) * (size - 2) / 6;
    }
    return cost;
}

ul materializedCost(Fixture& fixture,const std::vector<VertexID>& vertices,const Plan& plan) {
    ul result = staticCombinationCost(fixture,plan.split.R);
    auto remaining = plan.split.U;
    auto arrival = plan.arrival;
    while (!remaining.empty()) {
        const VertexID seed = fixture.graph->selectReversePivot(vertices,remaining);
        auto cover = fixture.graph->buildReverseCover(seed,vertices,arrival,remaining);
        require(cover.second.size() < remaining.size(),"A selected plan failed to cover any remaining vertex");
        result += staticCombinationCost(fixture,cover.first);
        remaining = std::move(cover.second);
    }
    return result;
}

void checkSelectedPlan(Fixture& fixture,const std::vector<VertexID>& vertices) {
    const auto pool = candidates(fixture,vertices);
    const auto plan = selectPlan(fixture,vertices);
    if(!plan.decompose) return;
    require(std::find(pool.begin(),pool.end(),plan.pivot) != pool.end(),"k=3 pivot bypassed the first two distinct original candidates");
    const auto expected = earliestOracle(fixture,vertices,plan.pivot);
    require(plan.arrival == expected,"Selected root returned time labels for another candidate");
    std::vector<VertexID> reached,unreached;
    for (ui i = 0; i < vertices.size(); ++i)
        (expected[i] == std::numeric_limits<ui>::max() ? unreached : reached).push_back(vertices[i]);
    require(plan.split.R == reached && plan.split.U == unreached,"Selected root split is inconsistent with its earliest arrivals");
    const auto cost=materializedCost(fixture,vertices,plan);
    const long double n=vertices.size();
    const long double directCost=n*(n-1)*(n-2)/6;
    require(cost<1.1L*directCost,
        "Selected complete plan must satisfy the original fixed split-cost threshold");
}

void testSelectedPlan() {
    Fixture fixture(132,triangleRing());
    checkSelectedPlan(fixture,fixture.vertices);
    auto shuffled = fixture.vertices;
    std::mt19937 random(20261031);
    std::shuffle(shuffled.begin(),shuffled.end(),random);
    checkSelectedPlan(fixture,shuffled);
    shuffled.resize(21);
    checkSelectedPlan(fixture,shuffled);
}

struct QueryResult {
    ul count;
};

QueryResult query(Fixture& fixture,ui left,ui right,ui k) {
    CaptureOutput capture;
    fixture.graph->enumerateDec(left,right,k);
    return {fixture.graph->getAnswerCount()};
}

void testLargeQueryCounts() {
    Fixture fixture(132,triangleRing());
    const auto components = fixture.graph->findWindowStaticSCCs(fixture.vertices,fixture.graph->getWindow(),2);
    require(components.size() == 1 && components.front().size() == 132,
        "Large pivot fixture must reach the 128-vertex branch as one static SCC");
    require(query(fixture,0,7,3).count == 44,"Pivot lost/duplicated an analytically known triangle answer");
    require(query(fixture,0,7,3).count == 44,"Repeated queries retained an earlier answer count");
    require(query(fixture,3,7,3).count == 0,
        "Cross-block edges alone must not form a three-point answer");
    require(query(fixture,2,2,3).count == 44,
        "Same-time triangles must remain valid after a narrow-window query");

    for (ui k : {2u,4u,5u}) {
        const auto result = query(fixture,0,7,k);
        const ul expected = k == 2 ? 132 : 0;
        require(result.count == expected,"Non-three-point query count differs from the independently known result");
    }
}

void testThresholdAndNoSplit() {
    std::vector<Event> cycle;
    for (VertexID vertex = 0; vertex < 128; ++vertex)
        cycle.push_back({vertex,(vertex + 1) % 128,0});
    Fixture fixture(128,std::move(cycle));
    require(query(fixture,0,7,3).count == 0,
        "The 128-point full-reachability cycle must not create three-point answers");
}

}

int main() {
    try {
        {
            CaptureOutput capture;
            testCandidateState();
            testInducedSetIsolation();
            testSelectedPlan();
            testLargeQueryCounts();
            testThresholdAndNoSplit();
        }
        std::cout << "temporal_pivot_test passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "temporal_pivot_test failed: " << error.what() << '\n';
        return 1;
    }
}
