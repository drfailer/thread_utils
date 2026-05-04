#include "matrix.hpp"
#include <stdio.h>
#include <cmath>

bool matrix_test_equal(Matrix const &found, Matrix const&expected, double tolerance) {
    auto const &f = found;
    auto const &e = expected;

    if (f.rows != e.rows || f.cols != e.cols) {
        printf("matrix_test_equal: matrix don't have the same size.\n");
        return false;
    }

    // TODO: parallelize this
    size_t ROWS = e.rows;
    size_t COLS = e.rows;
    for (size_t row = 0; row < ROWS; ++row) {
        for (size_t col = 0; col < COLS; ++col) {
            if (std::abs(f(row, col) - e(row, col)) > tolerance) {
                printf("m(%ld, %ld): found `%lf` expected `%lf`.\n",
                        row, col, f(row, col), e(row, col));
                return false;
            }
        }
    }
    return true;
}

void matrix_init_double(Matrix &m) {
    for (size_t i = 0; i < m.rows; ++i) {
        for (size_t j = 0; j < m.cols; ++j) {
            m(i, j) = 1. / double(i * m.cols + j + 1);
        }
    }
}

void matrix_init_int(Matrix &m) {
    for (size_t i = 0; i < m.rows; ++i) {
        for (size_t j = 0; j < m.cols; ++j) {
            m(i, j) = i * m.cols + j;
        }
    }
}

void matrix_zero(Matrix &m) {
    memset(m.data, 0, m.rows * m.cols * sizeof(*m.data));
}

void matrix_print(Matrix const &m) {
    if (m.rows > 16 || m.cols > 16) {
        printf("M[%ld, %ld]\n", m.rows, m.cols);
        return;
    }
    printf("M = \n");
    for (size_t row = 0; row < m.rows; ++row) {
        printf("\t");
        for (size_t col = 0; col < m.cols; ++col) {
            printf("%lf ", m(row, col));
        }
        printf("\n");
    }
}
