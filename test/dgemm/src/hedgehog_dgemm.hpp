#ifndef HEDGEHOG_DGEMM
#define HEDGEHOG_DGEMM
#include <hedgehog.h>
#include "data.hpp"

MatrixTile *allocate_tile(size_t rows, size_t cols, size_t row, size_t col);
void deallocate_tile(MatrixTile *tile);

using AMat = Mat<MatrixKind::A>;
using BMat = Mat<MatrixKind::B>;
using CMat = Mat<MatrixKind::C>;

using ATile = MatTile<MatrixKind::A>;
using BTile = MatTile<MatrixKind::B>;
using CTile = MatTile<MatrixKind::C>;
using PTile = MatTile<MatrixKind::P>;

using ATilePtr = std::shared_ptr<ATile>;
using BTilePtr = std::shared_ptr<BTile>;
using CTilePtr = std::shared_ptr<CTile>;
using PTilePtr = std::shared_ptr<PTile>;

struct SplitTask : hh::AbstractTask<3, AMat, BMat, CMat, ATile, BTile, CTile> {
    size_t tile_size;

    SplitTask(size_t tile_size, size_t numThreads)
        : hh::AbstractTask<3, AMat, BMat, CMat, ATile, BTile, CTile>("split_task", numThreads),
          tile_size(tile_size) {}

    void execute(std::shared_ptr<AMat> A) override { exec<MatrixKind::A>(std::move(A)); }
    void execute(std::shared_ptr<BMat> B) override { exec<MatrixKind::B>(std::move(B)); }
    void execute(std::shared_ptr<CMat> C) override { exec<MatrixKind::C>(std::move(C)); }

    template <MatrixKind K>
    void exec(std::shared_ptr<Mat<K>> M) {
        for (size_t i = 0; i < M->rows; i += tile_size) {
            for (size_t j = 0; j < M->cols; j += tile_size) {
                auto tile = std::make_shared<MatTile<K>>();
                tile->row = i / tile_size,
                tile->col = j / tile_size,
                tile->rows = std::min(M->rows - i, tile_size),
                tile->cols = std::min(M->cols - j, tile_size),
                tile->matrixRow = i,
                tile->matrixCol = j,
                tile->matrixRows = M->rows,
                tile->matrixCols = M->cols,
                tile->data = &M->operator()(i, j),
                this->addResult(tile);
            }
        }
    }

    std::shared_ptr<hh::AbstractTask<3, AMat, BMat, CMat, ATile, BTile, CTile>>
    copy() override {
        return std::make_shared<SplitTask>(tile_size, this->numberThreads());
    }
};

struct ProductTask : hh::AbstractTask<1, std::tuple<ATilePtr, BTilePtr, PTilePtr>, PTile> {
    ProductTask(size_t numThreads)
        : hh::AbstractTask<1, std::tuple<ATilePtr, BTilePtr, PTilePtr>, PTile>("product_task", numThreads) {}

    void execute(std::shared_ptr<std::tuple<ATilePtr, BTilePtr, PTilePtr>> tiles) override {
        auto [a, b, p] = *tiles;
        assert(a->rows == p->rows);
        assert(b->cols == p->cols);
        assert(a->cols == b->rows);
        cblas_dgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans, (blasint)a->rows, (blasint)b->cols, (blasint)a->cols,
                    1.f, (const double *)a->data, (blasint)a->cols, (const double *)b->data, (blasint)b->cols, 0,
                    (double *)p->data, (blasint)p->cols);
        this->addResult(p);
    }

    std::shared_ptr<hh::AbstractTask<1, std::tuple<ATilePtr, BTilePtr, PTilePtr>, PTile>>
    copy() override {
        return std::make_shared<ProductTask>(this->numberThreads());
    }
};

struct SumTask : hh::AbstractTask<1, std::tuple<PTilePtr, CTilePtr>, CTile> {
    SumTask(size_t numThreads)
        : hh::AbstractTask<1, std::tuple<PTilePtr, CTilePtr>, CTile>("sum_task", numThreads) {}

    void execute(std::shared_ptr<std::tuple<PTilePtr, CTilePtr>> tiles) override {
        auto [p, c] = *tiles;
        assert(p->rows == c->rows);
        assert(p->cols == c->cols);

        for (size_t row = 0; row < p->rows; ++row) {
            for (size_t col = 0; col < p->cols; ++col) {
                (*c)(row, col) += (*p)(row, col);
            }
        }
        this->addResult(c);
    }

    std::shared_ptr<hh::AbstractTask<1, std::tuple<PTilePtr, CTilePtr>, CTile>>
    copy() override {
        return std::make_shared<SumTask>(this->numberThreads());
    }
};

struct ProductState : hh::AbstractTask<2, ATile, BTile, std::tuple<ATilePtr, BTilePtr, PTilePtr>> {
    size_t TM, TN, TK;
    std::vector<ATilePtr> A_tiles;
    std::vector<BTilePtr> B_tiles;

    ProductState(size_t TM, size_t TN, size_t TK)
        : TM(TM), TN(TN), TK(TK), A_tiles(TM * TK), B_tiles(TK * TN) {}

    void execute(std::shared_ptr<ATile> tile) override {
        assert(A_tiles[tile->row * TK + tile->col] == nullptr);
        A_tiles[tile->row * TK + tile->col] = tile;
        for (size_t col = 0; col < TN; ++col) {
            if (B_tiles[tile->col * TN + col] != nullptr) {
                ATilePtr a = tile;
                BTilePtr b = B_tiles[tile->col * TN + col];
                PTile *p_ptr = reinterpret_cast<PTile*>(allocate_tile(a->rows, b->cols, a->row, b->col));
                PTilePtr p = std::shared_ptr<PTile>(p_ptr, [](PTile *tile) {
                    deallocate_tile(reinterpret_cast<MatrixTile*>(tile));
                });
                this->addResult(std::make_shared<std::tuple<ATilePtr, BTilePtr, PTilePtr>>(a, b, p));
            }
        }
    }

    void execute(std::shared_ptr<BTile> tile) override {
        assert(B_tiles[tile->row * TN + tile->col] == nullptr);
        B_tiles[tile->row * TN + tile->col] = tile;
        for (size_t row = 0; row < TM; ++row) {
            if (A_tiles[row * TK + tile->row] != nullptr) {
                ATilePtr a = A_tiles[row * TK + tile->row];
                BTilePtr b = tile;
                PTile *p_ptr = reinterpret_cast<PTile*>(allocate_tile(a->rows, b->cols, a->row, b->col));
                PTilePtr p = std::shared_ptr<PTile>(p_ptr, [](PTile *tile) {
                    deallocate_tile(reinterpret_cast<MatrixTile*>(tile));
                });
                this->addResult(std::make_shared<std::tuple<ATilePtr, BTilePtr, PTilePtr>>(a, b, p));
            }
        }
    }
};

struct SumState : hh::AbstractTask<2, CTile, PTile, std::tuple<PTilePtr, CTilePtr>, CTile> {
    size_t TM, TN, TK;
    size_t count;
    std::vector<std::vector<PTilePtr>> sum_queues;
    std::vector<CTilePtr> C_tiles;

    SumState(size_t TM, size_t TN, size_t TK)
        : TM(TM), TN(TN), TK(TK), count(TM * TN * TK), sum_queues(TM * TN), C_tiles(TM * TN) {}

    void execute(std::shared_ptr<CTile> tile) override {
        size_t c_idx = tile->row * TN + tile->col;
        assert(C_tiles[c_idx] == nullptr);
        if (sum_queues[c_idx].size() > 0) {
            auto p = sum_queues[c_idx].back();
            sum_queues[c_idx].pop_back();
            this->count -= 1;
            this->addResult(std::make_shared<std::tuple<PTilePtr, CTilePtr>>(p, tile));
        } else {
            C_tiles[c_idx] = tile;
        }
    }

    void execute(std::shared_ptr<PTile> tile) override {
        size_t c_idx = tile->row * TN + tile->col;
        if (C_tiles[c_idx] != nullptr) {
            auto c = C_tiles[c_idx];
            C_tiles[c_idx] = nullptr;
            this->count -= 1;
            this->addResult(std::make_shared<std::tuple<PTilePtr, CTilePtr>>(tile, c));
        } else {
            sum_queues[c_idx].push_back(tile);
        }
    }

    bool canTerminate() const override {
        return count == 0;
    }
};

struct DgemmGraph : hh::Graph<3, AMat, BMat, CMat, CTile> {
    DgemmGraph(size_t M, size_t N, size_t K, size_t tile_size) {
        size_t TM = M / tile_size + (M % tile_size == 0 ? 0 : 1);
        size_t TN = N / tile_size + (N % tile_size == 0 ? 0 : 1);
        size_t TK = K / tile_size + (K % tile_size == 0 ? 0 : 1);

        auto split_task = std::make_shared<SplitTask>(tile_size, 3);
        auto product_task = std::make_shared<ProductTask>(40);
        auto sum_task = std::make_shared<SumTask>(10);
        auto product_state = std::make_shared<ProductState>(TM, TN, TK);
        auto sum_state = std::make_shared<SumState>(TM, TN, TK);

        this->inputs(split_task);
        this->edges(split_task, product_state);
        this->edges(split_task, sum_state);
        this->edges(product_state, product_task);
        this->edges(product_task, sum_state);
        this->edges(sum_state, sum_task);
        this->edges(sum_task, sum_state);
        this->outputs(sum_state);
    }
};

#endif
