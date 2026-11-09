public class Main {
    static long run(int n) {
        long[] values = new long[n];
        long x = 12345;
        for (int i = 0; i < n; i++) {
            x = (x * 1664525L + 1013904223L) % 4294967296L;
            values[i] = x;
        }
        int[] stack = new int[4096];
        int top = 0;
        stack[top++] = 0;
        stack[top++] = n - 1;
        while (top > 0) {
            int high = stack[--top];
            int low = stack[--top];
            if (low < high) {
                long pivot = values[(low + high) / 2];
                int i = low, j = high;
                while (i <= j) {
                    while (values[i] < pivot) i++;
                    while (values[j] > pivot) j--;
                    if (i <= j) {
                        long swap = values[i];
                        values[i] = values[j];
                        values[j] = swap;
                        i++;
                        j--;
                    }
                }
                stack[top++] = low;
                stack[top++] = j;
                stack[top++] = i;
                stack[top++] = high;
            }
        }
        long checksum = 0;
        for (int i = 0; i < n; i++) {
            checksum = (checksum + values[i] * (i % 7 + 1)) % 1000000007L;
        }
        return checksum;
    }

    public static void main(String[] args) {
        System.out.println(run(Integer.parseInt(args[0])));
    }
}
