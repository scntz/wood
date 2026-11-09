package main

import (
	"fmt"
	"os"
	"strconv"
)

func run(size, limit int64) int64 {
	var inside int64
	for row := int64(0); row < size; row++ {
		ci := 2.0*float64(row)/float64(size) - 1.0
		for column := int64(0); column < size; column++ {
			cr := 2.0*float64(column)/float64(size) - 1.5
			zr, zi := 0.0, 0.0
			var step int64
			for step < limit && zr*zr+zi*zi <= 4.0 {
				nextZr := zr*zr - zi*zi + cr
				zi = 2.0*zr*zi + ci
				zr = nextZr
				step++
			}
			if step == limit {
				inside++
			}
		}
	}
	return inside
}

func main() {
	size, _ := strconv.ParseInt(os.Args[1], 10, 64)
	limit, _ := strconv.ParseInt(os.Args[2], 10, 64)
	fmt.Println(run(size, limit))
}
