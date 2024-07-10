#pragma once

#include "aiger.hpp"

#include <vector>

bool ic3(aiger *model, std::vector<std::vector<unsigned>> &cex);
