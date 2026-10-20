#pragma once
#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <vector>
namespace ptxlens {
struct Token {
  std::string text;
  std::size_t line = 1;
};
struct Declaration {
  std::string space, type, name;
  std::size_t elements = 1, line = 0;
};
struct Instruction {
  std::string opcode, family, category, predicate, memory_space, branch_target;
  std::vector<std::string> modifiers;
  std::vector<Token> operands;
  std::size_t line = 0;
};
struct Block {
  std::size_t begin = 0, end = 0;
  std::vector<std::size_t> successors;
};
struct Observation {
  std::string kind, evidence, explanation, confidence;
  std::size_t instruction = 0, line = 0;
};
struct Function {
  std::string name;
  bool entry = false, external = false;
  std::vector<Declaration> declarations;
  std::vector<Instruction> instructions;
  std::map<std::string, std::size_t> labels;
  std::vector<Block> blocks;
  std::vector<Observation> observations;
  std::map<std::string, std::size_t> categories, registers_by_type,
      memory_operations;
  std::set<std::string> unique_register_references;
  std::size_t register_reference_occurrences = 0, predicated = 0, branches = 0,
              unresolved_control_flow = 0;
};
struct Module {
  std::string version, target;
  std::size_t address_size = 0;
  std::vector<Declaration> declarations;
  std::vector<std::string> directives;
  std::vector<Function> functions;
};
std::vector<Token> tokenize(const std::string &source);
Module parse(const std::string &source);
std::string classify(const std::string &opcode);
std::set<std::string> recognized_families();
std::vector<std::string> category_names();
std::string json(const Module &module);
std::string quote(const std::string &value);
} // namespace ptxlens
