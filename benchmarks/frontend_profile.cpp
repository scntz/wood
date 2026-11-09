// Profile exposed frontend stages without counting input/setup or report output.
#include "native_memory.hpp"
#include "wood/compiler.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
int main(int argc, char** argv) {
    if (argc != 3)
        return 2;
    const std::string mode = argv[1];
    const std::string path = argv[2];
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return 2;
    const std::string source{std::istreambuf_iterator<char>(input), {}};
    std::vector<wood::Token> tokens;
    wood::Program program;
    std::string generated;
    if (mode != "lex" && mode != "all")
        tokens = wood::lex(source);
    if (mode == "analyze" || mode == "generate")
        program = wood::parse(tokens);
    if (mode == "generate")
        wood::analyze(program);
    bench_reset();
    const auto start = std::chrono::steady_clock::now();
    if (mode == "all")
        generated = wood::compile(source, path);
    else if (mode == "lex")
        tokens = wood::lex(source);
    else if (mode == "parse")
        program = wood::parse(tokens);
    else if (mode == "analyze")
        wood::analyze(program);
    else if (mode == "generate")
        generated = wood::generate(program, path);
    else
        return 2;
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                             std::chrono::steady_clock::now() - start)
                             .count();
    const auto allocations = bench_stop();
    std::cout << "{\"elapsed_ns\":" << elapsed << ",\"allocations\":" << allocations
              << ",\"allocated_bytes\":" << bench_bytes() << ",\"peak_bytes\":" << bench_peak()
              << ",\"process_peak_bytes\":" << bench_rss() << ",\"cpu_mask\":" << bench_cpu_mask()
              << ",\"output_bytes\":" << generated.size() << "}\n";
}
