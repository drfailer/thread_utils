#ifndef DATA
#define DATA

struct Matrix {
    size_t rows = 0;
    size_t cols = 0;
    double *data = nullptr;

    Matrix(size_t rows, size_t cols)
        : rows(rows), cols(cols), data(new double[rows * cols]) {}
    ~Matrix() { delete[] data; }

    Matrix(const Matrix &) = delete;
    Matrix &operator=(const Matrix &) = delete;

    double &operator()(size_t row, size_t col) { return data[row * cols + col]; }
    double const &operator()(size_t row, size_t col) const { return data[row * cols + col]; }
};

struct MatrixTile {
    size_t row;
    size_t col;
    size_t rows = 0;
    size_t cols = 0;
    size_t matrixRow = 0;
    size_t matrixCol = 0;
    size_t matrixRows = 0;
    size_t matrixCols = 0;
    double *data = nullptr;

    double &operator()(size_t row, size_t col) { return data[row * matrixCols + col]; }
    double const &operator()(size_t row, size_t col) const { return data[row * matrixCols + col]; }
};

struct TileTriplet {
    MatrixTile a, b, c;
};

#endif
