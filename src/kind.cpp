#include "aiger.hpp"

#include "cadical.hpp"
#include "utils.hpp"

#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

CaDiCaL::Solver *s;
aiger *model;

// mcaiger
static int ionly, bonly;
static int acs, mix;
static int ncs, dcs, rcs;
static unsigned *frames, sframes, nframes;
static unsigned nrcs;

#define picosat_ado_conflicts(...) (0u)
#define picosat_disable_ado(...) \
  do {                           \
  } while (0)
#define picosat_enable_ado(...) \
  do {                          \
  } while (0)
#define picosat_add_ado_lit(...) \
  do {                           \
  } while (0)
#define picosat_set_ado_conflict_limit(...) \
  do {                                      \
  } while (0)

static int frame(int k) {
  int res;
  res = k * model->maxvar + 2;
  if (dcs || rcs || mix) res += model->num_latches * k * (k - 1) / 2;
  return res;
}

static int lit(unsigned k, unsigned l) {
  int res;
  assert(0 <= l && l <= 2 * model->maxvar + 1);
  res = (l <= 1) ? 1 : frame(k) + (int)((l - 2) / 2);
  if (l & 1) res = -res;
  return res;
}

static int input(unsigned k, unsigned i) {
  assert(0 <= i && i < model->num_inputs);
  return lit(k, model->inputs[i].lit);
}

static int latch(unsigned k, unsigned i) {
  assert(0 <= i && i < model->num_latches);
  return lit(k, model->latches[i].lit);
}

static int next(unsigned k, unsigned i) {
  assert(0 <= i && i < model->num_latches);
  return lit(k, model->latches[i].next);
}

static int reset(unsigned i) {
  assert(0 <= i && i < model->num_latches);
  return model->latches[i].reset;
}

static int output(unsigned k, unsigned i) {
  // using output from aiger.hpp can deal with a bit more
  assert(i == 0);
  // assert(0 <= i && i < model->num_outputs);
  return lit(k, output(model));
}

static int constraint(unsigned k, unsigned i) {
  assert(0 <= i && i < model->num_constraints);
  return lit(k, model->constraints[i].lit);
}

static void unary(int a) {
  assert(a);
  s->add(a);
  s->add(0);
}

static void binary(int a, int b) {
  assert(a);
  s->add(a);
  assert(b);
  s->add(b);
  s->add(0);
}

static void ternary(int a, int b, int c) {
  assert(a);
  s->add(a);
  assert(b);
  s->add(b);
  assert(c);
  s->add(c);
  s->add(0);
}

static void gate(int lhs, int rhs0, int rhs1) {
  binary(-lhs, rhs0);
  binary(-lhs, rhs1);
  ternary(lhs, -rhs0, -rhs1);
}

static void eq(int lhs, int rhs) {
  binary(-lhs, rhs);
  binary(lhs, -rhs);
}

static void connect(unsigned k) {
  unsigned i;

  if (!k) return;

  for (i = 0; i < model->num_latches; i++)
    eq(next(k - 1, i), latch(k, i));

  L3 << k << "connect";
}

static void encode(unsigned k) {
  aiger_and *a;
  aiger_symbol *c;
  unsigned i;

  if (!k) unary(lit(k, 1)); /* true */

  for (i = 0; i < model->num_ands; i++) {
    a = model->ands + i;
    gate(lit(k, a->lhs), lit(k, a->rhs0), lit(k, a->rhs1));
  }

  for (i = 0; i < model->num_constraints; i++) {
    unary(constraint(k, i));
  }

  if (k) {
    // TODO What is this? All-different-from-frame-0-constraint assuming zero
    // reset?
    if (0 && model->num_latches > 0) {
      for (i = 0; i < model->num_latches; i++)
        s->add(latch(k, i));

      s->add(0);
    }
    unary(-output(k - 1, 0));
  }
  L3 << k << "encode";
}

static void ado(unsigned k) {
  unsigned i;
  if (model->num_latches > 0) {
    for (i = 0; i < model->num_latches; i++)
      picosat_add_ado_lit(ps, latch(k, i));
    picosat_add_ado_lit(ps, 0);
  }
  L3 << k << "ado";
}

static int diff(int k, int l, int i) {
  assert(0 <= i && i < model->num_latches);
  assert(l < k);
  return frame(k + 1) - i - l * model->num_latches - 1;
}

static void diffs(unsigned k, unsigned l) {
  unsigned i, tmp;
  assert(k != l);
  if (l > k) {
    tmp = k;
    k = l;
    l = tmp;
  }
  for (i = 0; i < model->num_latches; i++) {
    ternary(latch(l, i), latch(k, i), -diff(k, l, i));
    ternary(-latch(l, i), -latch(k, i), -diff(k, l, i));
  }
  if (model->num_latches > 0) {
    for (i = 0; i < model->num_latches; i++)
      s->add(diff(k, l, i));
    s->add(0);
  }
  L3 << "diffs" << l << k;
}

static void diffsk(unsigned k) {
  unsigned l;
  if (!k) return;
  for (l = 0; l < k; l++)
    diffs(k, l);
  L3 << k << "diffsk";
}

static void simple(unsigned k) {
  if (dcs)
    diffsk(k);
  else if (acs)
    ado(k);
  else
    assert(rcs || ncs);
}

static void bad(unsigned k) {
  // using output from aiger.hpp can deal with a bit more
  // assert(model->num_outputs == 1);
  s->assume(output(k, 0));
  L3 << k << "bad";
}

static void init(unsigned k) {
  unsigned i;
  int l, r;

  if (bonly && k) return;

  for (i = 0; i < model->num_latches; i++) {
    r = reset(i);
    if (r > 1) continue; // uninitialized
    l = latch(0, i) * (r ? 1 : -1);
    if (bonly)
      unary(l);
    else
      s->assume(l);
  }

  L3 << k << "init";
}

static int cmp_frames(const void *p, const void *q) {
  unsigned k = *(unsigned *)p;
  unsigned l = *(unsigned *)q;
  int a, b, res;
  unsigned i;

  for (i = 0; i < model->num_latches; i++) {
    a = s->val(latch(k, i));
    b = s->val(latch(l, i));
    res = a - b;
    if (res) return res;
  }

  return 0;
}

static int sat(unsigned k) {
  unsigned i;
  int res;

  if (rcs || mix) {
    if (k == nframes) {
      assert(k == nframes);

      if (k >= sframes) {
        sframes = sframes ? 2 * sframes : 1;
        frames = (unsigned *)realloc(frames, sframes * sizeof frames[0]);
      }

      assert(nframes < sframes);
      frames[nframes++] = k;
    }

    assert(nframes == k + 1);
  }

RESTART:
  res = s->solve();

  if (res == 20) return res;

  if (res == 10 && !rcs) return res;

  if (!res) {
    assert(mix);
    assert(!rcs);
    assert(acs);
    rcs = 1;
    acs = 0;
    picosat_disable_ado(ps);
    goto RESTART;
  }

  assert(rcs);
  assert(res == 10);

  if (model->num_latches) {
    qsort(frames, k + 1, sizeof frames[0], cmp_frames);
    for (i = 0; i < k; i++)
      if (!cmp_frames(frames + i, frames + i + 1)) {
        diffs(frames[i], frames[i + 1]);
        nrcs++;
        bad(k);
        goto RESTART;
      }

    assert(i == k); /* all different */
  }

  return 10;
}

static int step(unsigned k) {
  int res;
  if (mix && acs)
    picosat_set_ado_conflict_limit(ps, picosat_ado_conflicts(ps) + 1000);
  bad(k);
  L2 << k << "step";
  res = (sat(k) == 20);

  return res;
}

static int base(unsigned k) {
  int res;
  if (acs) picosat_disable_ado(ps);
  init(k);
  bad(k);
  L2 << k << "base";
  res = (sat(k) == 10);
  if (acs) picosat_enable_ado(ps);
  return res;
}

std::pair<bool, int> mcaiger() {
  const char *name = 0, *err;
  unsigned k, maxk = UINT_MAX;
  int i, cs;
  double delta;
  bool bug{};
  for (k = 0; k <= maxk; k++) {
    if (mix && acs && picosat_ado_conflicts(ps) >= 10000) {
      acs = 0;
      rcs = 1;
      picosat_disable_ado(ps);
    }
    connect(k);
    encode(k);
    simple(k);
    if (step(k)) {
      L1 << k << "inductive";
      bug = false;
      break;
    }
    if (base(k)) {
      L1 << k << "reachable";
      bug = true;
      break;
    }
  }
  if (rcs || mix) { L2 << nrcs << "refinements of simple path constraints"; }
  return {bug, k};
}

static void stimulus(int k, std::vector<std::vector<unsigned>> &cex) {
  assert(s->status() == 10);
  assert(cex.empty());
  cex.reserve(k + 2);
  unsigned i, j;
  int lit, l, v;
  std::vector<unsigned> frame;
  for (i = 0; i < model->num_latches; i++) {
    lit = latch(0, i);
    v = s->val(lit) < 0;
    l = model->latches[i].lit;
    frame.push_back(l + v);
  }
  cex.push_back(frame);
  for (i = 0; i <= k; i++) {
    frame.clear();
    for (j = 0; j < model->num_inputs; j++) {
      lit = input(i, j);
      LV5(i, j, l, s->val(lit));
      v = s->val(lit) < 0;
      l = model->inputs[j].lit;
      frame.push_back(l + v);
    }
    cex.push_back(frame);
  }
  for (auto &c : cex)
    L5 << c;
}

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
  LV5(num_inputs, num_latches, num_ands, total_number_literals, k, index, i,
      complement);
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
}

static void witness(int kin, aiger *&k_witness_model) {
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
  std::vector<unsigned> *Rs = new std::vector<unsigned>();
  std::vector<unsigned> *hs =
      new std::vector<unsigned>(); /* size of k-1, from 1..k-1, F^i =
                                      F(L^{i-1})*/
  std::vector<unsigned> *ps = new std::vector<unsigned>(); /* the properties */

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
    std::vector<unsigned> *latch_reset_equivs = new std::vector<unsigned>();

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
      latch_reset_equivs->push_back(current_index);
      current_index += 2;
    }
    unsigned prev = latch_reset_equivs->at(0);
    if (num_latches == 1) {
      Rs->push_back(prev);
    } else {

      for (unsigned i = 1; i < latch_reset_equivs->size(); i++) {
        unsigned x = latch_reset_equivs->at(i);
        aiger_add_and(k_witness_model, current_index, prev, x);
        if (i == latch_reset_equivs->size() - 1) {
          Rs->push_back(current_index);
        }
        prev = current_index;
        current_index += 2;
      }
    }
  }

  L4 << "added R(L^i)s";

  /* Resetting the model_latches */
  /* Resetting R_0 */
  /* R_or_1_to_k_1: V_{1,k-1} R^i, condition for Rp_0 (R'_0)*/
  std::vector<int> *R_or_i_to_k_1s = new std::vector<int>;
  assert(Rs->size() > 1);
  unsigned prev = Rs->at(1);
  /* the model is 2-inductive */
  if (Rs->size() == 2) {
    R_or_1_to_k_1 = prev;
    current_index = prev + 2;
  } else {
    /* R_or_k_1_to_1 */
    prev = Rs->at(Rs->size() - 1) + 1;
    for (unsigned i = k - 2; i != 0; i--) {
      unsigned R_i = Rs->at(i);
      aiger_add_and(k_witness_model, current_index, prev, R_i + 1);
      prev = current_index;
      current_index += 2;
      R_or_i_to_k_1s->insert(R_or_i_to_k_1s->begin(), current_index - 1);
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
      unsigned prev = Rs->at(i) + 1;
      for (unsigned j = i + 1; j < k; j++) {
        unsigned current_R = Rs->at(j);
        aiger_add_and(k_witness_model, current_index, prev, current_R + 1);
        prev = current_index;
        current_index += 2;
      }
      // R_or_i_to_k_1 = current_index - 1;}
      R_or_i_to_k_1 = R_or_i_to_k_1s->at(i - 1);
    } else {
      R_or_i_to_k_1 = R_or_1_to_k_1;
    }

    /* R'(L^{i-1}) */
    std::vector<unsigned> *rp = new std::vector<unsigned>;
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
      rp->push_back(current_index);
      current_index += 2;
    }

    prev = rp->at(0);
    for (unsigned j = 1; j < num_latches; j++) {
      aiger_add_and(k_witness_model, current_index, prev, rp->at(j));
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
  unsigned R_k_1 = Rs->at(k - 1);
  std::vector<unsigned> *rp = new std::vector<unsigned>;
  for (unsigned j = 0; j < num_latches; j++) {
    aiger_symbol *current_latch = model_latches + j;
    unsigned original_lit = current_latch->lit;
    unsigned L_i_1_lit =
        mapping_old_index_to_k_witness_circuit(original_lit, k - 2);
    aiger_symbol *L_i_1_latch = aiger_is_latch(k_witness_model, L_i_1_lit);
    // assert(L_i_1_latch->lit == L_i_1_lit);
    unsigned L_i_1_reset = L_i_1_latch->reset;
    aiger_add_and(k_witness_model, current_index, L_i_1_lit, neg(L_i_1_reset));
    current_index += 2;
    aiger_add_and(k_witness_model, current_index, L_i_1_lit + 1, L_i_1_reset);
    current_index += 2;
    aiger_add_and(k_witness_model, current_index, current_index - 3,
                  current_index - 1);
    rp->push_back(current_index);
    current_index += 2;
  }

  prev = rp->at(0);
  for (unsigned j = 1; j < num_latches; j++) {
    aiger_add_and(k_witness_model, current_index, prev, rp->at(j));
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
    aiger_add_reset(k_witness_model, l, current_latch->reset);
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
    unsigned R_i = Rs->at(i);

    unsigned prev1 = neg(Rs->at(i + 1));

    // for(unsigned j=i+2; j<k; j++){
    //   aiger_add_and(k_witness_model, current_index, prev1, neg(Rs->at(j)));
    //   prev1 = current_index;
    //   current_index += 2;
    // }
    // unsigned and_not_R_j = i==k-2? prev1: current_index-2;
    unsigned and_not_R_j = i == k - 2 ? prev1 : neg(R_or_i_to_k_1s->at(i));

    aiger_add_and(k_witness_model, current_index, R_i, and_not_R_j);

    current_index += 2;
    aiger_add_and(k_witness_model, current_index, b_i_1 + 1, current_index - 1);
    // aiger_add_reset(k_witness_model, b_i, current_index+1);
    aiger_add_reset(k_witness_model, b_i, 0);
    current_index += 2;
  }
  /* Reset b^0 */
  // prev = Rs->at(1);
  // prev = neg(prev);
  // for(unsigned i=2; i<k; i++){
  //   unsigned R_i = Rs->at(i);
  //   aiger_add_and(k_witness_model, current_index, prev, neg(R_i));
  //   prev = current_index;
  //   current_index += 2;
  // }
  // unsigned or_R = current_index-1;
  unsigned or_R = R_or_1_to_k_1;
  if (k == 2) { or_R = Rs->at(1); }
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

  std::vector<unsigned> *p3s = new std::vector<unsigned>();
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

    unsigned R_i_p_1 = Rs->at(i + 1);
    unsigned R_i_p_1_ = R_i_p_1 % 2 ? R_i_p_1 - 1 : R_i_p_1 + 1;
    aiger_add_and(k_witness_model, current_index, current_index - 2, R_i_p_1_);
    p3s->push_back(current_index + 1);
    current_index += 2;
  }

  /* p0:  b^i -> b^{i+1} for i in 0...k-2 */
  std::vector<unsigned> *b_i_i_1 = new std::vector<unsigned>();
  if (k == 2) {
    unsigned b_0 = B_k_1_begin + 2;
    aiger_add_and(k_witness_model, current_index, b_0, neg(B_k_1_begin));
    current_index += 2;
    ps->push_back(current_index - 1);
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
      b_i_i_1->push_back(current_index + 1);
      current_index += 2;
    }
    /* constructing p0 */
    assert(b_i_i_1->size() == k - 1);
    prev = b_i_i_1->at(0);
    for (unsigned i = 1; i < k - 1; i++) {
      unsigned index = b_i_i_1->at(i);
      aiger_add_and(k_witness_model, current_index, prev, index);
      prev = current_index;
      current_index += 2;
    }
    ps->push_back(current_index - 2);
  }

  /* p1: b^i -> h^i, for i in 1..k-1 */
  std::vector<unsigned> *p1s = new std::vector<unsigned>();
  for (unsigned i = 0; i < k - 1; i++) {
    unsigned complement = (k - 1) - i;
    unsigned b_i = B_k_1_begin + complement * 2;

    std::vector<unsigned> *h_ls = new std::vector<unsigned>();
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
      h_ls->push_back(current_index);
      current_index += 2;
    }
    prev = h_ls->at(0);
    // TODO this can't be right
    for (unsigned i = 1; i < num_latches; i++) {
      unsigned index = h_ls->at(i);
      aiger_add_and(k_witness_model, current_index, prev, index);
      prev = current_index;
      current_index += 2;
    }
    /* b^i ^ !h^i*/
    aiger_add_and(k_witness_model, current_index, b_i, current_index - 1);
    p1s->push_back(current_index + 1);
    current_index += 2;
  }

  /* p1 */
  prev = p1s->at(0);
  if (p1s->size() == 1) {
    ps->push_back(prev);
  } else {
    for (unsigned i = 1; i < k - 1; i++) {
      unsigned index = p1s->at(i);
      aiger_add_and(k_witness_model, current_index, prev, index);
      prev = current_index;
      current_index += 2;
    }
    ps->push_back(current_index - 2);
  }

  L4 << "p1 set";
  /* p2: b^i -> p^i */
  std::vector<unsigned> *p2s = new std::vector<unsigned>();
  for (unsigned i = 0; i < k; i++) {
    unsigned complement = (k - 1) - i;
    unsigned b_i = B_k_1_begin + complement * 2;
    unsigned p_i = mapping_old_index_to_k_witness_circuit(original_p, i);
    aiger_add_and(k_witness_model, current_index, b_i, neg(p_i));
    p2s->push_back(current_index + 1);
    current_index += 2;
  }
  /* p2 */
  prev = p2s->at(0);
  for (unsigned i = 1; i < k; i++) {
    unsigned index = p2s->at(i);
    aiger_add_and(k_witness_model, current_index, prev, index);
    prev = current_index;
    current_index += 2;
  }
  ps->push_back(current_index - 2);
  L4 << "p2 set";

  /* p3 */
  prev = p3s->at(0);
  if (p3s->size() == 1) {
    ps->push_back(prev);
  } else {
    for (unsigned i = 1; i < k - 1; i++) {
      unsigned index = p3s->at(i);
      aiger_add_and(k_witness_model, current_index, prev, index);
      prev = current_index;
      current_index += 2;
    }
    ps->push_back(current_index - 2);
  }

  /* p' = p1...p3 */
  prev = ps->at(0);
  for (unsigned i = 1; i < ps->size(); i++) {
    unsigned index = ps->at(i);
    aiger_add_and(k_witness_model, current_index, prev, index);
    prev = current_index;
    current_index += 2;
  }

  /* p4 */
  aiger_add_and(k_witness_model, current_index, prev, B_k_1_begin);

  unsigned new_property = current_index;
  w_output = current_index + 1;

  for (int i = 0; i < model->num_constraints; ++i) {
    aiger_add_constraint(k_witness_model, (model->constraints + i)->lit, "");
  }

  // TODO add uniquness constraint
  for (int i = 1; i < k; ++i) {
    for (int j = 0; j < i; ++j) {
      for (int l = 0; l < model->num_latches; ++l) {}
    }
  }

  // add mapping
  static constexpr unsigned MAX_SIZE = 12;

  for (unsigned i = 0; i < num_latches; i++) {
    aiger_symbol *current_latch = model_latches + i;
    unsigned latch_index =
        mapping_old_index_to_k_witness_circuit(current_latch->lit, k - 1);
    aiger_symbol *latch = aiger_is_latch(k_witness_model, latch_index);
    assert(latch);
    latch->name = static_cast<char *>(malloc(MAX_SIZE));
    assert(latch->name);
    std::snprintf(latch->name, MAX_SIZE, "= %d", current_latch->lit);
  }

  aiger_add_output(k_witness_model, w_output, "");
  aiger_reencode(k_witness_model);
}

bool kind(aiger *aig, aiger *&k_witness_model,
          std::vector<std::vector<unsigned>> &cex, unsigned simple_path) {
  if (simple_path == 0)
    ncs = 1;
  else if (simple_path == 1)
    dcs = 1;
  else if (simple_path == 2)
    rcs = 1;
  else
    assert(false);
  s = new CaDiCaL::Solver();
  model = aig;

  auto [bug, k] = mcaiger();
  if (bug)
    stimulus(k, cex);
  else
    witness(k, k_witness_model);

  delete s;
  return bug;
}
