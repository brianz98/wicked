#include <algorithm>
#include <iomanip>
#include <iostream>

#include "fmt/format.h"

#include "combinatorics.h"
#include "diag_operator.h"
#include "diag_operator_expression.h"
#include "helpers.h"
#include "orbital_space.h"
#include "stl_utils.hpp"

#include "../algebra/expression.h"
#include "../algebra/sqoperator.h"
#include "../algebra/tensor.h"
#include "../algebra/term.h"

#include "wick_theorem.h"

#define PRINT(detail, code)                                                    \
  if (print_ >= detail) {                                                      \
    code                                                                       \
  }

void print_key(std::tuple<int, int, bool, int> key, int n);
void print_contraction(const std::vector<DiagOperator> &ops,
                       const std::vector<Tensor> &tensors,
                       const std::vector<std::vector<bool>> &bit_map_vec,
                       const std::vector<SQOperator> &sqops,
                       const std::vector<int> sign_order);

using namespace std;

WickTheorem::WickTheorem() {}

void WickTheorem::set_print(PrintLevel print) { print_ = print; }
void WickTheorem::set_max_cumulant(int n) { maxcumulant_ = n; }

Expression WickTheorem::contract(scalar_t factor,
                                 const std::vector<DiagOperator> &ops,
                                 const int minrank, const int maxrank) {

  ncontractions_ = 0;
  contractions_.clear();
  elementary_contractions_.clear();

  PRINT(
      PrintLevel::Summary, std::cout << "\nContracting the operators: ";
      for (auto &op
           : ops) { std::cout << " " << op; };
      std::cout << std::endl;)

  // Step 1. Generate elementary contractions
  elementary_contractions_ = generate_elementary_contractions(ops);

  // Step 2. Generate allowed composite contractions
  generate_composite_contractions(ops, minrank, maxrank);

  // Step 3. Process contractions
  Expression result = process_contractions(factor, ops, minrank, maxrank);
  return result;
}

Expression WickTheorem::contract(scalar_t factor, const DiagOpExpression &expr,
                                 const int minrank, const int maxrank) {
  Expression result;
  for (const auto &[ops, f] : expr.terms()) {
    result += contract(factor * f, ops, minrank, maxrank);
  }
  return result;
}

std::vector<std::vector<DiagVertex>>
WickTheorem::generate_elementary_contractions(
    const std::vector<DiagOperator> &ops) {
  PRINT(PrintLevel::Summary,
        std::cout << "\n- Step 1. Generating elementary contractions"
                  << std::endl;)

  int nops = ops.size();

  // a vector that holds all the contractions
  std::vector<std::vector<DiagVertex>> contr_vec;

  PRINT(
      PrintLevel::Summary, cout << "\n  Operator   Space   Cre.   Ann.";
      cout << "\n  ------------------------------";
      for (int op = 0; op < nops; ++op) {
        for (int s = 0; s < osi->num_spaces(); s++) {
          cout << "\n      " << op << "        " << osi->label(s) << "      "
               << ops[op].cre(s) << "      " << ops[op].ann(s);
        }
      };
      cout << "\n";)

  // loop over orbital spaces
  for (int s = 0; s < osi->num_spaces(); s++) {
    PRINT(PrintLevel::Summary, std::cout
                                   << "\n  Elementary contractions for space "
                                   << osi->label(s) << ": ";)

    // differentiate between various types of spaces
    SpaceType space_type = osi->space_type(s);

    // 1. Pairwise contractions creation-annihilation:
    // ┌───┐
    // a^+ a
    if (space_type == SpaceType::Occupied) {
      for (int c = 0; c < nops; c++) {       // loop over creation (left)
        for (int a = c + 1; a < nops; a++) { // loop over annihilation (right)
          if (ops[c].cre(s) * ops[a].ann(s) > 0) { // is contraction viable?
            std::vector<DiagVertex> new_contr(nops);
            new_contr[c].set_cre(s, 1);
            new_contr[a].set_ann(s, 1);
            contr_vec.push_back(new_contr);
            PRINT(PrintLevel::Summary,
                  cout << fmt::format("\n    {:5d}:", contr_vec.size());
                  PRINT_ELEMENTS(new_contr, " "););
          }
        }
      }
    }

    // 2. Pairwise contractions creation-annihilation:
    // ┌───┐
    // a   a^+
    if (space_type == SpaceType::Unoccupied) {
      for (int a = 0; a < nops; a++) {       // loop over annihilation (left)
        for (int c = a + 1; c < nops; c++) { // loop over creation (right)
          if (ops[c].cre(s) * ops[a].ann(s) > 0) { // is contraction viable?
            std::vector<DiagVertex> new_contr(nops);
            new_contr[c].set_cre(s, 1);
            new_contr[a].set_ann(s, 1);
            contr_vec.push_back(new_contr);
            PRINT(PrintLevel::Summary,
                  cout << fmt::format("\n    {:5d}:", contr_vec.size());
                  PRINT_ELEMENTS(new_contr, " "););
          }
        }
      }
    }

    // 3. 2k-legged contractions (k >= 1) of k creation + k annihilation
    // operators:
    // ┌───┬───┬───┐
    // a^+ a   a   a^+
    if (space_type == SpaceType::General) {
      // compute the largest possible cumulant for this space
      int sumcre = 0;
      int sumann = 0;
      for (int A = 0; A < nops; A++) {
        sumcre += ops[A].cre(s);
        sumann += ops[A].ann(s);
      }
      // the number of legs is limited by the smallest of number of cre/ann
      // operators and the maximum cumulant level allowed
      int max_half_legs = std::min(std::min(sumcre, sumann), maxcumulant_);
      // loop over all possible contractions from 2 to max_legs
      for (int half_legs = 1; half_legs <= max_half_legs; half_legs++) {
        PRINT(PrintLevel::Summary,
              cout << "\n    " << 2 * half_legs << "-legs contractions";)
        auto half_legs_part =
            integer_partitions(half_legs, nops); // Experimental
        // create lists of leg partitionings among all operators that are
        // compatible with the number of creation and annihilation operators
        std::vector<std::vector<int>> cre_legs_vec, ann_legs_vec;
        // we copy the partition and permute it (with added zeros)
        for (const auto part : half_legs_part) {
          // if (part.size() <= nops) { // Experimental
          std::vector<int> perm(nops, 0);
          std::copy(part.begin(), part.end(), perm.begin());
          std::sort(perm.begin(), perm.end());
          do {
            // check if compatible with creation/annihilation operators
            bool cre_compatible = true;
            bool ann_compatible = true;
            for (int A = 0; A < nops; A++) {
              if (ops[A].cre(s) < perm[A]) {
                cre_compatible = false;
              }
              if (ops[A].ann(s) < perm[A]) {
                ann_compatible = false;
              }
            }
            if (cre_compatible) {
              cre_legs_vec.push_back(perm);
            }
            if (ann_compatible) {
              ann_legs_vec.push_back(perm);
            }
          } while (std::next_permutation(perm.begin(), perm.end()));
          // } // Experimental
        }

        // combine the creation and annihilation operators
        for (const auto cre_legs : cre_legs_vec) {
          for (const auto ann_legs : ann_legs_vec) {
            // std::vector<DiagVertex> new_contr(nops);
            // int ncontracted = 0;
            // for (int A = 0; A < nops; A++) {
            //   new_contr[A].set_cre(s, cre_legs[A]);
            //   new_contr[A].set_ann(s, ann_legs[A]);
            //   // count number of operators contracted
            //   if (cre_legs[A] + ann_legs[A] > 0) {
            //     ncontracted += 1;
            //   }
            // }
            // // exclude operators that have legs only on one operator
            // if (ncontracted > 1) {
            //   contr_vec.push_back(new_contr);
            // }
            // count number of operators contracted
            int nops_contracted = 0;
            for (int A = 0; A < nops; A++) {
              nops_contracted += (cre_legs[A] + ann_legs[A] > 0);
            }
            // exclude operators that have legs only on one operator
            if (nops_contracted < 2)
              continue;

            std::vector<DiagVertex> new_contr(nops);
            for (int A = 0; A < nops; A++) {
              new_contr[A].set_cre(s, cre_legs[A]);
              new_contr[A].set_ann(s, ann_legs[A]);
            }
            contr_vec.push_back(new_contr);

            PRINT(PrintLevel::Summary,
                  cout << fmt::format("\n    {:5d}:", contr_vec.size());
                  PRINT_ELEMENTS(new_contr, " "););
          }
        }
      }
    }
  }
  return contr_vec;
}

void print_contraction(const std::vector<DiagOperator> &ops,
                       const std::vector<std::vector<DiagVertex>> &contractions,
                       const std::vector<int> ops_perm,
                       const std::vector<int> contr_perm) {

  std::vector<DiagVertex> op_vertex_vec;
  for (int o : ops_perm) {
    op_vertex_vec.push_back(ops[o].vertex());
  }

  for (int o : ops_perm) {
    cout << setw(2) << ops[o].label() << "    ";
  }
  cout << endl;
  cout << to_string(op_vertex_vec) << endl;
  for (int c : contr_perm) {
    std::vector<DiagVertex> permuted_contr;
    for (int o : ops_perm) {
      permuted_contr.push_back(contractions[c][o]);
    }
    cout << "\n" << to_string(permuted_contr) << endl;
  }
  cout << endl;
}

std::string
contraction_signature(const std::vector<DiagOperator> &ops,
                      const std::vector<std::vector<DiagVertex>> &contractions,
                      const std::vector<int> &ops_perm,
                      const std::vector<int> &contr_perm) {

  std::string s;
  int nops = ops.size();
  for (int i = 0; i < nops; i++) {
    s += ops[ops_perm[i]].label();
    s += signature(ops[ops_perm[i]].vertex());
  }

  // 2. Compare contractions
  int ncontr = contractions.size();
  for (int i = 0; i < ncontr; i++) {
    for (int j = 0; j < nops; j++) {
      s += signature(contractions[contr_perm[i]][ops_perm[j]]);
    }
  }
  return s;
}

std::pair<std::vector<DiagOperator>, std::vector<std::vector<DiagVertex>>>
WickTheorem::canonicalize_contraction(const std::vector<DiagOperator> &ops,
                                      const std::vector<int> &contraction_vec) {
  std::vector<std::vector<DiagVertex>> contractions;
  for (int c : contraction_vec) {
    contractions.push_back(elementary_contractions_[c]);
  }

  for (const auto &op : ops) {
    if (op.rank() % 2 != 0) {
      cout << "\n\n  WickTheorem::canonicalize_contraction cannot yet handle "
              "operators with an even number of sqops.\n";
      exit(1);
    }
  }

  // create a connectivity matrix
  int nops = ops.size();
  int_matrix conn_mat(nops, nops);
  for (int c : contraction_vec) {
    std::vector<int> connections;
    for (int i = 0; i < nops; ++i) {
      if (elementary_contractions_[c][i].rank() > 0) {
        connections.push_back(i);
      }
    }
    int nconn = connections.size();
    for (int i = 0; i < nconn; i++) {
      for (int j = i + 1; j < nconn; j++) {
        conn_mat(connections[i], connections[j]) = 1;
        conn_mat(connections[j], connections[i]) = 1;
      }
    }
  }

  const int maxops = 16;
  std::vector<std::bitset<maxops>> left_masks(nops);
  // create a mask for each operator
  for (int i = 0; i < nops; i++) {
    for (int j = 0; j < i; j++) {
      if (conn_mat(j, i) != 0) {
        left_masks[i][j] = true;
      }
    }
  }
  //  cout << "\nOperator masks:" << endl;
  //  for (const auto &mask : left_masks) {
  //    cout << mask << endl;
  //  }

  // setup vectors that will store the best permutations
  std::vector<int> best_ops_perm(ops.size());
  std::vector<int> best_contr_perm(contractions.size());
  std::iota(best_ops_perm.begin(), best_ops_perm.end(), 0);
  std::iota(best_contr_perm.begin(), best_contr_perm.end(), 0);

  //  cout << "Contraction to canonicalize:" << endl;
  //  print_contraction(ops, contractions, best_ops_perm, best_contr_perm);

  std::vector<
      std::pair<std::string, std::pair<std::vector<int>, std::vector<int>>>>
      scores;
  // Loop over all permutations of operators
  std::vector<int> ops_perm(ops.size());
  std::iota(ops_perm.begin(), ops_perm.end(), 0);
  do {
    //    cout << "Permutation: ";
    //    PRINT_ELEMENTS(ops_perm);

    bool allowed = true;
    for (int i = 0; i < nops; i++) {
      std::bitset<maxops> i_mask;
      int i_perm = ops_perm[i];
      // create a mask of operators to the left of i
      for (int j = 0; j < i; j++) {
        i_mask[ops_perm[j]] = true;
      }
      if ((i_mask & left_masks[i_perm]) != left_masks[i_perm]) {
        allowed = false;
      }
    }
    //    if (not allowed) {
    //      cout << " is not allowed!";
    //    }
    //    cout << endl;

    if (allowed) {
      // find the "best" contraction permutation directly

      std::vector<std::pair<std::vector<DiagVertex>, int>> sorted_contractions;

      int ncontr = contractions.size();
      for (int i = 0; i < ncontr; i++) {
        std::vector<DiagVertex> permuted_contr;
        for (int j = 0; j < nops; j++) {
          permuted_contr.push_back(contractions[i][ops_perm[j]]);
        }
        sorted_contractions.push_back(std::make_pair(permuted_contr, i));
      }
      std::sort(sorted_contractions.begin(), sorted_contractions.end());

      std::vector<int> contr_perm;
      for (int i = 0; i < ncontr; i++) {
        contr_perm.push_back(sorted_contractions[i].second);
      }

      auto signature =
          contraction_signature(ops, contractions, ops_perm, contr_perm);
      scores.push_back(
          std::make_pair(signature, std::make_pair(ops_perm, contr_perm)));
    }
  } while (std::next_permutation(ops_perm.begin(), ops_perm.end()));

  std::sort(scores.begin(), scores.end());

  best_ops_perm = scores.begin()->second.first;
  best_contr_perm = scores.begin()->second.second;

  //  cout << "\n Best permutation of operators:    ";
  //  PRINT_ELEMENTS(best_ops_perm);
  //  cout << "\n Best permutation of contractions: ";
  //  PRINT_ELEMENTS(best_contr_perm);
  //  cout << endl;

  std::vector<DiagOperator> best_ops;
  for (int o : best_ops_perm) {
    best_ops.push_back(ops[o]);
  }

  std::vector<std::vector<DiagVertex>> best_contractions;
  for (int c : best_contr_perm) {
    best_contractions.push_back(contractions[c]);
  }

  // TODO: check if there is a sign change

  //  cout << "Canonical contraction:" << endl;
  //  print_contraction(ops, contractions, best_ops_perm, best_contr_perm);

  return std::make_pair(best_ops, best_contractions);
}

void WickTheorem::generate_composite_contractions(
    const std::vector<DiagOperator> &ops, const int minrank,
    const int maxrank) {
  PRINT(PrintLevel::Summary,
        std::cout << "\n- Step 2. Generating composite contractions"
                  << std::endl;)

  std::vector<int> a(100, -1);
  // create a vector that keeps track of the free (uncontracted vertices)
  std::vector<DiagVertex> free_vertex_vec;
  for (const auto &op : ops) {
    free_vertex_vec.push_back(op.vertex());
  }

  PRINT(PrintLevel::Summary,
        std::cout << "\n    Contractions found by backtracking:";
        std::cout
        << "\n\n      N   Op. Rank  Elementary      Uncontracted operators";
        std::cout << "\n                    Contractions ";
        std::cout
        << "\n    "
           "----------------------------------------------------------";)

  // generate all contractions by backtracking
  generate_contractions_backtrack(a, 0, elementary_contractions_,
                                  free_vertex_vec, minrank, maxrank);
  PRINT(PrintLevel::Summary, std::cout << "\n\n    Total contractions: "
                                       << ncontractions_ << std::endl;)
}

Expression
WickTheorem::process_contractions(scalar_t factor,
                                  const std::vector<DiagOperator> &ops,
                                  const int minrank, const int maxrank) {
  PRINT(PrintLevel::Summary,
        std::cout << "\n- Step 3. Processing contractions" << std::endl;)

  Expression result;

  int nprocessed = 0;
  int ops_rank = operators_rank(ops);
  for (const auto &contraction_vec : contractions_) {
    std::vector<std::vector<DiagVertex>> vertices;
    int contr_rank = 0;
    for (int c : contraction_vec) {
      contr_rank += vertices_rank(elementary_contractions_[c]);
    }
    int term_rank = ops_rank - contr_rank;

    if ((term_rank >= minrank) and (term_rank <= maxrank)) {
      nprocessed++;
      PRINT(PrintLevel::Basic,
            cout << "\n\n  Contraction: " << nprocessed
                 << "  Operator rank: " << ops_rank - contr_rank << endl;)

      auto ops_contractions = canonicalize_contraction(ops, contraction_vec);
      const auto &contractions = ops_contractions.second;

      std::pair<SymbolicTerm, scalar_t> term_factor =
          evaluate_contraction(ops, contractions, factor);

      // PRINT(PrintLevel::Summary, cout << term_factor << endl;)

      SymbolicTerm &term = term_factor.first;
      scalar_t canonicalize_factor = term.canonicalize();
      result.add(
          std::make_pair(term, term_factor.second * canonicalize_factor));

      PRINT(PrintLevel::Summary,
            Term t(term_factor.second * canonicalize_factor, term);
            cout << "\n    term: " << t << endl;)
    }
  }
  if (nprocessed == 0) {
    PRINT(PrintLevel::Summary, std::cout << "\n  No contractions generated\n"
                                         << std::endl;)
  }
  return result;
}

void WickTheorem::compare_contraction_perm(
    const std::vector<DiagOperator> &ops,
    const std::vector<std::vector<DiagVertex>> &contractions,
    const std::vector<int> &ops_perm, const std::vector<int> &contr_perm,
    std::vector<int> &best_ops_perm, std::vector<int> &best_contr_perm) {
  //  // 1. Compare operators
  //  int nops = ops.size();
  //  for (int i = 0; i < nops; i++) {
  //    if (ops[ops_perm[i]] < ops[best_ops_perm[i]]) {
  //      return;
  //    }
  //  }

  //  // 2. Compare contractions
  //  int ncontr = contractions.size();
  //  for (int j = 0; j < nops; j++) {
  //    for (int i = 0; i < ncontr; i++) {
  //      if (contractions[contr_perm[i]][ops_perm[j]] <
  //          contractions[best_contr_perm[i]][best_ops_perm[j]]) {
  //        return;
  //      }
  //    }
  //  }
  //  best_ops_perm = ops_perm;
  //  best_contr_perm = contr_perm;

  // 1. Compare operators
  int nops = ops.size();
  bool ops_better = false;
  for (int i = 0; i < nops; i++) {
    if (ops[best_ops_perm[i]] < ops[ops_perm[i]]) {
      ops_better = true;
      break;
    }
  }

  // 2. Compare contractions
  int ncontr = contractions.size();
  bool contr_better = false;
  for (int i = 0; i < ncontr; i++) {
    for (int j = 0; j < nops; j++) {
      if (contractions[best_contr_perm[i]][best_ops_perm[j]] <
          contractions[contr_perm[i]][ops_perm[j]]) {
        contr_better = true;
        break;
      }
    }
  }
  if (ops_better and contr_better) {
    cout << "\n Found better contraction" << endl;
    best_ops_perm = ops_perm;
    best_contr_perm = contr_perm;
    print_contraction(ops, contractions, ops_perm, contr_perm);
  }
}

void WickTheorem::generate_contractions_backtrack(
    std::vector<int> a, int k,
    const std::vector<std::vector<DiagVertex>> &el_contr_vec,
    std::vector<DiagVertex> &free_vertex_vec, const int minrank,
    const int maxrank) {

  process_contraction(a, k, free_vertex_vec, minrank, maxrank);

  k = k + 1;
  std::vector<int> candidates =
      construct_candidates(a, k, el_contr_vec, free_vertex_vec);

  for (const auto &c : candidates) {
    a[k - 1] = c;
    make_move(a, k, el_contr_vec, free_vertex_vec);
    generate_contractions_backtrack(a, k, el_contr_vec, free_vertex_vec,
                                    minrank, maxrank);
    unmake_move(a, k, el_contr_vec, free_vertex_vec);
  }
}

std::vector<int> WickTheorem::construct_candidates(
    std::vector<int> &a, int k,
    const std::vector<std::vector<DiagVertex>> &el_contr_vec,
    const std::vector<DiagVertex> &free_vertex_vec) {

  std::vector<int> candidates;
  int nops = free_vertex_vec.size();

  // determine the last elementary contraction used
  int minc = (k > 1) ? a[k - 2] : 0;
  int maxc = el_contr_vec.size();

  // loop over all potentially viable contractions
  for (int c = minc; c < maxc; c++) {
    const auto &el_contr = el_contr_vec[c];

    // test if this contraction is compatible, that is if the number of
    // operators we want to contract is less than or equal to the number of
    // free
    // (uncontracted) operators
    bool compatible = true;
    for (int A = 0; A < nops; A++) {
      for (int s = 0; s < osi->num_spaces(); s++) {
        if (free_vertex_vec[A].cre(s) < el_contr[A].cre(s)) {
          compatible = false;
        }
        if (free_vertex_vec[A].ann(s) < el_contr[A].ann(s)) {
          compatible = false;
        }
      }
    }
    if (compatible) {
      candidates.push_back(c);
    }
  }
  return candidates;
}

void WickTheorem::make_move(
    const std::vector<int> &a, int k,
    const std::vector<std::vector<DiagVertex>> &el_contr_vec,
    std::vector<DiagVertex> &free_vertex_vec) {
  int nops = free_vertex_vec.size();

  // remove the current elementary contraction
  int c = a[k - 1];
  const auto &el_contr = el_contr_vec[c];

  for (int A = 0; A < nops; A++) {
    free_vertex_vec[A] -= el_contr[A];
    // for (int s = 0; s < osi->num_spaces(); s++) {
    //   int ncre = free_vertex_vec[A].cre(s) - el_contr[A].cre(s);
    //   free_vertex_vec[A].cre(s, ncre);
    //   int nann = free_vertex_vec[A].ann(s) - el_contr[A].ann(s);
    //   free_vertex_vec[A].ann(s, nann);
    // }
  }
}

void WickTheorem::unmake_move(
    const std::vector<int> &a, int k,
    const std::vector<std::vector<DiagVertex>> &el_contr_vec,
    std::vector<DiagVertex> &free_vertex_vec) {
  int nops = free_vertex_vec.size();

  // remove the current elementary contraction
  int c = a[k - 1];
  const auto &el_contr = el_contr_vec[c];

  for (int A = 0; A < nops; A++) {
    // for (int s = 0; s < osi->num_spaces(); s++) {
    //   int ncre = free_vertex_vec[A].cre(s) + el_contr[A].cre(s);
    //   free_vertex_vec[A].cre(s, ncre);
    //   int nann = free_vertex_vec[A].ann(s) + el_contr[A].ann(s);
    //   free_vertex_vec[A].ann(s, nann);
    // }
    free_vertex_vec[A] += el_contr[A];
  }
}

void WickTheorem::process_contraction(
    const std::vector<int> &a, int k,
    const std::vector<DiagVertex> &free_vertex_vec, const int minrank,
    const int maxrank) {
  int rank = 0;
  for (const auto &free_vertex : free_vertex_vec) {
    rank += free_vertex.rank();
  }
  if ((rank >= minrank) and (rank <= maxrank)) {
    contractions_.push_back(std::vector<int>(a.begin(), a.begin() + k));
    PRINT(
        PrintLevel::Summary, DiagVertex free_ops;
        for (const auto &free_vertex
             : free_vertex_vec) { free_ops += free_vertex; };
        cout << fmt::format("\n  {:5d}    {:3d}    ", ncontractions_ + 1,
                            free_ops.rank());
        for (int i = 0; i < k; ++i) { cout << fmt::format(" {:3d}", a[i]); };
        cout << std::string(std::max(24 - 4 * k, 2), ' ') << free_ops;)
    ncontractions_++;
  }
}

std::pair<SymbolicTerm, scalar_t> WickTheorem::evaluate_contraction(
    const std::vector<DiagOperator> &ops,
    const std::vector<std::vector<DiagVertex>> &contractions, scalar_t factor) {
  // PRINT(PrintLevel::Basic, cout << "\n----------------------------" << endl;)

  // build a map of equivalent operators

  // 1. Create tensors, lay out the second quantized operators on a vector,
  // and create mappings
  auto tensors_sqops_op_map = contration_tensors_sqops(ops);
  std::vector<Tensor> &tensors = std::get<0>(tensors_sqops_op_map);
  std::vector<SQOperator> &sqops = std::get<1>(tensors_sqops_op_map);
  // this map takes the operator index (op), orbital space (s), the sqop type,
  // and an index and maps it to the operators as they are stored in a vector
  //  std::map<std::tuple<int, int, bool, int>, int> op_map;
  //                   op space cre    n
  std::map<std::tuple<int, int, bool, int>, int> &op_map =
      std::get<2>(tensors_sqops_op_map);

  // 2. Apply the contractions to the second quantized operators and add new
  // tensors (density matrices, cumulants)

  // counter of how many second quantized operators are not contracted
  std::vector<DiagVertex> ops_offset(ops.size());
  // vector to store the order of operators
  std::vector<int> sign_order(sqops.size(), -1);
  std::vector<std::vector<bool>> bit_map_vec;

  // a counter to keep track of the positions assigned to operators
  int sorted_position = 0;

  // a counter that keeps track of the number of sq operators contracted
  int nsqops_contracted = 0;

  // a sign factor that keeps into account a negative sign introduced by
  // sorting
  // the operators in a unoccupied-unoccupied contraction
  int unoccupied_sign = 1;

  index_map_t pair_contraction_reindex_map;
  // Loop over elementary contractions
  for (const std::vector<DiagVertex> &contraction : contractions) {
    std::vector<bool> bit_map(sqops.size(), false);

    // Find the rank and space of this contraction
    //    const auto &contraction = elementary_contractions_[c];
    int rank = vertices_rank(contraction);
    int s = vertices_space(contraction);
    nsqops_contracted += rank;

    // find the position of the creation operators
    std::vector<int> pos_cre_sqops =
        vertex_vec_to_pos(contraction, ops_offset, op_map, true);
    // find the position of the annihilation operators
    std::vector<int> pos_ann_sqops =
        vertex_vec_to_pos(contraction, ops_offset, op_map, false);

    // mark the creation operators contracted and their order
    for (int c : pos_cre_sqops) {
      bit_map[c] = true;
      sign_order[c] = sorted_position;
      sorted_position += 1;
    }
    // mark the annihilation operators contracted and their order
    for (int a : pos_ann_sqops) {
      bit_map[a] = true;
      sign_order[a] = sorted_position;
      sorted_position += 1;
    }

    SpaceType dmstruc = osi->space_type(s);

    // Pairwise contractions creation-annihilation:
    // ________
    // |      |
    // a^+(i) a(j) = delta(i,j)

    if (dmstruc == SpaceType::Occupied) {
      // Reindex the annihilator (j) to the creator (i)
      Index cre_index = sqops[pos_cre_sqops[0]].index();
      Index ann_index = sqops[pos_ann_sqops[0]].index();
      pair_contraction_reindex_map[ann_index] = cre_index;
    }

    // Pairwise contractions creation-annihilation:
    // ______
    // |    |
    // a(i) a^+(j) = delta(i,j)

    if (dmstruc == SpaceType::Unoccupied) {
      // Reindex the creator (j) to the annihilator (i)
      Index cre_index = sqops[pos_cre_sqops[0]].index();
      Index ann_index = sqops[pos_ann_sqops[0]].index();
      pair_contraction_reindex_map[cre_index] = ann_index;
      unoccupied_sign *= -1; // this factor is to compensate for the fact that
                             // we order operator in a canonical form in which
                             // annihilators are to the left of creation
                             // operators
    }

    // 2k-legged contractions (k >= 2) of k creation and k annihilation
    // operators:
    // _____________
    // |   |   |   |
    // a^+ a   a   a^+

    if (dmstruc == SpaceType::General) {
      std::vector<Index> lower;
      std::vector<Index> upper;
      // collect indices of creation operators for the upper indices
      for (int c : pos_cre_sqops) {
        upper.push_back(sqops[c].index());
      }
      // collect indices of annihilation operators for the lower indices
      for (int a : pos_ann_sqops) {
        lower.push_back(sqops[a].index());
      }
      // reverse the lower indices
      std::reverse(lower.begin(), lower.end());
      // prepare the label
      std::string label;
      if (rank == 2) {
        if (pos_cre_sqops[0] < pos_ann_sqops[0]) {
          label = "gamma" + std::to_string(rank / 2);
        } else {
          label = "eta" + std::to_string(rank / 2);
          unoccupied_sign *= -1; // this factor is to compensate for the fact
                                 // that we order operator in a canonical form
                                 // in which annihilators are to the left of
                                 // creation operators
        }
      } else {
        label = "lambda" + std::to_string(rank / 2);
      }
      // add the cumulant to the list of tensors
      tensors.push_back(Tensor(label, lower, upper));
    }
    bit_map_vec.push_back(bit_map);
  }

  // assign an order to the uncontracted operators
  // creation operators come before annihilation operators
  for (SQOperatorType type :
       {SQOperatorType::Creation, SQOperatorType::Annihilation}) {
    for (int s = 0; s < osi->num_spaces(); s++) {
      for (int i = 0; i < sqops.size(); i++) {
        if ((sign_order[i] == -1) and (sqops[i].index().space() == s) and
            (sqops[i].type() == type)) {
          sign_order[i] = sorted_position;
          sorted_position += 1;
        }
      }
    }
  }

  PRINT(PrintLevel::Basic,
        print_contraction(ops, tensors, bit_map_vec, sqops, sign_order);)

  int sign = unoccupied_sign * permutation_sign(sign_order);

  PRINT(PrintLevel::All, PRINT_ELEMENTS(sign_order, "\n  positions: "););

  std::vector<std::pair<int, SQOperator>> sorted_sqops;
  sorted_position = 0;
  for (const auto &sqop : sqops) {
    sorted_sqops.push_back(std::make_pair(sign_order[sorted_position], sqop));
    sorted_position += 1;
  }
  std::sort(sorted_sqops.begin(), sorted_sqops.end());

  sqops.clear();
  for (int i = nsqops_contracted; i < sorted_sqops.size(); ++i) {
    sqops.push_back(sorted_sqops[i].second);
  }

  // find the combinatorial factor associated with this contraction
  scalar_t comb_factor = combinatorial_factor(ops, contractions);

  SymbolicTerm term;

  for (const auto &tensor : tensors) {
    term.add(tensor);
  }
  for (const auto &sqop : sqops) {
    term.add(sqop);
  }
  for (const auto &op : ops) {
    factor *= op.factor();
  }

  term.reindex(pair_contraction_reindex_map);

  PRINT(PrintLevel::Summary,
        cout << fmt::format("  sign =                 {:d}", sign) << endl;
        cout << fmt::format("  factor =               {:s}", factor.repr())
             << endl;
        cout << fmt::format("  combinatorial factor = {:s}", comb_factor.repr())
             << endl;);

  return std::make_pair(term, sign * factor * comb_factor);
}

std::tuple<std::vector<Tensor>, std::vector<SQOperator>,
           std::map<std::tuple<int, int, bool, int>, int>>
WickTheorem::contration_tensors_sqops(const std::vector<DiagOperator> &ops) {

  std::vector<SQOperator> sqops;
  std::vector<Tensor> tensors;
  std::map<std::tuple<int, int, bool, int>, int> op_map;

  index_counter ic(osi->num_spaces());

  // Loop over all operators
  int n = 0;
  for (int o = 0; o < ops.size(); o++) {
    const auto &op = ops[o];

    // Loop over creation operators (lower indices)
    std::vector<Index> lower;
    for (int s = 0; s < osi->num_spaces(); s++) {
      for (int c = 0; c < op.cre(s); c++) {
        Index idx(s, ic.next_index(s)); // get next available index
        sqops.push_back(SQOperator(SQOperatorType::Creation, idx));
        lower.push_back(idx);
        auto key = std::make_tuple(o, s, true, c);
        op_map[key] = n;
        PRINT(PrintLevel::All, print_key(key, n););
        n += 1;
      }
    }

    // Loop over annihilation operators (upper indices)
    // the annihilation operators are layed out in a reversed order (hence the
    // need to reverse the upper indices of the tensor, see below)
    std::vector<Index> upper;
    for (int s = osi->num_spaces() - 1; s >= 0; s--) {
      for (int a = op.ann(s) - 1; a >= 0; a--) {
        Index idx(s, ic.next_index(s)); // get next available index
        sqops.push_back(SQOperator(SQOperatorType::Annihilation, idx));
        upper.push_back(idx);
        auto key = std::make_tuple(o, s, false, a);
        op_map[key] = n;
        PRINT(PrintLevel::All, print_key(key, n););
        n += 1;
      }
    }

    // reverse the order of the upper indices
    std::reverse(upper.begin(), upper.end());
    tensors.push_back(Tensor(op.label(), lower, upper));
  }
  return make_tuple(tensors, sqops, op_map);
}

std::vector<int> WickTheorem::vertex_vec_to_pos(
    const std::vector<DiagVertex> &vertex_vec,
    std::vector<DiagVertex> &ops_offset,
    std::map<std::tuple<int, int, bool, int>, int> &op_map, bool creation) {

  std::vector<int> result;

  int s = vertices_space(vertex_vec);

  PRINT(PrintLevel::All, cout << "\n  Vertex to position:" << endl;);

  // Loop over all vertices
  for (int v = 0; v < vertex_vec.size(); v++) {
    const DiagVertex &vertex = vertex_vec[v];
    int nops = creation ? vertex.cre(s) : vertex.ann(s);
    // assign the operator indices
    int ops_off = creation ? ops_offset[v].cre(s) : ops_offset[v].ann(s);
    for (int i = 0; i < nops; i++) {
      // find the operator corresponding to this leg
      auto key =
          creation
              ? std::make_tuple(v, s, true,
                                ops_off + i) // start from the leftmost operator
              : std::make_tuple(v, s, false, ops_off + i);
      if (op_map.count(key) == 0) {
        PRINT(PrintLevel::All, print_key(key, -1););
        cout << " NOT FOUND!!!" << endl;
        exit(1);
      } else {
        int sqop_pos = op_map[key];
        result.push_back(sqop_pos);
        PRINT(PrintLevel::All, print_key(key, sqop_pos););
      }
    }
    // update the creator's offset
    if (creation) {
      ops_offset[v].set_cre(s, ops_off + nops);
    } else {
      ops_offset[v].set_ann(s, ops_off + nops);
    }
  }
  return result;
}

scalar_t WickTheorem::combinatorial_factor(
    const std::vector<DiagOperator> &ops,
    const std::vector<std::vector<DiagVertex>> &contractions) {

  scalar_t factor = 1;

  // stores the offset for each uncontracted operator
  std::vector<DiagVertex> free_vertices;
  for (const auto &op : ops) {
    free_vertices.push_back(op.vertex());
  }

  // for each contraction find the combinatorial factor
  for (const auto &contraction : contractions) {
    for (int v = 0; v < contraction.size(); v++) {
      const DiagVertex &vertex = contraction[v];
      for (int s = 0; s < osi->num_spaces(); s++) {
        auto &[kcre, kann] = vertex.vertex(s);
        auto &[ncre, nann] = free_vertices[v].vertex(s);
        factor *= binomial(ncre, kcre);
        factor *= binomial(nann, kann);
      }
      free_vertices[v] -= vertex;
    }
  }

  std::map<std::vector<DiagVertex>, int> contraction_count;
  for (const auto &contraction : contractions) {
    contraction_count[contraction] += 1;
  }
  //  for (int c : contraction) {
  //    contraction_count[c] += 1;
  //  }
  for (const auto &kv : contraction_count) {
    factor /= binomial(kv.second, 1);
  }

  return factor;
}

void print_key(std::tuple<int, int, bool, int> key, int n) {
  cout << "key[vertex = " << std::get<0>(key)
       << ", space = " << std::get<1>(key)
       << ", creation = " << std::get<2>(key) << ", num = " << std::get<3>(key)
       << "] -> " << n << endl;
}

void print_contraction(const std::vector<DiagOperator> &ops,
                       const std::vector<Tensor> &tensors,
                       const std::vector<std::vector<bool>> &bit_map_vec,
                       const std::vector<SQOperator> &sqops,
                       const std::vector<int> sign_order) {
  std::string pre("          ");
  // 1. Draw the contraction legs
  for (const auto &bit_map : bit_map_vec) {
    int first = std::distance(bit_map.begin(),
                              std::find(bit_map.begin(), bit_map.end(), true));
    int last = std::distance(std::find(bit_map.rbegin(), bit_map.rend(), true),
                             bit_map.rend());
    std::cout << pre;
    for (int k = 0; k < first; k++) {
      cout << "   ";
    }
    std::cout << " ┌─";
    for (int k = first + 1; k < last - 1; k++) {
      std::cout << (bit_map[k] ? "─┬─" : "───");
    }
    std::cout << "─┐ " << std::endl;
  }

  // 2. Show the type of operator (+ = creation, - = annihilation)
  std::cout << "  type    ";
  for (const auto &sqop : sqops) {
    std::cout << ((sqop.type() == SQOperatorType::Creation) ? " + " : " - ");
  }
  std::cout << endl;

  // 3. Show the operator index
  std::cout << "  indices ";
  for (const auto &sqop : sqops) {
    std::cout << fmt::format("{:3s}", sqop.index().str());
  }
  std::cout << endl;

  // 3. Show the permutation of the operators
  std::cout << "  order  ";
  for (int order : sign_order) {
    std::cout << fmt::format("{:3d}", order);
  }
  std::cout << "\n" << endl;

  // 5. Show the indices of the tensors involved
  std::map<Index, int> index_map;
  int k = 0;
  for (int k = 0; k < sqops.size(); k++) {
    index_map[sqops[k].index()] = k;
  }
  for (const auto &tensor : tensors) {
    std::vector<int> indices(index_map.size(), 0);
    for (const auto &idx : tensor.upper()) {
      indices[index_map[idx]] = 1;
    }
    for (const auto &idx : tensor.lower()) {
      indices[index_map[idx]] = -1;
    }
    std::cout << "          ";
    for (int i : indices) {
      if (i == 1) {
        std::cout << "─┸─";
      } else if (i == -1) {
        std::cout << "─┰─";
      } else {
        std::cout << "───";
      }
    }
    std::cout << "── " << tensor.str() << std::endl;
  }
  std::cout << std::endl;
}