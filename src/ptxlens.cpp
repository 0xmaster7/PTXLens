#include "ptxlens.hpp"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
namespace ptxlens {
namespace {
bool word(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$' ||
         c == '.' || c == '%';
}
std::vector<std::string> split(const std::string &s) {
  std::vector<std::string> parts;
  std::size_t begin = 0;
  for (std::size_t n = 0; n <= s.size(); ++n)
    if (n == s.size() || s[n] == '.') {
      parts.push_back(s.substr(begin, n - begin));
      begin = n + 1;
    }
  return parts;
}
const std::map<std::string, std::set<std::string>> &classes() {
  static const std::map<std::string, std::set<std::string>> table = {
      {"arithmetic",
       {"add",   "addc", "sub", "subc", "mul", "mad", "madc", "fma",
        "div",   "rem",  "abs", "neg",  "min", "max", "sad",  "sqrt",
        "rsqrt", "rcp",  "sin", "cos",  "lg2", "ex2", "testp"}},
      {"logical",
       {"and", "or", "xor", "not", "cnot", "shl", "shr", "shf", "lop3", "bfe",
        "bfi", "brev", "bfind", "popc", "clz", "set", "setp"}},
      {"data_movement",
       {"mov", "selp", "slct", "shfl", "prmt", "ld", "ldu", "st"}},
      {"control_flow", {"bra", "brx", "call", "ret", "exit", "trap", "brkpt"}},
      {"synchronization", {"bar", "barrier", "membar", "fence"}},
      {"atomics", {"atom", "red"}},
      {"conversion", {"cvt", "cvta"}}};
  return table;
}
std::size_t integer(const Token &t) {
  std::size_t value = 0;
  auto r = std::from_chars(t.text.data(), t.text.data() + t.text.size(), value);
  if (r.ec != std::errc{} || r.ptr != t.text.data() + t.text.size())
    throw std::runtime_error("expected unsigned integer at line " +
                             std::to_string(t.line));
  return value;
}
bool identifier(const std::string &s) {
  if (s.empty() || !(std::isalpha(static_cast<unsigned char>(s[0])) ||
                     s[0] == '_' || s[0] == '$' || s[0] == '%'))
    return false;
  return std::all_of(s.begin(), s.end(), [](char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' ||
           c == '$' || c == '%';
  });
}
bool is_register(const std::string &name, const Function &f) {
  if (!name.empty() && name[0] == '%')
    return true; // Includes PTX special registers; no hardware allocation
                 // claim.
  for (const auto &d : f.declarations)
    if (d.space == "reg") {
      if (d.elements == 1 && name == d.name)
        return true;
      if (name.size() > d.name.size() &&
          name.compare(0, d.name.size(), d.name) == 0) {
        auto suffix = name.substr(d.name.size());
        std::size_t index = 0;
        auto r = std::from_chars(suffix.data(), suffix.data() + suffix.size(),
                                 index);
        if (r.ec == std::errc{} && r.ptr == suffix.data() + suffix.size() &&
            index < d.elements)
          return true;
      }
    }
  return false;
}
void analyze(Function &f) {
  for (const auto &c : category_names())
    f.categories[c] = 0;
  for (const auto &d : f.declarations)
    if (d.space == "reg")
      f.registers_by_type[d.type] += d.elements;
  std::set<std::size_t> leaders;
  if (!f.instructions.empty())
    leaders.insert(0);
  for (const auto &[name, pos] : f.labels) {
    (void)name;
    leaders.insert(pos);
  }
  std::map<std::string, std::pair<std::size_t, std::size_t>> loads;
  std::set<std::string> warned;
  std::size_t memory_count = 0;
  for (std::size_t n = 0; n < f.instructions.size(); ++n) {
    const auto &i = f.instructions[n];
    ++f.categories[i.category];
    if (!i.predicate.empty())
      ++f.predicated;
    if (i.family == "bra" || i.family == "brx")
      ++f.branches;
    if (!i.memory_space.empty()) {
      ++f.memory_operations[i.memory_space];
      ++memory_count;
    }
    for (const auto &token : i.operands)
      if (is_register(token.text, f)) {
        ++f.register_reference_occurrences;
        f.unique_register_references.insert(token.text);
      }
    if (!i.predicate.empty()) {
      auto predicate =
          i.predicate[0] == '!' ? i.predicate.substr(1) : i.predicate;
      f.unique_register_references.insert(predicate);
      ++f.register_reference_occurrences;
    }
    const bool terminator = i.family == "bra" || i.family == "brx" ||
                            i.family == "ret" || i.family == "exit" ||
                            i.family == "trap";
    if (terminator && n + 1 < f.instructions.size())
      leaders.insert(n + 1);
    if (i.family == "bra") {
      auto label = f.labels.find(i.branch_target);
      if (label == f.labels.end())
        throw std::runtime_error("unresolved direct branch label: " +
                                 i.branch_target + " in " + f.name);
      leaders.insert(label->second);
    }
    if (i.family == "brx")
      ++f.unresolved_control_flow;
    if (i.family == "ld" || i.family == "ldu") {
      std::string address;
      bool inside = false;
      for (const auto &token : i.operands) {
        if (token.text == "[")
          inside = true;
        if (inside)
          address += token.text;
        if (token.text == "]")
          inside = false;
      }
      if (!address.empty()) {
        auto key = i.opcode + ":" + address;
        auto found = loads.find(key);
        if (found != loads.end() && warned.insert(key).second)
          f.observations.push_back(
              {"repeated_syntactic_load",
               "same opcode/address " + key + "; first at line " +
                   std::to_string(found->second.second),
               "Repeated syntax only; intervening writes, aliases, and thread "
               "execution can change the value. No load-elimination proof.",
               "low", n, i.line});
        else if (found == loads.end())
          loads[key] = {n, i.line};
      }
    }
    if (i.family == "mov" && i.operands.size() == 3 &&
        i.operands[1].text == "," && i.operands[0].text == i.operands[2].text &&
        is_register(i.operands[0].text, f))
      f.observations.push_back(
          {"self_move",
           "identical scalar source/destination " + i.operands[0].text,
           "Candidate redundant scalar self-move; no transformation is "
           "performed and no hardware timing claim follows.",
           "high for syntactic identity", n, i.line});
  }
  std::vector<std::size_t> starts(leaders.begin(), leaders.end());
  for (std::size_t b = 0; b < starts.size(); ++b)
    f.blocks.push_back(
        {starts[b],
         b + 1 < starts.size() ? starts[b + 1] : f.instructions.size(),
         {}});
  auto owner = [&](std::size_t pos) {
    return static_cast<std::size_t>(
        std::upper_bound(starts.begin(), starts.end(), pos) - starts.begin() -
        1);
  };
  for (std::size_t b = 0; b < f.blocks.size(); ++b) {
    auto &block = f.blocks[b];
    if (block.begin == block.end)
      continue;
    const auto &last = f.instructions[block.end - 1];
    if (last.family == "bra")
      block.successors.push_back(owner(f.labels.at(last.branch_target)));
    const bool terminal = last.family == "bra" || last.family == "brx" ||
                          last.family == "ret" || last.family == "exit" ||
                          last.family == "trap";
    if ((!terminal || !last.predicate.empty()) && b + 1 < f.blocks.size())
      block.successors.push_back(b + 1);
    std::sort(block.successors.begin(), block.successors.end());
    block.successors.erase(
        std::unique(block.successors.begin(), block.successors.end()),
        block.successors.end());
  }
  if (!f.instructions.empty()) {
    const auto total = f.instructions.size();
    const auto global = f.memory_operations["global"];
    if (global * 100 >= total * 30)
      f.observations.push_back(
          {"global_memory_mix",
           std::to_string(global) + "/" + std::to_string(total) +
               " instructions access global memory",
           "Static mix suggests reviewing global accesses. It does not "
           "establish bandwidth limits or coalescing.",
           "heuristic; no hardware measurements", 0, f.instructions[0].line});
    if (f.branches >= 2 && f.branches * 100 >= total * 15)
      f.observations.push_back(
          {"branch_mix",
           std::to_string(f.branches) + "/" + std::to_string(total) +
               " branch instructions",
           "Review control flow; static branches do not establish warp "
           "divergence.",
           "heuristic; runtime predicates unknown", 0, f.instructions[0].line});
    std::size_t registers = 0;
    for (const auto &[type, count] : f.registers_by_type) {
      (void)type;
      registers += count;
    }
    if (registers > 128)
      f.observations.push_back(
          {"declared_register_set",
           std::to_string(registers) + " declared scalar register variables",
           "PTX virtual declarations are not allocated hardware registers; "
           "occupancy and spilling cannot be inferred.",
           "high declaration count; hardware impact unknown", 0,
           f.instructions[0].line});
  }
  (void)memory_count;
}
class Parser {
  std::vector<Token> tokens;
  std::size_t pos = 0;
  Module module;
  bool end() const { return pos == tokens.size(); }
  bool at(const std::string &s) const {
    return !end() && tokens[pos].text == s;
  }
  Token take() {
    if (end())
      throw std::runtime_error("unexpected end of PTX");
    return tokens[pos++];
  }
  void expect(const std::string &s) {
    auto t = take();
    if (t.text != s)
      throw std::runtime_error("expected " + s + " at line " +
                               std::to_string(t.line));
  }
  std::vector<Token> statement() {
    std::vector<Token> result;
    std::vector<std::string> nesting;
    while (!end()) {
      auto t = take();
      if (t.text == ";" && nesting.empty())
        return result;
      if (t.text == "}" && nesting.empty())
        throw std::runtime_error(
            "missing semicolon before closing scope at line " +
            std::to_string(t.line));
      if (t.text == "[" || t.text == "(" || t.text == "{")
        nesting.push_back(t.text);
      if (t.text == "]" || t.text == ")" || t.text == "}") {
        const std::string want = t.text == "]"   ? "["
                                 : t.text == ")" ? "("
                                                 : "{";
        if (nesting.empty() || nesting.back() != want)
          throw std::runtime_error("mismatched operand delimiters");
        nesting.pop_back();
      }
      result.push_back(t);
    }
    throw std::runtime_error("unterminated statement");
  }
  void declarations(const std::vector<Token> &st,
                    std::vector<Declaration> &out) {
    if (st.empty())
      throw std::runtime_error("empty declaration");
    const std::set<std::string> spaces = {".reg",   ".param",  ".shared",
                                          ".local", ".global", ".const"};
    const std::set<std::string> types = {
        ".pred", ".b8",    ".b16", ".b32", ".b64",  ".b128",  ".s8",
        ".s16",  ".s32",   ".s64", ".u8",  ".u16",  ".u32",   ".u64",
        ".f16",  ".f16x2", ".f32", ".f64", ".bf16", ".bf16x2"};
    std::string space, type;
    std::size_t start = 0;
    for (std::size_t n = 0; n < st.size(); ++n) {
      if (spaces.count(st[n].text))
        space = st[n].text.substr(1);
      if (types.count(st[n].text)) {
        type = st[n].text.substr(1);
        start = n + 1;
        break;
      }
    }
    if (space.empty() || type.empty())
      throw std::runtime_error("unsupported storage declaration at line " +
                               std::to_string(st[0].line));
    for (std::size_t n = start; n < st.size();) {
      if (st[n].text == ",") {
        ++n;
        continue;
      }
      if (st[n].text == "=")
        break;
      if (!identifier(st[n].text))
        throw std::runtime_error("invalid declared name at line " +
                                 std::to_string(st[n].line));
      Declaration d;
      d.name = st[n].text;
      d.space = space;
      d.type = type;
      d.line = st[n].line;
      ++n;
      if (n < st.size() && (st[n].text == "<" || st[n].text == "[")) {
        const auto close = st[n].text == "<" ? ">" : "]";
        const bool registers = st[n].text == "<";
        ++n;
        if (registers && space != "reg")
          throw std::runtime_error("register range outside .reg");
        if (n < st.size() && st[n].text == close) {
          if (registers)
            throw std::runtime_error("empty register range");
          d.elements = 0;
        } else {
          if (n >= st.size())
            throw std::runtime_error("unterminated declaration array");
          d.elements = integer(st[n++]);
          if (d.elements == 0 || d.elements > 1000000)
            throw std::runtime_error(
                "declaration element count outside 1..1000000");
        }
        if (n >= st.size() || st[n].text != close)
          throw std::runtime_error("invalid declaration array");
        ++n;
      }
      out.push_back(d);
      if (n < st.size() && st[n].text != "," && st[n].text != "=")
        throw std::runtime_error("unsupported declaration suffix");
    }
    if (start >= st.size())
      throw std::runtime_error("missing declared name");
  }
  void parameters(Function &f) {
    expect("(");
    std::vector<Token> param;
    std::size_t depth = 0;
    while (!end()) {
      auto t = take();
      if (t.text == ")" && depth == 0) {
        if (!param.empty())
          declarations(param, f.declarations);
        return;
      }
      if (t.text == "," && depth == 0) {
        if (param.empty())
          throw std::runtime_error("empty parameter");
        declarations(param, f.declarations);
        param.clear();
        continue;
      }
      if (t.text == "[")
        ++depth;
      if (t.text == "]") {
        if (depth == 0)
          throw std::runtime_error("unmatched parameter bracket");
        --depth;
      }
      if (t.text == "{" || t.text == ";" || t.text == "(")
        throw std::runtime_error("invalid parameter list");
      param.push_back(t);
    }
    throw std::runtime_error("unterminated parameter list");
  }
  void body(Function &f, std::size_t scope_depth = 0) {
    if (scope_depth >= 128)
      throw std::runtime_error("scope nesting limit exceeded");
    expect("{");
    while (!end()) {
      if (at("}")) {
        take();
        return;
      }
      if (at("{")) {
        body(f, scope_depth + 1);
        continue;
      }
      if (at(";")) {
        take();
        continue;
      }
      if (pos + 1 < tokens.size() && tokens[pos + 1].text == ":") {
        auto label = take();
        take();
        if (!identifier(label.text) ||
            !f.labels.emplace(label.text, f.instructions.size()).second)
          throw std::runtime_error("invalid/duplicate label");
        continue;
      }
      if (at(".loc") || at(".file")) {
        skip_line();
        continue;
      }
      auto st = statement();
      if (st.empty())
        continue;
      if (st[0].text[0] == '.') {
        if (st[0].text == ".reg" || st[0].text == ".param" ||
            st[0].text == ".shared" || st[0].text == ".local" ||
            st[0].text == ".global" || st[0].text == ".const")
          declarations(st, f.declarations);
        else
          module.directives.push_back(st[0].text);
        continue;
      }
      std::size_t n = 0;
      Instruction i;
      i.line = st[0].line;
      if (st[n].text == "@") {
        ++n;
        if (n < st.size() && st[n].text == "!") {
          i.predicate = "!";
          ++n;
        }
        if (n >= st.size() || !identifier(st[n].text))
          throw std::runtime_error("invalid predicate");
        i.predicate += st[n++].text;
      }
      if (n >= st.size() || !identifier(split(st[n].text)[0]))
        throw std::runtime_error("invalid opcode");
      i.opcode = st[n++].text;
      auto parts = split(i.opcode);
      i.family = parts[0];
      i.modifiers.assign(parts.begin() + 1, parts.end());
      i.category = classify(i.opcode);
      i.operands.assign(st.begin() + static_cast<std::ptrdiff_t>(n), st.end());
      if (i.family == "ld" || i.family == "ldu" || i.family == "st" ||
          i.family == "atom" || i.family == "red") {
        i.memory_space = "generic";
        for (const auto &part : i.modifiers)
          for (const auto &space :
               {"global", "shared", "local", "param", "const"})
            if (part == space || part.rfind(std::string(space) + "::", 0) == 0)
              i.memory_space = space;
        if (std::none_of(i.operands.begin(), i.operands.end(),
                         [](const auto &t) { return t.text == "["; }))
          throw std::runtime_error(
              "memory instruction requires bracketed operand");
      }
      // Check operand boundaries independently of opcode modifier semantics.
      std::size_t groups = i.operands.empty() ? 0 : 1, depth = 0;
      for (std::size_t k = 0; k < i.operands.size(); ++k) {
        const auto &t = i.operands[k].text;
        if (t == "[" || t == "(" || t == "{")
          ++depth;
        if (t == "]" || t == ")" || t == "}")
          --depth;
        if (t == "," && depth == 0) {
          if (k == 0 || k + 1 == i.operands.size() ||
              i.operands[k - 1].text == ",")
            throw std::runtime_error("empty instruction operand");
          ++groups;
        }
        if (k > 0 && word(t[0]) && word(i.operands[k - 1].text[0]))
          throw std::runtime_error(
              "adjacent operand tokens; possible missing semicolon");
      }
      static const std::map<std::string, std::size_t> arities = {
          {"mov", 2}, {"cvt", 2},  {"cvta", 2}, {"ld", 2},  {"ldu", 2},
          {"st", 2},  {"add", 3},  {"sub", 3},  {"mul", 3}, {"mad", 4},
          {"fma", 4}, {"div", 3},  {"rem", 3},  {"and", 3}, {"or", 3},
          {"xor", 3}, {"selp", 4}, {"bra", 1},  {"ret", 0}, {"exit", 0},
          {"trap", 0}};
      auto arity = arities.find(i.family);
      if (arity != arities.end() && groups != arity->second)
        throw std::runtime_error("unsupported operand count for " + i.opcode);
      if (i.family == "bra") {
        if (i.operands.size() != 1 || !identifier(i.operands[0].text))
          throw std::runtime_error("direct branch requires one label");
        i.branch_target = i.operands[0].text;
      }
      f.instructions.push_back(std::move(i));
    }
    throw std::runtime_error("unclosed function/scope");
  }
  void skip_line() {
    auto line = take().line;
    while (!end() && tokens[pos].line == line)
      take();
  }

public:
  explicit Parser(std::vector<Token> ts) : tokens(std::move(ts)) {}
  Module run() {
    std::set<std::string> names;
    while (!end()) {
      if (at(";")) {
        take();
        continue;
      }
      if (at(".version")) {
        take();
        if (!module.version.empty())
          throw std::runtime_error("duplicate .version");
        auto value = take().text;
        auto parts = split(value);
        if (parts.size() != 2)
          throw std::runtime_error("invalid PTX version");
        for (const auto &part : parts)
          if (part.empty() ||
              part.find_first_not_of("0123456789") != std::string::npos)
            throw std::runtime_error("invalid PTX version");
        module.version = value;
        continue;
      }
      if (at(".target")) {
        take();
        if (!module.target.empty())
          throw std::runtime_error("duplicate .target");
        module.target = take().text;
        if (module.target.rfind("sm_", 0) != 0)
          throw std::runtime_error("unsupported target syntax");
        while (at(",")) {
          take();
          module.target += "," + take().text;
        }
        continue;
      }
      if (at(".address_size")) {
        take();
        if (module.address_size)
          throw std::runtime_error("duplicate .address_size");
        module.address_size = integer(take());
        if (module.address_size != 32 && module.address_size != 64)
          throw std::runtime_error("address size must be 32 or 64");
        continue;
      }
      if (at(".file") || at(".loc")) {
        skip_line();
        continue;
      }
      bool external = false;
      while (at(".visible") || at(".extern") || at(".weak")) {
        if (take().text == ".extern")
          external = true;
      }
      if (end())
        throw std::runtime_error("dangling top-level qualifier");
      if (at(".entry") || at(".func")) {
        Function f;
        f.entry = take().text == ".entry";
        f.external = external;
        if (!f.entry && at("("))
          parameters(f);
        auto name = take();
        if (!identifier(name.text) || !names.insert(name.text).second)
          throw std::runtime_error("invalid/duplicate function name");
        f.name = name.text;
        if (at("("))
          parameters(f);
        while (!end() && !at("{") && !at(";")) {
          auto t = take();
          if (t.text.empty() || t.text[0] != '.')
            throw std::runtime_error("unexpected function suffix");
          module.directives.push_back(t.text);
          while (!end() && tokens[pos].text[0] != '.' && !at("{") && !at(";"))
            take();
        }
        if (at(";")) {
          take();
          if (f.entry)
            throw std::runtime_error("entry requires body");
          f.external = true;
        } else {
          if (external)
            throw std::runtime_error("extern function cannot have body");
          body(f);
          analyze(f);
        }
        module.functions.push_back(std::move(f));
        continue;
      }
      if (at(".reg") || at(".param") || at(".global") || at(".shared") ||
          at(".local") || at(".const")) {
        declarations(statement(), module.declarations);
        continue;
      }
      throw std::runtime_error("unsupported top-level syntax at line " +
                               std::to_string(tokens[pos].line) + ": " +
                               tokens[pos].text);
    }
    if (module.version.empty() || module.target.empty() || !module.address_size)
      throw std::runtime_error("required PTX header directive missing");
    if (module.functions.empty())
      throw std::runtime_error("no entry points or functions");
    return module;
  }
};
} // namespace
std::vector<Token> tokenize(const std::string &source) {
  std::vector<Token> tokens;
  std::size_t line = 1;
  std::vector<char> balance;
  for (std::size_t n = 0; n < source.size();) {
    char c = source[n];
    if (std::isspace(static_cast<unsigned char>(c))) {
      if (c == '\n')
        ++line;
      ++n;
      continue;
    }
    if (c == '/' && n + 1 < source.size() && source[n + 1] == '/') {
      while (n < source.size() && source[n] != '\n')
        ++n;
      continue;
    }
    if (c == '/' && n + 1 < source.size() && source[n + 1] == '*') {
      n += 2;
      bool closed = false;
      while (n < source.size()) {
        if (source[n] == '\n')
          ++line;
        if (source[n] == '*' && n + 1 < source.size() && source[n + 1] == '/') {
          n += 2;
          closed = true;
          break;
        }
        ++n;
      }
      if (!closed)
        throw std::runtime_error("unterminated block comment");
      continue;
    }
    const auto start = n;
    const auto start_line = line;
    if (c == '"') {
      ++n;
      bool closed = false;
      while (n < source.size()) {
        if (source[n] == '\\') {
          n += std::min<std::size_t>(2, source.size() - n);
          continue;
        }
        if (source[n] == '"') {
          ++n;
          closed = true;
          break;
        }
        if (source[n] == '\n')
          throw std::runtime_error("newline in quoted string");
        ++n;
      }
      if (!closed)
        throw std::runtime_error("unterminated string");
    } else if (word(c)) {
      ++n;
      while (n < source.size()) {
        if (word(source[n])) {
          ++n;
          continue;
        }
        if (source[n] == ':' && n + 1 < source.size() && source[n + 1] == ':') {
          n += 2;
          continue;
        }
        break;
      }
    } else {
      if (c == '{' || c == '[' || c == '(')
        balance.push_back(c);
      if (c == '}' || c == ']' || c == ')') {
        char want = c == '}' ? '{' : c == ']' ? '[' : '(';
        if (balance.empty() || balance.back() != want)
          throw std::runtime_error("unbalanced delimiter at line " +
                                   std::to_string(line));
        balance.pop_back();
      }
      ++n;
    }
    tokens.push_back({source.substr(start, n - start), start_line});
  }
  if (!balance.empty())
    throw std::runtime_error("unclosed delimiter");
  return tokens;
}
std::string classify(const std::string &opcode) {
  auto parts = split(opcode);
  const auto &family = parts[0];
  if (family == "ld" || family == "ldu" || family == "st")
    for (const auto &part : parts)
      for (const auto &space : {"global", "shared", "local"})
        if (part == space || part.rfind(std::string(space) + "::", 0) == 0)
          return std::string(space) + "_memory";
  for (const auto &[category, families] : classes())
    if (families.count(family))
      return category;
  return "other_unknown";
}
std::set<std::string> recognized_families() {
  std::set<std::string> result;
  for (const auto &[category, families] : classes()) {
    (void)category;
    result.insert(families.begin(), families.end());
  }
  return result;
}
std::vector<std::string> category_names() {
  return {"arithmetic",    "logical",      "data_movement", "global_memory",
          "shared_memory", "local_memory", "control_flow",  "synchronization",
          "atomics",       "conversion",   "other_unknown"};
}
Module parse(const std::string &source) {
  return Parser(tokenize(source)).run();
}
std::string quote(const std::string &s) {
  std::ostringstream o;
  o << '"';
  for (unsigned char c : s) {
    if (c == '"' || c == '\\')
      o << '\\' << static_cast<char>(c);
    else if (c < 32 || c >= 127)
      o << "\\u00" << std::hex << std::setw(2) << std::setfill('0')
        << static_cast<unsigned>(c) << std::dec;
    else
      o << static_cast<char>(c);
  }
  o << '"';
  return o.str();
}
namespace {
void declaration_json(std::ostream &o, const std::vector<Declaration> &ds) {
  o << '[';
  for (std::size_t n = 0; n < ds.size(); ++n) {
    if (n)
      o << ',';
    const auto &d = ds[n];
    o << "{\"space\":" << quote(d.space) << ",\"type\":" << quote(d.type)
      << ",\"name\":" << quote(d.name) << ",\"elements\":" << d.elements
      << ",\"line\":" << d.line << '}';
  }
  o << ']';
}
void map_json(std::ostream &o,
              const std::map<std::string, std::size_t> &counts) {
  o << '{';
  bool first = true;
  for (const auto &[name, value] : counts) {
    if (!first) {
      o << ',';
    }
    first = false;
    o << quote(name) << ':' << value;
  }
  o << '}';
}
} // namespace
std::string json(const Module &m) {
  std::ostringstream o;
  o << "{\"version\":" << quote(m.version) << ",\"target\":" << quote(m.target)
    << ",\"address_size\":" << m.address_size
    << ",\"recognized_opcode_families\":" << recognized_families().size()
    << ",\"supported_categories\":" << category_names().size()
    << ",\"module_declarations\":";
  declaration_json(o, m.declarations);
  o << ",\"other_directives\":[";
  for (std::size_t n = 0; n < m.directives.size(); ++n) {
    if (n)
      o << ',';
    o << quote(m.directives[n]);
  }
  o << "],\"functions\":[";
  std::size_t total = 0, kernels = 0;
  for (std::size_t n = 0; n < m.functions.size(); ++n) {
    if (n)
      o << ',';
    const auto &f = m.functions[n];
    total += f.instructions.size();
    if (f.entry)
      ++kernels;
    o << "{\"name\":" << quote(f.name)
      << ",\"entry\":" << (f.entry ? "true" : "false")
      << ",\"external\":" << (f.external ? "true" : "false")
      << ",\"instruction_count\":" << f.instructions.size()
      << ",\"predicated_instructions\":" << f.predicated
      << ",\"branch_instructions\":" << f.branches
      << ",\"register_reference_occurrences\":"
      << f.register_reference_occurrences
      << ",\"unresolved_control_flow\":" << f.unresolved_control_flow
      << ",\"categories\":";
    map_json(o, f.categories);
    o << ",\"register_declarations_by_type\":";
    map_json(o, f.registers_by_type);
    o << ",\"memory_operations_by_space\":";
    map_json(o, f.memory_operations);
    o << ",\"unique_register_references\":[";
    bool first = true;
    for (const auto &r : f.unique_register_references) {
      if (!first) {
        o << ',';
      }
      first = false;
      o << quote(r);
    }
    o << "],\"declarations\":";
    declaration_json(o, f.declarations);
    o << ",\"instructions\":[";
    for (std::size_t j = 0; j < f.instructions.size(); ++j) {
      if (j)
        o << ',';
      const auto &i = f.instructions[j];
      o << "{\"index\":" << j << ",\"line\":" << i.line
        << ",\"opcode\":" << quote(i.opcode)
        << ",\"family\":" << quote(i.family)
        << ",\"category\":" << quote(i.category)
        << ",\"predicate\":" << quote(i.predicate)
        << ",\"memory_space\":" << quote(i.memory_space) << ",\"modifiers\":[";
      for (std::size_t k = 0; k < i.modifiers.size(); ++k) {
        if (k)
          o << ',';
        o << quote(i.modifiers[k]);
      }
      o << "],\"operands\":[";
      for (std::size_t k = 0; k < i.operands.size(); ++k) {
        if (k)
          o << ',';
        o << quote(i.operands[k].text);
      }
      o << "]}";
    }
    o << "],\"blocks\":[";
    for (std::size_t j = 0; j < f.blocks.size(); ++j) {
      if (j)
        o << ',';
      const auto &b = f.blocks[j];
      o << "{\"id\":" << j << ",\"begin\":" << b.begin << ",\"end\":" << b.end
        << ",\"successors\":[";
      for (std::size_t k = 0; k < b.successors.size(); ++k) {
        if (k)
          o << ',';
        o << b.successors[k];
      }
      o << "]}";
    }
    o << "],\"observations\":[";
    for (std::size_t j = 0; j < f.observations.size(); ++j) {
      if (j)
        o << ',';
      const auto &ob = f.observations[j];
      o << "{\"kind\":" << quote(ob.kind)
        << ",\"instruction\":" << ob.instruction << ",\"line\":" << ob.line
        << ",\"evidence\":" << quote(ob.evidence)
        << ",\"explanation\":" << quote(ob.explanation)
        << ",\"confidence\":" << quote(ob.confidence) << '}';
    }
    o << "]}";
  }
  o << "],\"total_instructions\":" << total << ",\"kernels\":" << kernels
    << '}';
  return o.str();
}
} // namespace ptxlens
