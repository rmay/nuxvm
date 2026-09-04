/* Fluxio multi-file demo. Fluxio has no module/namespace keyword -- it's a
 * single flat global namespace, same as the rest of the language, so
 * cross-file name collisions are caught by the ordinary "already defined"
 * check. The convention is descriptive prefixed names (fx_max, fx_factorial
 * below), same as C/embedded-C practice.
 *
 *   bin/fluxioc -target headless -o examples/fluxio/include_demo.bin examples/fluxio/include_demo.fx
 *   bin/nux examples/fluxio/include_demo.bin
 */

version 399000;

include "include_lib/mathlib.fx";

/** entry point */
int main() {
    int m = fx_max(17, 42);
    int f = fx_factorial(5);
    return m + f; /* 42 + 120 = 162 */
}
