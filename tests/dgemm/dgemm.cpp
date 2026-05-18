#include <cstdio>
#include <cassert>
#include <tuple>
#include <functional>
#include <blas.hpp>
#include "thread_utils/thread_utils.hpp"
#include "dfg_dgemm.hpp"
#include "matrix.hpp"
#include "timer.hpp"
#include "defer.hpp"

constexpr size_t M_SIZE = 20000;
constexpr size_t M = M_SIZE, N = M_SIZE, K = M_SIZE, TILE_SIZE = 2048;
#define DGEMM_HH

#ifdef DGEMM_HH
#include "hedgehog_dgemm.hpp"
#endif // DGEMM_HH

void matmul(Matrix const &A, Matrix const &B, Matrix &C) {
    assert(A.rows == C.rows);
    assert(B.cols == C.cols);
    assert(A.cols == B.rows);
    timer_start(cblas_dgemm);
    cblas_dgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans, (blasint)A.rows, (blasint)B.cols, (blasint)A.cols,
                1.f, (const double *)A.data, (blasint)A.cols, (const double *)B.data, (blasint)B.cols, .0f,
                (double *)C.data, (blasint)C.cols);
    timer_end(cblas_dgemm);
    timer_report(cblas_dgemm);
}

MatrixTile *allocate_tile(size_t rows, size_t cols, size_t row, size_t col) {
    auto tile = new MatrixTile();
    tile->row = row;
    tile->col = col;
    tile->rows = rows;
    tile->cols = cols;
    tile->matrixRow = rows;
    tile->matrixCols = cols;
    tile->data = new double[rows * cols];
    return tile;
}

void deallocate_tile(MatrixTile *tile) {
    delete[] tile->data;
    delete tile;
}

void run_lambda(TU_TaskManagerContext tm_ctx, void *ctx, void *data, tu_i64 type) {
    auto lambda = (std::function<void(TU_TaskManagerContext, void*, void*, tu_i64)>*)ctx;
    lambda->operator()(tm_ctx, nullptr, data, type);
}

void tm_dgemm(Matrix &A, Matrix &B, Matrix &C, size_t tile_size) {// {{{
    std::vector<TileTriplet> tiles;
    TU_TaskManager tm;

    tu_tm_init(&tm);


    // tu_u64 sum_state_group = tu_tm_add_thread_group(&tm, 1);
    // tu_u64 product_state_group = tu_tm_add_thread_group(&tm, 1);
    // tu_u64 split_task_group = tu_tm_add_thread_group(&tm, 3);
    // tu_u64 product_task_group = tu_tm_add_thread_group(&tm, 40);
    // tu_u64 sum_task_group = tu_tm_add_thread_group(&tm, 10);

    // tu_u64 task_group = tu_tm_add_thread_group(&tm, 40);
    // tu_u64 sum_state_group = tu_tm_add_thread_group(&tm, 1);
    // tu_u64 product_state_group =  tu_tm_add_thread_group(&tm, 1);
    // tu_u64 split_task_group = task_group;
    // tu_u64 product_task_group = task_group;
    // tu_u64 sum_task_group = task_group;

    tu_u64 unique_group = tu_tm_add_thread_group(&tm, 4);
    tu_u64 sum_state_group = unique_group;
    tu_u64 product_state_group = unique_group;
    tu_u64 split_task_group = unique_group;
    tu_u64 product_task_group = unique_group;
    tu_u64 sum_task_group = unique_group;

    tu_tm_start(&tm);

    TU_TaskManagerStateContext product_state_cxt, sum_state_ctx;

    assert(A.rows == C.rows);
    assert(B.cols == C.cols);
    assert(A.cols == B.rows);

    size_t TM = C.rows / tile_size + (C.rows % tile_size == 0 ? 0 : 1);
    size_t TN = C.cols / tile_size + (C.cols % tile_size == 0 ? 0 : 1);
    size_t TK = A.cols / tile_size + (A.cols % tile_size == 0 ? 0 : 1);

    std::function<void(TU_TaskManagerContext, void*, void*, tu_i64)> split_task, product_task,
        sum_task, product_state, sum_state;

    std::vector<MatrixTile> tiles_mem[3]; // tiles memory for A, B, C and P
    tiles_mem[0].reserve(TM * TK);
    tiles_mem[1].reserve(TK * TN);
    tiles_mem[2].reserve(TM * TN);
    split_task = [&](TU_TaskManagerContext tm_ctx, void *, void *rawdata, tu_i64 type) {
        auto M = (Matrix*)rawdata;
        auto matrix_kind = (MatrixKind)type;
        for (size_t i = 0; i < M->rows; i += tile_size) {
            for (size_t j = 0; j < M->cols; j += tile_size) {
                // there is only one thread splitting the matrix M so we can
                // use the memory safely
                tiles_mem[type].push_back(MatrixTile{
                    .row = i / tile_size,
                    .col = j / tile_size,
                    .rows = std::min(M->rows - i, tile_size),
                    .cols = std::min(M->cols - j, tile_size),
                    .matrixRow = i,
                    .matrixCol = j,
                    .matrixRows = M->rows,
                    .matrixCols = M->cols,
                    .data = &M->operator()(i, j),
                });
                if (matrix_kind == MatrixKind::C) {
                    tu_tm_push_state(tm_ctx, sum_state_group, &sum_state_ctx, &run_lambda,
                            &sum_state, &tiles_mem[type].back(), type);
                } else {
                    tu_tm_push_state(tm_ctx, product_state_group, &product_state_cxt, &run_lambda,
                            &product_state, &tiles_mem[type].back(), type);
                }
            }
        }
    };

    product_task = [&](TU_TaskManagerContext tm_ctx, void *, void *rawdata, tu_i64) {
        auto tiles = (std::tuple<MatrixTile *, MatrixTile *, MatrixTile *>*)rawdata;
        auto [a, b, p] = *tiles;
        assert(a->rows == p->rows);
        assert(b->cols == p->cols);
        assert(a->cols == b->rows);

        p->row = a->row;
        p->col = b->col;
        cblas_dgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans, (blasint)a->rows, (blasint)b->cols, (blasint)a->cols,
                    1.f, (const double *)a->data, (blasint)a->matrixCols, (const double *)b->data, (blasint)b->matrixCols, 0,
                    (double *)p->data, (blasint)p->cols);

        // TODO: this is tmp, we will implement proper memory management helpers later
        delete tiles; // tiles was dynamically allocated in the product state
        tu_tm_push_state(tm_ctx, sum_state_group, &sum_state_ctx, &run_lambda, &sum_state,
                p, (size_t)MatrixKind::P);
    };

    sum_task = [&](TU_TaskManagerContext tm_ctx, void *, void *rawdata, tu_i64) {
        auto tiles = (std::pair<MatrixTile *, MatrixTile *>*)rawdata;
        auto [p, c] = *tiles;
        assert(p->rows == c->rows);
        assert(p->cols == c->cols);

        for (size_t row = 0; row < p->rows; ++row) {
            for (size_t col = 0; col < p->cols; ++col) {
                (*c)(row, col) += (*p)(row, col);
            }
        }
        // TODO: this is tmp, we will implement proper memory management helpers later
        delete tiles; // tiles was dynamically allocated in the sum state
        deallocate_tile(p);
        tu_tm_push_state(tm_ctx, sum_state_group, &sum_state_ctx, &run_lambda, &sum_state, c, (size_t)MatrixKind::C);
    };

    std::vector<MatrixTile*> A_tiles(TM * TK);
    std::vector<MatrixTile*> B_tiles(TK * TN);
    product_state = [&](TU_TaskManagerContext tm_ctx, void *, void *rawdata, tu_i64 type) {
        auto tile = (MatrixTile*)rawdata;
        auto matrix_kind = (MatrixKind)type;

        switch (matrix_kind) {
        case MatrixKind::A: {
            assert(A_tiles[tile->row * TK + tile->col] == nullptr);
            A_tiles[tile->row * TK + tile->col] = tile;
            for (size_t col = 0; col < TN; ++col) {
                if (B_tiles[tile->col * TN + col] != nullptr) {
                    MatrixTile *a = tile;
                    MatrixTile *b = B_tiles[tile->col * TN + col];
                    MatrixTile *p = allocate_tile(a->rows, b->cols, a->row, b->col);
                    auto tiles = new std::tuple<MatrixTile *, MatrixTile *, MatrixTile *>(a, b, p);
                    tu_tm_push_task(tm_ctx, product_task_group, &run_lambda, &product_task, tiles, -1);
                }
            }
        } break;
        case MatrixKind::B: {
            assert(B_tiles[tile->row * TN + tile->col] == nullptr);
            B_tiles[tile->row * TN + tile->col] = tile;
            for (size_t row = 0; row < TM; ++row) {
                if (A_tiles[row * TK + tile->row] != nullptr) {
                    MatrixTile *a = A_tiles[row * TK + tile->row];
                    MatrixTile *b = tile;
                    MatrixTile *p = allocate_tile(a->rows, b->cols, a->row, b->col);
                    auto tiles = new std::tuple<MatrixTile *, MatrixTile *, MatrixTile *>(a, b, p);
                    tu_tm_push_task(tm_ctx, product_task_group, &run_lambda, &product_task, tiles, -1);
                }
            }
        } break;
        case MatrixKind::C: printf("error: C tile received in product state\n"); break;
        case MatrixKind::P: printf("error: P tile received in product state\n"); break;
        };
    };

    std::vector<std::vector<MatrixTile*>> sum_queues(TM * TN);
    std::vector<MatrixTile*> C_tiles(TM * TN);
    sum_state = [&](TU_TaskManagerContext tm_ctx, void *, void *rawdata, tu_i64 type) {
        auto tile = (MatrixTile*)rawdata;
        auto matrix_kind = (MatrixKind)type;

        switch (matrix_kind) {
        case MatrixKind::A: printf("error: A tile received in sum state\n"); break;
        case MatrixKind::B: printf("error: B tile received in sum state\n"); break;
        case MatrixKind::C: {
            size_t c_idx = tile->row * TN + tile->col;
            assert(C_tiles[c_idx] == nullptr);
            if (sum_queues[c_idx].size() > 0) {
                auto p = sum_queues[c_idx].back();
                sum_queues[c_idx].pop_back();
                auto tiles = new std::pair<MatrixTile *, MatrixTile *>(p, tile);
                tu_tm_push_task(tm_ctx, sum_task_group, &run_lambda, &sum_task, tiles, -1);
            } else {
                C_tiles[c_idx] = tile;
            }
        } break;
        case MatrixKind::P: {
            size_t c_idx = tile->row * TN + tile->col;
            if (C_tiles[c_idx] != nullptr) {
                auto c = C_tiles[c_idx];
                C_tiles[c_idx] = nullptr;
                auto tiles = new std::pair<MatrixTile *, MatrixTile *>(tile, c);
                tu_tm_push_task(tm_ctx, sum_task_group, &run_lambda, &sum_task, tiles, -1);
            } else {
                sum_queues[c_idx].push_back(tile);
            }
        } break;
        }
    };

    tu_tm_push_op(&tm, split_task_group, nullptr, &run_lambda, &split_task, &A, (tu_i64)MatrixKind::A);
    tu_tm_push_op(&tm, split_task_group, nullptr, &run_lambda, &split_task, &B, (tu_i64)MatrixKind::B);
    tu_tm_push_op(&tm, split_task_group, nullptr, &run_lambda, &split_task, &C, (tu_i64)MatrixKind::C);

    tu_tm_wait_completion(&tm);
    tu_tm_fini(&tm);
    tu_tm_print_profile_infos(&tm);
    tu_tm_state_print_profile_infos(&product_state_cxt, "product_state");
    tu_tm_state_print_profile_infos(&sum_state_ctx, "sum_state");
}// }}}

void test_dgemm_tm(Matrix &A, Matrix &B, Matrix &C, Matrix const &E) {
    printf("\nrunning tm dgemm...\n");
    timer_start(dgemm);
    tm_dgemm(A, B, C, TILE_SIZE);
    timer_end(dgemm);
    timer_report(dgemm);

    matrix_print(C);

    if (!matrix_test_equal(C, E)) {
        printf("dgemm_tm(%ld, %ld, %ld, %ld) failed.\n", M, N, K, TILE_SIZE);
        return;
    }
    printf("dgemm_tm(%ld, %ld, %ld, %ld) success.\n", M, N, K, TILE_SIZE);
}

void test_dgemm_dfg(Matrix &A, Matrix &B, Matrix &C, Matrix const &E) {
    assert(A.rows == C.rows);
    assert(B.cols == C.cols);
    assert(A.cols == B.rows);

    size_t TM = C.rows / TILE_SIZE + (C.rows % TILE_SIZE == 0 ? 0 : 1);
    size_t TN = C.cols / TILE_SIZE + (C.cols % TILE_SIZE == 0 ? 0 : 1);
    size_t TK = A.cols / TILE_SIZE + (A.cols % TILE_SIZE == 0 ? 0 : 1);

    printf("\ndfg dgemm...\n");

    timer_start(dgemm_dfg);
    TU_Dfg dfg = tu_dfg_create();
    defer(tu_dfg_destroy(&dfg));

    // tu_u64 split_group = tu_dfg_add_worker_group(&dfg, 3, 1);
    // tu_u64 product_group = tu_dfg_add_worker_group(&dfg, 40, 32);
    // tu_u64 sum_group = tu_dfg_add_worker_group(&dfg, 10, 128);
    // tu_u64 product_state_group = tu_dfg_add_worker_group(&dfg, 1, 0);
    // tu_u64 sum_state_group = tu_dfg_add_worker_group(&dfg, 1, 0);

    // tu_u64 task_group = tu_dfg_add_worker_group(&dfg, 40, 128);
    // tu_u64 state_group = tu_dfg_add_worker_group(&dfg, 1, 0);
    // tu_u64 split_group = task_group;
    // tu_u64 product_group = task_group;
    // tu_u64 sum_group = task_group;
    // tu_u64 product_state_group = state_group;
    // tu_u64 sum_state_group = state_group;

    tu_u64 group = tu_dfg_add_worker_group(&dfg, 40, 8);
    tu_u64 split_group = group;
    tu_u64 product_group = group;
    tu_u64 sum_group = group;
    tu_u64 product_state_group = group;
    tu_u64 sum_state_group = group;

    Graph *graph = tu_graph_create("dgemm", {T_MatrixA, T_MatrixB, T_MatrixC}, {T_TileC});
    defer(tu_graph_destroy(graph));

    SplitTaskData split_task_data{
        .tile_size = TILE_SIZE,
        .tiles_mem = { {}, {}, {} },
        .TM = TM,
        .TN = TN,
        .TK = TK,
    };
    split_task_data.tiles_mem[0].reserve(TM * TK);
    split_task_data.tiles_mem[1].reserve(TK * TN);
    split_task_data.tiles_mem[2].reserve(TM * TN);

    ProductStateData product_state_data{
        .A_tiles = std::vector<MatrixTile*>(TM * TK),
        .B_tiles = std::vector<MatrixTile*>(TK * TN),
        .TM = TM,
        .TN = TN,
        .TK = TK,
    };

    SumStateData sum_state_data{
        .sum_queues = std::vector<std::vector<MatrixTile*>>(TM * TN),
        .C_tiles = std::vector<MatrixTile *>(TM * TN),
        .count = TM * TN * TK,
        .TM = TM,
        .TN = TN,
        .TK = TK,
    };

    auto split_task    = tu_task(graph, "split_task", &split_task_data, {T_MatrixA, T_MatrixB, T_MatrixC}, {T_TileA, T_TileB, T_TileC}, split_group, 3);
    auto product_task  = tu_task(graph, "product_task", nullptr, {T_ABPTiles}, {T_TileP}, product_group, 40);
    auto sum_task      = tu_task(graph, "sum_task", nullptr, {T_PCTiles}, {T_PCTiles}, sum_group, 40);
    auto product_state = tu_state(graph, "product_state", &product_state_data, {T_TileA, T_TileB}, {T_ABPTiles}, product_state_group);
    auto sum_state     = tu_state(graph, "sum_state", &sum_state_data, {T_TileC, T_TileP, T_PCTiles}, {T_PCTiles, T_TileC}, sum_state_group);

    tu_exec(split_task, T_MatrixA, &split_task_exec);
    tu_exec(split_task, T_MatrixB, &split_task_exec);
    tu_exec(split_task, T_MatrixC, &split_task_exec);

    tu_exec(product_task, T_ABPTiles, &product_task_exec);

    tu_exec(sum_task, T_PCTiles, &sum_task_exec);

    tu_exec(product_state, T_TileA, &product_state_exec_tile_a);
    tu_exec(product_state, T_TileB, &product_state_exec_tile_b);

    tu_exec(sum_state, T_TileC, &sum_state_exec_tile_c);
    tu_exec(sum_state, T_TileP, &sum_state_exec_tile_p);
    tu_exec(sum_state, T_PCTiles, &sum_state_exec_tile_pc_tiles);

    tu_add_inputs(graph, split_task);
    tu_edges(split_task, product_state);
    tu_edges(split_task, sum_state);
    tu_edges(product_state, product_task);
    tu_edges(product_task, sum_state);
    tu_edges(sum_state, sum_task);
    tu_edges(sum_task, sum_state);
    tu_add_outputs(graph, sum_state);

    tu_graph_check(graph);

    tu_dfg_set_graph(&dfg, graph);
    tu_dfg_exec(&dfg);
    tu_dfg_push_data(&dfg, &A, T_MatrixA);
    tu_dfg_push_data(&dfg, &B, T_MatrixB);
    tu_dfg_push_data(&dfg, &C, T_MatrixC);
    tu_dfg_wait_result(&dfg); // only one result before termination
    tu_dfg_term(&dfg);
    timer_end(dgemm_dfg);
    timer_report(dgemm_dfg);

    tu_graph_print_to_dot(&dfg, "graph.dot");

    matrix_print(C);
    if (!matrix_test_equal(C, E)) {
        printf("dgemm_dfg(%ld, %ld, %ld, %ld) failed.\n", M, N, K, TILE_SIZE);
        return;
    }
    printf("dgemm_dfg(%ld, %ld, %ld, %ld) success.\n", M, N, K, TILE_SIZE);
}

#ifdef DGEMM_HH
void test_dgemm_hh(Matrix &A, Matrix &B, Matrix &C, Matrix const &E) {
    auto Aptr = std::shared_ptr<AMat>(reinterpret_cast<AMat *>(&A), [](AMat *){ /* do not delete */ });
    auto Bptr = std::shared_ptr<BMat>(reinterpret_cast<BMat *>(&B), [](BMat *){ /* do not delete */ });
    auto Cptr = std::shared_ptr<CMat>(reinterpret_cast<CMat *>(&C), [](CMat *){ /* do not delete */ });
    printf("\nrunning hh dgemm...\n");
    timer_start(dgemm_hh);
    DgemmGraph graph(M, N, K, TILE_SIZE);
    graph.executeGraph(true);
    graph.pushData(Aptr);
    graph.pushData(Bptr);
    graph.pushData(Cptr);
    graph.finishPushingData();
    graph.waitForTermination();
    timer_end(dgemm_hh);
    timer_report(dgemm_hh);

    graph.createDotFile("graph_hh.dot", hh::ColorScheme::EXECUTION, hh::StructureOptions::QUEUE);

    matrix_print(C);
    if (!matrix_test_equal(C, E)) {
        printf("dgemm_hh(%ld, %ld, %ld, %ld) failed.\n", M, N, K, TILE_SIZE);
        return;
    }
    printf("dgemm_hh(%ld, %ld, %ld, %ld) success.\n", M, N, K, TILE_SIZE);
}
#endif // DGEMM_HH

int main(int, char **) {
    // initialization (we do that before changing openblas thread count so we
    // can compute the ground truth efficiently)
    Matrix A(M, K);
    Matrix B(K, N);
    Matrix C(M, N);
    Matrix E(M, N); // Expected
    matrix_init_double(A);
    matrix_init_double(B);
    // matrix_init_int(A);
    // matrix_init_int(B);
    matrix_zero(C);
    matrix_zero(E);

    printf("compute ground truth...\n");
    matmul(A, B, E);
    matrix_print(E);

    openblas_set_num_threads(1);

    // matrix_zero(C);
    // test_dgemm_tm(A, B, C, E);
    matrix_zero(C);
    test_dgemm_dfg(A, B, C, E);
#ifdef DGEMM_HH
    matrix_zero(C);
    test_dgemm_hh(A, B, C, E);
#endif // DGEMM_HH
    return 0;
}
