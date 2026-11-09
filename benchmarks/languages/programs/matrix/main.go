package main

import (
	"fmt"
	"os"
	"strconv"
)

func fill(n int, seed float64) []float64 {
	values := make([]float64, n*n)
	for i := 0; i < n; i++ {
		for j := 0; j < n; j++ {
			values[i*n+j] = float64((i*7+j*3)%11) * seed
		}
	}
	return values
}

func run(n int) int64 {
	left := fill(n, 0.5)
	right := fill(n, 0.25)
	product := make([]float64, n*n)
	for i := 0; i < n; i++ {
		for k := 0; k < n; k++ {
			factor := left[i*n+k]
			for j := 0; j < n; j++ {
				product[i*n+j] = product[i*n+j] + factor*right[k*n+j]
			}
		}
	}
	trace := 0.0
	for i := 0; i < n; i++ {
		trace = trace + product[i*n+i]
	}
	return int64(trace)
}

func main() {
	n, _ := strconv.Atoi(os.Args[1])
	fmt.Println(run(n))
}
