#ifndef DATA
#define DATA

enum class MatrixKind { A, B, C, P };

template <MatrixKind Kind = MatrixKind::A>
struct Mat {
    size_t rows = 0;
    size_t cols = 0;
    double *data = nullptr;

    Mat(size_t rows, size_t cols)
        : rows(rows), cols(cols), data(new double[rows * cols]) {}
    ~Mat() { delete[] data; }

    Mat(const Mat &) = delete;
    Mat &operator=(const Mat &) = delete;

    double &operator()(size_t row, size_t col) { return data[row * cols + col]; }
    double const &operator()(size_t row, size_t col) const { return data[row * cols + col]; }
};
using Matrix = Mat<>;

template <MatrixKind Kind = MatrixKind::A>
struct MatTile {
    size_t row = 0;
    size_t col = 0;
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
using MatrixTile = MatTile<>;

struct TileTriplet {
    MatrixTile a, b, c;
};

#endif
