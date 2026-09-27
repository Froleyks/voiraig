#pragma once

#include <vector>

extern "C" {
#include "aiger.h"
}

// Complete a projected, possibly sparse trace against the original model.
// cex[0] contains initial latch literals; cex[t+1] contains input literals at
// state t.  The state after the final input frame must close an accepting
// nonempty loop.  On success the cubes become total and justice_index names
// an original justice property whose signals and all fairness signals occur
// on that loop.  On failure neither output is changed.
bool complete_liveness_trace(aiger *original,
                             std::vector<std::vector<unsigned>> &cex,
                             unsigned &justice_index);
