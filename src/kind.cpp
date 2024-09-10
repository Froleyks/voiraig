#include "aiger.hpp"
#include "mcaiger.hpp"
#include "utils.hpp"

#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static aiger *model;

// aigcertify_kind
aiger *k_witness_model;
unsigned k;
unsigned num_inputs;
unsigned num_latches;
unsigned num_ands;
unsigned total_number_literals;

unsigned kw_num_inputs;
unsigned kw_num_latches;
unsigned kw_num_ands;
unsigned kw_total_number_literals;

std::vector<std::vector<unsigned>> map;

bool is_input(unsigned index) {
  unsigned offset = index / 2;
  return offset <= num_inputs;
}

bool is_latch(unsigned index) {
  unsigned offset = index / 2;
  return offset > num_inputs && offset <= (num_inputs + num_latches);
}

bool is_and(unsigned index) {
  unsigned offset = index / 2;
  return offset > num_inputs + num_latches && offset <= total_number_literals;
}

unsigned neg(unsigned l) {
  if (l % 2 == 1) return l - 1;
  return l + 1;
}

/* for i-th copy of the circuits, given original index
 * inputs: index + num_inputs * complement * 2
 * latches: index + (k-1)*num_inputs*2 + num_latches * complement * 2
 * ands: index + (k-1)*num_inputs*2 + (k-1)*num_latches*2 + k * 2 + num_ands *
 * complement * 2
 * */
unsigned mapping_old_index_to_k_witness_circuit(unsigned index, unsigned i) {
  if (index == 0 || index == 1) return index;
  unsigned complement = (k - 1) - i;
  unsigned new_index;
  // LV5(num_inputs, num_latches, num_ands, total_number_literals, k, index, i,
  //     complement);
  if (is_input(index)) {
    new_index = index + complement * num_inputs * 2;
    L5 << "is input" << index << "at" << i << "maps to" << new_index;
  } else if (is_latch(index)) {
    new_index = index + (k - 1) * num_inputs * 2 + complement * num_latches * 2;
    L5 << "is latch" << index << "at" << i << "maps to" << new_index;
  } else if (is_and(index)) {
    new_index = index + (k - 1) * num_inputs * 2 + (k - 1) * num_latches * 2 +
                k * 2 + num_ands * complement * 2;
    L5 << "is and" << index << "at" << i << "maps to" << new_index;
  }
  assert(new_index < 4200000000);
  return new_index;
}

bool kw_is_input(unsigned index) {
  unsigned offset = index / 2;
  return offset <= kw_num_inputs;
}

bool kw_is_latch(unsigned index) {
  unsigned offset = index / 2;
  return offset > kw_num_inputs && offset <= (kw_num_inputs + kw_num_latches);
}

bool kw_is_and(unsigned index) {
  unsigned offset = index / 2;
  return offset > kw_num_inputs + kw_num_latches &&
         offset <= kw_total_number_literals;
}

unsigned map_original(unsigned l) {
  if (l == 0 || l == 1) return l;
  if (kw_is_input(l)) {
    return l;
  } else if (kw_is_latch(l)) {
    return l + kw_num_inputs * 2;
  } else if (kw_is_and(l)) {
    return l + (kw_num_inputs + kw_num_latches) * 2;
  }
  assert(false);
  return 0;
}

unsigned map_to_next(unsigned l) {
  if (l == 0 || l == 1) return l;
  if (kw_is_input(l)) {
    L4 << "l:" << l << "/n";
    return l + kw_num_inputs * 2;
  } else if (kw_is_latch(l)) {
    unsigned idx = l / 2;
    aiger_symbol *s = aiger_is_latch(k_witness_model, l);
    L4 << "l:" << l << " "
       << "s->lit: " << s->lit << "/n";
    unsigned new_next = map_original(s->next);
    return l % 2 ? neg(new_next) : new_next;
  } else if (kw_is_and(l)) {
    L4 << "land :" << l << "/n";
    return l + kw_total_number_literals * 2;
  }
  assert(false);
  return 0;
}

static void witness(int kin, aiger *&k_witness_model) {
  bool not_supported = model->num_constraints;
  for (auto [l, r] : latches(model) | resets) {
    if (not_supported) break;
    if (r < 2 || r == l) continue;
    not_supported = true;
  }
  if (not_supported) {
    std::cerr << "Voiraig: Constraints and reset functions not supported with kInd without simple path";
    exit(1);
  }
  k = kin;
  if (k < 2) {
    k_witness_model = model;
    return;
  }

  num_inputs = model->num_inputs;
  num_latches = model->num_latches;
  num_ands = model->num_ands;
  total_number_literals = num_inputs + num_latches + num_ands;
  map.resize(k + 1, std::vector<unsigned>(total_number_literals, 0));
  assert(num_latches > 0);

  L4 << "inputs=" << num_inputs << ", model_latches=" << num_latches
     << ", and_gates=" << num_ands;

  aiger_and *model_ands = model->ands;
  aiger_symbol *model_latches = model->latches;

  k_witness_model = aiger_init();

  /* indices in the k-witness aiger model */
  unsigned L_k_1_begin;
  unsigned L_0_begin;
  unsigned B_k_1_begin;
  unsigned and_gate_begin;
  // unsigned uniqueness_begin;
  // unsigned uniqueness;
  unsigned R_i_size;
  unsigned R_or_1_to_k_1;
  unsigned Rp_0_begin;
  unsigned original_output;
  unsigned original_p;
  unsigned w_output;
  std::vector<unsigned> Rs;
  std::vector<unsigned> ps; /* the properties */

  unsigned num_comparisons = k * (k - 1) / 2;

  L_k_1_begin = num_inputs * k * 2 + 2;
  L_0_begin = L_k_1_begin + (k - 1) * num_latches * 2;
  B_k_1_begin = (num_inputs + num_latches) * k * 2 + 2;
  and_gate_begin = (k * (num_inputs + num_latches) + k) * 2 + 2;

  // uniqueness_begin = and_gate_begin + num_ands * k * 2;
  // uniqueness = uniqueness_begin + (4 * num_latches -1) * k * (k-1) - 2 +
  // (num_comparisons-1)*2;
  R_i_size = (3 * num_latches + num_latches - 1) * 2;
  original_output = output(model);
  original_p = neg(original_output);
  /* only one output in the original circuit */

  L4 << "L_k_1_begin=" << L_k_1_begin;
  L4 << "L_0_begin=" << L_0_begin;
  L4 << "B_k_1_begin=" << B_k_1_begin;
  L4 << "original property=" << original_p;

  /* adding original set of inputs */
  for (unsigned i = 0; i < num_inputs; i++) {
    aiger_add_input(k_witness_model, (i + 1) * 2, "");
    simulates(k_witness_model, (i + 1) * 2, (i + 1) * 2);
  }

  /* adding k-1 copies of inputs, as model_latches */
  for (unsigned i = k - 2; i != -1; i--) {
    unsigned complement = k - 1 - i;
    unsigned X_i_begin = 2 + complement * num_inputs * 2;
    unsigned X_i_1_begin = 2 + (complement - 1) * num_inputs * 2;

    for (unsigned j = 0; j < num_inputs; j++) {
      unsigned input_index = X_i_begin + j * 2;
      unsigned next = X_i_1_begin + j * 2;
      aiger_add_latch(k_witness_model, input_index, next, "");
      aiger_add_reset(k_witness_model, input_index, input_index);
    }
  }

  L4 << "added k copies of inputs";

  /* adding k copies of model_latches */
  /* copying L_{k-1} from original circuit */
  L4 << "adding" << num_latches << "latches";
  for (unsigned i = 0; i < num_latches; i++) {
    aiger_symbol *current_latch = model_latches + i;
    unsigned latch_index =
        mapping_old_index_to_k_witness_circuit(current_latch->lit, k - 1);
    unsigned next =
        mapping_old_index_to_k_witness_circuit(current_latch->next, k - 1);
    L5 << "adding" << latch_index << "with next" << next;
    aiger_add_latch(k_witness_model, latch_index, next, "");
    L5 << "and reset" << reset;
    simulates(k_witness_model, current_latch->lit, latch_index);
    // simulates(int *witness, unsigned int model_lit, unsigned int
    // witness_lit);
  }

  L4 << "added L^{k-1}";

  /* adding the rest k-1 copies of model_latches */
  for (unsigned i = k - 2; i != -1; i--) {
    unsigned complement = k - 1 - i;
    unsigned L_i_begin = L_k_1_begin + complement * num_latches * 2;
    unsigned L_i_1_begin =
        L_k_1_begin + (complement - 1) * num_latches * 2; /* L_{i+1}_begin */

    L4 << "adding L^" << complement << ", L_i_begin=" << L_i_begin
       << ", L_i_1_begin=" << L_i_1_begin;
    for (unsigned j = 0; j < num_latches; j++) {
      unsigned latch_index = L_i_begin + j * 2;
      unsigned next = L_i_1_begin + j * 2;
      aiger_add_latch(k_witness_model, latch_index, next, "");
      // L4<<"adding new latch: "<<latch_index<<"\n";
    }
  }

  L4 << "added k copies of model_latches";

  /* adding B */
  // aiger_add_latch(k_witness_model, B_k_1_begin, uniqueness + 2, "");
  aiger_add_latch(k_witness_model, B_k_1_begin, B_k_1_begin, "");
  for (unsigned i = 1; i < k; i++) {
    unsigned index = B_k_1_begin + i * 2;
    // unsigned next = uniqueness + 2*(i+1);
    unsigned next = B_k_1_begin + (i - 1) * 2;
    aiger_add_latch(k_witness_model, index, next, "");
  }

  L4 << "added B";

  /* copying the original circuit k times */
  unsigned current_index;
  for (unsigned i = k - 1; i != -1; i--) {
    // L4<<"copying the "<<complement<<"th circuit,"<<"\n";
    for (unsigned j = 0; j < num_ands; j++) {
      aiger_and *original_and = model_ands + j;
      unsigned old_index;
      unsigned old_rhs0;
      unsigned old_rhs1;

      old_index = original_and->lhs;
      old_rhs0 = original_and->rhs0;
      old_rhs1 = original_and->rhs1;
      /* new and-gate */
      unsigned and_index;
      unsigned rhs0;
      unsigned rhs1;

      and_index = mapping_old_index_to_k_witness_circuit(old_index, i);
      rhs0 = mapping_old_index_to_k_witness_circuit(old_rhs0, i);
      rhs1 = mapping_old_index_to_k_witness_circuit(old_rhs1, i);

      aiger_add_and(k_witness_model, and_index, rhs0, rhs1);
      // L4<<"and "<<and_index<<" "<<rhs0<<" "<<rhs1<<"\n";
      current_index = and_index;
    }
  }

  L4 << "added k copies of original circuits";
  current_index += 2;
  // unsigned current_index = uniqueness_begin;
  /* adding simple path constraints */
  /*
    unsigned lat_begin = model_latches->lit;
    std::vector<unsigned>* eqs = new std::vector<unsigned>;
    for(unsigned i=0; i<k-1; i++){
      for(unsigned j=i+1; j<k; j++){
        std::vector<unsigned>* ls = new std::vector<unsigned>;
        for(unsigned x=0; x<num_latches; x++){
          unsigned l_i = mapping_old_index_to_k_witness_circuit(lat_begin + x*2,
    i); unsigned l_j = mapping_old_index_to_k_witness_circuit(lat_begin + x*2,
    j); aiger_add_and(k_witness_model, current_index, l_i, l_j + 1);
          current_index += 2;
          aiger_add_and(k_witness_model, current_index, l_i + 1, l_j);
          current_index += 2;
          aiger_add_and(k_witness_model, current_index, current_index-3,
    current_index-1); ls->push_back(current_index); current_index += 2;
        }
        unsigned prev1 = ls->at(0);
        for(unsigned x=1; x<num_latches; x++){
          aiger_add_and(k_witness_model, current_index, prev1, ls->at(x));
          prev1 = current_index;
          current_index += 2;
        }
        eqs->push_back(current_index-1);
      }
    }
    assert(eqs->size()>0);
    unsigned x = eqs->at(0);
    L4<<"eqs size: "<<eqs->size()<<"\n";
    for(unsigned i=1; i<eqs->size(); i++){
      aiger_add_and(k_witness_model, current_index, x, eqs->at(i));
      x = current_index;
      current_index += 2;
    }
  */
  // L4<<"uniqueness: "<<uniqueness<<"\n";

  // assert(uniqueness == current_index - 2);

  /* next function for Bs with uniqueness */
  /*
  aiger_add_and(k_witness_model, current_index, B_k_1_begin, uniqueness);
  current_index += 2;
  for(unsigned i=1; i<k; i++){
    unsigned index = B_k_1_begin + i*2;
    unsigned next = B_k_1_begin + (i-1)*2;
    aiger_add_and(k_witness_model, current_index, next, uniqueness);
    current_index += 2;
  }
  */

  /* construct R(L^0)...R(L^{k-1}) */
  L4 << "constructing R0... Rk-1";
  for (unsigned i = 0; i < k; i++) {
    std::vector<unsigned> latch_reset_equivs;

    unsigned complement = k - 1 - i;
    /* L_i_begin: latch literal */
    unsigned L_i_begin = L_k_1_begin + (complement)*num_latches * 2;
    for (unsigned j = 0; j < num_latches; j++) {
      unsigned latch_literal = L_i_begin + j * 2;
      aiger_symbol *current_latch = model_latches + j;
      unsigned reset = current_latch->reset;

      aiger_add_and(k_witness_model, current_index, latch_literal,
                    neg(reset)); /* l ^ 1 */
      current_index += 2;
      aiger_add_and(k_witness_model, current_index, latch_literal + 1,
                    reset); /* -l ^ 0 */
      current_index += 2;
      aiger_add_and(k_witness_model, current_index, current_index - 3,
                    current_index - 1);
      latch_reset_equivs.push_back(current_index);
      current_index += 2;
    }
    unsigned prev = latch_reset_equivs.at(0);
    if (num_latches == 1) {
      Rs.push_back(prev);
    } else {

      for (unsigned i = 1; i < latch_reset_equivs.size(); i++) {
        unsigned x = latch_reset_equivs.at(i);
        aiger_add_and(k_witness_model, current_index, prev, x);
        if (i == latch_reset_equivs.size() - 1) { Rs.push_back(current_index); }
        prev = current_index;
        current_index += 2;
      }
    }
  }

  L4 << "added R(L^i)s";

  /* Resetting the model_latches */
  /* Resetting R_0 */
  /* R_or_1_to_k_1: V_{1,k-1} R^i, condition for Rp_0 (R'_0)*/
  std::vector<int> R_or_i_to_k_1s;
  assert(Rs.size() > 1);
  unsigned prev = Rs.at(1);
  /* the model is 2-inductive */
  if (Rs.size() == 2) {
    R_or_1_to_k_1 = prev;
    current_index = prev + 2;
  } else {
    /* R_or_k_1_to_1 */
    prev = Rs.at(Rs.size() - 1) + 1;
    for (unsigned i = k - 2; i != 0; i--) {
      unsigned R_i = Rs.at(i);
      aiger_add_and(k_witness_model, current_index, prev, R_i + 1);
      prev = current_index;
      current_index += 2;
      R_or_i_to_k_1s.insert(R_or_i_to_k_1s.begin(), current_index - 1);
    }

    R_or_1_to_k_1 = current_index - 1;
  }

  L4 << "R'0 reset";

  /* R'(L^0): (R_or_1_to_k_1 ^ l) V (R_or_1_to_k_1 ^ r_l) */
  for (unsigned i = 0; i < num_latches; i++) {
    aiger_symbol *current_latch = model_latches + i;
    unsigned latch_index = L_0_begin + i * 2;
    aiger_add_and(k_witness_model, current_index, R_or_1_to_k_1, latch_index);
    current_index += 2;
    aiger_add_and(k_witness_model, current_index, neg(R_or_1_to_k_1),
                  current_latch->reset);
    current_index += 2;
    aiger_add_and(k_witness_model, current_index, current_index - 3,
                  current_index - 1);
    // aiger_add_reset(k_witness_model, latch_index, current_index+1);
    aiger_add_reset(k_witness_model, latch_index, latch_index);
    current_index += 2;
  }

  /* constructing f(L^1) ... f(L^{k-1}) */
  /* and resettinng R'(L^1)...R'(L^{k-2}), only for k>2 */
  for (unsigned i = 1; i < k - 1; i++) {
    unsigned complement = (k - 1) - i;

    unsigned R_or_i_to_k_1;
    if (i != 1) {
      unsigned prev = Rs.at(i) + 1;
      for (unsigned j = i + 1; j < k; j++) {
        unsigned current_R = Rs.at(j);
        aiger_add_and(k_witness_model, current_index, prev, current_R + 1);
        prev = current_index;
        current_index += 2;
      }
      // R_or_i_to_k_1 = current_index - 1;}
      R_or_i_to_k_1 = R_or_i_to_k_1s.at(i - 1);
    } else {
      R_or_i_to_k_1 = R_or_1_to_k_1;
    }

    /* R'(L^{i-1}) */
    std::vector<unsigned> rp;
    for (unsigned j = 0; j < num_latches; j++) {
      aiger_symbol *current_latch = model_latches + j;
      unsigned original_lit = current_latch->lit;
      unsigned L_i_1_lit =
          mapping_old_index_to_k_witness_circuit(original_lit, i - 1);

      aiger_symbol *L_i_1_latch = aiger_is_latch(k_witness_model, L_i_1_lit);

      // assert(L_i_1_latch->lit == L_i_1_lit);
      unsigned L_i_1_reset = L_i_1_latch->reset;
      aiger_add_and(k_witness_model, current_index, L_i_1_lit,
                    neg(L_i_1_reset));
      current_index += 2;
      aiger_add_and(k_witness_model, current_index, L_i_1_lit + 1, L_i_1_reset);
      current_index += 2;
      aiger_add_and(k_witness_model, current_index, current_index - 3,
                    current_index - 1);
      rp.push_back(current_index);
      current_index += 2;
    }

    prev = rp.at(0);
    for (unsigned j = 1; j < num_latches; j++) {
      aiger_add_and(k_witness_model, current_index, prev, rp.at(j));
      prev = current_index;
      current_index += 2;
    }

    unsigned rp_i_1 = current_index - 2;

    for (unsigned j = 0; j < num_latches; j++) {

      aiger_symbol *current_latch = model_latches + j;
      unsigned original_next = current_latch->next;
      unsigned new_next = mapping_old_index_to_k_witness_circuit(
          original_next, i - 1); /* f^{i-1} */

      aiger_add_and(k_witness_model, current_index, new_next, new_next);
      current_index += 2;

      unsigned L_i_latch_lit =
          mapping_old_index_to_k_witness_circuit(current_latch->lit, i);

      aiger_add_and(k_witness_model, current_index, R_or_i_to_k_1 - 1,
                    current_index - 2); /* or_R ^ f */
      current_index += 2;
      aiger_add_and(k_witness_model, current_index, R_or_i_to_k_1,
                    L_i_latch_lit); /* or_R ^ l */
      current_index += 2;
      aiger_add_and(k_witness_model, current_index, current_index - 3,
                    current_index - 1); /* or_R ^ l */
      // aiger_add_reset(k_witness_model, L_i_latch_lit, current_index+1);
      aiger_add_reset(k_witness_model, L_i_latch_lit, L_i_latch_lit);

      current_index += 2;
    }
  }

  /* Reset R^{k-1} */
  unsigned R_k_1 = Rs.at(k - 1);
  std::vector<unsigned> rp;
  for (unsigned j = 0; j < num_latches; j++) {
    aiger_symbol *current_latch = model_latches + j;
    unsigned original_lit = current_latch->lit;
    unsigned L_i_1_lit =
        mapping_old_index_to_k_witness_circuit(original_lit, k - 2);
    aiger_symbol *L_i_1_latch = aiger_is_latch(k_witness_model, L_i_1_lit);
    // assert(L_i_1_latch->lit == L_i_1_lit);
    unsigned L_i_1_reset =
        mapping_old_index_to_k_witness_circuit(L_i_1_latch->reset, k - 1);
    aiger_add_and(k_witness_model, current_index, L_i_1_lit, neg(L_i_1_reset));
    current_index += 2;
    aiger_add_and(k_witness_model, current_index, L_i_1_lit + 1, L_i_1_reset);
    current_index += 2;
    aiger_add_and(k_witness_model, current_index, current_index - 3,
                  current_index - 1);
    rp.push_back(current_index);
    current_index += 2;
  }

  prev = rp.at(0);
  for (unsigned j = 1; j < num_latches; j++) {
    aiger_add_and(k_witness_model, current_index, prev, rp.at(j));
    prev = current_index;
    current_index += 2;
  }

  unsigned rp_i_1 = current_index - 2;
  for (unsigned i = 0; i < num_latches; i++) {
    aiger_symbol *current_latch = model_latches + i;
    unsigned original_next = current_latch->next;
    unsigned new_next = mapping_old_index_to_k_witness_circuit(
        original_next, k - 2); /* f^{i-1} */
    aiger_add_and(k_witness_model, current_index, new_next, new_next);
    unsigned f = current_index;
    current_index += 2;

    unsigned l =
        mapping_old_index_to_k_witness_circuit(current_latch->lit, k - 1);
    aiger_add_and(k_witness_model, current_index, R_k_1, l);
    current_index += 2;
    aiger_add_and(k_witness_model, current_index, neg(R_k_1), f);
    current_index += 2;
    aiger_add_and(k_witness_model, current_index, current_index - 3,
                  current_index - 1);

    // aiger_add_reset(k_witness_model, l, current_index+1);
    unsigned reset =
        mapping_old_index_to_k_witness_circuit(current_latch->reset, k - 1);
    aiger_add_reset(k_witness_model, l, reset);
    current_index += 2;
  }

  /* Reset B */
  /* Reset b^{k-1} */
  aiger_add_reset(k_witness_model, B_k_1_begin, 1);
  /* Reset b^i for i in 1...k-2 */
  L4 << "initialising Bi";
  for (unsigned i = 1; i < k - 1; i++) {
    unsigned complement = (k - 1) - i;
    unsigned b_i = B_k_1_begin + complement * 2;
    unsigned b_i_1 = B_k_1_begin + (complement + 1) * 2;
    unsigned R_i = Rs.at(i);

    unsigned prev1 = neg(Rs.at(i + 1));

    // for(unsigned j=i+2; j<k; j++){
    //   aiger_add_and(k_witness_model, current_index, prev1, neg(Rs.at(j)));
    //   prev1 = current_index;
    //   current_index += 2;
    // }
    // unsigned and_not_R_j = i==k-2? prev1: current_index-2;
    unsigned and_not_R_j = i == k - 2 ? prev1 : neg(R_or_i_to_k_1s.at(i));

    aiger_add_and(k_witness_model, current_index, R_i, and_not_R_j);

    current_index += 2;
    aiger_add_and(k_witness_model, current_index, b_i_1 + 1, current_index - 1);
    // aiger_add_reset(k_witness_model, b_i, current_index+1);
    aiger_add_reset(k_witness_model, b_i, 0);
    current_index += 2;
  }
  /* Reset b^0 */
  // prev = Rs.at(1);
  // prev = neg(prev);
  // for(unsigned i=2; i<k; i++){
  //   unsigned R_i = Rs.at(i);
  //   aiger_add_and(k_witness_model, current_index, prev, neg(R_i));
  //   prev = current_index;
  //   current_index += 2;
  // }
  // unsigned or_R = current_index-1;
  unsigned or_R = R_or_1_to_k_1;
  if (k == 2) { or_R = Rs.at(1); }
  aiger_add_and(k_witness_model, current_index, or_R, 0);
  current_index += 2;
  aiger_add_and(k_witness_model, current_index, neg(or_R), 1);
  current_index += 2;
  aiger_add_and(k_witness_model, current_index, current_index - 1,
                current_index - 3);
  current_index += 2;
  // aiger_add_reset(k_witness_model, B_k_1_begin + (k-1)*2, neg(or_R));
  aiger_add_reset(k_witness_model, B_k_1_begin + (k - 1) * 2, 0);
  /* Construct the property */
  /* p3  */
  /* p3: d^i->R(L^{i+1}) for i in 0..k-2 */
  /*
  std::vector<unsigned>* sis = new std::vector<unsigned>();
  std::vector<unsigned>* vis = new std::vector<unsigned>();

  for(unsigned i=0; i<k-1; i++){
      unsigned complement = (k-1)-i;
      unsigned b_j = B_k_1_begin + complement * 2;
      if(i==0){
        sis->push_back(b_j+1);
      }else{
        aiger_add_and(k_witness_model, current_index, b_j+1, sis->at(i-1));
        sis->push_back(current_index);
        current_index+=2;
      }

  }

  for(unsigned i=0; i<k-1; i++){
      unsigned b_j = B_k_1_begin + i * 2;
      if(i==0){
        vis->push_back(b_j);
      }else{
        aiger_add_and(k_witness_model, current_index, b_j, vis->at(i-1));
        vis->push_back(current_index);
        current_index+=2;
      }

  }
  */

  std::vector<unsigned> p3s;
  for (unsigned i = 0; i < k - 1; i++) {
    unsigned prev;
    // prev = sis->at(i);
    unsigned complement = (k - 1) - (i + 1);
    // aiger_add_and(k_witness_model, current_index, prev, vis->at(complement));
    unsigned bi = B_k_1_begin + complement * 2;
    unsigned bi1 = bi + 2;
    aiger_add_and(k_witness_model, current_index, bi, neg(bi1));
    current_index += 2;
    /*
    for(unsigned j=i+1; j<k; j++){
      unsigned complement = (k-1)-j;
      unsigned b_j = B_k_1_begin + complement * 2;
      aiger_add_and(k_witness_model, current_index, prev, b_j);
      prev = current_index;
      current_index += 2;
    }
    */

    unsigned R_i_p_1 = Rs.at(i + 1);
    unsigned R_i_p_1_ = R_i_p_1 % 2 ? R_i_p_1 - 1 : R_i_p_1 + 1;
    aiger_add_and(k_witness_model, current_index, current_index - 2, R_i_p_1_);
    p3s.push_back(current_index + 1);
    current_index += 2;
  }

  /* p0:  b^i -> b^{i+1} for i in 0...k-2 */
  std::vector<unsigned> b_i_i_1;
  if (k == 2) {
    unsigned b_0 = B_k_1_begin + 2;
    aiger_add_and(k_witness_model, current_index, b_0, neg(B_k_1_begin));
    current_index += 2;
    ps.push_back(current_index - 1);
  } else {
    for (unsigned i = 0; i < k - 1; i++) {
      unsigned complement = (k - 1) - i;
      unsigned b_i = B_k_1_begin + complement * 2;
      /* b^i ^ !b^{i+1} */
      unsigned bis = b_i - 1;
      /*
      if(i+1<k-1){
        unsigned prev;
        for(unsigned j=i+1; j<k;j++){
          unsigned c = (k-1)-j;
          unsigned b_j = B_k_1_begin + c * 2;

          if(j==i+1){
            prev = b_j;
          }else{
            aiger_add_and(k_witness_model, current_index, prev, b_j);
            prev = current_index;
            current_index += 2;
          }

        }
        bis = current_index - 1;
      }
      */
      // or simply, bis=b_i-1;
      aiger_add_and(k_witness_model, current_index, b_i, bis);
      b_i_i_1.push_back(current_index + 1);
      current_index += 2;
    }
    /* constructing p0 */
    assert(b_i_i_1.size() == k - 1);
    prev = b_i_i_1.at(0);
    for (unsigned i = 1; i < k - 1; i++) {
      unsigned index = b_i_i_1.at(i);
      aiger_add_and(k_witness_model, current_index, prev, index);
      prev = current_index;
      current_index += 2;
    }
    ps.push_back(current_index - 2);
  }

  /* p1: b^i -> h^i, for i in 1..k-1 */
  std::vector<unsigned> p1s;
  for (unsigned i = 0; i < k - 1; i++) {
    unsigned complement = (k - 1) - i;
    unsigned b_i = B_k_1_begin + complement * 2;

    std::vector<unsigned> h_ls;
    for (unsigned j = 0; j < num_latches; j++) {
      aiger_symbol *original_latch = model_latches + j;
      unsigned l =
          mapping_old_index_to_k_witness_circuit(original_latch->lit, i + 1);

      unsigned original_next = original_latch->next;
      unsigned new_next_i =
          mapping_old_index_to_k_witness_circuit(original_next, i);
      /* l<->new_next_i */
      /* l^!new_next_i */
      aiger_add_and(k_witness_model, current_index, l, neg(new_next_i));
      current_index += 2;
      /* !l ^ new_next_i */
      aiger_add_and(k_witness_model, current_index, new_next_i, l + 1);
      current_index += 2;
      aiger_add_and(k_witness_model, current_index, current_index - 3,
                    current_index - 1);
      h_ls.push_back(current_index);
      current_index += 2;
    }
    prev = h_ls.at(0);
    // TODO this can't be right
    for (unsigned i = 1; i < num_latches; i++) {
      unsigned index = h_ls.at(i);
      aiger_add_and(k_witness_model, current_index, prev, index);
      prev = current_index;
      current_index += 2;
    }
    /* b^i ^ !h^i*/
    aiger_add_and(k_witness_model, current_index, b_i, current_index - 1);
    p1s.push_back(current_index + 1);
    current_index += 2;
  }

  /* p1 */
  prev = p1s.at(0);
  if (p1s.size() == 1) {
    ps.push_back(prev);
  } else {
    for (unsigned i = 1; i < k - 1; i++) {
      unsigned index = p1s.at(i);
      aiger_add_and(k_witness_model, current_index, prev, index);
      prev = current_index;
      current_index += 2;
    }
    ps.push_back(current_index - 2);
  }

  L4 << "p1 set";
  /* p2: b^i -> p^i */
  std::vector<unsigned> p2s;
  for (unsigned i = 0; i < k; i++) {
    unsigned complement = (k - 1) - i;
    unsigned b_i = B_k_1_begin + complement * 2;
    unsigned p_i = mapping_old_index_to_k_witness_circuit(original_p, i);
    aiger_add_and(k_witness_model, current_index, b_i, neg(p_i));
    p2s.push_back(current_index + 1);
    current_index += 2;
  }
  /* p2 */
  prev = p2s.at(0);
  for (unsigned i = 1; i < k; i++) {
    unsigned index = p2s.at(i);
    aiger_add_and(k_witness_model, current_index, prev, index);
    prev = current_index;
    current_index += 2;
  }
  ps.push_back(current_index - 2);
  L4 << "p2 set";

  /* p3 */
  prev = p3s.at(0);
  if (p3s.size() == 1) {
    ps.push_back(prev);
  } else {
    for (unsigned i = 1; i < k - 1; i++) {
      unsigned index = p3s.at(i);
      aiger_add_and(k_witness_model, current_index, prev, index);
      prev = current_index;
      current_index += 2;
    }
    ps.push_back(current_index - 2);
  }

  /* p' = p1...p3 */
  prev = ps.at(0);
  for (unsigned i = 1; i < ps.size(); i++) {
    unsigned index = ps.at(i);
    aiger_add_and(k_witness_model, current_index, prev, index);
    prev = current_index;
    current_index += 2;
  }

  /* p4 */
  aiger_add_and(k_witness_model, current_index, prev, B_k_1_begin);

  unsigned new_property = current_index;
  w_output = current_index + 1;
  current_index += 2;

  for (int i = 0; i < model->num_constraints; ++i) {
    aiger_add_constraint(k_witness_model, (model->constraints + i)->lit, "");
  }

  aiger_add_output(k_witness_model, w_output, "");
  aiger_reencode(k_witness_model);
}

void unique_witness(int kin, aiger *&witness) {
  k = kin;
  if (k < 2) {
    witness = model;
    return;
  }
  witness = aiger_init();
  std::vector<std::vector<unsigned>> m(
      k, std::vector<unsigned>(size(model), INVALID_LIT));
  auto map = [&m, &witness](int j, unsigned from, unsigned to,
                            bool sim = false) {
    assert(m.at(j).at(from) == INVALID_LIT);
    assert(from != INVALID_LIT && to != INVALID_LIT);
    m.at(j).at(from) = to;
    m.at(j).at(aiger_not(from)) = aiger_not(to);
    if (sim) simulates(witness, from, to);
  };
  // False[0..k-1]
  for (int j = 0; j < k; ++j)
    map(j, aiger_false, aiger_false);
  // IO
  for (unsigned l : inputs(model) | lits)
    map(0, l, input(witness), true);
  // I[1..k-1]
  for (int j = 1; j < k; ++j)
    for (unsigned l : inputs(model) | lits)
      map(j, l, oracle(witness));
  // L0
  for (unsigned l : latches(model) | lits) {
    map(0, l, latch(witness), true);
  }
  // A[0..k-1], L[1..k-1]
  for (int j = 0; j < k; ++j) {
    if (j)
      for (auto [l, n] : latches(model) | nexts)
        map(j, l, m.at(j - 1).at(n));
    for (auto [a, x, y] : ands(model)) {
      L5 << "step" << j << "and" << a << "=" << x << "&" << y;
      assert(m.at(j).at(x) != INVALID_LIT && m.at(j).at(y) != INVALID_LIT);
      map(j, a, conj(witness, m.at(j).at(x), m.at(j).at(y)));
    }
  }
  // L0 reset and next
  for (auto l : latches(model)) {
    aiger_symbol *w = aiger_is_latch(witness, m.at(0).at(l.lit));
    assert(w && m.at(0).at(l.reset) != INVALID_LIT &&
           m.at(0).at(l.next) != INVALID_LIT);
    w->reset = m.at(0).at(l.reset);
    w->next = m.at(0).at(l.next);
  }
  std::vector<unsigned> properties;
  properties.reserve(k);
  const unsigned p{aiger_not(output(model))};
  for (int j = 0; j < k; ++j)
    properties.push_back(m.at(j).at(p));
  aiger_add_output(witness, aiger_not(conj(witness, properties)), nullptr);
}

bool kind(aiger *aig, aiger *&k_witness_model,
          std::vector<std::vector<unsigned>> &cex, unsigned simple_path,
          bool always_unique) {
  auto [bug, k] = mcaiger(aig, simple_path);
  L0 << "k: " << k << '\n';
  model = aig;
  if (bug)
    stimulus(k, cex);
  else if (simple_path || always_unique)
    unique_witness(k, k_witness_model);
  else
    witness(k, k_witness_model);
  mcaiger_free();

  return bug;
}
