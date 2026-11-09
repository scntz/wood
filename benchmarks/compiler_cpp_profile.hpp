// Private allocation/work profiling, never a normal compiler build dependency.
#pragma once
#include "native_memory.hpp"
#include <cstdio>
struct WoodCompilerAllocationReport {
    ~WoodCompilerAllocationReport() {
        const auto count=bench_stop();
        std::fprintf(stderr,"{\"allocations\":%lld,\"requested_bytes\":%lld,\"peak_additional_bytes\":%lld}\n",
            static_cast<long long>(count),static_cast<long long>(bench_bytes()),static_cast<long long>(bench_peak()));
    }
};
static WoodCompilerAllocationReport wood_compiler_report;
