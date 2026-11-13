#include "temporal_test_support.h"

#include <cmath>
#include <random>

namespace {

struct MutualOracle {
    ul cliques = 0, vertices = 0, edges = 0;
    ui components = 0;
};

MutualOracle mutualOracle(const Fixture& fixture,ui left,ui right,ui k) {
    const ui n = fixture.vertices.size();
    std::vector<std::vector<std::pair<ui,ui>>> outgoing(n);
    std::vector<std::vector<bool>> staticReach(n,std::vector<bool>(n,false));
    for (ui v = 0; v < n; ++v) staticReach[v][v] = true;
    for (const auto& event : fixture.events) {
        if (event.time < left || event.time > right) continue;
        outgoing[event.source].push_back({event.target,event.time});
        staticReach[event.source][event.target] = true;
    }
    for (ui middle = 0; middle < n; ++middle)
        for (ui source = 0; source < n; ++source)
            for (ui target = 0; target < n; ++target)
                staticReach[source][target] = staticReach[source][target] ||
                    (staticReach[source][middle] && staticReach[middle][target]);

    MutualOracle result;
    std::vector<unsigned char> seen(n,0),eligible(n,0);
    for (ui seed = 0; seed < n; ++seed) {
        if (seen[seed]) continue;
        std::vector<ui> component;
        for (ui v = 0; v < n; ++v) {
            if (staticReach[seed][v] && staticReach[v][seed]) {
                component.push_back(v);
                seen[v] = 1;
            }
        }
        if (component.size() < k) continue;
        ++result.components;
        result.vertices += component.size();
        for (ui v : component) eligible[v] = 1;
    }

    constexpr ui infinity = std::numeric_limits<ui>::max();
    std::vector<std::vector<bool>> reachable(n,std::vector<bool>(n,false));
    for (ui source = 0; source < n; ++source) {
        std::vector<ui> arrival(n,infinity);
        std::vector<unsigned char> queued(n,0);
        std::queue<ui> pending;
        arrival[source] = left;
        pending.push(source);
        queued[source] = 1;
        while (!pending.empty()) {
            const ui u = pending.front();
            pending.pop();
            queued[u] = 0;
            for (const auto& event : outgoing[u]) {
                const ui v = event.first,time = event.second;
                if ((u != source && time <= arrival[u]) || time >= arrival[v]) continue;
                arrival[v] = time;
                if (!queued[v]) {
                    pending.push(v);
                    queued[v] = 1;
                }
            }
        }
        for (ui v = 0; v < n; ++v) reachable[source][v] = arrival[v] != infinity;
    }

    std::vector<ui> candidates,chosen;
    for (ui v = 0; v < n; ++v) if (eligible[v]) candidates.push_back(v);
    for (ui u = 0; u < n; ++u) if (eligible[u])
        for (ui v = u + 1; v < n; ++v) if (eligible[v])
            if (reachable[u][v] && reachable[v][u]) ++result.edges;

    const auto enumerate = [&](auto&& self,std::size_t start) -> void {
        if (chosen.size() == k) {
            ++result.cliques;
            return;
        }
        const std::size_t need = k - chosen.size();
        for (std::size_t i = start; i < candidates.size() && need <= candidates.size() - i; ++i) {
            const ui v = candidates[i];
            bool adjacent = true;
            for (ui u : chosen) adjacent = adjacent && reachable[u][v] && reachable[v][u];
            if (!adjacent) continue;
            chosen.push_back(v);
            self(self,i + 1);
            chosen.pop_back();
        }
    };
    enumerate(enumerate,0);
    return result;
}

struct MutualResult {
    ul cliqueCount, answerCount;
};

MutualResult checkMutual(Fixture& fixture,ui left,ui right,ui k) {
    const auto expected = mutualOracle(fixture,left,right,k);
    const auto actual = fixture.graph->enumerateRG(left,right,k);
    const ul expectedAnswers = oracleCount(fixture,fixture.vertices,k);
    const auto context = " window=" + std::to_string(left) + ":" + std::to_string(right) +
                         " k=" + std::to_string(k);
    require(actual == expectedAnswers,"Validated temporal SCC count differs from independent oracle" + context);
    require(fixture.graph->getAnswerCount() == actual,"RG returned and stored counts differ");

    MutualOracle observed;
    const auto components = fixture.graph->findWindowStaticSCCs(
        flatten(fixture.graph->getGlobalSCCs()),fixture.graph->getWindow(),k - 1);
    observed.components = components.size();
    for (const auto& component : components) {
        const auto graph = fixture.graph->buildMutualReachabilityGraph(component);
        observed.vertices += graph.size();
        for (const auto& row : graph) observed.edges += row.size();
        std::vector<ui> chosen;
        const auto enumerate = [&](auto&& self,ui start) -> void {
            if (chosen.size() == k) {
                ++observed.cliques;
                return;
            }
            for (ui v = start; v < graph.size(); ++v) {
                bool connected = true;
                for (ui u : chosen)
                    if (std::find(graph[u].begin(),graph[u].end(),v) == graph[u].end()) connected = false;
                if (!connected) continue;
                chosen.push_back(v);
                self(self,v + 1);
                chosen.pop_back();
            }
        };
        enumerate(enumerate,0);
    }
    observed.edges /= 2;
    require(observed.cliques == expected.cliques,"Mutual k-clique count differs" + context);
    require(actual <= observed.cliques,"Validated answers exceed candidate k-cliques" + context);
    require(observed.vertices == expected.vertices,"Mutual vertex count differs" + context);
    require(observed.edges == expected.edges,"Mutual edge count differs" + context);
    require(observed.components == expected.components,"Window static component count differs" + context);
    return {observed.cliques,actual};
}

}

int main() {
    try {
        OutputCapture capture;
        std::vector<Event> cycleEvents;
        for(ui t=2;t<=5;++t)for(ui u=0;u<4;++u)cycleEvents.push_back({u,(u+1)%4,t});
        Fixture cycle(4,8,std::move(cycleEvents));
        cycle.graph->enumerateDec(0,7,3);
        require(cycle.graph->getAnswerCount() == 0,"Cycle should have no induced temporal triple");
        const auto cycleTriples = checkMutual(cycle,0,7,3);
        require(cycleTriples.cliqueCount == 4,"Cycle mutual graph must contain four triangles");
        require(cycleTriples.answerCount == 0,"Cycle candidate triangles must all fail induced validation");
        cycle.graph->enumerateDec(0,7,4);
        require(cycle.graph->getAnswerCount() == 1,"Full cycle should be one temporal SCC");
        checkMutual(cycle,0,7,3);
        require(cycle.graph->getAnswerCount() == 0,"RG must store the answer count of its own query");
        const auto fullCycle = checkMutual(cycle,2,2,4);
        require(fullCycle.cliqueCount == 0 && fullCycle.answerCount == 0,"Same-time multi-hop reachability must be rejected");
        require(checkMutual(cycle,6,7,3).cliqueCount == 0,"Empty window retained prior mutual graph");
        checkMutual(cycle,0,7,3);
        checkMutual(cycle,0,7,3);
        checkMutual(cycle,0,7,5);

        Fixture sparse(13,8,{{2,6,1},{6,2,1},{6,9,1},{9,6,1},{9,2,1},{2,9,1},
                             {1,7,3},{7,1,3},{7,11,3},{11,7,3},{11,1,3},{1,11,3},
                             {9,1,4},{9,1,4},{2,2,1}});
        const auto sparseTriples = checkMutual(sparse,0,7,3);
        require(sparseTriples.cliqueCount == 2 && sparseTriples.answerCount == 2,
                "Sparse original IDs must map back before induced validation");
        require(checkMutual(sparse,0,7,2).cliqueCount == 6,"Mutual edges must be counted once");
        checkMutual(sparse,1,1,3);
        checkMutual(sparse,4,7,2);
        std::vector<Event> mixedEvents{{3,6,4},{6,3,4},{6,11,4},{11,6,4},{11,3,4},{3,11,4},{12,3,5}};
        const std::vector<VertexID> sparseCycle{2,5,8,12};
        for(ui t=2;t<=5;++t)for(ui u=0;u<4;++u)mixedEvents.push_back({sparseCycle[u],sparseCycle[(u+1)%4],t});
        Fixture mixedSparse(14,8,std::move(mixedEvents));
        const auto mixedTriples = checkMutual(mixedSparse,0,7,3);
        require(mixedTriples.cliqueCount == 5 && mixedTriples.answerCount == 1,
                "Mixed sparse components must validate only the original-ID triangle");
        Fixture empty(7,8,{});
        checkMutual(empty,0,7,2);
        checkMutual(empty,0,7,8);

        std::vector<Event> largeCycleEvents;
        for(ui t=0;t<9;++t)for (ui v = 0; v < 9; ++v) largeCycleEvents.push_back({v,(v + 1) % 9,t});
        Fixture largeCycle(9,10,std::move(largeCycleEvents));
        const auto largeCycleCliques = checkMutual(largeCycle,0,9,5);
        require(largeCycleCliques.cliqueCount == 126,
                "CCR core-clique combinations must expand and reach the counting callback");
        require(largeCycleCliques.answerCount == 0,"Nine-cycle candidates must fail induced five-point validation");

        std::mt19937 random(20261103);
        for (ui trial = 0; trial < 32; ++trial) {
            const ui n = 3 + random() % 7;
            std::vector<Event> events;
            for (ui u = 0; u < n; ++u) for (ui v = 0; v < n; ++v) if (u != v)
                if (random() % 100 < 48) {
                    events.push_back({u,v,static_cast<ui>(random() % 8)});
                    if (random() % 4 == 0) events.push_back(events.back());
                }
            Fixture fixture(n,8,std::move(events));
            for (const auto& window : {std::pair<ui,ui>{0,7},{1,5},{3,3},{7,7}})
                for (ui k = 2; k <= 6; ++k) {
                    const ul decAnswers = fixture.graph->enumerateDec(window.first,window.second,k);
                    const auto actual = checkMutual(fixture,window.first,window.second,k);
                    require(actual.answerCount == decAnswers,
                            "Validated mutual-clique answers differ from V1 on a random window");
                }
        }

        const auto before = cycle.graph->getWindow();
        const ul answerBefore = cycle.graph->getAnswerCount();
        for (const auto& input : {std::array<ui,3>{{6,2,3}},{{0,8,3}},{{0,7,1}}}) {
            bool rejected = false;
            try { cycle.graph->enumerateRG(input[0],input[1],input[2]); }
            catch (const std::out_of_range&) { rejected = true; }
            catch (const std::invalid_argument&) { rejected = true; }
            require(rejected,"Invalid mutual query was accepted");
            const auto& after = cycle.graph->getWindow();
            require(after.left == before.left && after.right == before.right &&
                    after.outgoing == before.outgoing && after.incoming == before.incoming,
                    "Invalid mutual query changed the previous window");
            require(cycle.graph->getAnswerCount() == answerBefore,"Invalid mutual query changed V1 count");
        }
        std::cerr << "Mutual clique regression passed (independent candidate/induced oracles, V1 agreement, k2..6, windows, sparse IDs)\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
