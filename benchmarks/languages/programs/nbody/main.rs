#[derive(Clone, Copy)]
struct Body {
    x: f64,
    y: f64,
    z: f64,
    vx: f64,
    vy: f64,
    vz: f64,
    mass: f64,
}

fn body(x: f64, y: f64, z: f64, vx: f64, vy: f64, vz: f64, mass: f64) -> Body {
    Body { x, y, z, vx, vy, vz, mass }
}

fn start() -> Vec<Body> {
    vec![
        body(0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 39.47841760435743),
        body(4.84143144246472082945e+00, -1.16032004402742839362e+00, -1.03622044471123109503e-01, 6.06326392995832e-01, 2.81198684491626e+00, -2.52183616598876e-02, 3.76936749096e-02),
        body(8.34336671801457167004e+00, 4.12479856412430479603e+00, -4.03523417825932481042e-01, -1.01077434617879e+00, 1.82566237123041e+00, 8.41576137658415e-03, 1.12863261319e-02),
        body(1.28943695621391310164e+01, -1.51111514016986312525e+01, -2.23307578892655733682e-01, 1.08279100644154e+00, 8.68713018169608e-01, -1.08326374013636e-02, 1.72372405705e-03),
        body(1.53796971148509165498e+01, -2.59193146099879641146e+01, 1.79258772950371181758e-01, 9.79090732243897e-01, 5.94698998647676e-01, -3.47555552510468e-02, 2.03368686992e-03),
    ]
}

fn energy(bodies: &[Body]) -> f64 {
    let mut total = 0.0;
    for i in 0..bodies.len() {
        total = total + 0.5 * bodies[i].mass * (bodies[i].vx * bodies[i].vx + bodies[i].vy * bodies[i].vy + bodies[i].vz * bodies[i].vz);
        for j in (i + 1)..bodies.len() {
            let dx = bodies[i].x - bodies[j].x;
            let dy = bodies[i].y - bodies[j].y;
            let dz = bodies[i].z - bodies[j].z;
            total = total - bodies[i].mass * bodies[j].mass / (dx * dx + dy * dy + dz * dz).sqrt();
        }
    }
    total
}

fn run(steps: i64) -> i64 {
    let mut bodies = start();
    let dt = 0.01;
    let count = bodies.len();
    for _ in 0..steps {
        for i in 0..count {
            for j in (i + 1)..count {
                let dx = bodies[i].x - bodies[j].x;
                let dy = bodies[i].y - bodies[j].y;
                let dz = bodies[i].z - bodies[j].z;
                let squared = dx * dx + dy * dy + dz * dz;
                let magnitude = dt / (squared * squared.sqrt());
                let mass_i = bodies[i].mass;
                let mass_j = bodies[j].mass;
                bodies[i].vx = bodies[i].vx - dx * mass_j * magnitude;
                bodies[i].vy = bodies[i].vy - dy * mass_j * magnitude;
                bodies[i].vz = bodies[i].vz - dz * mass_j * magnitude;
                bodies[j].vx = bodies[j].vx + dx * mass_i * magnitude;
                bodies[j].vy = bodies[j].vy + dy * mass_i * magnitude;
                bodies[j].vz = bodies[j].vz + dz * mass_i * magnitude;
            }
        }
        for i in 0..count {
            bodies[i].x = bodies[i].x + dt * bodies[i].vx;
            bodies[i].y = bodies[i].y + dt * bodies[i].vy;
            bodies[i].z = bodies[i].z + dt * bodies[i].vz;
        }
    }
    (energy(&bodies) * 1000000000.0) as i64
}

fn main() {
    let steps: i64 = std::env::args().nth(1).unwrap().parse().unwrap();
    println!("{}", run(steps));
}
