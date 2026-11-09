public class Main {
    static double[] fill(int n, double seed) {
        double[] values = new double[n * n];
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) {
                values[i * n + j] = (double) ((i * 7 + j * 3) % 11) * seed;
            }
        }
        return values;
    }

    static long run(int n) {
        double[] left = fill(n, 0.5);
        double[] right = fill(n, 0.25);
        double[] product = new double[n * n];
        for (int i = 0; i < n; i++) {
            for (int k = 0; k < n; k++) {
                double factor = left[i * n + k];
                for (int j = 0; j < n; j++) {
                    product[i * n + j] = product[i * n + j] + factor * right[k * n + j];
                }
            }
        }
        double trace = 0.0;
        for (int i = 0; i < n; i++) {
            trace = trace + product[i * n + i];
        }
        return (long) trace;
    }

    public static void main(String[] args) {
        System.out.println(run(Integer.parseInt(args[0])));
    }
}
