#pragma once

#include "aiger.hpp"

#include <vector>

bool backward(aiger *model, std::vector<std::vector<unsigned>> &cex);
