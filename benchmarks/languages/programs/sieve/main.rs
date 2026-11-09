fn run(limit: i64) -> i64 {
    let mut composite = vec![false; (limit + 1) as usize];
    let mut count = 0;
    let mut prime = 2;
    while prime <= limit {
        if !composite[prime as usize] {
            count += 1;
            let mut multiple = prime * prime;
            while multiple <= limit {
                composite[multiple as usize] = true;
                multiple += prime;
            }
        }
        prime += 1;
    }
    count
}

fn main() {
    let limit: i64 = std::env::args().nth(1).unwrap().parse().unwrap();
    println!("{}", run(limit));
}
