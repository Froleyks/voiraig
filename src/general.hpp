#pragma once

#include "aiger.hpp"

// Check all safety and generalized Buchi properties together.  The returned
// trace uses the original model's literals and identifies the violated
// property.
bool general_liveness(aiger *model, aiger *&witness,
                      std::vector<std::vector<unsigned>> &cex,
                      unsigned &property_index, bool &is_justice);
