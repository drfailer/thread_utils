#include "matrix.hpp"
#include "defer.hpp"
#include <vector>
#include "thread_utils/thread_utils.hpp"

void tm_hadamard(Matrix &A, Matrix &B, Matrix &C, size_t tile_size) {
    std::vector<TileTriplet> tiles;
    TU_TaskManager tm;

    tu_tm_init(&tm);

    tu_tm_add_thread_group(&tm, 10);

    tu_tm_start(&tm);

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
            tu_tm_push_op(&tm, 0, nullptr, [](TU_TaskManagerContext, void *, void *rawdata, tu_i64) {
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

    tu_tm_wait_completion(&tm);
    tu_tm_fini(&tm);
}

enum Types : tu_i64 {
    T_MatrixA,
    T_MatrixB,
    T_MatrixC,
    T_TileA,
    T_TileB,
    T_TileC,
    T_ABCTiles,
};

void dfg_hadamard(Matrix &A, Matrix &B, Matrix &C, size_t tile_size) {
    TU_Dfg dfg = tu_dfg_create();
    defer(tu_dfg_destroy(&dfg));
    tu_u64 group = tu_dfg_add_worker_group(&dfg, 40, 16, 16);

    TU_Graph graph = tu_graph_create("hadamard", {T_ABCTiles}, {T_ABCTiles});
    defer(tu_graph_destroy(&graph));

    auto product_task = tu_task(&graph, "product_task", nullptr, {T_ABCTiles}, {T_ABCTiles}, group, 40);

    tu_add_inputs(&graph, product_task);
    tu_add_outputs(&graph, product_task);

    tu_exec(product_task, T_ABCTiles, [](TU_ExecContext *ctx, void *data, tu_i64 type) {
        assert(data != nullptr);
        auto tiles = (TileTriplet*)data;
        // printf("C[%ld,%ld] = A[%ld,%ld] o B[%ld,%ld]\n",
        //        tiles->c.row, tiles->c.col, tiles->a.row, tiles->a.col, tiles->b.row, tiles->b.col);
        for (size_t i = 0; i < tiles->c.rows; ++i) {
            for (size_t j = 0; j < tiles->c.cols; ++j) {
                tiles->c(i, j) = tiles->a(i, j) * tiles->b(i, j);
            }
        }
        tu_result(ctx, data, type);
    });

    assert(tu_graph_check(&graph));


    printf("starting dfg...\n");
    tu_dfg_set_graph(&dfg, &graph);
    tu_dfg_exec(&dfg);

    size_t tile_count = 0;
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
            auto tiles = new TileTriplet(tile, tile, tile);
            tiles->a.data = &A(i, j);
            tiles->b.data = &B(i, j);
            tiles->c.data = &C(i, j);
            tu_dfg_push_data(&dfg, tiles, T_ABCTiles);
            tile_count += 1;
        }
    }

    for (size_t i = 0; i < tile_count; ++i) {
        auto result = tu_dfg_wait_result(&dfg);
        delete ((TileTriplet*)result.data);
        // printf("result: %ld/%ld\n", i + 1, tile_count);
    }
    printf("stop dfg...\n");
    tu_dfg_term(&dfg);
    printf("dfg stopped\n");
}

int main(int, char **) {
    size_t M = 4096, N = 4096, K = 4096, TILE_SIZE = 512;
    Matrix A(M, K), B(K, N), C(M, N);
    matrix_init_double(A);
    matrix_init_double(B);
    matrix_zero(C);
    // tm_hadamard(A, B, C, TILE_SIZE);
    dfg_hadamard(A, B, C, TILE_SIZE);

    for (size_t i = 0; i < C.rows; ++i) {
        for (size_t j = 0; j < C.cols; ++j) {
            double expected = A(i, j) * B(i, j);
            if (C(i, j) != expected) {
                printf("hadamard failed at (%ld, %ld), expected `%lf` found `%lf`.\n",
                       i, j, expected, C(i, j));
                return 1;
            }
        }
    }
    printf("hadamard success.\n");
    return 0;
}
