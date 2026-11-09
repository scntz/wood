// Included after the generated application with its entry point renamed.
// Counts its complete execution, including I/O, input setup and JSON output.
#ifdef _WIN32
#undef wmain
int wmain(int argc, wchar_t** argv) {
#else
#undef main
int main(int argc, char** argv) {
#endif
    bench_reset();
    const auto started = std::chrono::steady_clock::now();
    const auto status = wood_bench_application_main(argc, argv);
    std::cout.flush();
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                             std::chrono::steady_clock::now() - started)
                             .count();
    const auto allocations = bench_stop();
    std::cerr << "{\"elapsed_ns\":" << elapsed << ",\"allocations\":" << allocations
              << ",\"allocated_bytes\":" << bench_bytes() << ",\"peak_bytes\":" << bench_peak()
              << ",\"process_peak_bytes\":" << bench_rss() << ",\"cpu_mask\":" << bench_cpu_mask()
              << "}\n";
    return status;
}
