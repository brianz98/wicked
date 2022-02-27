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
void print_contraction(const std::vector<DiagOperator> &ops,
                       const std::vector<std::vector<DiagVertex>> &contractions,
                       const std::vector<int> ops_perm,
                       const std::vector<int> contr_perm);

std::string
contraction_signature(const std::vector<DiagOperator> &ops,
                      const std::vector<std::vector<DiagVertex>> &contractions,
                      const std::vector<int> &ops_perm,
                      const std::vector<int> &contr_perm);

using namespace std;

Expression
WickTheorem::process_contractions(scalar_t factor,
                                  const std::vector<DiagOperator> &ops,
                                  const int minrank, const int maxrank) {
  PRINT(PrintLevel::Summary,
        std::cout << "\n- Step 3. Processing contractions" << std::endl;)

  Expression result;

  // loop over all contractions and keep those with the correct rank
  // contraction_vec stores a list of elementary contractions appearing
  // in a term
  int nprocessed = 0;
  int ops_rank = sum_num_ops(ops);
  for (const auto &contraction_vec : contractions_) {
    int contr_rank = 0;
    for (int c : contraction_vec) {
      contr_rank += sum_num_ops(elementary_contractions_[c]);
    }
    int term_rank = ops_rank - contr_rank;

    if ((term_rank >= minrank) and (term_rank <= maxrank)) {
      nprocessed++;
      PRINT(PrintLevel::Basic,
            cout << "\n\n  Contraction: " << nprocessed
                 << "  Operator rank: " << ops_rank - contr_rank << endl;)

      // canonicalize the contraction
      auto ops_contractions = canonicalize_contraction(ops, contraction_vec);
      const auto &contractions = ops_contractions.second;

      std::pair<SymbolicTerm, scalar_t> term_factor =
          evaluate_contraction(ops, contractions, factor);

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

std::pair<std::vector<DiagOperator>, std::vector<std::vector<DiagVertex>>>
WickTheorem::canonicalize_contraction(const std::vector<DiagOperator> &ops,
                                      const std::vector<int> &contraction_vec) {
  // create a vector of elementary contractions to apply
  std::vector<std::vector<DiagVertex>> contractions;
  for (int c : contraction_vec) {
    contractions.push_back(elementary_contractions_[c]);
  }

  for (const auto &op : ops) {
    if (op.num_ops() % 2 != 0) {
      auto msg =
          "\n\n  WickTheorem::canonicalize_contraction cannot yet handle "
          "operators with an even number of sqops.\n";
      throw std::runtime_error(msg);
    }
  }

  // create a connectivity matrix conn_mat[i][j] such that
  // if operators i and j are contracted, then conn_mat[i][j] = 1
  int nops = ops.size();
  int_matrix conn_mat(nops, nops);
  for (int c : contraction_vec) {
    std::vector<int> connections;
    for (int i = 0; i < nops; ++i) {
      if (elementary_contractions_[c][i].num_ops() > 0) {
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

  const int maxops = 64;
  std::vector<std::bitset<maxops>> left_masks(nops);
  // create a mask for each operator
  for (int i = 0; i < nops; i++) {
    for (int j = 0; j < i; j++) {
      if (conn_mat(j, i) != 0) {
        left_masks[i][j] = true;
      }
    }
  }
  PRINT(
      PrintLevel::Detailed, cout << "\n  Contraction canonicalization" << endl;
      cout << "\n  Operator masks:" << endl;
      for (const auto &mask
           : left_masks) { cout << mask << endl; });

  // setup vectors that will store the best permutations
  std::vector<int> best_ops_perm(ops.size());
  std::vector<int> best_contr_perm(contractions.size());
  std::iota(best_ops_perm.begin(), best_ops_perm.end(), 0);
  std::iota(best_contr_perm.begin(), best_contr_perm.end(), 0);

  PRINT(PrintLevel::Detailed, cout << "Contraction to canonicalize:" << endl;
        print_contraction(ops, contractions, best_ops_perm, best_contr_perm););

  std::vector<
      std::pair<std::string, std::pair<std::vector<int>, std::vector<int>>>>
      scores;
  // Loop over all permutations of operators
  std::vector<int> ops_perm(ops.size());
  std::iota(ops_perm.begin(), ops_perm.end(), 0);
  do {
    PRINT(PrintLevel::Detailed, cout << "  Operator permutation: ";
          PRINT_ELEMENTS(ops_perm););

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

    PRINT(PrintLevel::Detailed,
          cout << (allowed ? " is allowed" : " is not allowed!") << endl;);

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

  for (const auto &[score, op_contr] : scores) {
    cout << score << endl;
  }

  best_ops_perm = scores.begin()->second.first;
  best_contr_perm = scores.begin()->second.second;

  PRINT(PrintLevel::Detailed, cout << "\n Best permutation of operators:    ";
        PRINT_ELEMENTS(best_ops_perm);
        cout << "\n Best permutation of contractions: ";
        PRINT_ELEMENTS(best_contr_perm); cout << endl;);

  std::vector<DiagOperator> best_ops;
  for (int o : best_ops_perm) {
    best_ops.push_back(ops[o]);
  }

  std::vector<std::vector<DiagVertex>> best_contractions;
  for (int c : best_contr_perm) {
    best_contractions.push_back(contractions[c]);
  }

  // TODO: check if there is a sign change

  PRINT(PrintLevel::Detailed, cout << "Canonical contraction:" << endl;
        print_contraction(ops, contractions, best_ops_perm, best_contr_perm););

  return std::make_pair(best_ops, best_contractions);
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

std::pair<SymbolicTerm, scalar_t> WickTheorem::evaluate_contraction(
    const std::vector<DiagOperator> &ops,
    const std::vector<std::vector<DiagVertex>> &contractions, scalar_t factor) {
  // 1. Get the Tensor objects and SQOperator vector corresponding to the
  // uncontracted term
  auto tensors_sqops_op_map = contraction_tensors_sqops(ops);
  std::vector<Tensor> &tensors = std::get<0>(tensors_sqops_op_map);
  std::vector<SQOperator> &sqops = std::get<1>(tensors_sqops_op_map);
  // this map takes the operator index (op), orbital space (s), the sqop type
  // (true = cre, false = ann), and an index and maps it to the operators as
  // they are stored in a vector
  //  std::map<std::tuple<int, int, bool, int>, int> op_map;
  //                       op space cre    n
  std::map<std::tuple<int, int, bool, int>, int> &op_map =
      std::get<2>(tensors_sqops_op_map);

  // 2. Apply the contractions to the second quantized operators and add new
  // tensors (density matrices, cumulants)

  // counts of how many second quantized operators are not contracted
  std::vector<DiagVertex> ops_offset(ops.size());
  // vector to store the order of operators
  std::vector<int> sign_order(sqops.size(), -1);
  std::vector<std::vector<bool>> bit_map_vec;

  // a counter to keep track of the positions assigned to operators
  int sorted_position = 0;

  // a counter that keeps track of the number of sq operators contracted
  int nsqops_contracted = 0;

  // a sign factor that keeps into account a negative sign introduced by
  // sorting the operators in a unoccupied-unoccupied contraction
  int unoccupied_sign = 1;

  index_map_t pair_contraction_reindex_map;
  // Loop over elementary contractions
  for (const std::vector<DiagVertex> &contraction : contractions) {
    // a bit array to keep track of which operators are contracted
    std::vector<bool> bit_map(sqops.size(), false);

    // Find the rank and space of this contraction
    int rank = sum_num_ops(contraction);
    int s = spaces_in_vertices(contraction)[0];
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
          label = "gamma1";
        } else {
          label = "eta1";
          unoccupied_sign *= -1; // this factor is to compensate for the fact
                                 // that we order operator in a canonical form
                                 // in which annihilators are to the left of
                                 // creation operators
        }
      } else {
        label = "lambda" + std::to_string(rank / 2);
      }
      // add the cumulant to the list of tensors
      tensors.push_back(
          Tensor(label, lower, upper, SymmetryType::Antisymmetric));
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
WickTheorem::contraction_tensors_sqops(const std::vector<DiagOperator> &ops) {

  std::vector<SQOperator> sqops;
  std::vector<Tensor> tensors;
  std::map<std::tuple<int, int, bool, int>, int> op_map;

  index_counter ic(osi->num_spaces());

  // Loop over all operators
  int n = 0;
  for (int o = 0, maxo = ops.size(); o < maxo; o++) {
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
    tensors.push_back(
        Tensor(op.label(), lower, upper, SymmetryType::Antisymmetric));
  }
  return make_tuple(tensors, sqops, op_map);
}

std::vector<int> WickTheorem::vertex_vec_to_pos(
    const std::vector<DiagVertex> &vertex_vec,
    std::vector<DiagVertex> &ops_offset,
    std::map<std::tuple<int, int, bool, int>, int> &op_map, bool creation) {

  std::vector<int> result;

  int s = spaces_in_vertices(vertex_vec)[0];

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

/////

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