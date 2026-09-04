# Lux Tutorial
## A Crash Course
### For those who want to see how things work.

- [Numbers](#numbers)
- [Strings](#strings)
- [Comments](#comments)
- [Stack Manipulation](#stack-manipulation)
- [Bitwise Operations](#bitwise-operations)
- [Comparisons](#comparisons)
- [Output](#output)
- [User-defined Words](#user-defined-words)
- [Named locals](#named-locals)
- [Conditionals and Loops](#conditionals-and-loops)
- [Recursion](#recursion)
- [Modules](#modules)
- [Compiling Lux Source](#compiling-lux-source)
- [Error Handling](#error-handling)
- [Finally!](#finally)
- [Coming At Some Point!](#coming-at-some-point)

Lux is the companion language to the Nux VM.

Lux is heavily inspired by Forth, with nods to Factor, Joy, and a smattering of other sources. It's just a rumor I copied their homework.

It's a [concatenative language](https://concatenative.org/wiki/view/Concatenative%20language), Reverse Polish Notation (postfix, for those keeping score at home) style. 

If you want to follow along, this is easy to compile. Install Go, at least version 1.25, check out out this git repo and run `make buildall` from the root directory.

If all goes well, you'll have `nux`, `luxc`, and `luxrepl`. For this tutorial, we'll be using `luxrepl`.

I also suggest installing [`rlwrap`](https://github.com/hanslub42/rlwrap) for your environment.

Alright, I'm going to plow ahead.

From the command line, enter

> $ rlwrap ./luxrepl

```
╔═══════════════════════════════╗
║       LUX REPL 280K           ║
║  Stack-based Language REPL    ║
╚═══════════════════════════════╝

Type 'help' for commands, 'exit' to quit

lux>
```

The `280k` is the current version. I'm using the Kelvin versioning system as defined here: https://jtobin.io/kelvin-versioning because we can't make this too easy.

Back to the tutorial!

From the `lux> ` prompt we are going to look at some basics.

Lux is a stack-based language, a FILO "First In, Last Out" stack.  We add things to the stack and can use operations or manipulate the stack itself.


### Numbers
Type `5` and hit enter.

```forth
lux> 5
  Stack: [5]
```

The REPL returns the stack after a successful command. You can also see what's on the stack at any time with `.s`

Now, let's push a `6` on the stack.

```forth
lux> 6
  Stack: [5 6]
```

Great! We now have two items on our stack, `5` being the last element with `6` being the top.

Now we are going to pass in an operator to add the two elements.

```forth
lux> +
  Stack: [11]
```

What just happened? Why does the stack only have `11`?

The `+` told the REPL to take the top two values off the stack, add them together, and push the result on the stack.

We could do the same thing in a single line.

```forth
lux> cs.                        // Clear the stack
Stack cleared
lux> .s                         // Show it's empty
  Stack: []
lux> 5 6 +                      // RPN
  Stack: [11]
```

We can do more than just add.

```forth
lux> 5 6 /                    // Whole integers only
  Stack: [11 0]
lux> 5 6 *
  Stack: [11 0 30]
lux> 5 6 -
  Stack: [11 0 30 -1]
lux> 5 6 MOD                  // Modulous
  Stack: [11 0 30 -1 5]
lux> 5 INC
  Stack: [11 0 30 -1 5 6]
lux> 5 DEC
  Stack: [11 0 30 -1 5 6 4]
lux> 5 NEGATE
  Stack: [11 0 30 -1 5 6 4 -5]
```

Numbers can be in ye olde standard decimal or hexadecimal, which is sure to impress at any party.

```forth
lux> cs
Stack cleared
lux> 10
  Stack: [10]
lux> 0x0A
  Stack: [10 10]
```

Hexadecimals start with `0x`.



### Strings
Let's try strings!

```forth
lux> cs
Stack cleared
lux> "Hello, World\n"
Hello, World
  Stack: []
```

As you can see, REPL strings skip the stack.


### Comments

Lux supports two types of comments.

The standard `( ... )` for Forth, and `//`.

The parens handle multiple line comments, the `//` single line.

### Stack manipulation

```forth
dup           ( Duplicate top: a → a a )
drop          ( Remove top: a → )
swap          ( Swap top two: a b → b a )
over          ( Copy second: a b → a b a )
rot           ( Rotate three: a b c → b c a )
```

Let's run through these real quick.

```forth
lux> 5
  Stack: [5]
lux> dup
  Stack: [5 5]
lux> drop
  Stack: [5]
lux> 10
  Stack: [5 10]
lux> swap
  Stack: [10 5]
lux> over
  Stack: [10 5 10]
lux> rot
  Stack: [5 10 10]
```

No surprises there, if you've manipulated stacks in the past.

### Bitwise Operations

```forth
and or xor    ( Bitwise AND, OR, XOR )
not           ( Bitwise NOT )
lshift        ( Left shift )
```

### Comparisons

```forth
=             ( Equal )
<             ( Less than )
>             ( Greater than )
!=            ( Not equal )
```

Results: `1` for true, `0` for false. Unlike Forth, anything non-zero is true.

Is `5` greater than `6`?
```forth
lux> 5 6 >
  Stack: [0]
```
No, no it's not.

But wait! Is `5` less than `6`?
```forth
lux> 5 6 <
  Stack: [1]
```
Whew! It is.

### Output

We can use `.` for numbers.

```forth
lux> 1 . 2 . 3
12  Stack: [3]
```

Stepping through this, `1` is pushed on the stack, `.` pops it off and prints it to the console. The same thing happens for `2`. The last number, `3`, is left on the stack, no `.` to pop it off.

`emit` is used to output numbers as ascii characters. Just like `.` prints the number at the top of the stack, `emit` outputs that number as an ascii character.

```forth
lux> 72 emit 101 emit 108 emit 108 emit 111 emit 44 emit 32 emit 87 emit 111 emit 114 emit 108 emit 100 emit
Hello, World  Stack: []
```

That's a lot of numbers to enter. And let's say your "Hello, World" app says "Hello, World" a lot. You'd have to enter this many time. So many times. Like Sisyphus, you boasted of your skills, and now you have a boulder as your eternal companion.

Lux has a solution for this!

### User-defined words

You can define a word like so: `@WORD (BODY) ;`

*Make sure you have a space right before the semicolon, for your peace of mind and ours.*

```forth
lux> @square dup * ;
Defined word 'square'
lux> 5 square
  Stack: [25]
```

This looks promising.

```forth
lux> @hello 72 emit 101 emit 108 emit 108 emit 111 emit 44 emit 32 emit 87 emit 111 emit 114 emit 108 emit 100 emit ;
Defined word 'hello'
lux> hello
Hello, World  Stack: [25]
```

Nice! No need to type all those numbers and emits.

You can also use symbols like `!` or `@` in your word names (as long as they don't start with `@` which is the definition sigil). This is idiomatic for words that perform I/O or read state, like `pixel!` or `key@`.

### Named locals

Passing values around on the stack is honest work, but `dup swap rot` gets old when you just want to call something `n`. `GIRD` puts a name on the top of the stack. `UNGIRD` takes the name off.

```forth
lux> 5 GIRD n n n * UNGIRD
  Stack: [25]
```

`n` reads the local. `n!` writes it:

```forth
lux> 5 GIRD n n 1 + n! n UNGIRD
  Stack: [6]
```

At the REPL, or at the top of a file, you have to `UNGIRD`. The compiler will refuse an open frame at the end of a program (`Unclosed local frame`). Inside a word, `;` ungirds for you — so don't slap a leftover closer on the end of the definition.

```forth
lux> @square { n -- n² } n n * ;
Defined word 'square'
lux> 5 square
  Stack: [25]
```

`{ n -- n² }` is the same idea as `GIRD n`, plus a stack comment. Everything after `--` is ignored. The `}` there only ends the name list; it does **not** close a block.

Several names at once — last name is the top of the stack, same pop order as `frame!`:

```forth
lux> 100 200 { a b } a b + UNGIRD
  Stack: [300]
```

`a` is 100, `b` is 200.

Frames nest. Inner names shadow outer ones; outer names stay visible. Ungird from the inside out:

```forth
lux> 10 20 { a b } 3 GIRD c a c + UNGIRD UNGIRD
  Stack: [13]
```

A quotation `]` also ungirds any frames you opened inside it, which is why loop bodies can `GIRD` a scratch name and not mention `UNGIRD`.

`GIRD` / `{ names }` / `UNGIRD` compile to the same `FRAME` / `UNFRAME` opcodes as the numbered form (`N frame!` / `N local@` / `N local!` / `N unframe!`). No new opcodes. A stray `}` still ungirds, because that's what it used to mean — prefer `UNGIRD`.

A walkthrough you can compile lives in [`examples/lux/gird.lux`](../examples/lux/gird.lux):

```
./bin/nux examples/lux/gird.lux
```

### FIELDS

`FIELDS` generates a record layout and accessors at compile time:

```forth
FIELDS BTN x y w h ;
```

That defines:

| Word | Stack | Meaning |
|---|---|---|
| `BTN.SIZE` | `-- n` | size in bytes (`nfields * 4`) |
| `BTN.x` | `-- off` | byte offset of the field |
| `BTN.x@` | `addr -- v` | load field |
| `BTN.x!` | `v addr --` | store field |

Same for `y`, `w`, `h`. Inside a `MODULE`, the names are qualified (`UI::BTN.x@`).

```forth
FIELDS PT x y ;
0x9000 3 OVER PT.x!
7 OVER PT.y!
dup PT.x@ swap PT.y@ +
```

End the field list with `;`. Without it, the next word is treated as another field.

### RESERVE

`RESERVE` asks the compiler for memory instead of hand-picking an address:

```forth
RESERVE CUR_VAL 4 ;
RESERVE GRID  240 ;
```

Each name becomes a word that pushes an address, exactly like the older
`@CUR_VAL 0x8D0020 ;` idiom — so `LOADI`/`STOREI`, index arithmetic and
`FIELDS` offsets all work the same way:

```forth
RESERVE COUNT 4 ;
@bump COUNT LOADI 1 + COUNT STOREI ;
0 COUNT STOREI  bump bump  COUNT LOADI .
```

Prints `2`.

The byte count is required and positive; reservations are word-aligned and
handed out in source order from a band the compiler owns, so two of them can
never overlap. Inside a `MODULE` the name is qualified like any other word
(`CALC::CUR_VAL`), which is how two modules share one cell.

Use it for ordinary app and library state. Three things still take a
hand-picked address: large buffers (the band is 1MB), anything the C host or
the VM agrees on at a fixed address, and headless programs — the band sits
above the memory a headless `nux` machine maps. See
[`reserve-directive.md`](reserve-directive.md).

### A tiny window

Graphics live in `lib/ui.lux`. After `APP::init` and `UI::new`:

```forth
T"OK" 20 20 59 20 [ on-ok ] UI::button
[ UI::feed ] APP::on-mouse!
[ on-frame ] APP::on-frame!
APP::loop
```

`on-frame` should call `UI::handle` then `UI::draw`. See [`ui.md`](ui.md).

### Simulation vs. drawing

`on-frame` runs once per rendered frame, which is right for a program that only
redraws in response to input. Anything that moves *on its own* — a game, an
animation — wants a second hook:

```forth
[ on-tick  ] APP::on-tick!    ( physics and state, fixed 16ms step )
[ on-frame ] APP::on-frame!   ( drawing only )
```

`APP::loop` keeps a millisecond accumulator fed from `/dev/time` and calls
`on-tick` once per whole `APP::STEP_MS` (16 ms, ~60 Hz) of elapsed time —
usually once a frame, zero when a frame was quick, several when one was slow.
A frame that took 33 ms to draw runs two steps instead of quietly running the
world at half speed, so motion is measured in real time rather than in frames.
Two guards bound it: `APP::MAX_DT` (100 ms) clamps one frame's elapsed time,
and `APP::MAX_STEPS` (5) caps the steps a single frame may run, dropping the
remaining backlog rather than carrying it forward forever.

Because a fixed step never divides a frame period exactly, some frames run two
steps and drawing raw simulation state lurches on those. `APP::tick-alpha`
(0–255, how far into the current step the renderer is) and `APP::lerp` fix it:
keep last step's value alongside the current one and draw between them.

```forth
prev-x LOADI x LOADI APP::lerp    ( draws one step behind, but evenly )
```

Register no `on-tick` and none of this happens — the clock is never even read.
`apps/Snake.lux` and `apps/Breakout.lux` are the two examples; the design notes
are in [`games/breakout_clone.md`](games/breakout_clone.md).

### File Inclusion

If your project grows, you can split it into multiple files and use `INCLUDE` to bring them together:

```forth
INCLUDE "lib/system.lux"
```

The compiler will recursively include the file's content at that position.

### Conditionals and Loops

Lux uses runes here for that streamlined, Martian [look](https://docs.urbit.org/hoon/rune). 

| Rune | Sounds | Meaning |
|------|--------|---------|
| ?    | wut    | IF      |
| ?:   | wutcol | IF-ELSE |
| !:   | zapcol | UNLESS  |
| \|:  | barcol | WHILE   |
| #:   | ritcol | TIMES   |

---

The `?` combinator is a conditional execution operator that executes a quotation (code block) only if a condition is true. Like an IF statement, but with quotations and it comes at the end.


```forth
lux> 1 [ 42 ] ?
  Stack: [42]
```

If `1` is true, (any non-zero value is true), the quotation is executed. In this example, it pushes `42` onto the stack.

```forth
1 [ 42 6 + ] ?
  Stack: [48]
```

Here, the quotation is evaluated, and we end up with `48` on the stack.

Remember our `hello` word?

```forth
lux> 1 [ hello] ?
Hello, World  Stack: []
```

Slick!

And if the condition is false?

```forth
lux> 0 [ 42 ] ?
  Stack: []
```

Nothing happens on the stack.

```quote
lux> 1 1 + [ 42 ] ?
  Stack: [42]
lux> cs
Stack cleared
lux> 1 1 - [ 42 ] ?
  Stack: []
```

If you want to express things a little differently, you can!

```quote
lux> [1 1 +] CALL [ 42 ] ?
  Stack: [42]
```

This takes `[1 1 +]` invokes it with `CALL` and uses the result as the conditional.

The `?:` combinator is a conditional execution operator that executes the first quotation (code block) only if a condition is true, or the second if false. Like an IF/THEN statement, but with quotations.

```forth
lux> 1 [ 42 ] [ 99 ] ?:
  Stack: [42]
```

```forth
lux> 0 [ 42 ] [ 99 ] ?:
  Stack: [99]
```

---

The `!:` combinator is **unless** -- it's the opposite of `?`. It executes a quotation only if a condition is false (zero). If it could grow facial hair, it would be a beard.

```forth
lux> 0 [ 42 ] !:
  Stack: [42]
lux> cs
Stack cleared
lux> 1 [ 42 ] !:
  Stack: []
```

---

The `|:` combinator is **while** - it repeatedly executes a body quotation as long as a condition quotation evaluates to true (non-zero).

```forth
lux> 5 [ 0 > ] [ 1 - ] |:
  Stack: [0]
```

Let's break this down. We start with `5` as the inital counter, the next quotation defines the break condition, and the last is what to be applied each run. Finally, the `|:` rune is the while loop combinator.

Here's how you'd do a countdown:

```forth
lux>  5 [ 0 > ] [ DUP 1 - ] |:
  Stack: [5 4 3 2 1 0]
```

Or

```forth
5 [ 0 > ] [ DUP DEC ] |:
  Stack: [5 4 3 2 1 0]
```

Notice the ending value is the final value of the counter. If you don't want that, you can call `DROP`.

```forth
5 [ 0 > ] [ DUP DEC ] |: DROP
  Stack: [5 4 3 2 1]
```

---

The `#:` combinator is **times** - it executes a quotation a specific number of times.

```forth
lux> 0 [ 1 + ] 5 #:
  Stack: [5]
```

Push `0` on the stack. Now, pop and apply `[ 1 + ]` `5` times before pushing the final result onto the stack.

Let's revisit our `hello` word.

```forth
lux> @hello 72 emit 101 emit 108 emit 108 emit 111 emit 44 emit 32 emit 87 emit 111 emit 114 emit 108 emit 100 emit ;
lux> 0 [ hello 32 emit 32 emit ] 5 #:
Hello, World  Hello, World  Hello, World  Hello, World  Hello, World    Stack: [0]
```

### Recursion

As anyone knows, recursion is a high-water mark of sophistication and grace. It is the ultimate combination of GOTOs and Ouroboros, forged in the fires of Hephaestus, touched by Godel, and smiled upon by Escher. 

It is, also, a real pain to implement, as I have learned.

Here's the classic example of the Fibonacci function:

```forth
lux> @fib dup 1 > [ dup 1 - fib swap 2 - fib + ] ? ;
Defined word 'fib'
lux> 10 fib
  Stack: [55]
lux> 6 fib
  Stack: [55 8]
```

Yup, that looks correct.

I'm going to break this down into steps.

#### Definition Start: @fib begins defining the word `fib`. Everything until `;` is the body.
- Duplicate and Compare:
`dup`: Duplicates the top of the stack. Stack: `n n`
`1`: Pushes literal 1. Stack: `n n 1`
`>`: Pops two values, compares (second > first), pushes 1 (true) or 0 (false). Stack: `n (n>1)`

#### Quotation (Anonymous Block):
`[ dup 1 - fib swap 2 - fib + ]`: This is a "quotation" in Lux, a code block pushed as data onto the stack (its address in VM memory). It's like a lambda.
Stack now: `n (n>1) quot_addr`

- The quotation's body (executed only if condition is true):
`dup`: n n
`1 -`: n (n-1)
`fib`: Recursively calls `fib` on (n-1). Stack: `n fib(n-1)`
`swap`: fib(n-1) n
`2 -`: fib(n-1) (n-2)
`fib`: Recursively calls `fib` on (n-2). Stack: `fib(n-1) fib(n-2)`
`+`: Adds them. Stack: `fib(n)`

#### Conditional Combinator `?`:
This is a control-flow combinator of "if condition then execute quotation, else drop quotation."
`Stack before ?`: `n condition quot_addr`
`If condition (n > 1) is true (non-zero)`: Execute the quotation on the stack (consuming n, producing fib(n)).
`If false (n <= 1)`: Drop the quotation, leaving n (since fib(n) = n for base cases).
In the Nux VM, this compiles to bytecode like SWAP (rearrange stack), JZ (jump if zero/false to skip execution), CALLSTACK (execute quotation via jump with return stack push), and POP (drop if skipped).

- `End Definition`: `;` closes the word. Now fib can be called like any built-in word. For example: `10 fib` pushes 55 onto the stack.

Now, for the next example, an iterative factorial!

```forth
lux> @fact-iter 1 swap dup [ dup rot * swap 1 - ] swap #: drop ;
Defined word 'fact-iter'
lux> 5 fact-iter
  Stack: [120]
```

Let's break it down.

Start with input n=5 on stack: [5]

#### Definition Start: @fact-iter defines the word. Body ends at ;.
- Initialize Accumulator and Counter:
 1: Push 1 (initial acc). Stack: [5 1]
- `swap:` Swap top two. Stack: [1 5] // acc=1, i=5 (i will decrement)
- `dup`: Duplicate i. Stack: [1 5 5] // acc i count=5 (count for #: control)

#### Quotation (Loop Body):
- `[ dup rot * swap 1 - ]`: Push quotation address. Stack: [1 5 5 quot_addr]
This block transforms [acc i] → [acc*i (i-1)] each iteration.


#### Reorder for Combinator:
`swap`: Swap top two. Stack: [1 5 quot_addr 5] // acc i quot count

#### Times Combinator #::
Expects: ... data quot count → executes quot count times on data.
In compiler.c's compile_combinator() for `#:`: PUSHR the quot and count, PEEKR/JZ to check count>0, CALLSTACK the quot, DEC count, JMP loop.
Each iteration (dipping under i quot count):
- `dup`: [acc i i]
- `rot`: Cycles left [i i acc] // [a b c] → [b c a]
- `*`: Pops acc and top i, pushes (i * acc). Stack: [i (i*acc)]
- `swap`: [(i*acc) i]
- `1 -`: [(i*acc) (i-1)]

#### After 5 iterations:
Start: acc=1, i=5
Iter1: acc=1*5=5, i=4
Iter2: acc=5*4=20, i=3
Iter3: acc=20*3=60, i=2
Iter4: acc=60*2=120, i=1
Iter5: acc=120*1=120, i=0
`Stack after: [120 0]`

##### Cleanup:
- `drop`: Pops 0. Stack: [120] // Final n!

For further reading, see [Recursion](#recursion)

### Modules

Modules allow you to namespace your words.

```forth
MODULE MATH
@square dup * ;
@cube dup dup * * ;
```

You can use these words by qualifying them: `MATH::square`.

To make them easier to use, you can `IMPORT` a module:

```forth
IMPORT MATH
5 MATH::SQUARE .
```

Or use an alias:

```forth
IMPORT MATH AS M
5 M::SQUARE .
```

### Version

Every app build must declare a version, using the project's Kelvin
versioning scheme (see `AGENTS.md`): higher numbers are "hotter," and a
hotter build can run something just as hot or colder, but not the reverse.
NUX opcodes/implementation sit at 300K; everything else defaults to 400K
unless a specific app has a reason to declare otherwise.

```forth
MODULE MYAPP
VERSION 400000
```

`luxc` rejects an app build that never declares `VERSION <n>` anywhere in
the compiled unit (the main file or any `INCLUDE`d file), with:

```
luxc: myapp.lux: missing required 'VERSION <n>' directive
```

This check only applies to normal app builds. A library build (`luxc -base
0xADDR ...`, e.g. `lib/sf.lux` linked into a Fluxio host) is exempt — it
isn't a runnable app on its own.

### Compiling Lux Source

After playing around in the REPL, it stands to reason that the more inquisitive or just plain nosy might look into compiling code.

There are examples in `examples/lux/`.

Here's `hello.lux`

```forth
( === STRING EXAMPLES === )

// Single line comment

( This is a number )
42 .  // the . prints out numbers

// Now a space
32 emit

( Method 1: Manual ASCII codes )
( Print "Hi" )
// emit prints out the ASCII value of the number
72 emit   ( H ) 
105 emit  ( i )
10 emit   ( newline )

( Method 2: With string support added )
( Uncomment after adding string support to lexer/compiler )

"Hello, World!"
10 emit
( "LUX is cool!"
10 emit )

( === USEFUL ASCII CODES === )

( Common characters: )
( 10  = newline )
( 32  = space )
( 48-57 = 0-9 )
( 65-90 = A-Z )
( 97-122 = a-z )


( === HELPER WORD === )

( Define a word to print newline )
@cr 10 emit ;

( Use it: )
72 emit 105 emit 32 emit 2 . cr

```

Run the compiler using that file:

> $ ./luxc examples/lux/hello.lux
> Compiled: examples/lux/hello.bin

The `bin` file is the compiled opcodes.

Run that through the `nux vm`

> ./nux examples/lux/hello.bin
> 42 Hi
> Hello, World!
> Hi 2

There are more examples to check out at your leisure. `examples/lux/gird.lux` is the named-locals walkthrough (`GIRD` / `UNGIRD`).

---

### Error Handling

A man went seeking wisdom. He traveled to a monastery known for being a pious order and for providing answers for many seekers of knowledge. 

As the traveler reached the town center, he heard a great din. A monk was driving a herd of sheep before him, shouting orders at them, and forcing them along with his shepherd's crook.

"Hail, good brother in Christ! I have come seeking to understand the wisdom behind error handling!" the traveler called over the noise.

Whatever the monk said was lost in the confusion. Before the traveler could ask again, the monk was distracted by pulling a wayward sheep out of a ditch.

The only words the traveler heard were "Good luck!" as the monk drove the sheep along.

Continuing up the road to the monastery, the traveler saw another monk working in a field, pulling weeds from rows of vegetables. 

"Hail, good brother in Christ! I have come seeking to understand the wisdom behind error handling!" the traveler called over the fence between them.

The monk stopped, wiped the sweat from his brow, and pointed at his weeds. He had taken a vow of silence. The day was growing long, he still needed to finish his work before the next tolling of the bell.

The traveler's eyes were opened. He thanked the silent monk, hurried back to his team, and recounted the tale.

"So?" one team member asked contemptuously. "They didn't teach you anything! We aren't any better off than before."

"No matter how you handle errors, whether verbose or cryptic, you have to act!"

Lux error handling is sparse--focused on stack state, program counter, and operation.

```forth
lux> .s
 Stack: []
lux> dup
Runtime error: error at PC=4102: dup failed: stack underflow: need 1 value for DUP
lux> +
Runtime error: error at PC=4102: add failed: stack underflow: need 2 values for ADD
lux> monk
Compile error: unknown word 'monk' at line 1
```

## Finally!

Thank you for reading this quick tutorial on Lux. ==<insert joke here, wait for audience response>==

As I improve the language, I'll update this tutorial.


## Coming At Some Point!

- Bug fixes

