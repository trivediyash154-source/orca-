#include "GreedyAssignment.hpp"

#include <limits>

#include "../core/Validation.hpp"

namespace orca {

GreedyResult solveGreedyRectangularAssignment(
    const std::vector<std::vector<double>>& costMatrix,
    const std::vector<std::vector<bool>>& feasible) {
    const std::size_t rows = costMatrix.size();
    requireThat(rows > 0, "solveGreedyRectangularAssignment: costMatrix must not be empty");
    const std::size_t cols = costMatrix[0].size();
    requireThat(cols > 0, "solveGreedyRectangularAssignment: costMatrix rows must not be "
                          "empty");
    requireThat(feasible.size() == rows,
                "solveGreedyRectangularAssignment: feasible must match costMatrix's shape");
    for (std::size_t r = 0; r < rows; ++r) {
        requireThat(costMatrix[r].size() == cols && feasible[r].size() == cols,
                    "solveGreedyRectangularAssignment: costMatrix and feasible must be "
                    "rectangular and match each other's shape");
    }

    std::vector<bool> rowUsed(rows, false);
    std::vector<bool> colUsed(cols, false);
    GreedyResult result;
    result.rowToColumn.assign(rows, std::nullopt);

    const std::size_t maxPossibleAssignments = rows < cols ? rows : cols;
    for (std::size_t step = 0; step < maxPossibleAssignments; ++step) {
        double bestCost = std::numeric_limits<double>::infinity();
        std::size_t bestRow = 0;
        std::size_t bestCol = 0;
        bool found = false;

        for (std::size_t row = 0; row < rows; ++row) {
            if (rowUsed[row]) {
                continue;
            }
            for (std::size_t col = 0; col < cols; ++col) {
                if (colUsed[col] || !feasible[row][col]) {
                    continue;
                }
                if (costMatrix[row][col] < bestCost) {
                    bestCost = costMatrix[row][col];
                    bestRow = row;
                    bestCol = col;
                    found = true;
                }
            }
        }

        if (!found) {
            break;  // no feasible pair remains among unused rows/columns
        }

        rowUsed[bestRow] = true;
        colUsed[bestCol] = true;
        result.rowToColumn[bestRow] = bestCol;
        result.totalCost += bestCost;
        ++result.assignedCount;
    }

    return result;
}

}  // namespace orca
