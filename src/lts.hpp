#pragma once

#include "aiger.hpp"

#include <vector>

bool lts(aiger *model, aiger *&witness,
         std::vector<std::vector<unsigned>> &cex);
