#include "ptxlens.hpp"
#include <iostream>
#include <stdexcept>
void check(bool b) {
  if (!b)
    throw std::runtime_error("unit assertion failed");
}
int main() {
  auto tokens =
      ptxlens::tokenize("// add;\n/* st.global; */ mov.u32 %r1, 1;\n");
  check(tokens.size() == 5);
  check(tokens[0].text == "mov.u32");
  check(tokens[0].line == 2);
  check(ptxlens::classify("ld.global.nc.f32") == "global_memory");
  check(ptxlens::classify("atom.shared.add.u32") == "atomics");
  check(ptxlens::classify("madeup.foo") == "other_unknown");
  const auto m =
      ptxlens::parse(".version 8.0\n.target sm_70\n.address_size 64\n.visible "
                     ".entry k(){ .reg .b32 %r<4>; .reg .pred %p; mov.u32 %r0, "
                     "1; @%p bra done; add.u32 %r0, %r0, 1; done: ret; }");
  const auto &f = m.functions.at(0);
  check(f.instructions.size() == 4);
  check(f.blocks.size() == 3);
  check(f.blocks[0].successors.size() == 2);
  check(f.registers_by_type.at("b32") == 4);
  check(f.predicated == 1);
  check(f.instructions[1].predicate == "%p");
  std::cout << "tokenizer, classification, declarations, and CFG unit "
               "assertions passed\n";
}
