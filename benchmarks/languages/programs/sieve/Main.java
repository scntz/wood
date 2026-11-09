public class Main {
    static long run(int limit) {
        boolean[] composite = new boolean[limit + 1];
        long count = 0;
        for (int prime = 2; prime <= limit; prime++) {
            if (!composite[prime]) {
                count++;
                for (long multiple = (long) prime * prime; multiple <= limit; multiple += prime) {
                    composite[(int) multiple] = true;
                }
            }
        }
        return count;
    }

    public static void main(String[] args) {
        System.out.println(run(Integer.parseInt(args[0])));
    }
}
