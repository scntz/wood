#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
struct Body { double x, y, z, vx, vy, vz, mass; };
static std::vector<Body> start() {
    return {
        {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 39.47841760435743},
        {4.84143144246472082945e+00, -1.16032004402742839362e+00, -1.03622044471123109503e-01, 6.06326392995832e-01, 2.81198684491626e+00, -2.52183616598876e-02, 3.76936749096e-02},
        {8.34336671801457167004e+00, 4.12479856412430479603e+00, -4.03523417825932481042e-01, -1.01077434617879e+00, 1.82566237123041e+00, 8.41576137658415e-03, 1.12863261319e-02},
        {1.28943695621391310164e+01, -1.51111514016986312525e+01, -2.23307578892655733682e-01, 1.08279100644154e+00, 8.68713018169608e-01, -1.08326374013636e-02, 1.72372405705e-03},
        {1.53796971148509165498e+01, -2.59193146099879641146e+01, 1.79258772950371181758e-01, 9.79090732243897e-01, 5.94698998647676e-01, -3.47555552510468e-02, 2.03368686992e-03},
    };
}
static double energy(const std::vector<Body>& bodies) {
    double total = 0.0;
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        total = total + 0.5 * bodies[i].mass * (bodies[i].vx * bodies[i].vx + bodies[i].vy * bodies[i].vy + bodies[i].vz * bodies[i].vz);
        for (std::size_t j = i + 1; j < bodies.size(); ++j) {
            const double dx = bodies[i].x - bodies[j].x;
            const double dy = bodies[i].y - bodies[j].y;
            const double dz = bodies[i].z - bodies[j].z;
            total = total - bodies[i].mass * bodies[j].mass / std::sqrt(dx * dx + dy * dy + dz * dz);
        }
    }
    return total;
}
static std::int64_t run(std::int64_t steps) {
    std::vector<Body> bodies = start();
    const double dt = 0.01;
    const std::size_t count = bodies.size();
    for (std::int64_t step = 0; step < steps; ++step) {
        for (std::size_t i = 0; i < count; ++i) {
            for (std::size_t j = i + 1; j < count; ++j) {
                const double dx = bodies[i].x - bodies[j].x;
                const double dy = bodies[i].y - bodies[j].y;
                const double dz = bodies[i].z - bodies[j].z;
                const double squared = dx * dx + dy * dy + dz * dz;
                const double magnitude = dt / (squared * std::sqrt(squared));
                const double mass_i = bodies[i].mass;
                const double mass_j = bodies[j].mass;
                bodies[i].vx = bodies[i].vx - dx * mass_j * magnitude;
                bodies[i].vy = bodies[i].vy - dy * mass_j * magnitude;
                bodies[i].vz = bodies[i].vz - dz * mass_j * magnitude;
                bodies[j].vx = bodies[j].vx + dx * mass_i * magnitude;
                bodies[j].vy = bodies[j].vy + dy * mass_i * magnitude;
                bodies[j].vz = bodies[j].vz + dz * mass_i * magnitude;
            }
        }
        for (std::size_t i = 0; i < count; ++i) {
            bodies[i].x = bodies[i].x + dt * bodies[i].vx;
            bodies[i].y = bodies[i].y + dt * bodies[i].vy;
            bodies[i].z = bodies[i].z + dt * bodies[i].vz;
        }
    }
    return static_cast<std::int64_t>(energy(bodies) * 1000000000.0);
}
int main(int, char** argv) {
    std::printf("%lld\n", static_cast<long long>(run(std::atoll(argv[1]))));
    return 0;
}
