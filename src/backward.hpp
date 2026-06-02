#pragma once

#include "aiger.hpp"

#include <vector>

bool backward(aiger *model, std::vector<std::vector<unsigned>> &cex,
              unsigned k, bool use_flipping, bool use_simulation);
