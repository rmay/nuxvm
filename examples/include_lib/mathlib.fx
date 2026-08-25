/* Small library file, included by examples/include_demo.fx to show how a
 * Fluxio program splits across files. Paths in `include "...";` resolve
 * relative to the file that contains the directive. */

/** returns the greater of a and b */
int fx_max(int a, int b) {
    if (a > b) {
        return a;
    }
    return b;
}

/** iterative factorial */
int fx_factorial(int n) {
    int result = 1;
    for (int i = 2; i <= n; i = i + 1) {
        result = result * i;
    }
    return result;
}
