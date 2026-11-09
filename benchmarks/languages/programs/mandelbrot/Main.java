public class Main {
    static long run(long size, long limit) {
        long inside = 0;
        for (long row = 0; row < size; row++) {
            double ci = 2.0 * (double) row / (double) size - 1.0;
            for (long column = 0; column < size; column++) {
                double cr = 2.0 * (double) column / (double) size - 1.5;
                double zr = 0.0, zi = 0.0;
                long step = 0;
                while (step < limit && zr * zr + zi * zi <= 4.0) {
                    double nextZr = zr * zr - zi * zi + cr;
                    zi = 2.0 * zr * zi + ci;
                    zr = nextZr;
                    step++;
                }
                if (step == limit) inside++;
            }
        }
        return inside;
    }

    public static void main(String[] args) {
        System.out.println(run(Long.parseLong(args[0]), Long.parseLong(args[1])));
    }
}
