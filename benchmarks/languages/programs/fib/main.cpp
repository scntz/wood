#include <cstdint>
#include <cstdio>
#include <cstdlib>
static std::int64_t fib(std::int64_t n) {
    if (n < 2) return n;
    return fib(n - 1) + fib(n - 2);
}
int main(int, char** argv) {
    std::printf("%lld\n", static_cast<long long>(fib(std::atoll(argv[1]))));
    return 0;
}
