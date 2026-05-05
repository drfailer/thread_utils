/*
 * DFG is the primary data structure the user interact with. DFG is the graph
 * runner, it owns a graph and thread groups; and runs the graph and handles
 * the data transfer from node to node.
 */

#ifndef THREAD_UTILS_DFG
#define THREAD_UTILS_DFG
#include "../common.hpp"
#include "decl.hpp"
#include "graph.hpp"

// Dfg is based on a global thread pool (composed of multiple thread groups).
// Each group is responsible for processing multiple queues from multiple
// nodes. Therefore, each worker group will own a list of lanes that will be
// processed by the workers of that group. The lanes list are built when the
// dfg is started, and affectation is done evenly between the workers. Workers
// are affected to a lane, meaning that they always need to check the lanes
// they are affected to before looking at other lanes (this reduces contention
// on the queues, and keeps instruction cache warm).
struct TU_DfgLane {
    TU_GraphNode *node;
    TU_TypeId type;
};

// Operation information that allow the workers to cache future operation
// (avoid hitting a shared queue all the time, and preserve last processed
// data).
struct TU_GraphOperation {
    void *data;
    TU_TypeId type;
    TU_GraphNode *node;
};

struct TU_DfgWorker {
    TU_Thread thread;
    TU_DfgWorkerGroup *group = nullptr;
    tu_u64 worker_id = 0;
    TU_CacheQueue<TU_GraphOperation> cache{4};
    alignas(CACHE_LINE) TU_AtomicFlag parked = true;
    alignas(CACHE_LINE) TU_AtomicFlag can_terminate = false;
};

struct TU_DfgWorkerGroup {
    TU_Sem sem = TU_Sem{0};
    TU_Array<TU_DfgWorker> workers = {};
    TU_Dfg *dfg = nullptr;
    tu_u64 group_id = 0;
    TU_CacheQueue<TU_GraphOperation> cache{4};
    TU_Array<TU_DfgLane> lanes = {};

    // constructors
    TU_DfgWorkerGroup() = default;
    TU_DfgWorkerGroup(TU_DfgWorkerGroup const &) = delete;
    TU_DfgWorkerGroup(TU_DfgWorkerGroup &&other)
        : sem(0), workers(std::move(other.workers)), dfg(other.dfg),
          group_id(other.group_id), cache(std::move(other.cache)) {}
};

// graph runner
struct TU_Dfg {
    TU_Mutex mutex;
    TU_Cond cond;
    TU_Array<TU_DfgWorkerGroup> groups = {};
    TU_Graph *graph = nullptr;
    TU_Dfg(TU_Graph *graph) : graph(graph) {};
};

void tu_dfg_init(TU_Dfg *graph);
void tu_dfg_fini(TU_Dfg *graph);

tu_u64 tu_dfg_add_worker_group(TU_Dfg *graph, size_t thread_count);

void tu_dfg_start(TU_Dfg *graph);
void tu_dfg_push_data(TU_Dfg *graph, void *data, TU_TypeId type);
void tu_dfg_wait_completion(TU_Dfg *graph);

#endif
