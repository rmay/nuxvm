version 399000;

/** fibonacci, bounded */
recursive(32) int fib(int n) {
    if (n < 2) { return n; }
    return fib(n - 1) + fib(n - 2);
}

/** entry point */
int main() {
    return fib(10);
}
