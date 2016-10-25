#include "wdiag_operator_sum.h"

WDiagOperatorSum::WDiagOperatorSum() {}

WDiagOperatorSum::WDiagOperatorSum(const std::vector<WDiagOperator>& vec_dop,
                                   scalar_t factor) {
  add(vec_dop, factor);
}

void WDiagOperatorSum::add(const std::vector<WDiagOperator> &vec_dop,
                           scalar_t factor) {
  auto search = sum_.find(vec_dop);

  if (search != sum_.end()) {
    /// Found, then just add the factor to the existing term
    search->second += factor;
    if (search->second == 0) {
      sum_.erase(search);
    }
  } else {
    sum_[vec_dop] = factor;
  }
}

dop_sum_t &WDiagOperatorSum::sum() { return sum_; }

WDiagOperatorSum &WDiagOperatorSum::operator+=(WDiagOperatorSum &rhs) {
  for (auto &vec_dop_factor : rhs.sum()) {
    add(vec_dop_factor.first, vec_dop_factor.second);
  }
  return *this;
}

WDiagOperatorSum &WDiagOperatorSum::operator-=(WDiagOperatorSum &rhs) {
  for (auto &vec_dop_factor : rhs.sum()) {
    add(vec_dop_factor.first, -vec_dop_factor.second);
  }
  return *this;
}

WDiagOperatorSum &WDiagOperatorSum::operator*=(scalar_t factor) {
  for (auto &vec_dop_factor : sum_) {
    vec_dop_factor.second *= factor;
  }
  return *this;
}

WDiagOperatorSum &WDiagOperatorSum::operator/=(scalar_t factor) {
  for (auto &vec_dop_factor : sum_) {
    vec_dop_factor.second /= factor;
  }
  return *this;
}

std::string WDiagOperatorSum::str() const {
  std::string s;
  for (auto &vec_dop_factor : sum_) {
    s += vec_dop_factor.second.str();
    for (auto &dop : vec_dop_factor.first) {
      s += ' ' + dop.str();
    }
  }
  return s;
}

std::ostream &operator<<(std::ostream &os, const WDiagOperatorSum &opsum) {
  os << opsum.str();
  return os;
}

WDiagOperatorSum commutator(WDiagOperatorSum &A, WDiagOperatorSum &B) {
  WDiagOperatorSum result;
  for (auto &vec_factor_A : A.sum()) {
    for (auto &vec_factor_B : B.sum()) {
      auto &vec_A = vec_factor_A.first;
      auto &vec_B = vec_factor_B.first;

      std::vector<WDiagOperator> vec_AB;
      vec_AB.insert(vec_AB.end(), vec_A.begin(), vec_A.end());
      vec_AB.insert(vec_AB.end(), vec_B.begin(), vec_B.end());

      std::vector<WDiagOperator> vec_BA;
      vec_BA.insert(vec_BA.end(), vec_B.begin(), vec_B.end());
      vec_BA.insert(vec_BA.end(), vec_A.begin(), vec_A.end());

      scalar_t factor_A = vec_factor_A.second;
      scalar_t factor_B = vec_factor_B.second;

      result.add(vec_AB, factor_A * factor_B);
      result.add(vec_BA, -factor_A * factor_B);
    }
  }
  return result;
}
