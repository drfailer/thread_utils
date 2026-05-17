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

struct TU_CommPackage {
    void  *data;
    size_t size;
    tu_i64 type;
    tu_u64 dest;
    // TODO: remove this
    void *on_send_data;
    void (*on_send)(TU_CommPackage *);
};

struct TU_CommTaskData {
    TU_UcxContext ucx;
    TU_LockQueue<TU_CommPackage> package_queue; // this has to be thread safe because other threads will push to it
    TU_CommQueue<TU_UcxAMRndvData> rndv_queue;
    TU_CommBufferPool buffer_pool; // buffer pool for eager
    // progress thread data
    TU_Thread progress_thread;
    alignas(CACHE_LINE) TU_AtomicFlag can_terminate = false;
    // needed for transmitting results to the other nodes upon recv
    TU_Dfg *dfg;
    Node *node;
    TU_Map<TU_TypeId, TU_NodeExec> recv_exec; // we need to call those manually to recompute the type

    // TODO: we need request queues to avoid waiting
    // TODO: we need a storage to enable splitting the
};

Node *tu_comm_task(TU_Dfg *dfg, Graph *graph, const char *name, TU_CommTaskData *data, TU_Set<TU_TypeId> const &types, tu_u64 dfg_group);

// TODO: this is tmp
void tu_comm_task_destroy(TU_CommTaskData *comm);

// TODO: the dest should be outside of the package
// TODO: the package should be composed of several buffers, and we need a way to
//       express that.
void tu_comm_send(TU_ExecContext *ctx, TU_CommPackage package);

bool tu_comm_send_exec(Node *task, TU_TypeId type, TU_NodeExec exec);
bool tu_comm_recv_exec(Node *task, TU_TypeId type, TU_NodeExec exec);

#endif
