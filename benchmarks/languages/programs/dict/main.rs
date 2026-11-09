use std::collections::HashMap;

fn run(n: i64, distinct: i64) -> i64 {
    let mut counts: HashMap<String, i64> = HashMap::new();
    for i in 0..n {
        let key = format!("key{}", i % distinct);
        *counts.entry(key).or_insert(0) += 1;
    }
    let mut total = 0;
    for value in counts.values() {
        total += value;
    }
    total + counts.len() as i64
}

fn main() {
    let mut arguments = std::env::args().skip(1);
    let n: i64 = arguments.next().unwrap().parse().unwrap();
    let distinct: i64 = arguments.next().unwrap().parse().unwrap();
    println!("{}", run(n, distinct));
}
