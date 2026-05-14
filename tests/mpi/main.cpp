#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <format>
#include <mpi/mpi.h>
#include <ucp/api/ucp.h>
#include "thread_utils/thread_utils.hpp"
#include "comm_task.hpp"
#include <assert.h>
#include <vector>
#include "defer.hpp"
#include "timer.hpp"

void test_graph(uint32_t rank) {
    TU_Dfg dfg = tu_dfg_create();
    defer(tu_dfg_destroy(&dfg));
    tu_u64 compute_group = tu_dfg_add_worker_group(&dfg, 10, 16);
    tu_u64 comm_group = tu_dfg_add_worker_group(&dfg, 1, 1);

    TU_Graph graph = tu_graph_create("comm_graph", {1}, {1});
    defer(tu_graph_destroy(&graph));

    TU_CommTaskData comm_scatter = {};
    TU_GraphNode *scatter_task = tu_comm_task(&dfg, &graph, "comm_scatter", &comm_scatter, {1}, comm_group);
    assert(scatter_task != nullptr);
    defer(tu_comm_task_destroy(&comm_scatter));
    TU_CommTaskData comm_gather = {};
    TU_GraphNode *gather_task = tu_comm_task(&dfg, &graph, "comm_gather", &comm_gather, {1}, comm_group);
    assert(gather_task != nullptr);
    defer(tu_comm_task_destroy(&comm_gather));

    TU_GraphNode *init_task    = tu_task(&graph, "init", nullptr, {1}, {1}, compute_group, 40);
    TU_GraphNode *compute_task = tu_task(&graph, "compute", nullptr, {1}, {1}, compute_group, 40);
    TU_GraphNode *fini_task    = tu_task(&graph, "fini", nullptr, {1}, {1}, compute_group, 40);

    // normal nodes
    tu_exec(init_task, 1, [](TU_ExecContext *ctx, void *data, tu_i64 type) {
        printf("init task: %d.\n", *((int*)data));
        tu_result(ctx, data, type);
    });
    tu_exec(compute_task, 1, [](TU_ExecContext *ctx, void *data, tu_i64 type) {
        printf("compute task: %d.\n", *((int*)data));
        *((int *)data) += 1;
        tu_result(ctx, data, type);
    });
    tu_exec(fini_task, 1, [](TU_ExecContext *ctx, void *data, tu_i64 type) {
        printf("fini task: %d.\n", *((int*)data));
        tu_result(ctx, data, type);
    });

    // comm nodes
    // In hedgehog, the communicator tasks were using a strategy to determin
    // the destination, and a pack function to pack the data. In dfg, we allow
    // users to set an execute function for the communicator.

    tu_comm_send_exec(scatter_task, 1, [](TU_ExecContext *ctx, void *data, tu_i64 type) {
        printf("scatter send.\n");
        // inter-node
        tu_comm_send(ctx, TU_Package{
            .data = data,
            .size = sizeof(int),
            .type = type,
            .dest = 1,
            .on_send_data = nullptr,
            .on_send = [](TU_Package *) {
                printf("on send (scatter)\n");
            },
        });
        // tu_result(ctx, data, type); // intra-node
    });
    tu_comm_recv_exec(scatter_task, 1, [](TU_ExecContext *ctx, void *data, tu_i64 type) {
        printf("scatter recv: type = %ld, data = %d.\n", type, *((int *)data));
        tu_result(ctx, data, type);
    });

    tu_comm_send_exec(gather_task, 1, [](TU_ExecContext *ctx, void *data, tu_i64 type) {
        auto comm = (TU_CommTaskData*)tu_node_data(ctx);
        printf("gather send.\n");
        // inter-node
        tu_comm_send(ctx, TU_Package{
            .data = data,
            .size = sizeof(int),
            .type = type,
            .dest = 0,
            .on_send_data = comm,
            .on_send = [](TU_Package *) {
                printf("on send (gather)\n");
            },
        });
        // tu_result(ctx, data, type); // intra-node
    });
    tu_comm_recv_exec(gather_task, 1, [](TU_ExecContext *ctx, void *data, tu_i64 type) {
        printf("gather recv: type = %ld, data = %d.\n", type, *((int *)data));
        tu_result(ctx, data, type);
    });

    tu_add_inputs(&graph, init_task);
    tu_edges(init_task, scatter_task);
    tu_edges(scatter_task, compute_task);
    tu_edges(compute_task, gather_task);
    tu_edges(gather_task, fini_task);
    tu_add_outputs(&graph, fini_task);

    tu_dfg_set_graph(&dfg, &graph);

    tu_dfg_exec(&dfg);

    if (rank == 0) {
        int data = 4;
        tu_dfg_push_data(&dfg, &data, 1);
        [[maybe_unused]] auto result = tu_dfg_wait_result(&dfg);
        printf("[%d]: result = %d\n", rank, *((int *)result.data));
        // FIXME: the result is leaked for now
    }

    MPI_Barrier(MPI_COMM_WORLD);
    tu_dfg_term(&dfg);

    tu_graph_print_to_dot(&dfg, std::format("comm_graph_{}.dot", rank).c_str());
}


int main(int argc, char **argv) {
    MPI_Init(&argc, &argv);

    // TODO: create a service system
    int rank = -1;
    int nb_processes = -1;
    MPI_Comm_size(MPI_COMM_WORLD, &nb_processes);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    test_graph(rank);

    MPI_Barrier(MPI_COMM_WORLD);
    printf("end\n");
    MPI_Finalize();
}
