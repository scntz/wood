fn fill(n: usize, seed: f64) -> Vec<f64> {
    let mut values = vec![0.0; n * n];
    for i in 0..n {
        for j in 0..n {
            values[i * n + j] = ((i * 7 + j * 3) % 11) as f64 * seed;
        }
    }
    values
}

fn run(n: usize) -> i64 {
    let left = fill(n, 0.5);
    let right = fill(n, 0.25);
    let mut product = vec![0.0; n * n];
    for i in 0..n {
        for k in 0..n {
            let factor = left[i * n + k];
            for j in 0..n {
                product[i * n + j] = product[i * n + j] + factor * right[k * n + j];
            }
        }
    }
    let mut trace = 0.0;
    for i in 0..n {
        trace = trace + product[i * n + i];
    }
    trace as i64
}

fn main() {
    let n: usize = std::env::args().nth(1).unwrap().parse().unwrap();
    println!("{}", run(n));
}
