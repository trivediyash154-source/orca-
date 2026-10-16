#include "Hungarian.hpp"

#include <limits>

#include "../core/Validation.hpp"

namespace orca {

HungarianResult solveSquareAssignment(const std::vector<std::vector<double>>& costMatrix) {
    const std::size_t n = costMatrix.size();
    requireThat(n > 0, "solveSquareAssignment: costMatrix must not be empty");
    for (const auto& row : costMatrix) {
        requireThat(row.size() == n, "solveSquareAssignment: costMatrix must be square");
    }

    constexpr double kInfinity = std::numeric_limits<double>::infinity();

    // 1-indexed internally (index 0 is a sentinel "no row"/"no column" marker) -- this is
    // the standard formulation for this algorithm, and translating it to 0-indexed
    // obscures the potential updates without simplifying them. Converted back to 0-indexed
    // in HungarianResult at the end.
    std::vector<double> rowPotential(n + 1, 0.0);   // u[i]
    std::vector<double> colPotential(n + 1, 0.0);   // v[j]
    std::vector<std::size_t> colToRow(n + 1, 0);    // p[j]: row matched to column j (0 = none)
    std::vector<std::size_t> parentCol(n + 1, 0);   // way[j]: previous column on the
                                                    // augmenting path that reached j

    long operationCount = 0;

    for (std::size_t row = 1; row <= n; ++row) {
        colToRow[0] = row;
        std::size_t curCol = 0;
        std::vector<double> minReducedCost(n + 1, kInfinity);
        std::vector<bool> visited(n + 1, false);

        // Grow a shortest-path tree (over reduced costs) from `row` until it reaches an
        // unmatched column.
        do {
            visited[curCol] = true;
            const std::size_t curRow = colToRow[curCol];
            double delta = kInfinity;
            std::size_t nextCol = 0;

            for (std::size_t col = 1; col <= n; ++col) {
                if (visited[col]) {
                    continue;
                }
                ++operationCount;
                const double reduced = costMatrix[curRow - 1][col - 1] -
                                       rowPotential[curRow] - colPotential[col];
                if (reduced < minReducedCost[col]) {
                    minReducedCost[col] = reduced;
                    parentCol[col] = curCol;
                }
                if (minReducedCost[col] < delta) {
                    delta = minReducedCost[col];
                    nextCol = col;
                }
            }

            // Adjust potentials so the next-closest column becomes reachable at zero
            // reduced cost, without ever making any reduced cost negative (the invariant
            // that keeps the potentials a valid LP-duality certificate throughout).
            for (std::size_t col = 0; col <= n; ++col) {
                if (visited[col]) {
                    rowPotential[colToRow[col]] += delta;
                    colPotential[col] -= delta;
                } else {
                    minReducedCost[col] -= delta;
                }
            }

            curCol = nextCol;
        } while (colToRow[curCol] != 0);

        // Augment: flip matched/unmatched status along the path just found.
        while (curCol != 0) {
            const std::size_t prevCol = parentCol[curCol];
            colToRow[curCol] = colToRow[prevCol];
            curCol = prevCol;
        }
    }

    HungarianResult result;
    result.rowToColumn.assign(n, 0);
    result.operationCount = operationCount;
    for (std::size_t col = 1; col <= n; ++col) {
        const std::size_t row = colToRow[col];
        result.rowToColumn[row - 1] = col - 1;
    }
    for (std::size_t row = 0; row < n; ++row) {
        result.totalCost += costMatrix[row][result.rowToColumn[row]];
    }
    return result;
}

}  // namespace orca
