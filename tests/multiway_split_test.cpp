#include "TemporalSCCsTestAccess.h"

#include <algorithm>
#include <chrono>
#include <cmath>
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
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct TestEdge {
    VertexID source;
    VertexID target;
    ui time;
};

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

struct CoutCapture {
    std::ostringstream output;
    std::streambuf* previous;

    CoutCapture() : previous(std::cout.rdbuf(output.rdbuf())) {}
    ~CoutCapture() { std::cout.rdbuf(previous); }
};

std::string createTempDirectory() {
    static unsigned sequence = 0;
    const auto name = "temporal_multiway_test_" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()) +
        "_" + std::to_string(++sequence);
#if defined(__MINGW32__) && __GNUC__ < 9
    char base[MAX_PATH];
    const auto length = GetTempPathA(MAX_PATH, base);
    require(length > 0 && length < MAX_PATH, "Cannot find temporary directory");
    const std::string directory = std::string(base) + name;
    require(CreateDirectoryA(directory.c_str(), nullptr) != 0,
            "Cannot create temporary directory");
    return directory;
#else
    const auto directory = std::filesystem::temp_directory_path() / name;
    require(std::filesystem::create_directory(directory), "Cannot create temporary directory");
    return directory.generic_string();
#endif
}

void removeTempDirectory(const std::string& directory) {
#if defined(__MINGW32__) && __GNUC__ < 9
    DeleteFileA((directory + "/graph.txt").c_str());
    DeleteFileA((directory + "/graph_SCCs.txt").c_str());
    RemoveDirectoryA(directory.c_str());
#else
    std::error_code error;
    std::filesystem::remove_all(directory, error);
#endif
}

struct GraphFixture {
    std::string directory;
    std::vector<TestEdge> edges;
    std::vector<VertexID> vertices;
    std::unique_ptr<TemporalSCCsTestAccess> graph;
    ui timeCount;

    GraphFixture(ui vertexCount, ui times, std::vector<TestEdge> input,
                 std::vector<VertexID> candidates = {})
        : edges(std::move(input)), vertices(std::move(candidates)), timeCount(times) {
        directory = createTempDirectory();
        if (vertices.empty()) {
            for (VertexID v = 0; v < vertexCount; ++v) vertices.push_back(v);
        }
        std::stable_sort(edges.begin(), edges.end(),
            [](const TestEdge& a, const TestEdge& b) { return a.time < b.time; });
        {
            std::ofstream output(directory + "/graph.txt");
            output << vertexCount << ' ' << edges.size() << ' ' << times << '\n';
            for (const auto& edge : edges)
                output << edge.source << ' ' << edge.target << ' ' << edge.time << '\n';
            require(output.good(), "Cannot write temporary graph");
        }
        {
            std::ofstream output(directory + "/graph_SCCs.txt");
            output << 1 << '\n' << vertices.size() << '\n';
            for (VertexID v : vertices) output << v << ' ';
            output << '\n';
            require(output.good(), "Cannot write temporary global SCC");
        }
        graph = std::make_unique<TemporalSCCsTestAccess>(
            directory + "/", "graph");
        graph->buildWindow(0, times - 1, vertices);
    }

    ~GraphFixture() {
        graph.reset();
        removeTempDirectory(directory);
    }
};

std::vector<std::vector<VertexID>> materializeSplit(
    GraphFixture& fixture, const std::vector<VertexID>& vertices, ui k) {
    if (k < 2) throw std::invalid_argument("k must be at least 2");
    if (vertices.size() < k) return {};
    auto plan = fixture.graph->buildSplitPlan(vertices,k);
    if (!plan.decompose) return {vertices};
    std::vector<std::vector<VertexID>> groups{std::move(plan.forward.R)};
    auto remaining = std::move(plan.forward.U);
    std::size_t cursor = 0;
    while (!remaining.empty()) {
        if (!plan.reverse.empty()) {
            require(cursor < plan.reverse.size(),"Cached split plan ended before its remaining set");
            auto& cover = plan.reverse[cursor++];
            groups.push_back(std::move(cover.covered));
            remaining = std::move(cover.remaining);
        } else {
            const auto seed = fixture.graph->selectReversePivot(vertices,remaining);
            auto cover = fixture.graph->buildReverseCover(seed,vertices,plan.arrival,remaining);
            groups.push_back(std::move(cover.first));
            remaining = std::move(cover.second);
        }
    }
    require(plan.reverse.empty() || cursor == plan.reverse.size(),"Unused cached reverse plan suffix");
    return groups;
}

std::vector<VertexID> sortedSet(std::vector<VertexID> values) {
    std::sort(values.begin(), values.end());
    require(std::adjacent_find(values.begin(), values.end()) == values.end(),
            "A candidate set contains duplicate vertices");
    return values;
}

bool containsAll(const std::vector<VertexID>& candidates,
                 const std::vector<VertexID>& chosen) {
    for (VertexID v : chosen)
        if (std::find(candidates.begin(), candidates.end(), v) == candidates.end())
            return false;
    return true;
}

std::vector<ui> oracleArrival(const GraphFixture& fixture,
                              const std::vector<VertexID>& vertices,
                              VertexID source) {
    const auto& window = fixture.graph->getWindow();
    const ui unreachable = std::numeric_limits<ui>::max();
    std::vector<ui> arrival(fixture.graph->getVertexCount(), unreachable);
    std::vector<unsigned char> selected(arrival.size(), 0);
    for (VertexID v : vertices) selected[v] = 1;
    std::vector<const TestEdge*> relevant;
    for (const auto& edge : fixture.edges)
        if (edge.time >= window.left && edge.time <= window.right &&
            selected[edge.source] && selected[edge.target]) relevant.push_back(&edge);
    arrival[source] = window.left;
    for (ui t = window.left; t <= window.right; ++t) {
        bool changed;
        do {
            changed = false;
            for (const auto* edge : relevant) {
                if (edge->time != t) continue;
                if ((edge->source == source || arrival[edge->source] < t) && arrival[edge->target] > t) {
                    arrival[edge->target] = t;
                    changed = true;
                }
            }
        } while (changed);
    }
    return arrival;
}

bool oracleTemporalSCC(const GraphFixture& fixture,
                       const std::vector<VertexID>& chosen) {
    for (VertexID source : chosen) {
        const auto arrival = oracleArrival(fixture, chosen, source);
        for (VertexID v : chosen)
            if (arrival[v] == std::numeric_limits<ui>::max()) return false;
    }
    return true;
}

std::vector<std::vector<VertexID>> oracleAnswers(
    const GraphFixture& fixture, const std::vector<VertexID>& vertices, ui k) {
    std::vector<std::vector<VertexID>> answers;
    std::vector<VertexID> chosen;
    const auto choose = [&](auto&& self, std::size_t start) -> void {
        if (chosen.size() == k) {
            if (oracleTemporalSCC(fixture, chosen)) answers.push_back(chosen);
            return;
        }
        const std::size_t remaining = k - chosen.size();
        for (std::size_t i = start; i < vertices.size() && remaining <= vertices.size() - i; ++i) {
            chosen.push_back(vertices[i]);
            self(self, i + 1);
            chosen.pop_back();
        }
    };
    choose(choose, 0);
    return answers;
}

std::vector<VertexID> oracleCover(
    const GraphFixture& fixture, const std::vector<VertexID>& remaining,
    const std::vector<ui>& arrival, VertexID seed) {
    const auto& window = fixture.graph->getWindow();
    std::vector<unsigned char> selected(arrival.size(), 0), included(arrival.size(), 0);
    for (VertexID v : remaining) selected[v] = 1;
    included[seed] = 1;
    bool changed;
    do {
        changed = false;
        for (const auto& edge : fixture.edges) {
            if (edge.time < window.left || edge.time > window.right ||
                !selected[edge.source] || !selected[edge.target] ||
                !included[edge.target] || included[edge.source]) continue;
            if (edge.time < arrival[edge.target]) {
                included[edge.source] = 1;
                changed = true;
            }
        }
    } while (changed);
    std::vector<VertexID> cover;
    for (VertexID v : remaining)
        if (included[v]) cover.push_back(v);
    return sortedSet(std::move(cover));
}

VertexID oracleDegreePivot(const GraphFixture& fixture, const std::vector<VertexID>& vertices) {
    require(!vertices.empty(), "Pivot oracle requires a nonempty candidate set");
    const auto& window = fixture.graph->getWindow();
    std::vector<ul> degrees(fixture.graph->getVertexCount(), 0);
    for (const auto& edge : fixture.edges) {
        if (edge.time < window.left || edge.time > window.right ||
            !containsAll(vertices, {edge.source, edge.target})) continue;
        ++degrees[edge.source];
        ++degrees[edge.target];
    }
    VertexID selected = vertices.front();
    for (VertexID v : vertices)
        if (degrees[v] > degrees[selected]) selected = v;
    return selected;
}

VertexID oracleNeighborPivot(const GraphFixture& fixture, const std::vector<VertexID>& vertices) {
    const auto& window = fixture.graph->getWindow();
    VertexID best = vertices.front();
    std::size_t largest = 0;
    for (VertexID vertex : vertices) {
        std::set<VertexID> neighbors;
        for (const auto& edge : fixture.edges) {
            if (edge.time < window.left || edge.time > window.right ||
                !containsAll(vertices, {edge.source, edge.target})) continue;
            if (edge.source == vertex) neighbors.insert(edge.target);
            if (edge.target == vertex) neighbors.insert(edge.source);
        }
        if (neighbors.size() > largest) {
            largest = neighbors.size();
            best = vertex;
        }
    }
    return best;
}

VertexID verifyReversePivot(GraphFixture& fixture, const std::vector<VertexID>& vertices,
                            const std::vector<ui>& globalArrival,
                            const std::vector<VertexID>& remainingU) {
    const auto n = fixture.graph->getVertexCount();
    const auto& window = fixture.graph->getWindow();
    std::vector<unsigned char> included(n, 0);
    for (VertexID vertex : vertices) included[vertex] = 1;
    std::vector<ul> degrees(n, 0), scores(n, 0);
    std::vector<long long> departure(n, std::numeric_limits<int>::max()), incoming(n, -1);
    for (const auto& edge : fixture.edges) {
        if (edge.time < window.left || edge.time > window.right ||
            !included[edge.source] || !included[edge.target]) continue;
        ++degrees[edge.source];
        ++degrees[edge.target];
        departure[edge.source] = std::min(departure[edge.source], static_cast<long long>(edge.time));
        incoming[edge.target] = std::max(incoming[edge.target], static_cast<long long>(edge.time));
    }
    for (VertexID root : remainingU) {
        ul unreachable = 0, earlyNeighbors = 0;
        for (VertexID other : vertices)
            if (root != other && incoming[other] < departure[root]) ++unreachable;
        std::vector<unsigned char> counted(n, 0);
        for (const auto& edge : fixture.edges) {
            if (edge.source != root || !included[edge.target] ||
                edge.time < window.left || edge.time > window.right ||
                edge.time > departure[edge.target] || counted[edge.target]) continue;
            counted[edge.target] = 1;
            ++earlyNeighbors;
        }
        scores[root] = unreachable * earlyNeighbors;
    }
    VertexID expected = remainingU.front();
    for (VertexID vertex : remainingU)
        if (degrees[vertex] > degrees[expected] ||
            (degrees[vertex] == degrees[expected] && scores[vertex] > scores[expected]))
            expected = vertex;
    std::vector<ui> localArrival;
    for (VertexID vertex : vertices) localArrival.push_back(globalArrival[vertex]);
    const auto selected = fixture.graph->selectReversePivot(vertices, remainingU);
    require(containsAll(remainingU, {selected}), "The reverse seed must belong to the current Ux");
    require(selected == expected,
            "The reverse seed differs from independent event-degree/time-feature ranking");
    return selected;
}

std::vector<std::vector<VertexID>> oracleSplitForPivot(
    GraphFixture& fixture, const std::vector<VertexID>& vertices, VertexID pivot) {
    const auto arrival = oracleArrival(fixture, vertices, pivot);
    std::vector<VertexID> reachable, remainingU;
    for (VertexID vertex : vertices) {
        if (arrival[vertex] == std::numeric_limits<ui>::max()) remainingU.push_back(vertex);
        else reachable.push_back(vertex);
    }
    std::vector<std::vector<VertexID>> groups{reachable};
    while (!remainingU.empty()) {
        const auto seed = verifyReversePivot(fixture, vertices, arrival, remainingU);
        auto cover = oracleCover(fixture, vertices, arrival, seed);
        require(containsAll(cover, {seed}) && cover.size() < vertices.size(),
                "A reverse cover must include its seed and strictly shrink the parent set");
        const auto previous = remainingU.size();
        remainingU.erase(std::remove_if(remainingU.begin(), remainingU.end(),
            [&](VertexID vertex) { return containsAll(cover, {vertex}); }), remainingU.end());
        require(remainingU.size() < previous, "Every reverse iteration must consume original U vertices");
        groups.push_back(std::move(cover));
    }
    return groups;
}

std::pair<ul, std::size_t> oracleSplitCost(
    const GraphFixture& fixture, const std::vector<std::vector<VertexID>>& groups, ui k) {
    ul cost = 0;
    std::size_t largest = 0;
    const auto& window = fixture.graph->getWindow();
    for (const auto& group : groups) {
        const auto n = group.size();
        std::vector<std::vector<unsigned char>> reach(n, std::vector<unsigned char>(n, 0));
        for (std::size_t i = 0; i < n; ++i) reach[i][i] = 1;
        for (const auto& edge : fixture.edges) {
            if (edge.time < window.left || edge.time > window.right) continue;
            const auto a = std::find(group.begin(), group.end(), edge.source);
            const auto b = std::find(group.begin(), group.end(), edge.target);
            if (a != group.end() && b != group.end())
                reach[a - group.begin()][b - group.begin()] = 1;
        }
        for (std::size_t via = 0; via < n; ++via)
            for (std::size_t i = 0; i < n; ++i)
                for (std::size_t j = 0; j < n; ++j)
                    reach[i][j] = reach[i][j] || (reach[i][via] && reach[via][j]);
        std::vector<unsigned char> assigned(n, 0);
        for (std::size_t i = 0; i < n; ++i) {
            if (assigned[i]) continue;
            std::size_t size = 0;
            for (std::size_t j = i; j < n; ++j)
                if (reach[i][j] && reach[j][i]) { assigned[j] = 1; ++size; }
            if (size < k) continue;
            largest = std::max(largest, size);
            ul combinations = 1;
            for (ui j = 1; j <= k; ++j)
                combinations = combinations * (size - k + j) / j;
            cost += combinations;
        }
    }
    return {cost, largest};
}

void verifySplit(GraphFixture& fixture, const std::vector<VertexID>& vertices, ui k) {
    if (vertices.size() < k) {
        require(materializeSplit(fixture,vertices,k).empty(),"An undersized set must not be split");
        return;
    }
    auto plan = fixture.graph->buildSplitPlan(vertices,k);
    std::vector<VertexID> candidates;
    const auto add = [&](VertexID vertex) {
        const std::size_t limit = k == 3 ? 2 : (vertices.size() < 128 ? 3 : 6);
        if (candidates.size() < limit && std::find(candidates.begin(),candidates.end(),vertex) == candidates.end())
            candidates.push_back(vertex);
    };
    add(oracleDegreePivot(fixture,vertices));
    fixture.graph->prepareSubsetIndex(vertices);
    add(fixture.graph->selectPivot(vertices));
    add(oracleNeighborPivot(fixture,vertices));
    add(vertices.front());add(vertices[vertices.size()/2]);add(vertices.back());
    auto best = candidates.front();
    auto bestScore = std::make_pair(std::numeric_limits<ul>::max(),std::numeric_limits<std::size_t>::max());
    for (VertexID candidate : candidates) {
        const auto covers = oracleSplitForPivot(fixture,vertices,candidate);
        if (covers.size() == 1) continue;
        const auto score = oracleSplitCost(fixture,covers,k);
        if (score < bestScore) {best = candidate;bestScore = score;}
    }
    ul direct = 1;
    for (ui i=1;i<=k;++i) direct=direct*(vertices.size()-k+i)/i;
    const bool expectedDecompose = bestScore.first != std::numeric_limits<ul>::max() &&
        static_cast<long double>(bestScore.first) < 1.1L * direct;
    require(plan.decompose == expectedDecompose,"Bounded split decision differs from complete independent candidate scores");
    const auto actual = materializeSplit(fixture,vertices,k);
    if (plan.decompose) {
        require(plan.pivot == best,"Split winner must minimize full cost, largest SCC and candidate order");
        const auto expected = oracleSplitForPivot(fixture,vertices,plan.pivot);
        require(actual.size() == expected.size(),"Split plan has the wrong number of cover groups");
        for (std::size_t i=0;i<actual.size();++i)
            require(sortedSet(actual[i]) == sortedSet(expected[i]),"Cached cover differs from the frozen-arrival oracle");
    } else require(actual == std::vector<std::vector<VertexID>>{vertices},"A rejected plan must preserve the parent set");
    for (const auto& answer : oracleAnswers(fixture,vertices,k)) {
        bool covered = false;
        for (const auto& group : actual) if (containsAll(group,answer)) covered=true;
        require(covered,"Multiway split lost a valid answer");
    }
}

using HitGroups = std::vector<std::vector<VertexID>>;

struct OwnedLeaf {
    std::vector<VertexID> vertices;
    HitGroups requiredHits;
};

std::optional<HitGroups> oracleRequiredGroups(
    const std::vector<VertexID>& vertices, const HitGroups& requiredHits, ui k) {
    if (vertices.size() < k) return std::nullopt;
    HitGroups normalized;
    for (const auto& source : requiredHits) {
        std::vector<VertexID> intersection;
        for (VertexID v : vertices)
            if (containsAll(source, {v})) intersection.push_back(v);
        if (intersection.empty()) return std::nullopt;
        if (intersection.size() <= vertices.size() - k)
            normalized.push_back(std::move(intersection));
    }
    return normalized;
}

bool meetsRequiredGroups(const std::vector<VertexID>& answer, const HitGroups& groups) {
    for (const auto& group : groups) {
        bool hit = false;
        for (VertexID v : answer)
            if (containsAll(group, {v})) hit = true;
        if (!hit) return false;
    }
    return true;
}

void requireOracleCount(const GraphFixture& fixture,
                        const std::vector<VertexID>& vertices, ui k, ul actual,
                        const HitGroups& requiredHits = {}) {
    ul expected = 0;
    for (const auto& answer : oracleAnswers(fixture, vertices, k))
        if (meetsRequiredGroups(answer, requiredHits)) ++expected;
    require(actual == expected,
            "Actual temporal SCC answer count differs from the independent time-bucket oracle");
}

std::vector<VertexID> excludedFromGroup(
    const std::vector<VertexID>& vertices, const std::vector<VertexID>& group) {
    std::vector<VertexID> excluded;
    for (VertexID v : vertices)
        if (!containsAll(group, {v})) excluded.push_back(v);
    return excluded;
}

void collectLeaves(GraphFixture& fixture, const std::vector<VertexID>& vertices, ui k,
                   const HitGroups& requiredHits, std::vector<OwnedLeaf>& leaves) {
    const auto normalized = oracleRequiredGroups(vertices, requiredHits, k);
    if (!normalized) return;
    if (vertices.size() == k) {
        leaves.push_back({vertices, *normalized});
        return;
    }
    const auto split = materializeSplit(fixture, vertices, k);
    if (split.size() <= 1) {
        leaves.push_back({vertices, *normalized});
        return;
    }
    auto branchHits = *normalized;
    for (const auto& group : split) {
        require(group.size() < vertices.size(), "A recursive group made no progress");
        for (const auto& component : fixture.graph->findWindowStaticSCCs(
                group, fixture.graph->getWindow(), k - 1))
            collectLeaves(fixture, component, k, branchHits, leaves);
        branchHits.push_back(excludedFromGroup(vertices, group));
    }
}

void verifyRecursiveCoverage(GraphFixture& fixture, ui k, const HitGroups& requiredHits = {}) {
    std::vector<OwnedLeaf> leaves;
    collectLeaves(fixture, fixture.vertices, k, requiredHits, leaves);
    for (const auto& answer : oracleAnswers(fixture, fixture.vertices, k)) {
        std::size_t owners = 0;
        for (const auto& leaf : leaves)
            if (containsAll(leaf.vertices, answer) &&
                meetsRequiredGroups(answer, leaf.requiredHits)) ++owners;
        require(owners == (meetsRequiredGroups(answer, requiredHits) ? 1u : 0u),
                "Recursive requiredHits decomposition lost or multiply owned a valid answer");
    }
}

using MutualGraph = std::vector<std::vector<ui>>;

MutualGraph oracleMutualGraph(const GraphFixture& fixture,
                             const std::vector<VertexID>& vertices) {
    std::vector<std::vector<ui>> arrivals;
    for (VertexID source : vertices)
        arrivals.push_back(oracleArrival(fixture, vertices, source));
    MutualGraph graph(vertices.size());
    const auto unreachable = std::numeric_limits<ui>::max();
    for (ui i = 0; i < vertices.size(); ++i)
        for (ui j = i + 1; j < vertices.size(); ++j)
            if (arrivals[i][vertices[j]] != unreachable &&
                arrivals[j][vertices[i]] != unreachable) {
                graph[i].push_back(j);
                graph[j].push_back(i);
            }
    return graph;
}

MutualGraph canonicalMutualGraph(MutualGraph graph) {
    for (ui i = 0; i < graph.size(); ++i) {
        auto& row = graph[i];
        std::sort(row.begin(), row.end());
        require(std::adjacent_find(row.begin(), row.end()) == row.end(),
                "Mutual graph contains duplicate edges");
        for (ui j : row)
            require(j < graph.size() && j != i &&
                    std::find(graph[j].begin(), graph[j].end(), i) != graph[j].end(),
                    "Mutual graph must use valid local indices and symmetric non-self edges");
    }
    return graph;
}

std::vector<std::vector<VertexID>> canonicalGroups(std::vector<std::vector<VertexID>> groups) {
    for (auto& group : groups) group = sortedSet(std::move(group));
    std::sort(groups.begin(), groups.end());
    return groups;
}

MutualGraph verifyMutualGraph(GraphFixture& fixture, const std::vector<VertexID>& vertices) {
    const auto actual = fixture.graph->buildMutualReachabilityGraph(vertices);
    const auto expected = oracleMutualGraph(fixture, vertices);
    require(actual.size() == vertices.size() &&
            canonicalMutualGraph(actual) == canonicalMutualGraph(expected),
            "Mutual graph differs from the independent temporal reachability oracle");
    return actual;
}

void testDeterministicSplit() {
    const std::vector<VertexID> ids = {1, 4, 7, 10, 13, 16};
    GraphFixture fixture(18, 4, {
        {1, 4, 2}, {1, 4, 2}, {1, 4, 2}, {1, 4, 2},
        {1, 7, 3}, {1, 7, 3}, {1, 7, 3}, {1, 7, 3},
        {4, 7, 2}, {7, 10, 1}, {10, 7, 1},
        {13, 16, 1}, {16, 13, 1},
        {0, 10, 0}, {10, 17, 0}
    }, ids);
    const auto frozenGroups = oracleSplitForPivot(fixture, ids, 1);
    require(canonicalGroups(frozenGroups) ==
                canonicalGroups({{1, 4, 7}, {13, 16}, {4, 7, 10}}),
            "Frozen-label fixture must retain both reverse covers under strict forward propagation");
    for (ui k = 2; k <= 4; ++k) {
        verifySplit(fixture, ids, k);
        verifyRecursiveCoverage(fixture, k);
    }
    std::vector<VertexID> reversed(ids.rbegin(), ids.rend());
    verifySplit(fixture, reversed, 2);
    verifySplit(fixture, ids, 2);
}

void testPivotSelection() {
    GraphFixture fixture(6, 4, {
        {0, 1, 0}, {0, 1, 1}, {0, 1, 2},
        {1, 2, 1}, {2, 1, 1}, {2, 3, 1},
        {2, 4, 1}, {2, 5, 1}
    });
    require(oracleDegreePivot(fixture, fixture.vertices) == 1,
            "The included event-degree enumerateCom must count repeated edge events");
    for (const std::vector<VertexID> vertices : {
            fixture.vertices, std::vector<VertexID>{2, 1, 0}, {5, 4, 3}, {4}})
        verifySplit(fixture, vertices, 2);
    const auto first = fixture.graph->buildSplitPlan(fixture.vertices, 3);
    fixture.graph->prepareSubsetIndex({4});
    const auto repeated = fixture.graph->buildSplitPlan(fixture.vertices, 3);
    require(repeated.decompose == first.decompose && (!first.decompose || repeated.pivot == first.pivot),
            "Repeated forward scoring must not depend on a stale subset index");
    fixture.graph->buildWindow(3, 3, fixture.vertices);
    verifySplit(fixture, {3, 2, 1}, 2);
    verifySplit(fixture, {4, 2, 0}, 2);
}

void testReversePivotRanking() {
    const std::vector<VertexID> ids = {2, 5, 8, 11, 14, 17};
    GraphFixture fixture(20, 4, {
        {2, 5, 1}, {5, 8, 1}, {8, 5, 1},
        {8, 11, 2}, {11, 14, 2}, {14, 11, 2}, {2, 17, 1},
        {17, 19, 2}, {19, 14, 2}
    }, ids);
    const auto unreachable = std::numeric_limits<ui>::max();
    std::vector<ui> arrival(fixture.graph->getVertexCount(), unreachable);
    require(verifyReversePivot(fixture, ids, arrival, {14, 11}) == 11 &&
            verifyReversePivot(fixture, ids, arrival, {11, 14}) == 11,
            "Higher event degree must take priority over remaining-set order");
    require(verifyReversePivot(fixture, ids, arrival, {2, 14}) == 14,
            "Equal-degree reverse seeds must use the full parent time-feature score");
    verifyReversePivot(fixture, ids, arrival, {2, 8, 14, 11, 17});
    require(verifyReversePivot(fixture, ids, arrival, {17}) == 17,
            "A single remaining Ux vertex must be returned directly");
    arrival[11] = 2;
    require(verifyReversePivot(fixture, ids, arrival, {11, 14}) == 11,
            "Frozen forward arrival must not change degree/time-feature ranking");
    arrival[11] = unreachable;
    require(verifyReversePivot(fixture, {2, 8, 11, 14}, arrival, {2, 14}) == 14,
            "Reverse degree and time features must exclude events outside the parent set");
    fixture.graph->buildWindow(0, 0, ids);
    require(verifyReversePivot(fixture, ids, arrival, {8, 2}) == 8,
            "A window without events must preserve remaining-set tie order");
    fixture.graph->buildWindow(0, 3, ids);
    verifyReversePivot(fixture, ids, arrival, {2, 14, 17});
}

void testReverseCoverHelper() {
    const std::vector<VertexID> ids = {1, 4, 7, 10, 13, 16};
    GraphFixture fixture(18, 4, {
        {1, 4, 2}, {4, 7, 3}, {7, 10, 1}, {10, 13, 2}, {13, 16, 1}
    }, ids);
    fixture.graph->prepareSubsetIndex(ids);
    TemporalSCCsTestAccess::SplitResult forward;
    std::vector<ui> arrival;
    fixture.graph->computeEarliestArrival(1, ids, arrival, forward, true);
    const auto frozen = arrival;
    require(sortedSet(forward.R) == std::vector<VertexID>({1, 4, 7}) &&
            sortedSet(forward.U) == std::vector<VertexID>({10, 13, 16}),
            "Helper fixture must contain both reached and unreachable vertices");
    fixture.graph->prepareSubsetIndex({16});
    const auto first = fixture.graph->buildReverseCover(10, ids, arrival, forward.U);
    require(sortedSet(first.first) == std::vector<VertexID>({7, 10}) &&
            sortedSet(first.second) == std::vector<VertexID>({13, 16}),
            "Reverse helper must return complete U1 and exactly Ux minus U1");
    require(arrival == frozen, "Reverse helper modified the initial arrival labels");
    fixture.graph->prepareSubsetIndex(first.second);
    const auto second = fixture.graph->buildReverseCover(16, ids, arrival, first.second);
    require(sortedSet(second.first) == std::vector<VertexID>({7, 10, 13, 16}) &&
            second.second.empty(),
            "Later groups must supplement from all original candidates, including prior U1");
    require(arrival == frozen, "Later reverse helper compacted or modified arrival");
    require(!containsAll(second.first, {4}),
            "Reverse closure must exclude an incoming edge equal to the frozen arrival");
    require(oracleArrival(fixture, ids, 10)[16] == std::numeric_limits<ui>::max() &&
            containsAll(second.first, {10}),
            "Threshold reverse closure must retain a predecessor with no temporal path to the seed");
}

void testRandomSplits() {
    std::mt19937 random(20261017);
    for (unsigned trial = 0; trial < 48; ++trial) {
        const ui n = 6 + random() % 3;
        std::vector<TestEdge> edges;
        for (VertexID u = 0; u < n; ++u)
            for (VertexID v = 0; v < n; ++v) {
                if (u == v || random() % 100 >= 28 + trial % 35) continue;
                edges.push_back({u, v, static_cast<ui>(random() % 5)});
                if (random() % 4 == 0)
                    edges.push_back({u, v, static_cast<ui>(random() % 5)});
            }
        GraphFixture fixture(n, 5, std::move(edges));
        for (ui k = 2; k <= 4; ++k) {
            verifySplit(fixture, fixture.vertices, k);
            verifyRecursiveCoverage(fixture, k);
            verifyRecursiveCoverage(fixture, k,
                {{0, 1, n + 10}, {1, 2, 3}, fixture.vertices});
            verifyRecursiveCoverage(fixture, k, {{n + 10}});
        }
    }
}

ul verifyQuery(GraphFixture& fixture, ui left, ui right, ui k) {
    CoutCapture capture;
    const auto answers = fixture.graph->enumerateDec(left, right, k);
    require(answers == fixture.graph->getAnswerCount(), "Query return value differs from stored count");
    requireOracleCount(fixture, fixture.vertices, k, fixture.graph->getAnswerCount());
    require(capture.output.str().empty(), "V1 queries must not emit diagnostic output");
    return fixture.graph->getAnswerCount();
}

struct TraversalStatistics {
    ul calls = 0;
    ul expanded = 0;
    ul leafCount = 0;
    ul vertexSum = 0;
    ul combinationSum = 0;
    ul duplicateStates = 0;
    ul prunedStates = 0;
};

enum class ReferenceMode { NoOwnership, RequiredHits };

struct ReferenceTraversal {
    GraphFixture& fixture;
    ui k;
    ReferenceMode mode;
    bool useStoppingRules;
    TraversalStatistics statistics;
    std::set<std::vector<VertexID>> canonicalSeen;

    ReferenceTraversal(GraphFixture& input, ui size, ReferenceMode traversalMode,
                       bool stopEarly = true)
        : fixture(input), k(size), mode(traversalMode), useStoppingRules(stopEarly) {}

    void visit(const std::vector<VertexID>& vertices, const HitGroups& requiredHits = {}) {
        if (vertices.size() < k) return;
        require(++statistics.calls <= 20000, "Reference traversal exceeded its call limit");
        HitGroups branchHits;
        if (mode != ReferenceMode::NoOwnership) {
            const auto normalized = oracleRequiredGroups(vertices, requiredHits, k);
            if (!normalized) {
                ++statistics.prunedStates;
                return;
            }
            branchHits = *normalized;
        }
        const bool newCanonical = canonicalSeen.insert(sortedSet(vertices)).second;
        if (!newCanonical) ++statistics.duplicateStates;
        ++statistics.expanded;
        const bool direct = useStoppingRules &&
            fixture.graph->shouldEnumerateDirectly(vertices, k);
        if (vertices.size() == k || direct) {
            recordLeaf(vertices.size());
            return;
        }
        const auto groups = materializeSplit(fixture, vertices, k);
        if (groups.size() <= 1) {
            recordLeaf(vertices.size());
            return;
        }
        if (useStoppingRules) {
            ul combinations = 1;
            for (ui i = 1; i <= k; ++i)
                combinations = combinations * (vertices.size() - k + i) / i;
            if (oracleSplitCost(fixture, groups, k).first >=
                1.1L * combinations) {
                recordLeaf(vertices.size());
                return;
            }
        }
        for (const auto& group : groups) {
            require(group.size() < vertices.size(), "Reference split made no progress");
            for (const auto& component : fixture.graph->findWindowStaticSCCs(
                    group, fixture.graph->getWindow(), k - 1))
                visit(component, branchHits);
            if (mode != ReferenceMode::NoOwnership)
                branchHits.push_back(excludedFromGroup(vertices, group));
        }
    }

    void recordLeaf(ul size) {
        ++statistics.leafCount;
        statistics.vertexSum += size;
        ul combinations = 1;
        for (ui i = 1; i <= k; ++i)
            combinations = combinations * (size - k + i) / i;
        statistics.combinationSum += combinations;
    }
};

void testRequiredHitsEntry() {
    constexpr ui n = 6;
    std::vector<TestEdge> edges;
    for (VertexID u = 0; u < n; ++u)
        for (VertexID v = 0; v < n; ++v)
            if (u != v) edges.push_back({u, v, 0});
    GraphFixture fixture(n, 1, std::move(edges));
    const std::vector<HitGroups> cases = {
        {}, {{0}}, {{1}}, {{0}, {1}}, {{0, n + 3}, fixture.vertices},
        {{n + 3}}, {{}}, {fixture.vertices}
    };
    for (const auto& item : cases) {
        CoutCapture capture;
        const auto before = fixture.graph->getAnswerCount();
        fixture.graph->decompose(fixture.vertices, 3, item);
        requireOracleCount(fixture, fixture.vertices, 3,
                           fixture.graph->getAnswerCount() - before, item);
            require(capture.output.str().empty(), "Constrained search must not emit diagnostic output");
    }
    const std::vector<VertexID> subset = {0, 2, 4};
    for (VertexID required : {VertexID{2}, VertexID{1}, VertexID{2}}) {
        CoutCapture capture;
        const auto before = fixture.graph->getAnswerCount();
        fixture.graph->decompose(subset, 3, {{required}});
        requireOracleCount(fixture, subset, 3,
                           fixture.graph->getAnswerCount() - before, {{required}});
            require(capture.output.str().empty(), "Size-k search must not emit diagnostic output");
    }
    bool badK = false;
    try { fixture.graph->decompose(fixture.vertices, 1, {}); }
    catch (const std::invalid_argument&) { badK = true; }
    require(badK, "The constrained V1 entry must reject invalid k");
}

void testRecursiveRequiredHits() {
    constexpr ui n = 44, times = 8;
    std::mt19937 random(20261023);
    std::vector<TestEdge> edges;
    for (VertexID v = 0; v < n; ++v)
        edges.push_back({v, (v + 1) % n, static_cast<ui>(random() % times)});
    for (ui i = 0; i < n * 2; ++i) {
        const VertexID u = random() % n, v = random() % n;
        if (u != v) edges.push_back({u, v, static_cast<ui>(random() % times)});
    }
    GraphFixture fixture(n, times, std::move(edges));
    require(!fixture.graph->shouldEnumerateDirectly(fixture.vertices, 3),
            "Repeated-state fixture must exceed the real enumeration threshold");
    const auto roots = fixture.graph->findWindowStaticSCCs(
        fixture.vertices, fixture.graph->getWindow(), 2);
    require(roots.size() == 1 && roots.front().size() == n,
            "Repeated-state fixture must start from one full static SCC");
    ReferenceTraversal unowned(fixture, 3, ReferenceMode::NoOwnership, false);
    ReferenceTraversal owned(fixture, 3, ReferenceMode::RequiredHits, false);
    unowned.visit(fixture.vertices);
    owned.visit(fixture.vertices);
    const auto& old = unowned.statistics;
    const auto& directExpected = owned.statistics;
    require(old.duplicateStates > 0 && directExpected.prunedStates > 0,
            "Fixture must repeat candidate states and reject impossible ownership branches");
    require(old.expanded > directExpected.expanded && old.leafCount >= directExpected.leafCount &&
            old.vertexSum >= directExpected.vertexSum && old.combinationSum >= directExpected.combinationSum,
            "RequiredHits must reduce expanded recursion without increasing candidate leaves");

    const auto first = verifyQuery(fixture, 0, times - 1, 3);

    for (unsigned pass = 0; pass < 3; ++pass) {
        auto vertices = fixture.vertices;
        if (pass == 1) std::reverse(vertices.begin(), vertices.end());
        CoutCapture capture;
        const auto before = fixture.graph->getAnswerCount();
        fixture.graph->decompose(vertices, 3, {});
        requireOracleCount(fixture, vertices, 3, fixture.graph->getAnswerCount() - before);
            require(capture.output.str().empty(), "Recursive search must not emit diagnostic output");
    }

    const std::vector<HitGroups> inheritedCases = {
        {{0}}, {{1}}, {{0, 1}, {2, 3}}, {{n + 10}}, {fixture.vertices}, {}
    };
    for (const auto& requiredHits : inheritedCases) {
        CoutCapture capture;
        const auto before = fixture.graph->getAnswerCount();
        fixture.graph->decompose(fixture.vertices, 3, requiredHits);
        requireOracleCount(fixture, fixture.vertices, 3,
                           fixture.graph->getAnswerCount() - before, requiredHits);
            require(capture.output.str().empty(), "Constrained search must not emit diagnostic output");
    }

    verifyQuery(fixture, 0, times - 1, 2);
    verifyQuery(fixture, 1, times - 1, 3);
    for (unsigned pass = 0; pass < 2; ++pass) {
        require(verifyQuery(fixture, 0, times - 1, 3) == first,
                "Repeated V1 query must reset answers and ownership state after intervening queries");
    }
}

void testQueryIntegration() {
    constexpr ui n = 33;
    constexpr ul combinations = 5456;
    std::vector<TestEdge> edges;
    for (VertexID u = 0; u < n; ++u) edges.push_back({u, (u + 1) % n, u % 3});
    for (VertexID u = 0; u < n; ++u)
        for (VertexID v = 0; v < n; ++v)
            if (u != v) edges.push_back({u, v, 3});
    GraphFixture fixture(n, 4, std::move(edges));
    require(!fixture.graph->shouldEnumerateDirectly(fixture.vertices, 3),
            "Integration fixture must exceed the k+alpha size threshold");
    const auto denseV1 = verifyQuery(fixture, 3, 3, 3);
    require(denseV1 == combinations, "Dense query must accept every triple");
    require(verifyQuery(fixture, 0, 2, 3) == 0,
            "The temporal cycle must have no induced temporal triples");
    require(fixture.graph->buildSplitPlan(fixture.vertices, 3).decompose,
            "The temporal cycle must exercise recursive decomposition");
    require(verifyQuery(fixture, 3, 3, 3) == denseV1,
            "Repeated V1 query accumulated answers across queries");
    bool badOrder = false, badRange = false, badK = false;
    try { fixture.graph->enumerateDec(2, 1, 3); }
    catch (const std::out_of_range&) { badOrder = true; }
    try { fixture.graph->enumerateDec(0, 4, 3); }
    catch (const std::out_of_range&) { badRange = true; }
    try { fixture.graph->enumerateDec(0, 2, 1); }
    catch (const std::invalid_argument&) { badK = true; }
    require(badOrder && badRange && badK, "V1 must reject invalid windows and k");
    require(fixture.graph->getWindow().left == 3 && fixture.graph->getWindow().right == 3,
            "Invalid queries must preserve the previous window");
    require(verifyQuery(fixture, 3, 3, 3) == denseV1,
            "A valid V1 query must work after rejected input");
}

void testQueryMethodIsolation() {
    constexpr ui n = 44, times = 8;
    std::mt19937 random(20261027);
    std::vector<TestEdge> edges;
    for (VertexID v = 0; v < n; ++v)
        edges.push_back({v, (v + 1) % n, static_cast<ui>(random() % times)});
    for (ui i = 0; i < n * 2; ++i) {
        const VertexID u = random() % n, v = random() % n;
        if (u != v) edges.push_back({u, v, static_cast<ui>(random() % times)});
    }
    GraphFixture fixture(n, times, std::move(edges));
    const auto first = verifyQuery(fixture, 0, times - 1, 3);
    for (ui k : {ui{2}, ui{3}}) {
        CoutCapture capture;
        fixture.graph->enumerateCom(0, times - 1, k);
        requireOracleCount(fixture, fixture.vertices, k, fixture.graph->getAnswerCount());
        const auto mutual = fixture.graph->enumerateRG(0, times - 1, k);
        requireOracleCount(fixture, fixture.vertices, k, mutual);
        fixture.graph->enumerateDec(0, times - 1, k);
        requireOracleCount(fixture, fixture.vertices, k, fixture.graph->getAnswerCount());
        require(capture.output.str().empty(), "Query methods must not emit diagnostic output");
    }
    verifyQuery(fixture, 1, times - 1, 3);
    require(verifyQuery(fixture, 0, times - 1, 3) == first,
            "Other methods, windows and k values must not leak state into optimized queries");
}

void testPrunedQueryWindowsAndRoots() {
    constexpr ui n = 44, times = 8;
    std::mt19937 random(20261022);
    std::vector<TestEdge> edges;
    for (VertexID v = 0; v < n; ++v)
        edges.push_back({v, (v + 1) % n, static_cast<ui>(random() % times)});
    for (ui i = 0; i < n; ++i) {
        const VertexID u = random() % n, v = random() % n;
        if (u != v) edges.push_back({u, v, static_cast<ui>(random() % times)});
    }
    GraphFixture fixture(n, times, std::move(edges));
    require(!fixture.graph->shouldEnumerateDirectly(fixture.vertices, 3),
            "Pruned traversal fixture must exceed the k+alpha size threshold");
    const auto first = verifyQuery(fixture, 0, times - 1, 3);
    verifyQuery(fixture, 0, times - 1, 2);
    verifyQuery(fixture, 1, times - 1, 3);
    require(verifyQuery(fixture, 0, times - 1, 3) == first,
            "Answer counts must reset across windows and k changes");

    GraphFixture small(4, 3, {{0, 1, 2}, {1, 2, 2}, {2, 0, 2}});
    const auto direct = verifyQuery(small, 2, 2, 3);
    const auto oneRoot = small.graph->findWindowStaticSCCs(small.vertices,small.graph->getWindow(),2);
    require(oneRoot.size() == 1 && oneRoot.front().size() == 3,
            "The size-k fixture must contain one eligible static root");
    require(verifyQuery(small, 0, 1, 3) == 0,
            "An empty window must have no answers");
    require(verifyQuery(small, 2, 2, 3) == direct,
            "An empty query must not leak answers into the following query");

    GraphFixture multiple(8, 3,
        {{0, 1, 2}, {1, 2, 2}, {2, 0, 2}, {3, 4, 2}, {4, 5, 2}, {5, 3, 2}});
    verifyQuery(multiple, 2, 2, 3);
    const auto roots = multiple.graph->findWindowStaticSCCs(multiple.vertices,multiple.graph->getWindow(),2);
    require(roots.size() == 2 && roots[0].size() == 3 && roots[1].size() == 3,
            "The multiple-root fixture must retain both eligible static roots");
}

void verifyV1Search(GraphFixture& fixture, const std::vector<VertexID>& vertices, ui k) {
    CoutCapture capture;
    const auto before = fixture.graph->getAnswerCount();
    fixture.graph->decompose(vertices, k, {});
    requireOracleCount(fixture, vertices, k, fixture.graph->getAnswerCount() - before);
    require(capture.output.str().empty(), "V1 search must not emit diagnostic output");
}

void testMutualGraph() {
    const std::vector<VertexID> ids = {1, 4, 7, 10, 13};
    GraphFixture fixture(18, 4, {
        {7, 4, 1}, {4, 1, 1}, {4, 7, 1}, {1, 4, 1},
        {7, 10, 2}, {10, 7, 2}, {10, 13, 0}, {13, 10, 0},
        {13, 0, 2}, {0, 1, 2}, {1, 0, 2}, {0, 13, 2}
    }, ids);
    const auto graph = verifyMutualGraph(fixture, ids);
    require(canonicalMutualGraph(graph) == MutualGraph({{1}, {0, 2}, {1, 3}, {2, 4}, {3}}),
            "Equal-time multi-hop paths must remain unreachable");
    for (ui k : {ui{2}, ui{3}, ui{4}})
        verifyV1Search(fixture, ids, k);
    fixture.graph->prepareSubsetIndex({10, 7});
    verifyMutualGraph(fixture, {13, 7, 1, 10});
    verifyMutualGraph(fixture, {7});
    verifyMutualGraph(fixture, {});
    verifyV1Search(fixture, {7}, 2);
    verifyV1Search(fixture, {}, 2);
    fixture.graph->buildWindow(1, 1, ids);
    const auto narrow = verifyMutualGraph(fixture, ids);
    require(canonicalMutualGraph(narrow) == MutualGraph({{1}, {0, 2}, {1}, {}, {}}),
            "Mutual reachability must exclude events outside the active window");
    fixture.graph->buildWindow(0, 3, ids);
    verifyMutualGraph(fixture, ids);
}

void testMutualGraphAndV1Random() {
    std::mt19937 random(20261023);
    for (ui trial = 0; trial < 36; ++trial) {
        const ui n = 5 + random() % 4;
        std::vector<TestEdge> edges;
        for (VertexID u = 0; u < n; ++u)
            for (VertexID v = 0; v < n; ++v) {
                if (u == v || random() % 100 >= 18 + trial % 42) continue;
                edges.push_back({u, v, static_cast<ui>(random() % 5)});
                if (random() % 5 == 0)
                    edges.push_back({u, v, static_cast<ui>(random() % 5)});
            }
        GraphFixture fixture(n, 5, std::move(edges));
        verifyMutualGraph(fixture, fixture.vertices);
        for (ui k = 2; k <= 4; ++k)
            verifyV1Search(fixture, fixture.vertices, k);
        auto reversed = fixture.vertices;
        std::reverse(reversed.begin(), reversed.end());
        verifyMutualGraph(fixture, reversed);
    }
}

void testMutualGraphInducedRebuild() {
    GraphFixture fixture(7, 5, {
        {0, 1, 2}, {0, 5, 4}, {1, 0, 0}, {1, 6, 4},
        {2, 1, 0}, {2, 3, 2}, {2, 4, 1}, {3, 1, 4}, {3, 2, 1},
        {4, 2, 3}, {4, 5, 0}, {4, 6, 4}, {5, 2, 2}, {5, 3, 2},
        {5, 4, 1}, {6, 4, 1}, {6, 5, 0}
    });
    const auto parent = verifyMutualGraph(fixture, fixture.vertices);
    require(std::find(parent[1].begin(), parent[1].end(), 6) != parent[1].end(),
            "The full induced graph must retain the witnessed mutual edge");
    const auto child = verifyMutualGraph(fixture, {1, 2, 3, 4, 5, 6});
    require(std::find(child[0].begin(), child[0].end(), 5) != child[0].end(),
            "The intermediate induced graph must retain its valid mutual edge");
    require(canonicalMutualGraph(verifyMutualGraph(fixture, {1, 6})) == MutualGraph(2),
            "Removing intermediate vertices must discard their former mutual edge");
    for (ui k : {ui{2}, ui{3}, ui{4}})
        verifyV1Search(fixture, fixture.vertices, k);
    verifyQuery(fixture, 0, 4, 3);
}

void testOptimizedQueryIntegration() {
    std::vector<TestEdge> edges;
    for (VertexID v = 0; v < 6; ++v) {
        edges.push_back({v, v + 1, v});
        edges.push_back({v + 1, v, v});
    }
    for (VertexID u = 0; u < 7; ++u)
        for (VertexID v = 0; v < 7; ++v)
            if (u != v) edges.push_back({u, v, 6});
    GraphFixture fixture(7, 7, std::move(edges));
    require(fixture.graph->shouldEnumerateDirectly(fixture.vertices,3),
            "V1 must retain its direct stop for this small candidate set");
    const auto dense = verifyQuery(fixture, 6, 6, 3);
    require(dense == 35, "A complete seven-point temporal graph must accept every triple");
    verifyQuery(fixture, 0, 5, 3);
    verifyQuery(fixture, 0, 5, 2);
    verifyQuery(fixture, 0, 5, 3);
    require(verifyQuery(fixture, 6, 6, 3) == dense,
            "V1 answer counts must reset after other windows and k values");

    GraphFixture sizeK(3, 2, {{0, 1, 0}, {1, 0, 0}, {1, 2, 1}, {2, 1, 1}});
    verifyQuery(sizeK, 0, 1, 3);
    verifyV1Search(sizeK, sizeK.vertices, 3);

    GraphFixture multiple(8, 3,
        {{0, 1, 2}, {1, 2, 2}, {2, 0, 2}, {3, 4, 2}, {4, 5, 2}, {5, 3, 2}});
    const auto bothRoots = verifyQuery(multiple, 2, 2, 3);
    require(verifyQuery(multiple, 0, 1, 3) == 0,
            "A V1 query without eligible static SCCs must have no answers");
    require(verifyQuery(multiple, 2, 2, 3) == bothRoots,
            "An empty V1 query must not leak answers into a later nonempty query");

    bool badOrder = false, badRange = false, badK = false, badSearchK = false;
    try { fixture.graph->enumerateDec(2, 1, 3); }
    catch (const std::out_of_range&) { badOrder = true; }
    try { fixture.graph->enumerateDec(0, 7, 3); }
    catch (const std::out_of_range&) { badRange = true; }
    try { fixture.graph->enumerateDec(0, 5, 1); }
    catch (const std::invalid_argument&) { badK = true; }
    try { fixture.graph->decompose(fixture.vertices, 1, {}); }
    catch (const std::invalid_argument&) { badSearchK = true; }
    require(badOrder && badRange && badK && badSearchK,
            "V1 query and direct search must validate windows and minimum k");
    require(fixture.graph->getWindow().left == 6 && fixture.graph->getWindow().right == 6,
            "Rejected V1 input must preserve the previous valid window");
    require(verifyQuery(fixture, 6, 6, 3) == dense,
            "A valid V1 query must remain usable after rejected input");
}

void verifyDirectEnumeration(GraphFixture& fixture, const std::vector<VertexID>& vertices,
                             ui k, const HitGroups& requiredHits = {}) {
    CoutCapture capture;
    const auto before = fixture.graph->getAnswerCount();
    fixture.graph->enumerateCombinations(vertices, k, requiredHits);
    requireOracleCount(fixture, vertices, k, fixture.graph->getAnswerCount() - before, requiredHits);
}

void testActualEnumerationGroups() {
    const std::vector<VertexID> ids = {9, 1, 7, 3, 11};
    std::vector<TestEdge> edges;
    for (VertexID u : ids)
        for (VertexID v : ids)
            if (u != v) edges.push_back({u, v, 1});
    GraphFixture fixture(13, 3, std::move(edges), ids);
    const std::vector<HitGroups> cases = {
        {}, {{9}}, {{9}, {1}}, {{9, 1}, {7, 3}},
        {{9, 9, 1, 9}, {9, 9}, {7, 7, 3, 7}},
        {{99}}, {{}}, {{9, 99}, {1, 1}, {7, 3, 11}},
        {{9}, {9, 1}, {9, 7}, {9, 3}}, {{9}, {1}, {7}}
    };
    for (ui k : {ui{2}, ui{3}, ui{4}})
        for (const auto& requiredHits : cases)
            verifyDirectEnumeration(fixture, ids, k, requiredHits);
    const std::vector<VertexID> exactlyK = {7, 9, 1};
    for (const HitGroups requiredHits : {HitGroups{}, HitGroups{{7, 7}},
                                        HitGroups{{3}}, HitGroups{{}}})
        verifyDirectEnumeration(fixture, exactlyK, 3, requiredHits);
    verifyDirectEnumeration(fixture, ids, 3, {{9}, {1}});
    verifyDirectEnumeration(fixture, {9}, 2);

    std::vector<TestEdge> complete;
    for (VertexID u = 0; u < 5; ++u)
        for (VertexID v = 0; v < 5; ++v)
            if (u != v) complete.push_back({u, v, 0});
    GraphFixture repeated(5, 1, std::move(complete));
    {
        CoutCapture capture;
        const auto before = repeated.graph->getAnswerCount();
        repeated.graph->decompose(repeated.vertices, 3, {{0, 0, 0}});
        requireOracleCount(repeated, repeated.vertices, 3,
                           repeated.graph->getAnswerCount() - before, {{0}});
        require(repeated.graph->getAnswerCount() - before == 6,
                "A repeated-ID required group must still require vertex 0 in every answer");
    }
}

void testActualEnumerationTemporalPaths() {
    GraphFixture sameTime(4, 4,
        {{2, 3, 2}, {1, 2, 2}, {0, 1, 2}, {3, 0, 2}});
    for (ui k : {ui{2}, ui{3}, ui{4}})
        verifyDirectEnumeration(sameTime, sameTime.vertices, k);
    require(oracleAnswers(sameTime, sameTime.vertices, 4).empty(),
            "Equal-time cycle fixture must not have a strict four-point answer");

    GraphFixture waiting(3, 4,
        {{0, 1, 0}, {1, 0, 0}, {1, 2, 2}, {2, 0, 3}, {2, 1, 3}});
    verifyDirectEnumeration(waiting, waiting.vertices, 3);
    require(oracleAnswers(waiting, waiting.vertices, 3).size() == 1,
            "Waiting between events must preserve the fixture's three-point answer");
    waiting.graph->buildWindow(1, 3, waiting.vertices);
    verifyDirectEnumeration(waiting, waiting.vertices, 3);
    require(oracleAnswers(waiting, waiting.vertices, 3).empty(),
            "Removing the early departure must invalidate the waiting fixture");

    GraphFixture decreasing(3, 3, {{0, 1, 2}, {1, 2, 1}, {2, 0, 2}});
    verifyDirectEnumeration(decreasing, decreasing.vertices, 3);
    require(oracleAnswers(decreasing, decreasing.vertices, 3).empty(),
            "A static directed cycle with decreasing timestamps must not be a temporal answer");

    GraphFixture bridge(3, 2, {{0, 1, 0}, {1, 0, 0}, {1, 2, 0}, {2, 1, 0},
                                {0, 1, 1}, {1, 0, 1}, {1, 2, 1}, {2, 1, 1}});
    verifyDirectEnumeration(bridge, bridge.vertices, 2);
    verifyDirectEnumeration(bridge, {2, 0}, 2);
    verifyDirectEnumeration(bridge, bridge.vertices, 3);
    require(oracleAnswers(bridge, {0, 2}, 2).empty() &&
            oracleAnswers(bridge, bridge.vertices, 3).size() == 1,
            "Temporal paths for an answer must stay inside its selected k vertices");

    GraphFixture improved(4, 5, {{0, 1, 3}, {0, 2, 0}, {2, 1, 1}, {1, 3, 2},
                                {3, 0, 4}, {3, 1, 4}, {3, 2, 4}});
    for (ui k : {ui{2}, ui{3}, ui{4}})
        verifyDirectEnumeration(improved, improved.vertices, k);
    require(oracleAnswers(improved, improved.vertices, 4).size() == 1,
            "An improved arrival must reopen an earlier useful outgoing event");
}

void testActualEnumerationQueries() {
    std::mt19937 random(20261027);
    for (ui trial = 0; trial < 24; ++trial) {
        const ui n = 5 + random() % 4;
        std::vector<TestEdge> edges;
        for (VertexID u = 0; u < n; ++u)
            for (VertexID v = 0; v < n; ++v) {
                if (u == v || random() % 100 >= 22 + trial % 55) continue;
                edges.push_back({u, v, static_cast<ui>(random() % 5)});
                if (random() % 4 == 0)
                    edges.push_back({u, v, static_cast<ui>(random() % 5)});
            }
        GraphFixture fixture(n, 5, std::move(edges));
        for (const auto window : {std::pair<ui, ui>{0, 4}, {1, 3}, {2, 2}})
            for (ui k : {ui{2}, ui{3}, ui{4}}) {
                verifyQuery(fixture, window.first, window.second, k);
            }
    }
}

bool verifyV1DirectDecision(GraphFixture& fixture,const std::vector<VertexID>& vertices,
                            ui k,const HitGroups& groups = {}) {
    const auto before = fixture.graph->getAnswerCount();
    const auto alpha = fixture.graph->getDecompositionAlpha();
    const bool expected = static_cast<ul>(vertices.size()) <= static_cast<ul>(k) + alpha;
    const bool actual = fixture.graph->shouldEnumerateDirectly(vertices,k);
    require(actual == expected && fixture.graph->shouldEnumerateDirectly(vertices,k) == expected,
            "Direct stopping must match the independent n <= k + alpha boundary");
    require(fixture.graph->getAnswerCount() == before && fixture.graph->getDecompositionAlpha() == alpha,
            "Stopping decisions must not change answer counts or alpha");
    return actual;
}

void testV1AlphaSizeBoundaries() {
    GraphFixture fixture(40,1,{{0,1,0}});
    for (ui alpha : {0u,2u,4u,6u,8u,10u,12u}) {
        fixture.graph->setDecompositionAlpha(alpha);
        for (ui k = 2; k <= 8; ++k)
            for (ui n = 0; n <= k + alpha + 1; ++n) {
                std::vector<VertexID> vertices;
                for (ui i = 0; i < n; ++i) vertices.push_back(39-i);
                require(verifyV1DirectDecision(fixture,vertices,k) == (n <= k+alpha),
                        "Alpha must accept the exact size boundary and reject the next vertex");
            }
    }
    fixture.graph->setDecompositionAlpha(0);
    std::vector<VertexID> oldBudget;
    for (VertexID v = 0; v < 12; ++v) oldBudget.push_back(v);
    require(!verifyV1DirectDecision(fixture,oldBudget,2),
            "The removed 4096-combination budget must not override alpha=0");
    fixture.graph->setDecompositionAlpha(std::numeric_limits<ui>::max());
    require(verifyV1DirectDecision(fixture,fixture.vertices,2) &&
            verifyV1DirectDecision(fixture,{0},std::numeric_limits<ui>::max()),
            "The threshold must not overflow when alpha or k is at its maximum");
}

void testV1AlphaConstraintIndependence() {
    GraphFixture fixture(40,1,{{0,1,0}});
    std::vector<VertexID> vertices;
    for (VertexID v = 0; v < 35; ++v) vertices.push_back(v);
    const std::vector<HitGroups> constraints = {
        {},{{0}},{{0},{1}},{{33},{34}},{{}},{{99}},
        {{0,0,99},{1,1},{0,1},vertices},{{0,1},{0,2},{0,3},{0,4},{5,6}}
    };
    for (ui alpha : {0u,2u,4u,6u,8u,10u,12u}) {
        fixture.graph->setDecompositionAlpha(alpha);
        std::vector<VertexID> boundary(vertices.begin(),vertices.begin()+3+alpha);
        for (const auto& groups : constraints) {
            require(!verifyV1DirectDecision(fixture,vertices,3,groups),
                    "Ownership must not reintroduce the old DFS or verification stopping budgets");
            require(verifyV1DirectDecision(fixture,boundary,3,groups),
                    "Ownership must not change the exact alpha size boundary");
        }
    }
    bool badZero=false,badOne=false;
    try { fixture.graph->shouldEnumerateDirectly({0,1},0); }
    catch (const std::invalid_argument&) { badZero=true; }
    try { fixture.graph->shouldEnumerateDirectly({0,1},1); }
    catch (const std::invalid_argument&) { badOne=true; }
    require(badZero && badOne,"The alpha stopping rule must reject k below two");
}

void testV1AlphaConstrainedSearch() {
    std::vector<TestEdge> edges;
    for (VertexID v=0;v<34;++v) {
        edges.push_back({v,v+1,v%4});edges.push_back({v+1,v,v%4});
    }
    for (VertexID a=0;a<3;++a) for (VertexID b=0;b<3;++b)
        if (a!=b) edges.push_back({a,b,0});
    GraphFixture fixture(35,4,std::move(edges));
    for (ui alpha : {0u,12u,32u}) {
        fixture.graph->setDecompositionAlpha(alpha);
        require(verifyV1DirectDecision(fixture,fixture.vertices,3,{{0},{1}}) == (alpha==32),
                "Fixture must cover recursive search and the exact direct-stop boundary");
        for (const HitGroups groups : {HitGroups{{0},{1}},HitGroups{{34}},HitGroups{{99}},HitGroups{{}}}) {
            const auto before=fixture.graph->getAnswerCount();
            {
                CoutCapture capture;
                fixture.graph->decompose(fixture.vertices,3,groups);
                require(capture.output.str().empty(),"Constrained search must remain silent");
            }
                    requireOracleCount(fixture,fixture.vertices,3,fixture.graph->getAnswerCount()-before,groups);
        }
    }
}

ul verifyOptimizedEnumeration(GraphFixture& fixture,const std::vector<VertexID>& vertices,
                              ui k,const HitGroups& requiredHits = {},
                              std::optional<ul> expected = std::nullopt) {
    ul oldCount = 0;
    ul newCount = 0;
    std::string oldOutput;
    std::string newOutput;
    {
        CoutCapture capture;
        const auto before = fixture.graph->getAnswerCount();
        fixture.graph->enumerateCombinations(vertices,k,requiredHits);
        oldCount = fixture.graph->getAnswerCount() - before;
        oldOutput = capture.output.str();
    }
    {
        CoutCapture capture;
        const auto before = fixture.graph->getAnswerCount();
        fixture.graph->enumerateOptimized(vertices,k,requiredHits);
        newCount = fixture.graph->getAnswerCount() - before;
        newOutput = capture.output.str();
    }
    require(newCount == oldCount,
            "Optimized enumeration count must match the retained original enumerator");
    require(newOutput.empty() && oldOutput.empty(),
            "Enumeration must not emit diagnostic output");
    if (expected)
        require(newCount == *expected,
                "Optimized enumeration differs from the fixture's independently derived count");
    else
        requireOracleCount(fixture,vertices,k,newCount,requiredHits);
    return newCount;
}

void testOptimizedEnumerationGroups() {
    const std::vector<VertexID> ids = {9,1,7,3,11,5,12,0};
    std::vector<TestEdge> edges;
    for (VertexID u : ids)
        for (VertexID v : ids)
            if (u != v) edges.push_back({u,v,2});
    GraphFixture fixture(14,4,std::move(edges),ids);
    const std::vector<HitGroups> cases = {
        {},{{9}},{{9},{1}},{{9,1},{7,3}},
        {{9,9,1,9},{9,9},{7,7,3,7}},{{99}},{{}},
        {{9,99},{1,1},{7,3,11}},{{9},{9,1},{9,7},{9,3}},
        {{9},{1},{7}},{{9,1},{1,7},{7,3},{3,9}}
    };
    for (ui k = 2; k <= 6; ++k) {
        for (const auto& groups : cases)
            verifyOptimizedEnumeration(fixture,ids,k,groups);
        const std::vector<VertexID> exactlyK(ids.begin(),ids.begin() + k);
        verifyOptimizedEnumeration(fixture,exactlyK,k,{{ids.front()}});
        verifyOptimizedEnumeration(fixture,exactlyK,k,{{99}});
        verifyOptimizedEnumeration(fixture,exactlyK,k,{{}});
    }
    verifyOptimizedEnumeration(fixture,{9},2);
    verifyOptimizedEnumeration(fixture,{},2);

    HitGroups groups(64,std::vector<VertexID>{ids.front()});
    verifyOptimizedEnumeration(fixture,ids,6,groups,21);
    groups.back() = {ids.back()};
    verifyOptimizedEnumeration(fixture,ids,6,groups,15);
    groups.back() = {ids.front()};
    groups.push_back({ids.front()});
    verifyOptimizedEnumeration(fixture,ids,6,groups,21);
    groups.back() = {ids.back()};
    verifyOptimizedEnumeration(fixture,ids,6,groups,15);

    verifyOptimizedEnumeration(fixture,ids,7,{},8);
    bool badZero = false;
    bool badOne = false;
    try { fixture.graph->enumerateOptimized(ids,0,{}); }
    catch (const std::invalid_argument&) { badZero = true; }
    try { fixture.graph->enumerateOptimized(ids,1,{}); }
    catch (const std::invalid_argument&) { badOne = true; }
    require(badZero && badOne,
            "Optimized enumeration must reject k below two");
}

void testOptimizedEnumerationTemporalPaths() {
    GraphFixture sameTime(6,4,{
        {5,0,2},{3,4,2},{1,2,2},{4,5,2},{0,1,2},{2,3,2},
        {0,1,2},{0,0,1},{5,5,3}
    });
    for (ui k = 2; k <= 6; ++k)
        verifyOptimizedEnumeration(sameTime,sameTime.vertices,k,
                                   {},ul{0});
    verifyOptimizedEnumeration(sameTime,{0,2,4},3,{},0);
    sameTime.graph->buildWindow(3,3,sameTime.vertices);
    verifyOptimizedEnumeration(sameTime,sameTime.vertices,6,{},0);
    sameTime.graph->buildWindow(2,2,sameTime.vertices);
    verifyOptimizedEnumeration(sameTime,{5,3,1,4,0,2},6,{},0);

    GraphFixture decreasing(6,7,{
        {0,1,5},{1,2,4},{2,3,3},{3,4,2},{4,5,1},{5,0,5}
    });
    verifyOptimizedEnumeration(decreasing,decreasing.vertices,6,{},0);
    for (ui k = 2; k <= 5; ++k)
        verifyOptimizedEnumeration(decreasing,decreasing.vertices,k);

    std::vector<TestEdge> bridgeEdges;
    const std::vector<VertexID> path{0,6,1,2,3,4,5};
    for(ui t=0;t<6;++t)for(ui i=1;i<path.size();++i) {
        bridgeEdges.push_back({path[i-1],path[i],t});bridgeEdges.push_back({path[i],path[i-1],t});
    }
    GraphFixture bridge(7,7,std::move(bridgeEdges));
    verifyOptimizedEnumeration(bridge,{0,1,2,3,4,5},6,{},0);
    verifyOptimizedEnumeration(bridge,{0,1,2,3,4,6},6,{},1);

    GraphFixture improved(6,6,{
        {0,1,4},{0,2,0},{2,1,1},{1,3,2},{3,4,3},{4,5,4},
        {5,0,5},{5,1,5},{5,2,5},{5,3,5},{5,4,5}
    });
    verifyOptimizedEnumeration(improved,improved.vertices,6,{},1);
    for (ui k = 2; k <= 5; ++k)
        verifyOptimizedEnumeration(improved,improved.vertices,k);
}

void testOptimizedEnumerationLargeCandidates() {
    const std::vector<VertexID> active = {0,1,2,3,4,63,64,69};
    std::vector<TestEdge> edges;
    for (VertexID u : active)
        for (VertexID v : active)
            if (u != v) edges.push_back({u,v,1});
    GraphFixture wide(70,3,std::move(edges));
    verifyOptimizedEnumeration(wide,wide.vertices,2,{{63},{64}},1);
    verifyOptimizedEnumeration(wide,wide.vertices,3,{{0},{63},{69}},1);
    verifyOptimizedEnumeration(wide,wide.vertices,6,{{0},{1},{2},{3},{4}},3);
    verifyOptimizedEnumeration(wide,wide.vertices,6,{{0},{1},{2},{3},{4},{69}},1);
    verifyOptimizedEnumeration(wide,wide.vertices,6,{{0},{1},{2},{3},{4},{68}},0);

    std::vector<TestEdge> smallClique;
    for (VertexID u = 0; u < 6; ++u)
        for (VertexID v = 0; v < 6; ++v)
            if (u != v) smallClique.push_back({u,v,1});
    GraphFixture fallback(4097,2,std::move(smallClique));
    verifyOptimizedEnumeration(fallback,fallback.vertices,6,
                               {{0},{1},{2},{3},{4},{5}},1);
}

void testOptimizedEnumerationCenterCounts() {
    std::vector<TestEdge> edges;
    std::vector<TestEdge> reverse;
    for (VertexID v = 1; v <= 20; ++v) {
        edges.push_back({v,0,1});
        edges.push_back({0,v,5});
        reverse.push_back({v,0,5});
        reverse.push_back({0,v,1});
    }
    GraphFixture center(21,7,std::move(edges));
    verifyOptimizedEnumeration(center,center.vertices,6,{},15504);
    verifyOptimizedEnumeration(center,center.vertices,6,{{1},{2}},816);
    verifyOptimizedEnumeration(center,center.vertices,6,{{1,2},{2,3}},4556);
    verifyOptimizedEnumeration(center,center.vertices,6,{{19},{20}},816);
    verifyOptimizedEnumeration(center,center.vertices,6,{{1,2},{2,3},{18,20}},1721);
    std::vector<VertexID> centerLast;
    for (VertexID v = 1; v <= 20; ++v) centerLast.push_back(v);
    centerLast.push_back(0);
    verifyOptimizedEnumeration(center,centerLast,6,{},15504);
    verifyOptimizedEnumeration(center,{1,2,3,4,5,6,7,8},6,{},0);
    center.graph->buildWindow(2,6,center.vertices);
    verifyOptimizedEnumeration(center,center.vertices,6,{},0);
    center.graph->buildWindow(1,5,center.vertices);
    verifyOptimizedEnumeration(center,center.vertices,6,{{1},{2}},816);

    GraphFixture lateIncoming(21,7,std::move(reverse));
    verifyOptimizedEnumeration(lateIncoming,lateIncoming.vertices,6,{},0);
    verifyOptimizedEnumeration(lateIncoming,lateIncoming.vertices,2,{},20);

    std::vector<VertexID> shuffled = {140};
    std::vector<TestEdge> wideEdges;
    for (ui i = 0; i < 69; ++i) {
        const VertexID v = 2 * ((i * 29) % 69);
        shuffled.push_back(v);
        wideEdges.push_back({v,140,1});
        wideEdges.push_back({140,v,5});
    }
    GraphFixture wideCenter(141,7,std::move(wideEdges),shuffled);
    verifyOptimizedEnumeration(wideCenter,shuffled,4,{{shuffled[65]},{shuffled[69]}},67);
    verifyOptimizedEnumeration(wideCenter,shuffled,4,
                               {{shuffled[65]},{shuffled[69]},{shuffled[64],shuffled[67]}},2);
}

void testOptimizedEnumerationRandom() {
    std::mt19937 random(20261029);
    for (ui trial = 0; trial < 18; ++trial) {
        const ui n = 6 + random() % 4;
        std::vector<TestEdge> edges;
        for (VertexID u = 0; u < n; ++u)
            for (VertexID v = 0; v < n; ++v) {
                if (random() % 100 >= 25 + trial % 55) continue;
                edges.push_back({u,v,static_cast<ui>(random() % 6)});
                if (random() % 4 == 0)
                    edges.push_back({u,v,static_cast<ui>(random() % 6)});
            }
        if (trial % 4 == 0)
            for (VertexID v = 0; v < n; ++v)
                edges.push_back({v,(v + 1) % n,2});
        std::shuffle(edges.begin(),edges.end(),random);
        GraphFixture fixture(n,6,std::move(edges));
        auto order = fixture.vertices;
        std::shuffle(order.begin(),order.end(),random);
        const std::vector<HitGroups> groups = {
            {},{{order.front()},{order.back()}},
            {{order[0],order[2],order[4]},{order[1],order[2],order[5]}}
        };
        for (const auto window : {std::pair<ui,ui>{0,5},{1,4},{2,2}}) {
            fixture.graph->buildWindow(window.first,window.second,fixture.vertices);
            for (ui k = 2; k <= 6; ++k)
                for (const auto& required : groups)
                    verifyOptimizedEnumeration(fixture,order,k,required);
        }
        if (trial < 4)
            for (ui k : {ui{5},ui{6}})
                verifyQuery(fixture,0,5,k);
    }
}

}

int main() {
    try {
        {
            CoutCapture capture;
            testDeterministicSplit();
            testPivotSelection();
            testReversePivotRanking();
            testReverseCoverHelper();
            testRandomSplits();
            testQueryIntegration();
            testRequiredHitsEntry();
            testRecursiveRequiredHits();
            testQueryMethodIsolation();
            testPrunedQueryWindowsAndRoots();
            testMutualGraph();
            testMutualGraphAndV1Random();
            testMutualGraphInducedRebuild();
            testOptimizedQueryIntegration();
            testActualEnumerationGroups();
            testActualEnumerationTemporalPaths();
            testActualEnumerationQueries();
            testV1AlphaSizeBoundaries();
            testV1AlphaConstraintIndependence();
            testV1AlphaConstrainedSearch();
            testOptimizedEnumerationGroups();
            testOptimizedEnumerationTemporalPaths();
            testOptimizedEnumerationLargeCandidates();
            testOptimizedEnumerationCenterCounts();
            testOptimizedEnumerationRandom();
        }
        std::cout << "multiway_split_test passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "multiway_split_test failed: " << error.what() << '\n';
        return 1;
    }
}
