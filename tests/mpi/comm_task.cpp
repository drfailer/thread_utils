#include "comm_task.hpp"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <mpi/mpi.h>
#include <ucp/api/ucp.h>
#include "thread_utils/thread_utils.hpp"
#include <assert.h>
#include "defer.hpp"

// ucx functions
#define check_ucx(status) check_ucx_(status, __FILE__, __LINE__)
bool check_ucx_(ucs_status_t status, char const *filename, size_t line);
static bool ucx_init(TU_UcxContext *ucx, size_t rank, size_t nb_processes);
static void ucx_finalize(TU_UcxContext *ucx);
ucs_status_t ucx_request_wait(TU_UcxContext *ucx, void *request);

// communicator functions
static void comm_progress_run(TU_CommTaskData *comm);
static void comm_progress_result(TU_CommTaskData *comm, void *data, tu_i64 type);


#define COMM_AM_HANDLER_ID 0
static ucs_status_t comm_am_handler(void *arg, const void *header, size_t header_length,
                                    void *data_or_desc, size_t length,
                                    const ucp_am_recv_param_t *param);

// TODO: For now we consider that one com task is one context and one worker
// TODO: unlike the hedgehog version, dfg's communicator task will not
//       serialize the data, the user will have to explicitely add extra tasks
//       to do that (it is simpler, faster because the serialization can be
//       done in // if needed and it is more flexible because we don't always
//       want to send the same part of a same type).
Node *tu_comm_task(TU_Dfg *dfg, Graph *graph, const char *name, TU_CommTaskData *comm,
                           TU_Set<TU_TypeId> const &types, tu_u64 dfg_group) {
    int rank = -1, nb_processes = -1;
    MPI_Comm_size(MPI_COMM_WORLD, &nb_processes);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    if (!ucx_init(&comm->ucx, (size_t)rank, (size_t)nb_processes)) {
        printf("[TU_ERROR]: failed to initialize ucx context.\n");
        return nullptr;
    }

    ucp_am_handler_param_t param;
    param.field_mask = UCP_AM_HANDLER_PARAM_FIELD_ID
                     | UCP_AM_HANDLER_PARAM_FIELD_CB
                     | UCP_AM_HANDLER_PARAM_FIELD_ARG;
    param.id         = COMM_AM_HANDLER_ID;
    param.cb         = comm_am_handler;
    param.arg        = comm;
    if (!check_ucx(ucp_worker_set_am_recv_handler(comm->ucx.worker, &param))) {
        return nullptr;
    }

    // TODO: setup the progress thread
    // - for now we will not create a dedicated worker for this, however, we
    //   migh in the future if it can make things faster (for this we need to
    //   abstract the worker)
    comm->can_terminate.store(false);
    comm->progress_thread = TU_Thread(comm_progress_run, comm);

    TU_Set<TU_TypeId> input_types;
    for (TU_TypeId type : types) {
        if (type < 0) {
            printf("[TU_ERROR]: communicator tasks requres type ids to be positive (negative ids are reserved for internal use).\n");
            assert(false && "TODO: exit properly.");
        }
        input_types.insert(type);
        input_types.insert(-type - 1); // we substract 1 to solve the -0 case
    }

    comm->dfg = dfg;
    comm->node = tu_task(graph, name, comm, input_types, types, dfg_group, 1);
    assert(comm->node != nullptr);

    return comm->node;
}

void tu_comm_task_destroy(TU_CommTaskData *comm) {
    comm->can_terminate.store(true);
    ucp_worker_signal(comm->ucx.worker);
    if (comm->progress_thread.joinable()) {
        comm->progress_thread.join();
    }
    ucx_finalize(&comm->ucx);
}

bool tu_comm_send_exec(Node *task, TU_TypeId type, TU_NodeExec exec) {
    if (type < 0) {
        printf("[TU_ERROR]: communicator tasks requres type ids to be positive (negative ids are reserved for internal use).\n");
        return false;
    }
    return tu_exec(task, type, exec);
}

bool tu_comm_recv_exec(Node *task, TU_TypeId type, TU_NodeExec exec) {
    if (type < 0) {
        printf("[TU_ERROR]: communicator tasks requres type ids to be positive (negative ids are reserved for internal use).\n");
        return false;
    }
    auto exec_node = (ExecNode *)task;
    auto comm = (TU_CommTaskData *)exec_node->data;
    // To distinguish the data to send and the data that is received, we use
    // negative type ids. On the receiver end, the type will be negative and we
    // must compute the original positive id for the user exec function. This
    // allows using the workers from the group instead of the progress thread
    // to do the unpacking, as well as using only one graph node for both the
    // sender and the receiver.
    //
    // we don't verify the type here because it will be done in t_exec
    comm->recv_exec[type] = exec;
    return tu_exec(task, -type - 1, [](TU_ExecContext *ctx, void *data, TU_TypeId recv_type) {
        TU_TypeId type = -(recv_type + 1);
        auto comm = (TU_CommTaskData*)tu_node_data(ctx);
        comm->recv_exec[type](ctx, data, type);
    });
}

// used in the user exec
void tu_comm_send(TU_ExecContext *ctx, TU_CommPackage package) {
    auto comm = (TU_CommTaskData*)tu_node_data(ctx);
    comm->package_queue.push(std::move(package));
    ucp_worker_signal(comm->ucx.worker);
}

static void comm_progress_result(TU_CommTaskData *comm, void *data, tu_i64 type) {
    auto exec_node = (ExecNode *)comm->node;
    assert(exec_node->inputs.contains(-type - 1));
    exec_node->inputs[-type - 1].queue.push(GraphData{data, -type - 1});
    assert(exec_node->group < comm->dfg->groups.size());
    comm->dfg->groups[exec_node->group]->sem.release();
}

static ucs_status_t comm_am_handler(void *arg, const void *header_ptr, size_t header_length,
                                    void *data_or_desc, size_t length,
                                    const ucp_am_recv_param_t *param) {
    TU_CommTaskData *comm = (TU_CommTaskData*)arg;
    assert(header_length == sizeof(TU_UcxAMHeader));
    TU_UcxAMHeader header = *((TU_UcxAMHeader *)header_ptr);

    if (param->recv_attr & UCP_AM_RECV_ATTR_FLAG_RNDV) {
        comm->rndv_queue.add(TU_UcxAMRndvData{data_or_desc, length, header.type});
        return UCS_INPROGRESS;
    }
    // TODO: at some point we will not use the buffer pool
    // - eager is a bit trickier because we cannot just leave without saving
    //   the data somewhere. We will have to force users to preallocate enough
    //   data when using this protocol (which shouldn't be that bad, even if we
    //   tell UCX to save the buffer, it will allocate memory so there is no
    //   magic solution in this case).
    TU_CommBuffer buffer = comm->buffer_pool.alloc_cpu(length);
    memcpy(buffer.data, data_or_desc, buffer.size);
    // FIXME: we need to allocate the data and not the buffer!
    comm_progress_result(comm, buffer.data, header.type);
    return UCS_OK;
}

/******************************************************************************/
/*                              progress thread                               */
/******************************************************************************/

static void process_recv_ops(TU_CommTaskData *comm) {
    for (auto it = comm->rndv_queue.begin(); it != comm->rndv_queue.end();) {
        // TODO: at some point we will not use the buffer pool
        TU_CommBuffer buffer = comm->buffer_pool.alloc_cpu(it->size);
        ucp_request_param_t params = {};
        params.op_attr_mask |= UCP_OP_ATTR_FLAG_NO_IMM_CMPL; // FIXME: I don't think we want this
        // params.op_attr_mask |= UCP_OP_ATTR_FLAG_MULTI_SEND; // TODO: try this one out
        void *request = ucp_am_recv_data_nbx(comm->ucx.worker, it->desc, buffer.data, buffer.size, &params);
        // TODO: wait here is not ideal, we will have a dedicated queue later
        check_ucx(ucx_request_wait(&comm->ucx, request));
        it = comm->rndv_queue.remove(it);
        // result
        // FIXME: we need to allocate the data and not the buffer!
        comm_progress_result(comm, buffer.data, it->type);
    }
    // we don't have eager queue for now
}

static void process_send_ops(TU_CommTaskData *comm) {
    TU_CommPackage package;
    while (comm->package_queue.pop(&package)) {
        TU_UcxAMHeader header = {
            .type = package.type,
        };
        size_t header_len = sizeof(TU_UcxAMHeader);
        ucp_request_param_t send_params = {};
        assert(package.dest < comm->ucx.nb_processes);
        void *request = ucp_am_send_nbx(comm->ucx.endpoints[package.dest], 0,
                &header, header_len, package.data, package.size, &send_params);
        // TODO: we shouldn't wait here
        assert(ucx_request_wait(&comm->ucx, request) == UCS_OK);
        if (package.on_send != nullptr) {
            package.on_send(&package);
        }
    }
}

static void comm_progress_run(TU_CommTaskData *comm) {
    for (;;) {
        if (comm->can_terminate.load()) {
            break;
        }
        if (ucp_worker_progress(comm->ucx.worker) == 0) {
            // TODO: use ucp_worker_signal(worker); to wake it up from outside
            ucp_worker_wait(comm->ucx.worker);
        }
        if (comm->can_terminate.load()) {
            break;
        }
        process_send_ops(comm); // this is mendatory because the worker can only be used from one thread
        process_recv_ops(comm); // TODO: we need a way to access dfg if we want to transmit the result
    }
}

/******************************************************************************/
/*                                    UCX                                     */
/******************************************************************************/

bool check_ucx_(ucs_status_t status, char const *filename, size_t line) {
    if (status == UCS_OK) {
        return true;
    }
    fprintf(stderr, "[UCX_ERROR] %s(%ld): %s.\n", filename, line, ucs_status_string(status));
    return false;
}

static void failure_handler(void *request, ucp_ep_h ep, ucs_status_t status) {
    fprintf(stderr, "CLH failure handler called: %s {request = %p, ep = %p}\n",
            ucs_status_string(status), request, ep);
}

ucs_status_t ucx_request_wait(TU_UcxContext *ucx, void *request) {
    ucs_status_t status;
    if (request == NULL) {
        return UCS_OK;
    }
    if (UCS_PTR_IS_ERR(request)) {
        return UCS_PTR_STATUS(request);
    }
    for (;;) {
        ucp_worker_progress(ucx->worker);
        status = ucp_request_check_status(request);
        if (status != UCS_INPROGRESS) {
            break;
        }
    }
    ucp_request_free(request);
    return status;
}

static inline bool init_ucp_context(TU_UcxContext *ucx) {
    ucp_config_t *ucp_config;
    if (!check_ucx(ucp_config_read("MPI", NULL, &ucp_config))) {
        return false;
    }
    defer(ucp_config_release(ucp_config));
    ucp_params_t ucp_params = {};
    ucp_params.field_mask = UCP_PARAM_FIELD_FEATURES | UCP_PARAM_FIELD_ESTIMATED_NUM_EPS;
    ucp_params.features = UCP_FEATURE_TAG | UCP_FEATURE_WAKEUP | UCP_FEATURE_AM;
    ucp_params.estimated_num_eps = ucx->nb_processes;
    return check_ucx(ucp_init(&ucp_params, ucp_config, &ucx->context));
}

static inline bool init_ucp_worker(TU_UcxContext *ucx) {
    ucp_worker_params_t worker_params = {};
    worker_params.field_mask = UCP_WORKER_PARAM_FIELD_THREAD_MODE;
    worker_params.thread_mode = UCS_THREAD_MODE_SINGLE; // TODO: do we want this?
    return check_ucx(ucp_worker_create(ucx->context, &worker_params, &ucx->worker));
}

static inline bool init_ucp_endpoints(TU_UcxContext *ucx) {
    if (!check_ucx(ucp_worker_get_address(ucx->worker, &ucx->address, &ucx->address_len))) {
        return false;
    }
    ucx->endpoints = new ucp_ep_h[ucx->nb_processes];
    for (uint32_t rank = 0; rank < ucx->nb_processes; ++rank) {
        if (rank == ucx->rank) {
            continue;
        }
        char peer_addr[1024] = {0};
        size_t peer_addr_len = 0;

        MPI_Status status;
        MPI_Sendrecv(&ucx->address_len, sizeof(ucx->address_len), MPI_BYTE, rank, 0, &peer_addr_len, sizeof(peer_addr_len), MPI_BYTE, rank, 0, MPI_COMM_WORLD, &status);
        MPI_Sendrecv(ucx->address, ucx->address_len, MPI_BYTE, rank, 0, peer_addr, peer_addr_len, MPI_BYTE, rank, 0, MPI_COMM_WORLD, &status);

        printf("connect %d:[%ld] -> %d:[%ld]\n", ucx->rank, ucx->address_len, rank, peer_addr_len);

        ucp_ep_params_t ep_params = {};
        ep_params.field_mask = UCP_EP_PARAM_FIELD_REMOTE_ADDRESS
                             | UCP_EP_PARAM_FIELD_ERR_HANDLING_MODE
                             | UCP_EP_PARAM_FIELD_ERR_HANDLER;
        ep_params.address = (ucp_address_t *)peer_addr; // the address will be copied so we can pass tmp memory
        ep_params.err_mode = UCP_ERR_HANDLING_MODE_PEER;
        ep_params.err_handler.cb = &failure_handler;
        ep_params.err_handler.arg = NULL;

        if (!check_ucx(ucp_ep_create(ucx->worker, &ep_params, &ucx->endpoints[rank]))) {
            return false;
        }
    }
    return true;
}

static bool ucx_init(TU_UcxContext *ucx, size_t rank, size_t nb_processes) {
    ucx->nb_processes = nb_processes;
    ucx->rank = rank;
    if (!init_ucp_context(ucx)) {
        printf("init_ucp_context failed\n");
        return false;
    }
    if (!init_ucp_worker(ucx)) {
        printf("init_ucp_worker failed\n");
        return false;
    }
    if (!init_ucp_endpoints(ucx)) {
        printf("init_ucp_endpoints failed\n");
        return false;
    }
    return true;
}

static void ucx_finalize(TU_UcxContext *ucx) {
    for (uint32_t rank = 0; rank < ucx->nb_processes; ++rank) {
        if (rank == ucx->rank) {
            continue;
        }
        ucp_request_param_t params;
        params.op_attr_mask = UCP_OP_ATTR_FIELD_FLAGS;
        params.flags = UCP_EP_CLOSE_FLAG_FORCE;
        ucs_status_ptr_t status_ptr = ucp_ep_close_nbx(ucx->endpoints[rank], &params);
        assert(!UCS_PTR_IS_ERR(status_ptr));
        if (UCS_PTR_IS_PTR(status_ptr)) {
            for (;;) {
                ucs_status_t status = ucp_request_check_status(status_ptr);
                ucp_worker_progress(ucx->worker);
                if (status != UCS_INPROGRESS) {
                    break;
                }
            }
            ucp_request_free(status_ptr);
        }
    }
    delete[] ucx->endpoints;
    ucp_worker_release_address(ucx->worker, ucx->address);
    ucp_worker_destroy(ucx->worker);
    ucp_cleanup(ucx->context);
}

