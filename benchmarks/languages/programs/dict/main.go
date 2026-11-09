package main

import (
	"fmt"
	"os"
	"strconv"
)

func run(n, distinct int64) int64 {
	counts := map[string]int64{}
	for i := int64(0); i < n; i++ {
		key := "key" + strconv.FormatInt(i%distinct, 10)
		counts[key]++
	}
	var total int64
	for _, value := range counts {
		total += value
	}
	return total + int64(len(counts))
}

func main() {
	n, _ := strconv.ParseInt(os.Args[1], 10, 64)
	distinct, _ := strconv.ParseInt(os.Args[2], 10, 64)
	fmt.Println(run(n, distinct))
}
