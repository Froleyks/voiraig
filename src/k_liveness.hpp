#pragma once

#include "aiger.hpp"

#include <vector>

// Transform a single fairness property into a k-liveness safety instance and
// discharge it with IC3. The returned witness is owned by the caller and may
// alias 'model' when no transformation is needed.
bool k_liveness(aiger *model, aiger *&witness,
                std::vector<std::vector<unsigned>> &cex);
