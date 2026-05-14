#ifndef TESTS_MPI_COMM_TASK
#define TESTS_MPI_COMM_TASK
#include "thread_utils/thread_utils.hpp"
#include "comm_queue.hpp"
#include "comm_buffer.hpp"
#include <ucp/api/ucp.h>

struct TU_UcxContext {
    ucp_context_h context;
    ucp_worker_h worker;
    ucp_address_t *address;
    size_t address_len;
    ucp_ep_h *endpoints;
    uint32_t nb_processes;
    uint32_t rank;

};

struct TU_UcxAMRndvData {
    void *desc;
    size_t size;
    tu_i64 type;
};

struct TU_UcxAMHeader {
    tu_i64 type;
};

struct TU_Package {
    void  *data;
    size_t size;
    tu_i64 type;
    tu_u64 dest;
    void *on_send_data;
    void (*on_send)(TU_Package *);
};

struct TU_CommTaskData {
    TU_UcxContext ucx;
    TU_LockQueue<TU_Package> package_queue; // this has to be thread safe because other threads will push to it
    TU_CommQueue<TU_UcxAMRndvData> rndv_queue;
    TU_CommBufferPool buffer_pool; // buffer pool for eager
    // progress thread data
    TU_Thread progress_thread;
    alignas(CACHE_LINE) TU_AtomicFlag can_terminate = false;
    // needed for transmitting results to the other nodes upon recv
    TU_Dfg *dfg;
    TU_GraphNode *node;
    TU_Map<TU_TypeId, TU_NodeExec> recv_exec; // we need to call those manually to recompute the type
};

TU_GraphNode *tu_comm_task(TU_Dfg *dfg, TU_Graph *graph, const char *name, TU_CommTaskData *data, TU_Set<TU_TypeId> const &types, tu_u64 dfg_group);

// TODO: this is tmp
void tu_comm_task_destroy(TU_CommTaskData *comm);

void tu_comm_send(TU_ExecContext *ctx, TU_Package package);

bool tu_comm_send_exec(TU_GraphNode *task, TU_TypeId type, TU_NodeExec exec);
bool tu_comm_recv_exec(TU_GraphNode *task, TU_TypeId type, TU_NodeExec exec);

#endif
