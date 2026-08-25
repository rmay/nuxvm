/* Fluxio struct demo: UpperCamelCase type names, int-only fields, '.'
 * field access. Same reference semantics as arrays -- a global struct
 * decays to an address when passed to a function (with full field-offset
 * info preserved, since the parameter's declared type is known), so a
 * function can mutate the caller's struct through it; a local struct has
 * no stable address and can't be passed at all (compile error if tried).
 *
 *   bin/fluxioc -target headless -o examples/struct_demo.bin examples/struct_demo.fx
 *   bin/nux examples/struct_demo.bin
 */

/** a point in 2D space */
struct Point {
    int x;
    int y;
}

Point cursor;
Point origin;

/** moves a point by (dx, dy), mutating it through the reference */
int translate(Point p, int dx, int dy) {
    p.x = p.x + dx;
    p.y = p.y + dy;
    return 0;
}

/** squared distance between two points (avoids needing sqrt) */
int dist_sq(Point a, Point b) {
    int dx = a.x - b.x;
    int dy = a.y - b.y;
    return dx * dx + dy * dy;
}

/** entry point */
int main() {
    cursor.x = 0;
    cursor.y = 0;
    translate(cursor, 3, 4);

    origin.x = 0;
    origin.y = 0;

    return dist_sq(cursor, origin); /* 3*3 + 4*4 = 25 */
}
