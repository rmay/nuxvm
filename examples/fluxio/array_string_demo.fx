/* Fluxio v2 demo: fixed-size arrays, string-literal array initializers,
 * bounds-checked indexing, and array parameters (global arrays only --
 * see docs/fluxio-language-plan.md for why local arrays can't decay to
 * a passable address).
 *
 *   bin/fluxioc -target headless -o examples/fluxio/array_string_demo.bin examples/fluxio/array_string_demo.fx
 *   bin/nux examples/fluxio/array_string_demo.bin
 */

version 400000;

int greeting[] = "Hello, Fluxio!";

int numbers[10];

/** prints every character of a global char array up to (not including) len */
int print_str(int s[], int len) {
    for (int i = 0; i < len; i = i + 1) {
        emit(s[i]);
    }
    return 0;
}

/** sums the first n elements of a global int array */
int sum(int a[], int n) {
    int total = 0;
    for (int i = 0; i < n; i = i + 1) {
        total = total + a[i];
    }
    return total;
}

/** entry point */
int main() {
    print_str(greeting, 14);
    emit(10); /* newline */

    for (int i = 0; i < 10; i = i + 1) {
        numbers[i] = i * i;
    }

    print(sum(numbers, 10)); /* 0+1+4+...+81 = 285 */
    emit(10);

    return 0;
}
