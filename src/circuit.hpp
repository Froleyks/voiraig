#pragma once

#include <cassert>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// Circuit representation using a single vector
// Size: (M+1) * 2, where M is maxvar
// Layout:
// - Index 0: number of inputs (I)
// - Index 1: number of latches (L)
// - Indices 2 to 2*(I+1)-1: inputs (nothing to store, just alignment)
// - Indices 2*(I+1) to 2*(I+L+1)-1: latches (reset at pos lit, next at neg lit)
// - Remaining indices: AND gates (left at pos lit, right at neg lit)

class Circuit {
public:
  using id = unsigned;

private:
  std::vector<id> data;
  id C; // Number of constraints/outputs
  id P; // Property literal
  id Q; // Number of AND gates

public:
  Circuit() : C(0), P(0), Q(0) {}

  // Read from AAG (ASCII AIGER) file
  void read_from_file(const char *path) {
    std::ifstream file(path);
    if (!file.is_open()) {
      throw std::runtime_error(std::string("Cannot open file: ") + path);
    }

    std::string line;
    std::getline(file, line);
    
    std::istringstream header(line);
    std::string format;
    id M, I, L, O, A;
    
    header >> format >> M >> I >> L >> O >> A;
    
    if (format != "aag") {
      throw std::runtime_error("Only AAG (ASCII AIGER) format supported");
    }

    // Initialize data vector: (M+1) * 2
    data.resize((M + 1) * 2, 0);
    
    // Store metadata at indices 0 and 1
    data[0] = I;  // Number of inputs
    data[1] = L;  // Number of latches
    
    Q = A;  // Number of AND gates
    
    // Read inputs (just validate they exist in correct order)
    for (id i = 0; i < I; i++) {
      std::string input_line;
      std::getline(file, input_line);
      std::istringstream iss(input_line);
      id lit;
      iss >> lit;
      // Inputs should be 2, 4, 6, ... (even literals starting from 2)
      assert(lit == 2 * (i + 1));
    }
    
    // Read latches: each line has "latch next [reset]"
    // reset is optional; if not present, defaults to 0 (FALSE)
    for (id i = 0; i < L; i++) {
      id latch, next, reset;
      std::string latch_line;
      std::getline(file, latch_line);
      std::istringstream iss(latch_line);
      
      iss >> latch >> next;
      
      // Try to read optional reset value from the same line
      if (!(iss >> reset)) {
        // No reset value provided, defaults to 0 (initialized to FALSE)
        reset = 0;
      }
      
      // Latch literal should be 2*(I+i+1)
      assert(latch == 2 * (I + i + 1));
      
      // Store reset at positive literal index, next at negative literal index
      data[latch] = reset;
      data[latch ^ 1] = next;
    }
    
    // Read outputs/bad properties
    for (id i = 0; i < O; i++) {
      id output_lit;
      file >> output_lit;
      if (i == 0) {
        P = output_lit;  // Store first output/property
      }
    }
    
    // Read AND gates: each line has "lhs rhs0 rhs1"
    for (id i = 0; i < A; i++) {
      id lhs, rhs0, rhs1;
      file >> lhs >> rhs0 >> rhs1;
      
      // lhs should be even and > 2*(I+L+1)
      assert((lhs & 1) == 0);
      
      // Store left operand at positive literal index, right at negative literal index
      data[lhs] = rhs0;
      data[lhs ^ 1] = rhs1;
    }
    
    C = O;  // Number of outputs/constraints
  }

  // Write AAG representation to output stream
  friend std::ostream& operator<<(std::ostream& os, const Circuit& circuit) {
    id I = circuit.data[0];  // Number of inputs
    id L = circuit.data[1];  // Number of latches
    id A = circuit.Q;        // Number of AND gates
    id O = circuit.C;        // Number of outputs
    
    // Calculate M (maxvar)
    id M = (circuit.data.size() / 2) - 1;
    
    // Header line: aag M I L O A
    os << "aag " << M << " " << I << " " << L << " " << O << " " << A << "\n";
    
    // Output inputs
    for (id i = 0; i < I; i++) {
      os << (2 * (i + 1)) << "\n";
    }
    
    // Output latches with next and reset values
    // Format: latch next [reset] (reset omitted if it equals 0 = default)
    for (id i = 0; i < L; i++) {
      id latch_lit = 2 * (I + i + 1);
      id reset = circuit.data[latch_lit];
      id next = circuit.data[latch_lit ^ 1];
      os << latch_lit << " " << next;
      // Only output reset if it's different from 0 (non-default reset)
      if (reset != 0) {
        os << " " << reset;
      }
      os << "\n";
    }
    
    // Output property/output literals
    // Only output if there are outputs defined (O > 0)
    // Note: We only store and output the first output/property
    if (O > 0) {
      os << circuit.P << "\n";
    }
    
    // Output AND gates
    for (id i = 0; i < A; i++) {
      id lhs = 2 * (I + L + i + 1);
      id rhs0 = circuit.data[lhs];
      id rhs1 = circuit.data[lhs ^ 1];
      os << lhs << " " << rhs0 << " " << rhs1 << "\n";
    }
    
    return os;
  }

  // Getters for metadata
  id get_num_inputs() const { return data.size() > 0 ? data[0] : 0; }
  id get_num_latches() const { return data.size() > 1 ? data[1] : 0; }
  id get_num_ands() const { return Q; }
  id get_num_outputs() const { return C; }
  id get_property() const { return P; }
  
  // Access to data vector (for debugging/testing)
  const std::vector<id>& get_data() const { return data; }
};
