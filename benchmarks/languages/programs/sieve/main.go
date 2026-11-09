package main

import (
	"fmt"
	"os"
	"strconv"
)

func run(limit int64) int64 {
	composite := make([]bool, limit+1)
	var count int64
	for prime := int64(2); prime <= limit; prime++ {
		if !composite[prime] {
			count++
			for multiple := prime * prime; multiple <= limit; multiple += prime {
				composite[multiple] = true
			}
		}
	}
	return count
}

func main() {
	limit, _ := strconv.ParseInt(os.Args[1], 10, 64)
	fmt.Println(run(limit))
}
