package main

import (
	"fmt"
	"os"
	"strconv"
)

func run(n int64) int64 {
	var values []int64
	x := int64(12345)
	for i := int64(0); i < n; i++ {
		x = (x*1664525 + 1013904223) % 4294967296
		values = append(values, x)
	}
	stack := []int64{0, n - 1}
	for len(stack) > 0 {
		high := stack[len(stack)-1]
		low := stack[len(stack)-2]
		stack = stack[:len(stack)-2]
		if low < high {
			pivot := values[(low+high)/2]
			i, j := low, high
			for i <= j {
				for values[i] < pivot {
					i++
				}
				for values[j] > pivot {
					j--
				}
				if i <= j {
					values[i], values[j] = values[j], values[i]
					i++
					j--
				}
			}
			stack = append(stack, low, j, i, high)
		}
	}
	var checksum int64
	for i := int64(0); i < n; i++ {
		checksum = (checksum + values[i]*(i%7+1)) % 1000000007
	}
	return checksum
}

func main() {
	n, _ := strconv.ParseInt(os.Args[1], 10, 64)
	fmt.Println(run(n))
}
