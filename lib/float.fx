/* Fixed-point float library for Fluxio. Fluxio has no float hardware, no
 * operator overloading, and structs can never be returned by value -- so a
 * "float" here is a plain user-level struct of two ints, and arithmetic is
 * exposed as named functions that mutate an `out` struct parameter through
 * the same reference-passing mechanism `translate(Point p, ...)` already
 * uses (see examples/fluxio/struct_demo.fx). Every argument passed to these
 * functions (including `out`) must itself be a global Float or an incoming
 * struct parameter -- a local Float instance has no stable address and
 * can't be passed to a function at all (same VM constraint as local
 * structs/arrays generally).
 *
 * Representation: value = whole + frac/float_scale, with |frac| < float_scale
 * and sign(frac) matching sign(whole) whenever whole != 0 (frac may be
 * negative only when whole == 0). This falls directly out of the VM's
 * native truncating DIV/MOD (src/vm.c), so normalizing a combined value is
 * just `whole = total / float_scale; frac = total % float_scale;`.
 *
 * Known limitation: multiply/divide compute an intermediate "total" scaled
 * value (whole*float_scale) using plain 32-bit int ops with no overflow
 * detection -- same as every other arithmetic op on this VM (no opcode
 * traps on overflow anywhere). Safe for magnitudes roughly within +/-4.0
 * (so total*total stays under 2^31); larger values silently wrap, same
 * as any other int overflow in Fluxio.
 */

int float_scale = 10000;

/** value = whole + frac/float_scale */
struct Float {
    int whole;
    int frac;
}

/** internal: collapses a Float into a single scale-float_scale integer */
int fx_float_total(Float f) {
    return f.whole * float_scale + f.frac;
}

/** internal: splits a scale-float_scale integer back into out.whole/out.frac */
int fx_float_from_total(Float out, int total) {
    out.whole = total / float_scale;
    out.frac = total % float_scale;
    return 0;
}

/** out = (float)n */
int int_to_float(Float out, int n) {
    out.whole = n;
    out.frac = 0;
    return 0;
}

/** truncates f toward zero */
int float_to_int(Float f) {
    return f.whole;
}

/** out = a + b */
int float_add(Float out, Float a, Float b) {
    return fx_float_from_total(out, fx_float_total(a) + fx_float_total(b));
}

/** out = a - b */
int float_sub(Float out, Float a, Float b) {
    return fx_float_from_total(out, fx_float_total(a) - fx_float_total(b));
}

/** out = a * b */
int float_mul(Float out, Float a, Float b) {
    return fx_float_from_total(out, (fx_float_total(a) * fx_float_total(b)) / float_scale);
}

/** out = a / b */
int float_div(Float out, Float a, Float b) {
    return fx_float_from_total(out, (fx_float_total(a) * float_scale) / fx_float_total(b));
}

/** out = -a */
int float_neg(Float out, Float a) {
    return fx_float_from_total(out, -fx_float_total(a));
}

/** out = |a| */
int float_abs(Float out, Float a) {
    int t = fx_float_total(a);
    if (t < 0) {
        t = -t;
    }
    return fx_float_from_total(out, t);
}

/** returns 1 if a == b else 0 */
int float_eq(Float a, Float b) {
    if (fx_float_total(a) == fx_float_total(b)) {
        return 1;
    }
    return 0;
}

/** returns 1 if a < b else 0 */
int float_lt(Float a, Float b) {
    if (fx_float_total(a) < fx_float_total(b)) {
        return 1;
    }
    return 0;
}

/** returns 1 if a > b else 0 */
int float_gt(Float a, Float b) {
    if (fx_float_total(a) > fx_float_total(b)) {
        return 1;
    }
    return 0;
}

/** prints f as decimal (sign, whole, '.', 4-digit zero-padded fraction) via
 * the existing print(int)/emit(int) builtins */
int print_float(Float f) {
    int t = fx_float_total(f);
    int sign = 0;
    if (t < 0) {
        sign = 1;
        t = -t;
    }
    if (sign == 1) {
        emit(45); /* '-' */
    }
    print(t / float_scale);
    emit(46); /* '.' */
    int fr = t % float_scale;
    if (fr < 1000) {
        emit(48); /* '0' */
    }
    if (fr < 100) {
        emit(48);
    }
    if (fr < 10) {
        emit(48);
    }
    print(fr);
    return 0;
}
