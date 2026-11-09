fn run(n: i64) -> i64 {
    let mut values: Vec<i64> = Vec::new();
    let mut x: i64 = 12345;
    for _ in 0..n {
        x = (x * 1664525 + 1013904223) % 4294967296;
        values.push(x);
    }
    let mut stack: Vec<i64> = vec![0, n - 1];
    while let Some(high) = stack.pop() {
        let low = stack.pop().unwrap();
        if low < high {
            let pivot = values[((low + high) / 2) as usize];
            let (mut i, mut j) = (low, high);
            while i <= j {
                while values[i as usize] < pivot {
                    i += 1;
                }
                while values[j as usize] > pivot {
                    j -= 1;
                }
                if i <= j {
                    values.swap(i as usize, j as usize);
                    i += 1;
                    j -= 1;
                }
            }
            stack.push(low);
            stack.push(j);
            stack.push(i);
            stack.push(high);
        }
    }
    let mut checksum = 0;
    for i in 0..n {
        checksum = (checksum + values[i as usize] * (i % 7 + 1)) % 1000000007;
    }
    checksum
}

fn main() {
    let n: i64 = std::env::args().nth(1).unwrap().parse().unwrap();
    println!("{}", run(n));
}
