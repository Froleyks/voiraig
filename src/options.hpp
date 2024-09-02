#pragma once

#include <limits.h>
#include <stdbool.h>
#include <stdio.h>

#define INF INT_MAX

// clang-format off
// options have to be sorted!
// <let ((beg (progn (next-line 3) (bol))) (end (progn (forward-paragraph) (point)))) (shell-command-on-region beg end "sort -k 2" t t) (align-regexp beg end "\\(,\\s-*\\) " 1 1 t)>
//                     Name   Def Min Max Description
#define OPTIONS \
  OPTION(bool,     certificate, 1, 0, 1, "produce witness circuit") \
  OPTION(bool,     kind,        0, 0, 1, "use k-Induction") \
  LOGOPT(bool,     location,    1, 0, 1, "use location for logging") \
  OPTION(unsigned, paths,       2, 0, 2, "type of simple path constrains") \
  OPTION(bool,     trace,       1, 0, 1, "produce cex trace") \
  OPTION(bool,     unique,      0, 0, 1, "always use unique kind witness construction") \
  LOGOPT(unsigned, verbosity,   2, 0, 5, "verbosity level")

// clang-format on

#if defined(LOG)
#define LOGOPT OPTION
#else
#define LOGOPT(...) /**/
#endif

#ifndef NDEBUG
#define DBGOPT OPTION
#else
#define DBGOPT(...) /**/
#endif

struct options {
  unsigned seconds;
  unsigned optimize;
  bool summarize;

#define OPTION(TYPE, NAME, DEFAULT, MIN, MAX, DESCRIPTION) TYPE NAME;
  OPTIONS
#undef OPTION
  const char *model;
  const char *witness;
};

/*------------------------------------------------------------------------*/

void parse_options(int argc, char **argv, struct options *);
const char *match_and_find_option_argument(const char *, const char *);
bool parse_option_with_value(struct options *, const char *);
void report_non_default_options(struct options *);
