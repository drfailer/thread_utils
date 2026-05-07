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

struct TU_DfgWorker {
    TU_Thread thread;
    TU_DfgWorkerGroup *group = nullptr;
    tu_u64 id = 0;
    // TODO: the cache size should be configurable (maybe through the add group function)
    TU_CacheQueue<TU_GraphOperation> cache{16};
    alignas(CACHE_LINE) TU_AtomicFlag parked = true;
    alignas(CACHE_LINE) TU_AtomicFlag can_terminate = false;

    // constructors
    TU_DfgWorker() = default;
    TU_DfgWorker(TU_DfgWorker const &) = delete;
    TU_DfgWorker(TU_DfgWorker &&other)
        : thread(std::move(other.thread)), group(other.group), id(other.id),
          cache(std::move(other.cache)), parked(other.parked.load()),
          can_terminate(other.can_terminate.load()) {}
};

struct TU_DfgWorkerGroup {
    TU_Sem sem = TU_Sem{0};
    TU_Array<TU_DfgWorker> workers = {};
    TU_Dfg *dfg = nullptr;
    tu_u64 id = 0;
    TU_Array<TU_GraphNode *> nodes = {};
};

// graph runner
struct TU_Dfg {
    TU_Mutex mutex;
    TU_Cond cond;
    TU_Array<TU_DfgWorkerGroup *> groups = {};
    TU_Graph *graph = nullptr;

    // constructors
    TU_Dfg() = default;
    TU_Dfg(TU_Dfg const &) = delete;
    TU_Dfg(TU_Dfg &&other) : groups(std::move(other.groups)), graph(other.graph) {}
    ~TU_Dfg() {
        for (auto group : groups) {
            delete group;
        }
    }
};

// TODO(C_INTERFACE): will allocate
TU_Dfg tu_dfg_create();
void tu_dfg_destroy(TU_Dfg *dfg);

tu_u64 tu_dfg_add_worker_group(TU_Dfg *dfg, size_t thread_count);

void tu_dfg_set_graph(TU_Dfg *dfg, TU_Graph *graph);
void tu_dfg_clear(TU_Dfg *dfg);
void tu_dfg_exec(TU_Dfg *dfg);
void tu_dfg_term(TU_Dfg *dfg);

void tu_dfg_push_data(TU_Dfg *dfg, void *data, TU_TypeId type);
TU_GraphData tu_dfg_wait_result(TU_Dfg *dfg);

#endif
