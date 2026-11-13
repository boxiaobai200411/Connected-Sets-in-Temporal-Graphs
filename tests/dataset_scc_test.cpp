#include "temporal_test_support.h"

#include <cstdio>
#include <iterator>
#include <random>

namespace {
Groups staticOracle(ui n,const std::vector<Event>& edges) {
    std::vector<std::vector<bool>> reachable(n,std::vector<bool>(n,false));
    for (ui i = 0; i < n; ++i) reachable[i][i] = true;
    for (const auto& edge : edges) reachable[edge.source][edge.target] = true;
    for (ui k = 0; k < n; ++k)
        for (ui i = 0; i < n; ++i)
            for (ui j = 0; j < n; ++j)
                reachable[i][j] = reachable[i][j] || (reachable[i][k] && reachable[k][j]);
    std::vector<bool> used(n,false);
    Groups components;
    for (ui i = 0; i < n; ++i) {
        if (used[i]) continue;
        std::vector<VertexID> component;
        for (ui j = i; j < n; ++j)
            if (reachable[i][j] && reachable[j][i]) {
                component.push_back(j);
                used[j] = true;
            }
        if (component.size() >= 2) components.push_back(std::move(component));
    }
    return components;
}

std::string readFile(const std::string& path) {
    std::ifstream input(path);
    require(input.good(),"Cannot read generated SCC file");
    return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
}

void checkGeneration(Fixture& fixture) {
    const auto expected = staticOracle(static_cast<ui>(fixture.vertices.size()),fixture.events);
    require(fixture.graph->createGlobalSCCs() == expected,"Full-graph SCCs differ from static reachability oracle");
    const std::string path = fixture.directory + "/graph_SCCs.txt";
    require(std::remove(path.c_str()) == 0,"Cannot remove fixture SCC file");
    TemporalSCCsTestAccess generated(fixture.directory,"graph");
    require(generated.getGlobalSCCs() == expected,"Missing SCC file was not generated correctly");
    const auto contents = readFile(path);
    TemporalSCCsTestAccess cached(fixture.directory,"graph");
    require(cached.getGlobalSCCs() == expected && readFile(path) == contents,"Loading changed the cached SCC file");
    generated.enumerateCom(0,fixture.graph->getTimeCount() - 1,3);
    const auto expectedAnswers = generated.getAnswerCount();
    generated.enumerateDec(0,fixture.graph->getTimeCount() - 1,3);
    require(generated.getAnswerCount() == expectedAnswers,"Automatic SCC generation changed V1 answers");
    require(generated.enumerateRG(0,fixture.graph->getTimeCount() - 1,3) == expectedAnswers,
            "Automatic SCC generation changed mutual-clique answers");
}
}

int main() try {
    std::mt19937 random(20261111);
    for (ui trial = 0; trial < 90; ++trial) {
        const ui n = 1 + random() % 12;
        std::vector<Event> edges;
        for (ui u = 0; u < n; ++u)
            for (ui v = 0; v < n; ++v)
                if (random() % 5 == 0) {
                    edges.push_back({u,v,static_cast<ui>(random() % 4)});
                    if (random() % 3 == 0) edges.push_back(edges.back());
                }
        Fixture fixture(n,4,std::move(edges));
        checkGeneration(fixture);
    }
    Fixture invalid(3,2,{{0,1,1},{1,2,1},{2,0,1}});
    for (const std::string text : {"", "wrong\n", "-1\n", "1\n-3\n", "1\n3 0 1\n",
                                   "1\n3 0 1 999\n", "1\n2 0 0\n", "0\nextra\n"}) {
        {
            std::ofstream output(invalid.directory + "/graph_SCCs.txt");
            output << text;
        }
        bool rejected = false;
        try { TemporalSCCsTestAccess graph(invalid.directory,"graph"); }
        catch (const std::runtime_error&) { rejected = true; }
        require(rejected,"Malformed SCC file was accepted");
    }

    const std::string emptyDirectory = createDirectory();
    {
        std::ofstream output(emptyDirectory + "/graph.txt");
        output << "0 0 0\n";
    }
    {
        TemporalSCCsTestAccess empty(emptyDirectory,"graph");
        require(empty.getGlobalSCCs().empty() && readFile(emptyDirectory + "/graph_SCCs.txt") == "0\n",
                "Empty dataset did not produce an empty SCC file");
    }
    removeDirectory(emptyDirectory);

    std::vector<Event> cycle;
    constexpr ui size = 100000;
    for (ui i = 0; i < size; ++i) cycle.push_back({i,(i + 1) % size,0});
    Fixture deep(size,1,std::move(cycle));
    const auto components = deep.graph->createGlobalSCCs();
    require(components.size() == 1 && components.front().size() == size,
            "Deep SCC traversal failed");
    std::cout << "Dataset SCC tests passed (static oracle, cache, malformed files, empty graph, deep cycle)\n";
    return 0;
}
catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
