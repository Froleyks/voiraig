#pragma once

#include "aiger.hpp"

#include <vector>

bool kind(aiger *aig, aiger *&k_witness_model,
          std::vector<std::vector<unsigned>> &cex, unsigned simple_path,
          bool always_unique);
