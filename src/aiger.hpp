#pragma once

#include <cassert>
#include <iostream>
#include <numeric>
#include <ranges>
#include <span>
#include <vector>

// Wrapper around the aiger library.
extern "C" {
#include "aiger.h"
}

static constexpr unsigned INVALID_LIT = std::numeric_limits<unsigned>::max();

#define INV std::numeric_limits<unsigned>::max()
#define NOT(L) ((L) ^ 1u)
#define ABS(L) ((L) & ~1u)
#define SAT(L) ((int)((((L) >> 1) + 1) * ((L) & 1u ? -1 : 1)))
#define IDX(L) ((L) >> 1)
#define VAR(L) ((L) << 1)
#define SGN(L) ((L) & 1u)
#define STV(L) ((~(L)) & 1u)

bool is_input(aiger *aig, unsigned l);
bool is_latch(aiger *aig, unsigned l);
unsigned simulates_lit(aiger *aig, unsigned l);
const aiger_symbol *simulates_input(aiger *model, aiger *witness, unsigned l);
const aiger_symbol *simulates_latch(aiger *model, aiger *witness, unsigned l);
unsigned reset(aiger *aig, unsigned l);
unsigned next(aiger *aig, unsigned l);
unsigned output(const aiger *aig);

unsigned size(const aiger *aig);

unsigned input(aiger *aig);

unsigned conj(aiger *aig, unsigned x, unsigned y);
unsigned conj(aiger *aig, std::vector<unsigned> &v);
unsigned disj(aiger *aig, unsigned x, unsigned y);
unsigned disj(aiger *aig, std::vector<unsigned> &v);
unsigned impl(aiger *aig, unsigned x, unsigned y);
unsigned eq(aiger *aig, unsigned x, unsigned y);
unsigned ite(aiger *aig, unsigned c, unsigned t, unsigned e);

aiger_symbol *getLatch(aiger *medel, unsigned lit);

std::span<aiger_symbol> inputs(const aiger *aig);
std::span<aiger_symbol> latches(const aiger *aig);
std::span<aiger_and> ands(const aiger *aig);

static constexpr auto lits =
    std::views::transform([](const auto &l) { return l.lit; });
static constexpr auto nexts = std::views::transform([](const auto &l) {
  return std::pair{l.lit, l.next};
});
static constexpr auto resets = std::views::transform([](const auto &l) {
  return std::pair{l.lit, l.reset};
});
static constexpr auto initialized =
    std::views::filter([](const auto &l) { return l.reset != l.lit; });
static constexpr auto uninitialized =
    std::views::filter([](const auto &l) { return l.reset == l.lit; });

bool inputs_latches_reencoded(aiger *aig);

// Read only circuit for model and witness.
struct InAIG {
  aiger *aig;
  InAIG(const char *path) : aig(aiger_init()) {
    const char *err = aiger_open_and_read_from_file(aig, path);
    if (err) {
      std::cerr << "certifaiger: parse error reading " << path << ": " << err
                << "\n";
      exit(1);
    }
    if (!inputs_latches_reencoded(aig)) {
      std::cerr << "certifaiger: inputs and latches have to be reencoded even "
                   "in ASCII format: "
                << path << "\n";
      exit(2);
    }
    if (aig->num_constraints + aig->num_justice + aig->num_fairness)
      std::cerr << "certifaiger: WARNING constraints, justice and fairness are "
                   "not supported: "
                << path << "\n";
    if (aig->num_bad + aig->num_outputs > 1)
      std::cerr << "certifaiger: WARNING Multiple properties. Using "
                << (aig->num_bad ? "bad" : "output") << "0: " << path << "\n";
  }
  ~InAIG() { aiger_reset(aig); }
  aiger *operator*() const { return aig; }
};

// Wrapper for combinatorial circuits meant to be checked for validity via SAT.
struct OutAIG {
  aiger *aig; // combinatorial
  const char *path;
  OutAIG(const char *path) : aig(aiger_init()), path(path) {}
  ~OutAIG() {
    assert(!aig->num_latches);
    aiger_open_and_write_to_file(aig, path);
    aiger_reset(aig);
  }
  aiger *operator*() const { return aig; }
};
