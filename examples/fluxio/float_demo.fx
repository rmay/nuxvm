/* Fluxio fixed-point float demo. Float is a plain library struct (see
 * lib/float.fx), not a core language type -- arithmetic goes through named
 * functions like float_add/float_mul that mutate an `out` parameter,
 * since Fluxio has no operator overloading and structs can't be returned
 * by value.
 *
 *   bin/fluxioc -target headless -o examples/fluxio/float_demo.bin examples/fluxio/float_demo.fx
 *   bin/nux examples/fluxio/float_demo.bin
 */

version 399000;

include "../../lib/float.fx";

Float a;
Float b;
Float sum;
Float product;

/** entry point */
int main() {
    int_to_float(a, 3);
    a.frac = 5000;   /* a = 3.5 */

    int_to_float(b, 2);
    /* b = 2.0 */

    float_add(sum, a, b);       /* 5.5 */
    float_mul(product, a, b);   /* 7.0 */

    print_float(a);
    emit(32); /* space */
    print_float(b);
    emit(32);
    print_float(sum);
    emit(32);
    print_float(product);
    emit(10); /* newline */

    return float_to_int(product);
}
