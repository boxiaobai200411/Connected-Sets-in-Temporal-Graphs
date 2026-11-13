#ifndef TCS_ENUM_TEMPORAL_SCCS_H
#define TCS_ENUM_TEMPORAL_SCCS_H

#include "type.h"
#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

class TemporalSCCs {

public:
    TemporalSCCs(const std::string& dataset_path,const std::string& dataid);
    TemporalSCCs(const TemporalSCCs&) = delete;
    TemporalSCCs& operator=(const TemporalSCCs&) = delete;

    std::vector<std::vector<VertexID>> createGlobalSCCs() const;
    const std::vector<std::vector<VertexID>>& getGlobalSCCs() const { return globalSCCs; }
    ui getVertexCount() const { return vertex_count_; }
    ui getTimeCount() const { return time_count_; }
    ul getAnswerCount() const { return answerCount_; }
    void setDecompositionAlpha(ui alpha) { decompositionAlpha_ = alpha; }

    ul enumerateCom(ui left,ui right,ui k);
    ul enumerateRG(ui left,ui right,ui k);
    ul enumerateDec(ui left,ui right,ui k);

private:
    friend class TemporalSCCsTestAccess;
    static constexpr ui kMinSCCSize = 2;

    static constexpr ui kMaxEnumerationSize = 8;
    static constexpr ui kMaxEnumerationRequirements = 64 + 2 * kMaxEnumerationSize;

    struct Edge {
        VertexID source;
        VertexID target;
    };
    struct TemporalNeighbor {
        VertexID vertex;
        ui time;
    };
    struct Window {
        ui left = 0, right = 0;
        std::vector<std::vector<std::pair<VertexID, ui>>> outgoing;
        std::vector<std::vector<std::pair<VertexID, ui>>> incoming;
    };
    struct SplitResult {
        std::vector<VertexID> R;
        std::vector<VertexID> Q;
        std::vector<VertexID> U;
    };

    struct ReverseCover {
        VertexID seed;
        std::vector<VertexID> covered, remaining;
    };
    struct SplitPlan {
        bool decompose = false;
        VertexID pivot = 0;
        SplitResult forward;
        std::vector<ui> arrival;

        std::vector<ReverseCover> reverse;
    };
    SplitPlan buildSplitPlan(const std::vector<VertexID>& vertices,ui k);
    void decompose(const std::vector<VertexID>& vertices,ui k,
        const std::vector<std::vector<VertexID>>& requiredHits);

    const TemporalNeighbor* getVertexNeighbors(VertexID id, ui& count) const {
        if (id >= vertex_count_) throw std::out_of_range("Invalid vertex ID");
        count = offsets_[id + 1] - offsets_[id];
        return neighbors_.get() + offsets_[id];
    }

    const TemporalNeighbor* getVertexNeighbors(VertexID id, ui t1, ui t2, ui& count) const {
        const auto* begin = getVertexNeighbors(id, count);
        const auto* end = begin + count;
        if (t1 > t2) {
            count = 0;
            return begin;
        }
        const auto* first = std::lower_bound(begin, end, t1,
            [](const TemporalNeighbor& neighbor, ui time) {
                return neighbor.time < time;
            });
        const auto* last = std::upper_bound(first, end, t2,
            [](ui time, const TemporalNeighbor& neighbor) {
                return time < neighbor.time;
            });
        count = static_cast<ui>(last - first);
        return first;
    }

    const TemporalNeighbor* getVertexInNeighbors(VertexID id, ui& count) const {
        if (id >= vertex_count_) throw std::out_of_range("Invalid vertex ID");
        count = reverse_offsets_[id + 1] - reverse_offsets_[id];
        return reverse_neighbors_.get() + reverse_offsets_[id];
    }

    const TemporalNeighbor* getVertexInNeighbors(VertexID id, ui t1, ui t2, ui& count) const {
        const auto* begin = getVertexInNeighbors(id, count);
        const auto* end = begin + count;
        if (t1 > t2) {
            count = 0;
            return begin;
        }
        const auto* first = std::lower_bound(begin, end, t1,
            [](const TemporalNeighbor& neighbor, ui time) {
                return neighbor.time < time;
            });
        const auto* last = std::upper_bound(first, end, t2,
            [](ui time, const TemporalNeighbor& neighbor) {
                return time < neighbor.time;
            });
        count = static_cast<ui>(last - first);
        return first;
    }

    void prepareSubsetIndex(const std::vector<VertexID>& vertices);

    void buildWindow(ui t1, ui t2,std::vector<VertexID> candidates);

    std::vector<std::vector<VertexID>> findWindowStaticSCCs(const std::vector<VertexID>& vertices, const Window& window, ui k = 2) const;

    std::optional<std::vector<std::vector<VertexID>>> normalizeRequiredGroups(const std::vector<VertexID>& vertices,const std::vector<std::vector<VertexID>>& requiredGroups,ui k);

    bool shouldEnumerateDirectly(const std::vector<VertexID>& vertices,ui k);

    void enumerateCombinations(const std::vector<VertexID>& vertices,ui k,const std::vector<std::vector<VertexID>>& requiredGroups);

    struct EnumerationIndex {
        ui size = 0;
        ui words = 0;
        std::vector<ui> pairIndex;

        std::vector<ui> pairOffsets;
        std::vector<std::vector<ui>> pairTimes;
        std::vector<std::vector<ul>> outgoing;
        std::vector<std::vector<ul>> incoming;
        std::vector<std::vector<ul>> required;
        std::vector<ul> belongs;

        std::vector<std::vector<ul>> belongsWords;
    };

    EnumerationIndex buildEnumerationIndex(const std::vector<VertexID>& vertices,const std::vector<std::vector<VertexID>>& groups);

    ui findEnumerationPair(const EnumerationIndex& index,ui source,ui target) const;

    bool collectEnumerationRequirements(const EnumerationIndex& index,const std::vector<ui>& chosen,const std::vector<ul>& candidates,ul missing,ui slots,std::array<const ul*,kMaxEnumerationRequirements>& required,ui& count) const;

    bool checkEnumerationRequirements(const EnumerationIndex& index,const std::vector<ul>& candidates,ui slots,const ul* const* required,ui count) const;

    bool isTemporalSCCPacked(const EnumerationIndex& index,const std::vector<ui>& chosen) const;

    bool hasEnumerationCenter(const EnumerationIndex& index,const std::vector<ui>& chosen,const std::vector<ul>& candidates) const;

    static ul countKCombinationsCapped(ul n,ui k,ul cap);

    static long double estimateKCombinations(ul n,ui k);

    VertexID selectPivot(const std::vector<VertexID>& vertices);

    void computeEarliestArrival(const VertexID& selectedVertex,const std::vector<VertexID>& vertices, std::vector<ui>& arrival, SplitResult& split,bool flag);

    void buildOverlapCover(const std::vector<VertexID>& vertices,std::vector<ui>& arrival, SplitResult& split,bool flag);

    std::pair<std::vector<VertexID>,std::vector<VertexID>> buildReverseCover(const VertexID& selectedVertex, const std::vector<VertexID>& vertices,std::vector<ui>& arrival, const std::vector<VertexID>& Ux);

    struct PivotIndex {
        std::vector<VertexID> vertices;
        std::vector<std::vector<ui>> outgoing;
        std::vector<unsigned char> state;
        std::vector<ui> discovery, low, active;
        std::vector<std::pair<ui,std::size_t>> dfs;
    };

    PivotIndex buildPivotIndex(const std::vector<VertexID>& vertices,VertexID& degreePivot,VertexID& neighborPivot);

    long double scorePivotSubset(const std::vector<VertexID>& subset,ui k,PivotIndex& index,std::size_t& largest,long double cutoff=std::numeric_limits<long double>::infinity());

    bool collectEnumerationRequirementsMultiword(const EnumerationIndex& index,const std::vector<ui>& chosen,const std::vector<ul>& candidates,const std::vector<ul>& missing,ui slots,std::vector<const ul*>& required,ui& ownershipCount) const;

    void enumerateCenterCompletions(const EnumerationIndex& index,std::vector<ui>& chosen,const std::vector<ul>& candidates,const std::vector<ul>& missing,ui slots);

    struct PrefixLabels {
        ui size = 0;
        bool stronglyConnected = false;
        std::array<ul,kMaxEnumerationSize * kMaxEnumerationSize> earliest{};
        std::array<ul,kMaxEnumerationSize * kMaxEnumerationSize> latest{};
        std::array<ui,kMaxEnumerationSize> missing{};
        std::array<ul,kMaxEnumerationSize> allArrivals{};
        std::array<ul,kMaxEnumerationSize> allDepartures{};
    };

    PrefixLabels buildPrefixLabelsGeneral(const EnumerationIndex& index,const std::vector<ui>& chosen) const;

    struct PrefixState {
        ui size = 0;
        ul reachable = 0;
        bool stronglyConnected = true;
        std::array<ui,kMaxEnumerationSize * kMaxEnumerationSize> pairs{};
        std::array<ul,kMaxEnumerationSize * kMaxEnumerationSize> earliest{};
    };

    void relaxArrivals(const EnumerationIndex& index,const std::array<ui,kMaxEnumerationSize * kMaxEnumerationSize>& pairs,ui size,ui source,ui seed,ul* arrival) const;
    void extendPrefixState(const EnumerationIndex& index,const std::vector<ui>& chosen,const PrefixState& parent,ui vertex,PrefixState& next) const;

    std::vector<std::vector<ui>> buildMutualReachabilityGraph(const std::vector<VertexID>& vertices);

    void enumerateOptimized(const std::vector<VertexID>&,ui,const std::vector<std::vector<VertexID>>&);
    void enumerateWithPrefixCache(const std::vector<VertexID>&,ui,const std::vector<std::vector<VertexID>>&);
    void enumerateTriples(const std::vector<VertexID>&,ui,const std::vector<std::vector<VertexID>>&);

    struct ComponentCuts {
        std::array<ui,kMaxEnumerationSize> incoming{},outgoing{};
        ui incomingCount=0,outgoingCount=0;
    };
    ComponentCuts buildComponentCuts(const EnumerationIndex&,const std::vector<ui>&,bool whole=false) const;
    bool isTemporalTriple(const EnumerationIndex&,const std::vector<ui>&,ui,ui,ui) const;
    PrefixLabels buildPrefixLabels(const EnumerationIndex&,const std::vector<ui>&) const;
    bool isTemporalExtension(const EnumerationIndex&,const std::vector<ui>&,const PrefixLabels&,ui) const;

    struct PrefixCache {
        std::array<std::optional<PrefixLabels>,kMaxEnumerationSize+1> labels;
        std::array<std::array<ui,64>,kMaxEnumerationSize+1> pairs;
        std::array<bool,kMaxEnumerationSize+1> eligible{};
    };
    PrefixLabels extendPrefixLabels(const EnumerationIndex&,const std::vector<ui>&,
        const PrefixLabels&,const std::array<ui,64>&,ui) const;

    enum class QueryMethod { EnumerateDec, EnumerateCom };
    struct ReversePivotCache {
        std::vector<ul> degrees,scores;
        std::vector<unsigned char> scoreReady;
        std::vector<long long> departure,latest,sortedLatest;
        std::vector<std::size_t> seen;
    };
    std::vector<std::vector<VertexID>> prepareQuery(ui left,ui right,ui k);
    ul runQuery(ui t1,ui t2,ui k,QueryMethod method);
    VertexID selectReversePivot(const std::vector<VertexID>& vertices,const std::vector<VertexID>& Ux,ReversePivotCache& cache);

    void processDataset(const std::string& data_path,const std::string& dataid);
    void loadGlobalSCCs(const std::string& data_path,const std::string& dataid);

    Window window_;
    bool window_ready_ = false;
    ui decompositionAlpha_ = 10;

    ul answerCount_ = 0;

    ui vertex_count_ = 0;
    ui time_count_ = 0;
    std::unique_ptr<TemporalNeighbor[]> neighbors_;
    std::unique_ptr<ui[]> offsets_;
    std::unique_ptr<TemporalNeighbor[]> reverse_neighbors_;
    std::unique_ptr<ui[]> reverse_offsets_;
    std::vector<std::vector<VertexID>> globalSCCs;

    std::vector<ui> subsetStamp_;
    std::vector<ui> subsetIndex_;
    ui subsetEpoch_ = 0;

};

#endif
