#include <iostream>

#define PRINT_DEBUG 1

#include "wicked.h"
#include "test.h"

using namespace std;

WDiagTheorem wdt;

bool test_energy1() {
  auto T1 = make_operator("t", {"o->v"});
  auto Fov = make_operator("f", {"v->o"});

  // <F T1>
  auto val = wdt.contract_sum(1, Fov * T1, 0, 0);
  auto val_test = string_to_sum("f^{v0}_{o0} t^{o0}_{v0}");
  return (val == val_test);
}

bool test_energy2() {
  auto T2 = make_operator("t", {"oo->vv"});
  auto Voovv = make_operator("v", {"vv->oo"});

  // <V T2>
  auto val = wdt.contract_sum(1, Voovv * T2, 0, 0);
  auto val_test = string_to_sum("1/4 t^{o0,o1}_{v0,v1} v^{v0,v1}_{o0,o1}");
  return (val == val_test);
}

bool test_energy3() {
  auto T1 = make_operator("t", {"o->v"});
  auto Voovv = make_operator("v", {"vv->oo"});

  // 1/2 <V T1 T1>
  auto val = wdt.contract_sum(scalar_t(1, 2), Voovv * T1 * T1, 0, 0);
  auto val_test =
      string_to_sum("1/2 t^{o0}_{v0} t^{o1}_{v1} v^{v0,v1}_{o0,o1}");
  return (val == val_test);
}

bool test_r1_1() {
  auto Fvo = make_operator("f", {"o->v"});

  // <R1 F>
  auto sum = wdt.contract_sum(1, Fvo, 2, 2);
  WSum val;
  for (const auto &eq : sum.to_manybody_equation("r")) {
      val.add(eq.rhs());
  }
  auto val_test = string_to_sum("f^{o0}_{v0}").canonicalize();

  TEST_DEBUG_PRINT(cout << "Result: " << val << endl;)
  TEST_DEBUG_PRINT(cout << "Test:   " << val_test << endl;)
  return (val == val_test);
}

bool test_r1_2() {
  auto T1 = make_operator("t", {"o->v"});
  auto R1 = make_operator("r", {"v->o"});
  auto Fvv = make_operator("f", {"v->v"});

  // <R1 T1 F>
  auto sum = wdt.contract_sum(1, Fvv * T1, 2, 2);
  WSum val;
  for (const auto &eq : sum.to_manybody_equation("r")) {
      val.add(eq.rhs());
  }
  auto val_test = string_to_sum("f^{v1}_{v0} t^{o0}_{v1}");
  val_test.canonicalize();

  TEST_DEBUG_PRINT(cout << "Result: " << val << endl;)
  TEST_DEBUG_PRINT(cout << "Test:   " << val_test << endl;)

  return (val == val_test);
}

bool test_r1_3() {
  auto T1 = make_operator("t", {"o->v"});
  auto R1 = make_operator("r", {"v->o"});
  auto Foo = make_operator("f", {"o->o"});

  // <R1 T1 F>
  auto sum = wdt.contract_sum(1, Foo * T1, 2, 2);
  TEST_DEBUG_PRINT(cout << "Result: " << sum << endl;)
  WSum val;
  for (const auto &eq : sum.to_manybody_equation("r")) {
      val.add(eq.rhs());
  }
  auto val_test = string_to_sum("-1 f^{o0}_{o1} t^{o1}_{v0}");
//  val_test.canonicalize();
  TEST_DEBUG_PRINT(cout << "Result: " << val << endl;)
  TEST_DEBUG_PRINT(cout << "Test:   " << val_test << endl;)
  return (val == val_test);
}

int main(int argc, const char *argv[]) {

  // Define the orbital space
  osi = std::make_shared<OrbitalSpaceInfo>();
  osi->add_space("o", RDMType::Occupied, {"i", "j", "k", "l", "m", "n"});
  osi->add_space("v", RDMType::Unoccupied, {"a", "b", "c", "d", "e", "f"});

  // Assemble the tests
  auto test_functions = {
      //              Expectation,  test function,  User friendly description
      std::make_tuple(TestPass, test_energy1, "CCSD Energy <F T1>"),
      std::make_tuple(TestPass, test_energy2, "CCSD Energy <V T2>"),
      std::make_tuple(TestPass, test_energy3, "CCSD Energy 1/2 <V T1 T1>"),
      std::make_tuple(TestPass, test_r1_1, "CCSD T1 Residual F"),
      std::make_tuple(TestPass, test_r1_2, "CCSD T1 Residual Fvv T1"),
      std::make_tuple(TestPass, test_r1_3, "CCSD T1 Residual Foo T1"),

  };

  // Run the tests
  bool success = wicked_test(test_functions);

  return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
