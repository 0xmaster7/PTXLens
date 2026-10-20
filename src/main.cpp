#include "ptxlens.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
int main(int argc, char **argv) {
  try {
    if (argc < 3 || argc > 4 || (argc == 4 && std::string(argv[3]) != "--json"))
      throw std::runtime_error(
          "usage: ptxlens analyze|stats|cfg kernel.ptx [--json]");
    const std::string command = argv[1];
    if (command != "analyze" && command != "stats" && command != "cfg" &&
        command != "benchmark")
      throw std::runtime_error("unknown command");
    std::ifstream in(argv[2], std::ios::binary | std::ios::ate);
    if (!in)
      throw std::runtime_error("cannot open PTX file");
    auto length = in.tellg();
    if (length < 0 || length > 64 * 1024 * 1024)
      throw std::runtime_error("input exceeds 64 MiB limit");
    in.seekg(0);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (in.bad())
      throw std::runtime_error("input read failed");
    const auto source = buffer.str();
    auto module = ptxlens::parse(source);
    if (command == "benchmark") {
      std::vector<double> samples;
      std::size_t observed = 0, instructions = 0;
      for (const auto &f : module.functions)
        instructions += f.instructions.size();
      for (int s = 0; s < 15; ++s) {
        auto start = std::chrono::steady_clock::now();
        for (int n = 0; n < 10; ++n) {
          auto result = ptxlens::parse(source);
          for (const auto &f : result.functions)
            observed += f.instructions.size();
        }
        samples.push_back(std::chrono::duration<double, std::micro>(
                              std::chrono::steady_clock::now() - start)
                              .count() /
                          10.0);
      }
      std::sort(samples.begin(), samples.end());
      std::cout << "{\"instructions\":" << instructions
                << ",\"bytes\":" << source.size()
                << ",\"samples\":15,\"batch_size\":10,\"latency_us_median\":"
                << samples[7] << ",\"latency_us_min\":" << samples.front()
                << ",\"latency_us_max\":" << samples.back()
                << ",\"instructions_per_second\":"
                << (static_cast<double>(instructions) * 1000000.0 / samples[7])
                << ",\"observed_instructions\":" << observed
                << ",\"latency_us_samples\":[";
      for (std::size_t n = 0; n < samples.size(); ++n) {
        if (n)
          std::cout << ',';
        std::cout << samples[n];
      }
      std::cout << "]}\n";
      return 0;
    }
    if (argc == 4) {
      std::cout << ptxlens::json(module) << '\n';
      return 0;
    }
    std::cout << "PTX " << module.version << " target=" << module.target
              << " address_size=" << module.address_size << '\n';
    for (const auto &f : module.functions) {
      std::cout << (f.entry ? "entry " : "function ") << f.name << ": "
                << f.instructions.size() << " instructions, " << f.blocks.size()
                << " blocks, " << f.predicated << " predicated, " << f.branches
                << " branches\n";
      if (command == "cfg") {
        for (std::size_t n = 0; n < f.blocks.size(); ++n) {
          const auto &b = f.blocks[n];
          std::cout << "  B" << n << " [" << b.begin << "," << b.end << ") ->";
          for (auto next : b.successors)
            std::cout << " B" << next;
          std::cout << '\n';
        }
        if (f.unresolved_control_flow)
          std::cout << "  indirect branches have unresolved edges\n";
      } else {
        for (const auto &[category, count] : f.categories)
          if (count)
            std::cout << "  " << category << ": " << count << " ("
                      << (100.0 * static_cast<double>(count) /
                          static_cast<double>(f.instructions.size()))
                      << "%)\n";
        std::cout << "  register declarations (virtual, by type):";
        for (const auto &[type, count] : f.registers_by_type)
          std::cout << ' ' << type << '=' << count;
        std::cout << "; reference occurrences="
                  << f.register_reference_occurrences << '\n';
        if (command == "analyze")
          for (const auto &ob : f.observations)
            std::cout << "  line " << ob.line << " " << ob.kind << ": "
                      << ob.evidence << ". " << ob.explanation << " ["
                      << ob.confidence << "]\n";
      }
    }
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "ptxlens: " << e.what() << '\n';
    return 1;
  }
}
