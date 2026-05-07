#ifndef TESTS_DGEMM_DFG_DGEMM
#define TESTS_DGEMM_DFG_DGEMM
#include <unistd.h>
#include "thread_utils/thread_utils.hpp"
#include "matrix.hpp"

enum Types : tu_i64 {
    T_MatrixA,
    T_MatrixB,
    T_MatrixC,
    T_TileA,
    T_TileB,
    T_TileC,
    T_TileP,
    T_ABPTiles,
    T_PCTiles,
};

struct SplitTaskData {
    size_t tile_size;
    std::vector<MatrixTile> tiles_mem[3]; // tiles memory for A, B, C
    size_t TM, TN, TK;
};
void split_task_exec(TU_ExecContext *ctx, void *rawdata, tu_i64 type);

void product_task_exec(TU_ExecContext *ctx, void *rawdata, tu_i64 type);

void sum_task_exec(TU_ExecContext *ctx, void *rawdata, tu_i64 type);

struct ProductStateData {
    std::vector<MatrixTile*> A_tiles;
    std::vector<MatrixTile*> B_tiles;
    size_t TM, TN, TK;
};
void product_state_exec_tile_a(TU_ExecContext *ctx, void *rawdata, tu_i64 type);
void product_state_exec_tile_b(TU_ExecContext *ctx, void *rawdata, tu_i64 type);

struct SumStateData {
    std::vector<std::vector<MatrixTile*>> sum_queues;
    std::vector<MatrixTile*> C_tiles;
    size_t count;
    size_t TM, TN, TK;
};
void sum_state_exec_tile_c(TU_ExecContext *ctx, void *rawdata, tu_i64 type);
void sum_state_exec_tile_p(TU_ExecContext *ctx, void *rawdata, tu_i64 type);
void sum_state_exec_tile_pc_tiles(TU_ExecContext *ctx, void *rawdata, tu_i64 type);

#endif
