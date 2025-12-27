#pragma once

#include "aiger.hpp"

#include <vector>

// If provided, set to the first index in model->ands added for the invariant.
bool ic3(aiger *model, std::vector<std::vector<unsigned>> &cex,
         unsigned *first_added_gate = nullptr);
