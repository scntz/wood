fn run(n: i64, rounds: i64) -> i64 {
    let mut total = 0;
    for round in 0..rounds {
        let mut parts: Vec<String> = Vec::new();
        for i in 0..n {
            parts.push((i * 7 + round).to_string());
        }
        let text = parts.join(",");
        let fields: Vec<&str> = text.split(',').collect();
        for field in &fields {
            total += field.parse::<i64>().unwrap();
        }
        total += text.len() as i64;
    }
    total
}

fn main() {
    let mut arguments = std::env::args().skip(1);
    let n: i64 = arguments.next().unwrap().parse().unwrap();
    let rounds: i64 = arguments.next().unwrap().parse().unwrap();
    println!("{}", run(n, rounds));
}
