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
- [Conditionals and Loops](#conditionals-and-loops)
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
║       LUX REPL 300K           ║
║  Stack-based Language REPL    ║
╚═══════════════════════════════╝

Type 'help' for commands, 'exit' to quit

lux>
```

The `300k` is the current version. I'm using the Kelvin versioning system as defined here: https://jtobin.io/kelvin-versioning because we can't make this too easy.

Back to the tutorial!

From the `lux> ` prompt we are going to look at some basics.

Lux is a stack-based language, a FILO ""First In, Last Out" stack.  We add things to the stack and can use operations or manipulate the stack itself.


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
roll          ( Copy second: a b → a b a )
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
lux> roll
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


### Modules

Modules are in place, just a bit rough and not useful yet.

Here's an example:

```forth
MODULE MATH
@square dup * ;
@cube dup dup * * ;
@power4 square square ;

MODULE SHAPES
IMPORT MATH AS M
@area-circle 
    ( radius -- area )
    M::SQUARE 
    314 * 100 /    ( π ≈ 3.14 )
;

MODULE MAIN
IMPORT MATH
IMPORT SHAPES

5 MATH::SQUARE .        ( 25 )
3 MATH::CUBE .          ( 27 )
10 SHAPES::AREA-CIRCLE . ( 314 )
```

Tantalizing!

### Compiling Lux Source

After playing around in the REPL, it stands to reason that the more inquisitive or just plain nosy might look into compiling code.

There are examples in the examples directory.

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

> $ ./luxc ./luxc examples/hello.lux
> Compiled: examples/hello.bin

The `bin` file is the compiled opcodes.

Run that through the `nux vm`

> ./nux examples/hello.bin
> 42 Hi
> Hello, World!
> Hi 2

There are more examples to check out at your leisure.

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

- Recursion
- Importing code files
- Bug fixes

