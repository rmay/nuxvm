/* Fluxio "hello world" -- console variant.
 *
 * This is NOT a windowed Cloister app. Fluxio v1 has no string literals and
 * no way to reach Cloister's SCI syscall surface (window creation, DRAW::*,
 * APP::*) yet -- that machinery is Lux-only for now (see apps/Hello.lux for
 * the real windowed version, and docs/fluxio-language-plan.md for what a
 * Fluxio v2 would need to add: string literals + SCI/VFS builtin bindings).
 *
 * This prints "Hello, World!\n" to the console one character at a time via
 * emit(), the same way the Lux tutorial does it before string support, and
 * runs directly under bin/nux (no Cloister host needed):
 *
 *   bin/fluxioc -target headless -o examples/fluxio/hello_console.bin examples/fluxio/hello_console.fx
 *   bin/nux examples/fluxio/hello_console.bin
 */

version 399000;

/** prints "Hello, World!" followed by a newline, one ASCII code at a time */
int say_hello() {
    emit(72);  /* H */
    emit(101); /* e */
    emit(108); /* l */
    emit(108); /* l */
    emit(111); /* o */
    emit(44);  /* , */
    emit(32);  /* space */
    emit(87);  /* W */
    emit(111); /* o */
    emit(114); /* r */
    emit(108); /* l */
    emit(100); /* d */
    emit(33);  /* ! */
    emit(10);  /* newline */
    return 0;
}

/** entry point */
int main() {
    say_hello();
    return 0;
}
