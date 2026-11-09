import math
import sys


class Body:
    __slots__ = ("x", "y", "z", "vx", "vy", "vz", "mass")

    def __init__(self, x, y, z, vx, vy, vz, mass):
        self.x = x
        self.y = y
        self.z = z
        self.vx = vx
        self.vy = vy
        self.vz = vz
        self.mass = mass


def start():
    return [
        Body(0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 39.47841760435743),
        Body(4.84143144246472082945e+00, -1.16032004402742839362e+00, -1.03622044471123109503e-01, 6.06326392995832e-01, 2.81198684491626e+00, -2.52183616598876e-02, 3.76936749096e-02),
        Body(8.34336671801457167004e+00, 4.12479856412430479603e+00, -4.03523417825932481042e-01, -1.01077434617879e+00, 1.82566237123041e+00, 8.41576137658415e-03, 1.12863261319e-02),
        Body(1.28943695621391310164e+01, -1.51111514016986312525e+01, -2.23307578892655733682e-01, 1.08279100644154e+00, 8.68713018169608e-01, -1.08326374013636e-02, 1.72372405705e-03),
        Body(1.53796971148509165498e+01, -2.59193146099879641146e+01, 1.79258772950371181758e-01, 9.79090732243897e-01, 5.94698998647676e-01, -3.47555552510468e-02, 2.03368686992e-03),
    ]


def energy(bodies):
    total = 0.0
    for i in range(len(bodies)):
        total = total + 0.5 * bodies[i].mass * (bodies[i].vx * bodies[i].vx + bodies[i].vy * bodies[i].vy + bodies[i].vz * bodies[i].vz)
        for j in range(i + 1, len(bodies)):
            dx = bodies[i].x - bodies[j].x
            dy = bodies[i].y - bodies[j].y
            dz = bodies[i].z - bodies[j].z
            total = total - bodies[i].mass * bodies[j].mass / math.sqrt(dx * dx + dy * dy + dz * dz)
    return total


def run(steps):
    bodies = start()
    dt = 0.01
    count = len(bodies)
    for step in range(steps):
        for i in range(count):
            for j in range(i + 1, count):
                dx = bodies[i].x - bodies[j].x
                dy = bodies[i].y - bodies[j].y
                dz = bodies[i].z - bodies[j].z
                squared = dx * dx + dy * dy + dz * dz
                magnitude = dt / (squared * math.sqrt(squared))
                mass_i = bodies[i].mass
                mass_j = bodies[j].mass
                bodies[i].vx = bodies[i].vx - dx * mass_j * magnitude
                bodies[i].vy = bodies[i].vy - dy * mass_j * magnitude
                bodies[i].vz = bodies[i].vz - dz * mass_j * magnitude
                bodies[j].vx = bodies[j].vx + dx * mass_i * magnitude
                bodies[j].vy = bodies[j].vy + dy * mass_i * magnitude
                bodies[j].vz = bodies[j].vz + dz * mass_i * magnitude
        for i in range(count):
            bodies[i].x = bodies[i].x + dt * bodies[i].vx
            bodies[i].y = bodies[i].y + dt * bodies[i].vy
            bodies[i].z = bodies[i].z + dt * bodies[i].vz
    return int(energy(bodies) * 1000000000.0)


print(run(int(sys.argv[1])))
