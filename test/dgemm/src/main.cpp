#include <cstdio>
#include <cstring>
#include <cassert>
#include <tuple>
#include <functional>
#include <cblas.h>
#include "thread_utils.hpp"
#include "data.hpp"
#include "timer.hpp"
// #include "hedgehog_dgemm.hpp"

void graph_hadamard(Matrix &A, Matrix &B, Matrix &C, size_t tile_size) {
    std::vector<TileTriplet> tiles;
    TU_Graph graph;

    tu_graph_init(&graph);

    tu_graph_add_thread_group(&graph, 10);

    tu_graph_start(&graph);

    for (size_t i = 0; i < C.rows; i += tile_size) {
        for (size_t j = 0; j < C.cols; j += tile_size) {
            MatrixTile tile{
                .row = i / tile_size,
                .col = j / tile_size,
                .rows = std::min(C.rows - i, tile_size),
                .cols = std::min(C.cols - j, tile_size),
                .matrixRow = i,
                .matrixCol = j,
                .matrixRows = C.rows,
                .matrixCols = C.cols,
                .data = nullptr,
            };
            tiles.emplace_back(tile, tile, tile);
            tiles.back().a.data = &A(i, j);
            tiles.back().b.data = &B(i, j);
            tiles.back().c.data = &C(i, j);
            tu_graph_push_op(&graph, 0, nullptr, [](TU_GraphContext, void *, void *rawdata, tu_i64) {
                assert(rawdata != nullptr);
                auto data = (TileTriplet*)rawdata;
                for (size_t i = 0; i < data->c.rows; ++i) {
                    for (size_t j = 0; j < data->c.cols; ++j) {
                        data->c(i, j) = data->a(i, j) * data->b(i, j);
                    }
                }
            }, nullptr, &tiles.back(), 0);
        }
    }

    tu_graph_wait_completion(&graph);
    tu_graph_fini(&graph);
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

void run_lambda(TU_GraphContext graph_ctx, void *ctx, void *data, tu_i64 type) {
    auto lambda = (std::function<void(TU_GraphContext, void*, void*, tu_i64)>*)ctx;
    lambda->operator()(graph_ctx, nullptr, data, type);
}

void graph_dgemm(Matrix &A, Matrix &B, Matrix &C, size_t tile_size) {
    std::vector<TileTriplet> tiles;
    TU_Graph graph;

    tu_graph_init(&graph);


    // tu_u64 sum_state_group = tu_graph_add_thread_group(&graph, 1);
    // tu_u64 product_state_group = tu_graph_add_thread_group(&graph, 1);
    // tu_u64 split_task_group = tu_graph_add_thread_group(&graph, 3);
    // tu_u64 product_task_group = tu_graph_add_thread_group(&graph, 40);
    // tu_u64 sum_task_group = tu_graph_add_thread_group(&graph, 10);

    // tu_u64 task_group = tu_graph_add_thread_group(&graph, 40);
    // tu_u64 sum_state_group = tu_graph_add_thread_group(&graph, 1);
    // tu_u64 product_state_group =  tu_graph_add_thread_group(&graph, 1);
    // tu_u64 split_task_group = task_group;
    // tu_u64 product_task_group = task_group;
    // tu_u64 sum_task_group = task_group;

    tu_u64 unique_group = tu_graph_add_thread_group(&graph, 40);
    tu_u64 sum_state_group = unique_group;
    tu_u64 product_state_group = unique_group;
    tu_u64 split_task_group = unique_group;
    tu_u64 product_task_group = unique_group;
    tu_u64 sum_task_group = unique_group;

    tu_graph_start(&graph);

    TU_GraphState product_state_cxt, sum_state_ctx;

    assert(A.rows == C.rows);
    assert(B.cols == C.cols);
    assert(A.cols == B.rows);

    size_t TM = C.rows / tile_size + (C.rows % tile_size == 0 ? 0 : 1);
    size_t TN = C.cols / tile_size + (C.cols % tile_size == 0 ? 0 : 1);
    size_t TK = A.cols / tile_size + (A.cols % tile_size == 0 ? 0 : 1);

    std::function<void(TU_GraphContext, void*, void*, tu_i64)> split_task, product_task,
        sum_task, product_state, sum_state;

    std::vector<MatrixTile> tiles_mem[3]; // tiles memory for A, B, C and P
    tiles_mem[0].reserve(TM * TK);
    tiles_mem[1].reserve(TK * TN);
    tiles_mem[2].reserve(TM * TN);
    split_task = [&](TU_GraphContext graph_ctx, void *, void *rawdata, tu_i64 type) {
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
                    tu_graph_push_state(graph_ctx, sum_state_group, &sum_state_ctx, &run_lambda,
                            &sum_state, &tiles_mem[type].back(), type);
                } else {
                    tu_graph_push_state(graph_ctx, product_state_group, &product_state_cxt, &run_lambda,
                            &product_state, &tiles_mem[type].back(), type);
                }
            }
        }
    };

    product_task = [&](TU_GraphContext graph_ctx, void *, void *rawdata, tu_i64) {
        auto tiles = (std::tuple<MatrixTile *, MatrixTile *, MatrixTile *>*)rawdata;
        auto [a, b, p] = *tiles;
        assert(a->rows == p->rows);
        assert(b->cols == p->cols);
        assert(a->cols == b->rows);

        cblas_dgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans, (blasint)a->rows, (blasint)b->cols, (blasint)a->cols,
                    1.f, (const double *)a->data, (blasint)a->cols, (const double *)b->data, (blasint)b->cols, 0,
                    (double *)p->data, (blasint)p->cols);

        // TODO: this is tmp, we will implement proper memory management helpers later
        delete tiles; // tiles was dynamically allocated in the product state
        tu_graph_push_state(graph_ctx, sum_state_group, &sum_state_ctx, &run_lambda, &sum_state,
                p, (size_t)MatrixKind::P);
    };

    sum_task = [&](TU_GraphContext graph_ctx, void *, void *rawdata, tu_i64) {
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
        tu_graph_push_state(graph_ctx, sum_state_group, &sum_state_ctx, &run_lambda, &sum_state, c, (size_t)MatrixKind::C);
    };

    std::vector<MatrixTile*> A_tiles(TM * TK);
    std::vector<MatrixTile*> B_tiles(TK * TN);
    product_state = [&](TU_GraphContext graph_ctx, void *, void *rawdata, tu_i64 type) {
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
                    tu_graph_push_task(graph_ctx, product_task_group, &run_lambda, &product_task, tiles, -1);
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
                    tu_graph_push_task(graph_ctx, product_task_group, &run_lambda, &product_task, tiles, -1);
                }
            }
        } break;
        case MatrixKind::C: printf("error: C tile received in product state\n"); break;
        case MatrixKind::P: printf("error: P tile received in product state\n"); break;
        };
    };

    std::vector<std::vector<MatrixTile*>> sum_queues(TM * TN);
    std::vector<MatrixTile*> C_tiles(TM * TN);
    sum_state = [&](TU_GraphContext graph_ctx, void *, void *rawdata, tu_i64 type) {
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
                tu_graph_push_task(graph_ctx, sum_task_group, &run_lambda, &sum_task, tiles, -1);
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
                tu_graph_push_task(graph_ctx, sum_task_group, &run_lambda, &sum_task, tiles, -1);
            } else {
                sum_queues[c_idx].push_back(tile);
            }
        } break;
        }
    };

    tu_graph_push_op(&graph, split_task_group, nullptr, &run_lambda, &split_task, &A, (tu_i64)MatrixKind::A);
    tu_graph_push_op(&graph, split_task_group, nullptr, &run_lambda, &split_task, &B, (tu_i64)MatrixKind::B);
    tu_graph_push_op(&graph, split_task_group, nullptr, &run_lambda, &split_task, &C, (tu_i64)MatrixKind::C);

    tu_graph_wait_completion(&graph);
    tu_graph_fini(&graph);
    tu_graph_print_profile_infos(&graph);
    tu_graph_state_print_profile_infos(&product_state_cxt, "product_state");
    tu_graph_state_print_profile_infos(&sum_state_ctx, "sum_state");
}

void initialize_matrix(Matrix &m) {
    for (size_t i = 0; i < m.rows; ++i) {
        for (size_t j = 0; j < m.cols; ++j) {
            m(i, j) = 1. / double(i * m.cols + j + 1);
            // m(i, j) = i * m.cols + j;
        }
    }
}

void zero_matrix(Matrix &m) {
    memset(m.data, 0, m.rows * m.cols * sizeof(*m.data));
}

void test_hadamard() {
    size_t M = 1024, N = 1024, K = 1024;
    Matrix A(M, K), B(K, N), C(M, N);
    initialize_matrix(A);
    initialize_matrix(B);
    zero_matrix(C);
    graph_hadamard(A, B, C, 256);

    for (size_t i = 0; i < C.rows; ++i) {
        for (size_t j = 0; j < C.cols; ++j) {
            double expected = A(i, j) * B(i, j);
            if (C(i, j) != expected) {
                printf("hadamard failed at (%ld, %ld), expected `%lf` found `%lf`.\n",
                       i, j, expected, C(i, j));
                return;
            }
        }
    }
    printf("hadamard success.\n");
}

void matmul(Matrix const &A, Matrix const &B, Matrix &C) {
    assert(A.rows == C.rows);
    assert(B.cols == C.cols);
    assert(A.cols == B.rows);

    for (size_t row = 0; row < C.rows; ++row) {
        for (size_t col = 0; col < C.cols; ++col) {
            C(row, col) = 0;
            for (size_t k = 0; k < A.cols; ++k) {
                C(row, col) += A(row, k) * B(k, col);
            }
        }
    }
}

void test_dgemm() {
    size_t M = 10000, N = 10000, K = 10000, TILE_SIZE = 512;
    Matrix A(M, K), B(K, N), C(M, N), E(M, N);
    initialize_matrix(A);
    initialize_matrix(B);
    zero_matrix(C);
    zero_matrix(E);

    // printf("running matmul...\n");
    // matmul(A, B, E);
    printf("running graph dgemm...\n");
    timer_start(dgemm);
    graph_dgemm(A, B, C, TILE_SIZE);
    timer_end(dgemm);
    timer_report(dgemm);

    for (size_t row = 0; row < M; ++row) {
        for (size_t col = 0; col < N; ++col) {
            if (C(row, col) != E(row, col)) {
                printf("dgemm failed at (%ld, %ld), expected `%lf` found `%lf`.\n",
                       row, col, E(row, col), C(row, col));
                return;
            }
        }
    }
    printf("dgemm(%ld, %ld, %ld, %ld) success.\n", M, N, K, TILE_SIZE);
}

// void test_dgemm_hh() {
//     size_t M = 10000, N = 10000, K = 10000, TILE_SIZE = 512;
//     auto A = std::make_shared<AMat>(M, K);
//     auto B = std::make_shared<BMat>(K, N);
//     auto C = std::make_shared<CMat>(M, N);
//     initialize_matrix(*reinterpret_cast<Matrix*>(A.get()));
//     initialize_matrix(*reinterpret_cast<Matrix*>(B.get()));
//     zero_matrix(*reinterpret_cast<Matrix*>(C.get()));
//
//     printf("running hh dgemm...\n");
//     timer_start(dgemm_hh);
//     DgemmGraph graph(M, N, K, TILE_SIZE);
//     graph.executeGraph(true);
//     graph.pushData(A);
//     graph.pushData(B);
//     graph.pushData(C);
//     graph.finishPushingData();
//     graph.waitForTermination();
//     timer_end(dgemm_hh);
//     timer_report(dgemm_hh);
//
//     printf("dgemm_hh(%ld, %ld, %ld, %ld) success.\n", M, N, K, TILE_SIZE);
// }

int main(int, char **) {
    openblas_set_num_threads(1);
    // test_hadamard();
    // test_dgemm_hh();
    test_dgemm();
    return 0;
}
