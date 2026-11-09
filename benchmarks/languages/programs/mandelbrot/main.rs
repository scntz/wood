fn run(size: i64, limit: i64) -> i64 {
    let mut inside = 0;
    for row in 0..size {
        let ci = 2.0 * row as f64 / size as f64 - 1.0;
        for column in 0..size {
            let cr = 2.0 * column as f64 / size as f64 - 1.5;
            let (mut zr, mut zi) = (0.0f64, 0.0f64);
            let mut step = 0;
            while step < limit && zr * zr + zi * zi <= 4.0 {
                let next_zr = zr * zr - zi * zi + cr;
                zi = 2.0 * zr * zi + ci;
                zr = next_zr;
                step += 1;
            }
            if step == limit {
                inside += 1;
            }
        }
    }
    inside
}

fn main() {
    let mut arguments = std::env::args().skip(1);
    let size: i64 = arguments.next().unwrap().parse().unwrap();
    let limit: i64 = arguments.next().unwrap().parse().unwrap();
    println!("{}", run(size, limit));
}
