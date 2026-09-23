#pragma once

#include "aiger.hpp"

// Complete a sparse safety trace and identify the original property it
// violates.
bool complete_safety_trace(aiger *model,
                           std::vector<std::vector<unsigned>> &cex,
                           unsigned &property_index);
bool solve_safety(aiger *model, const options &opts, aiger *&witness,
                  std::vector<std::vector<unsigned>> &cex,
                  unsigned &property_index);
