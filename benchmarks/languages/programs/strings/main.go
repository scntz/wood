package main

import (
	"fmt"
	"os"
	"strconv"
	"strings"
)

func run(n, rounds int64) int64 {
	var total int64
	for round := int64(0); round < rounds; round++ {
		var parts []string
		for i := int64(0); i < n; i++ {
			parts = append(parts, strconv.FormatInt(i*7+round, 10))
		}
		text := strings.Join(parts, ",")
		fields := strings.Split(text, ",")
		for _, field := range fields {
			value, _ := strconv.ParseInt(field, 10, 64)
			total += value
		}
		total += int64(len(text))
	}
	return total
}

func main() {
	n, _ := strconv.ParseInt(os.Args[1], 10, 64)
	rounds, _ := strconv.ParseInt(os.Args[2], 10, 64)
	fmt.Println(run(n, rounds))
}
