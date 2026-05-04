#include "matrix.hpp"
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

int main(int, char **) {
    size_t M = 1024, N = 1024, K = 1024;
    Matrix A(M, K), B(K, N), C(M, N);
    matrix_init_double(A);
    matrix_init_double(B);
    matrix_zero(C);
    tm_hadamard(A, B, C, 256);

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
