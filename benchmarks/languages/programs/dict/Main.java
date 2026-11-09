import java.util.HashMap;

public class Main {
    static long run(long n, long distinct) {
        HashMap<String, Long> counts = new HashMap<>();
        for (long i = 0; i < n; i++) {
            String key = "key" + (i % distinct);
            counts.merge(key, 1L, Long::sum);
        }
        long total = 0;
        for (long value : counts.values()) {
            total += value;
        }
        return total + counts.size();
    }

    public static void main(String[] args) {
        System.out.println(run(Long.parseLong(args[0]), Long.parseLong(args[1])));
    }
}
