// Allocation/capacity tradeoff probe. Compile with native_memory.hpp and runtime
// include paths; put a preserved runtime directory first for before/after runs.
#include "native_memory.hpp"
#include "wood_runtime.hpp"
int main(int argc, char** argv) {
    constexpr wd::Site site{"list_growth.cpp", 1, 1};
    if (argc != 3) return 2;
    const auto count = wd::to_int(std::string(argv[2]), site);
    if (count < 1 || count > 1000000) return 2;
    wd::List<wd::Int> values;
    std::vector<wd::Int> reference;
    const std::string mode(argv[1]);
    if (mode != "wood" && mode != "cpp") return 2;
    bench_reset();
    for (wd::Int i = 0; i < count; ++i) {
        if (mode == "wood") values.push(i, site);
        else reference.push_back(i);
    }
    const auto allocations = bench_stop();
    wd::Int checksum = 0;
    for (wd::Int i = 0; i < count; ++i)
        checksum = wd::add(checksum, mode == "wood" ? values.get(i, site) : reference.at(i), site);
    std::cout << "{\"allocations\":" << allocations << ",\"allocated_bytes\":" << bench_bytes()
              << ",\"peak_bytes\":" << bench_peak() << ",\"checksum\":" << checksum << "}\n";
}
