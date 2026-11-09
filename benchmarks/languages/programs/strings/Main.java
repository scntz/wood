import java.util.ArrayList;

public class Main {
    static long run(long n, long rounds) {
        long total = 0;
        for (long round = 0; round < rounds; round++) {
            ArrayList<String> parts = new ArrayList<>();
            for (long i = 0; i < n; i++) {
                parts.add(Long.toString(i * 7 + round));
            }
            String text = String.join(",", parts);
            String[] fields = text.split(",");
            for (String field : fields) {
                total += Long.parseLong(field);
            }
            total += text.length();
        }
        return total;
    }

    public static void main(String[] args) {
        System.out.println(run(Long.parseLong(args[0]), Long.parseLong(args[1])));
    }
}
