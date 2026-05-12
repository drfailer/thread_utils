#include <blas.hpp>
#include <cstdio>
#include "dfg_dgemm.hpp"

MatrixTile *allocate_tile(size_t rows, size_t cols, size_t row, size_t col);
void deallocate_tile(MatrixTile *tile);

void split_task_exec(TU_ExecContext *ctx, void *rawdata, tu_i64 type) {
    assert(type == T_MatrixA || type == T_MatrixB || type == T_MatrixC);
    auto task_data = (SplitTaskData*)tu_node_data(ctx);
    auto M = (Matrix*)rawdata;

    for (size_t i = 0; i < M->rows; i += task_data->tile_size) {
        for (size_t j = 0; j < M->cols; j += task_data->tile_size) {
            // there is only one thread splitting the matrix M so we can
            // use the memory safely
            task_data->tiles_mem[type].push_back(MatrixTile{
                .row = i / task_data->tile_size,
                .col = j / task_data->tile_size,
                .rows = std::min(M->rows - i, task_data->tile_size),
                .cols = std::min(M->cols - j, task_data->tile_size),
                .matrixRow = i,
                .matrixCol = j,
                .matrixRows = M->rows,
                .matrixCols = M->cols,
                .data = &M->operator()(i, j),
            });
            tu_i64 output_type = 0;
            switch (type) {
            case T_MatrixA: output_type = T_TileA; break;
            case T_MatrixB: output_type = T_TileC; break;
            case T_MatrixC: output_type = T_TileB; break;
            default: assert(false); break;
            }
            tu_result(ctx, &task_data->tiles_mem[type].back(), output_type);
        }
    }
}

void product_task_exec(TU_ExecContext *ctx, void *rawdata, tu_i64 type) {
    assert(type == T_ABPTiles);
    auto tiles = (std::tuple<MatrixTile *, MatrixTile *, MatrixTile *>*)rawdata;
    auto [a, b, p] = *tiles;
    assert(a->rows == p->rows);
    assert(b->cols == p->cols);
    assert(a->cols == b->rows);

    p->row = a->row;
    p->col = b->col;
    // printf("product P[%ld,%ld]\n", p->row, p->col);
    cblas_dgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans, (blasint)a->rows, (blasint)b->cols, (blasint)a->cols,
                1.f, (const double *)a->data, (blasint)a->matrixCols, (const double *)b->data, (blasint)b->matrixCols, 0,
                (double *)p->data, (blasint)p->cols);

    // TODO: this is tmp, we will implement proper memory management helpers later
    delete tiles; // tiles was dynamically allocated in the product state
    tu_result(ctx, p, T_TileP);
}

void sum_task_exec(TU_ExecContext *ctx, void *rawdata, tu_i64 type) {
    assert(type == T_PCTiles);
    auto tiles = (std::pair<MatrixTile *, MatrixTile *>*)rawdata;
    auto [p, c] = *tiles;
    assert(p->rows == c->rows);
    assert(p->cols == c->cols);

    // printf("sum C[%ld,%ld]\n", c->row, c->col);
    for (size_t row = 0; row < p->rows; ++row) {
        for (size_t col = 0; col < p->cols; ++col) {
            (*c)(row, col) += (*p)(row, col);
        }
    }
    tu_result(ctx, tiles, type);
}

void product_state_exec_tile_a(TU_ExecContext *ctx, void *rawdata, tu_i64 type) {
    auto task_data = (ProductStateData*)tu_node_data(ctx);
    assert(type == T_TileA);
    auto tile = (MatrixTile*)rawdata;
    size_t TN = task_data->TN, TK = task_data->TK;

    assert(task_data->A_tiles[tile->row * TK + tile->col] == nullptr);
    task_data->A_tiles[tile->row * TK + tile->col] = tile;
    for (size_t col = 0; col < TN; ++col) {
        if (task_data->B_tiles[tile->col * TN + col] != nullptr) {
            MatrixTile *a = tile;
            MatrixTile *b = task_data->B_tiles[tile->col * TN + col];
            MatrixTile *p = allocate_tile(a->rows, b->cols, a->row, b->col);
            auto tiles = new std::tuple<MatrixTile *, MatrixTile *, MatrixTile *>(a, b, p);
            tu_result(ctx, tiles, T_ABPTiles);
        }
    }
}

void product_state_exec_tile_b(TU_ExecContext *ctx, void *rawdata, tu_i64 type) {
    auto task_data = (ProductStateData*)tu_node_data(ctx);
    assert(type == T_TileB);
    auto tile = (MatrixTile*)rawdata;
    size_t TM = task_data->TM, TN = task_data->TN, TK = task_data->TK;

    assert(task_data->B_tiles[tile->row * TN + tile->col] == nullptr);
    task_data->B_tiles[tile->row * TN + tile->col] = tile;
    for (size_t row = 0; row < TM; ++row) {
        if (task_data->A_tiles[row * TK + tile->row] != nullptr) {
            MatrixTile *a = task_data->A_tiles[row * TK + tile->row];
            MatrixTile *b = tile;
            MatrixTile *p = allocate_tile(a->rows, b->cols, a->row, b->col);
            auto tiles = new std::tuple<MatrixTile *, MatrixTile *, MatrixTile *>(a, b, p);
            tu_result(ctx, tiles, T_ABPTiles);
        }
    }
}

void sum_state_exec_tile_c(TU_ExecContext *ctx, void *rawdata, tu_i64 type) {
    auto task_data = (SumStateData*)tu_node_data(ctx);
    assert(type == T_TileC);
    auto tile = (MatrixTile*)rawdata;
    size_t TN = task_data->TN;
    size_t c_idx = tile->row * TN + tile->col;

    if (task_data->sum_queues[c_idx].size() > 0) {
        auto p = task_data->sum_queues[c_idx].back();
        task_data->sum_queues[c_idx].pop_back();
        auto tiles = new std::pair<MatrixTile *, MatrixTile *>(p, tile);
        tu_result(ctx, tiles, T_PCTiles);
    } else {
        task_data->C_tiles[c_idx] = tile;
    }
}

void sum_state_exec_tile_p(TU_ExecContext *ctx, void *rawdata, tu_i64 type) {
    auto task_data = (SumStateData*)tu_node_data(ctx);
    assert(type == T_TileP);
    auto tile = (MatrixTile*)rawdata;
    size_t TN = task_data->TN;
    size_t c_idx = tile->row * TN + tile->col;

    if (task_data->C_tiles[c_idx] != nullptr) {
        auto c = task_data->C_tiles[c_idx];
        task_data->C_tiles[c_idx] = nullptr;
        auto tiles = new std::pair<MatrixTile *, MatrixTile *>(tile, c);
        tu_result(ctx, tiles, T_PCTiles);
    } else {
        task_data->sum_queues[c_idx].push_back(tile);
    }
}

void sum_state_exec_tile_pc_tiles(TU_ExecContext *ctx, void *rawdata, tu_i64 type) {
    auto task_data = (SumStateData*)tu_node_data(ctx);
    assert(type == T_PCTiles);
    auto tiles = (std::pair<MatrixTile *, MatrixTile *>*)rawdata;
    auto c = tiles->second;

    delete tiles;
    task_data->count -= 1;
    if (task_data->count == 0) {
        printf("result\n");
        tu_result(ctx, c, T_TileC);
        return;
    }
    sum_state_exec_tile_c(ctx, c, T_TileC);
}
