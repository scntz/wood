public class Main {
    static final class Body {
        double x, y, z, vx, vy, vz, mass;

        Body(double x, double y, double z, double vx, double vy, double vz, double mass) {
            this.x = x; this.y = y; this.z = z; this.vx = vx; this.vy = vy; this.vz = vz; this.mass = mass;
        }
    }

    static Body[] start() {
        return new Body[] {
            new Body(0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 39.47841760435743),
            new Body(4.84143144246472082945e+00, -1.16032004402742839362e+00, -1.03622044471123109503e-01, 6.06326392995832e-01, 2.81198684491626e+00, -2.52183616598876e-02, 3.76936749096e-02),
            new Body(8.34336671801457167004e+00, 4.12479856412430479603e+00, -4.03523417825932481042e-01, -1.01077434617879e+00, 1.82566237123041e+00, 8.41576137658415e-03, 1.12863261319e-02),
            new Body(1.28943695621391310164e+01, -1.51111514016986312525e+01, -2.23307578892655733682e-01, 1.08279100644154e+00, 8.68713018169608e-01, -1.08326374013636e-02, 1.72372405705e-03),
            new Body(1.53796971148509165498e+01, -2.59193146099879641146e+01, 1.79258772950371181758e-01, 9.79090732243897e-01, 5.94698998647676e-01, -3.47555552510468e-02, 2.03368686992e-03),
        };
    }

    static double energy(Body[] bodies) {
        double total = 0.0;
        for (int i = 0; i < bodies.length; i++) {
            total = total + 0.5 * bodies[i].mass * (bodies[i].vx * bodies[i].vx + bodies[i].vy * bodies[i].vy + bodies[i].vz * bodies[i].vz);
            for (int j = i + 1; j < bodies.length; j++) {
                double dx = bodies[i].x - bodies[j].x;
                double dy = bodies[i].y - bodies[j].y;
                double dz = bodies[i].z - bodies[j].z;
                total = total - bodies[i].mass * bodies[j].mass / Math.sqrt(dx * dx + dy * dy + dz * dz);
            }
        }
        return total;
    }

    static long run(long steps) {
        Body[] bodies = start();
        double dt = 0.01;
        int count = bodies.length;
        for (long step = 0; step < steps; step++) {
            for (int i = 0; i < count; i++) {
                for (int j = i + 1; j < count; j++) {
                    double dx = bodies[i].x - bodies[j].x;
                    double dy = bodies[i].y - bodies[j].y;
                    double dz = bodies[i].z - bodies[j].z;
                    double squared = dx * dx + dy * dy + dz * dz;
                    double magnitude = dt / (squared * Math.sqrt(squared));
                    double massI = bodies[i].mass;
                    double massJ = bodies[j].mass;
                    bodies[i].vx = bodies[i].vx - dx * massJ * magnitude;
                    bodies[i].vy = bodies[i].vy - dy * massJ * magnitude;
                    bodies[i].vz = bodies[i].vz - dz * massJ * magnitude;
                    bodies[j].vx = bodies[j].vx + dx * massI * magnitude;
                    bodies[j].vy = bodies[j].vy + dy * massI * magnitude;
                    bodies[j].vz = bodies[j].vz + dz * massI * magnitude;
                }
            }
            for (int i = 0; i < count; i++) {
                bodies[i].x = bodies[i].x + dt * bodies[i].vx;
                bodies[i].y = bodies[i].y + dt * bodies[i].vy;
                bodies[i].z = bodies[i].z + dt * bodies[i].vz;
            }
        }
        return (long) (energy(bodies) * 1000000000.0);
    }

    public static void main(String[] args) {
        System.out.println(run(Long.parseLong(args[0])));
    }
}
