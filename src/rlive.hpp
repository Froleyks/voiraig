#pragma once

#include "aiger.hpp"

#include <vector>

// Placeholder for an alternative liveness engine.
bool rlive(aiger *model, aiger *&witness,
           std::vector<std::vector<unsigned>> &cex);
