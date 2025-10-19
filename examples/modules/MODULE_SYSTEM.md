# LUX Module System Documentation

## Overview

The LUX module system provides namespace organization for your code without requiring any new VM opcodes. It's purely a compile-time feature that uses the `::` separator to create qualified names.

## Key Features

**Symbol-Based Namespacing** - Uses `MODULE::WORD` naming convention
**Import with Aliases** - Shorthand access to module symbols  
**Namespace Isolation** - Same word names in different modules don't conflict
**Zero Runtime Cost** - All resolution happens at compile time
**No New Opcodes** - Works with existing VM architecture

## Syntax

### Defining a Module

```lux
MODULE moduleName

@word1 ... ;
@word2 ... ;
```

All words defined after a `MODULE` directive belong to that module and are stored with fully qualified names like `MODULENAME::WORD1`.

### Importing a Module

```lux
IMPORT moduleName          (( Use as moduleName::word ))
IMPORT moduleName AS alias (( Use as alias::word ))
```

### Calling Module Words

Three ways to call a word from a module:

1. **Fully Qualified**: `MATH::SQUARE`
2. **Import Alias**: `M::SQUARE` (after `IMPORT MATH AS M`)
3. **Auto-Resolution**: `SQUARE` (if defined in current module)

## Resolution Order

When the compiler encounters a word, it tries to resolve it in this order:

1. **Exact match** - Look for the exact name in dictionary
2. **Current module** - Try `CURRENTMODULE::WORD`
3. **Import resolution** - Expand aliases (e.g., `M::SQUARE` → `MATH::SQUARE`)
4. **Built-in words** - Check built-in operations

## Examples

### Example 1: Basic Module

```lux
MODULE MATH

@square dup * ;
@cube dup square * ;

(( Main code ))
5 MATH::SQUARE .
10 MATH::CUBE .
```

### Example 2: Imports and Aliases

```lux
MODULE UTILS
@abs dup 0 < @neg-if ; exit ;
@neg-if negate ;

MODULE MAIN
IMPORT UTILS AS U

@test -5 U::ABS . ;
test
```

### Example 3: Multiple Modules

```lux
(( Graphics module ))
MODULE GFX
@newline 10 emit ;
@space 32 emit ;

(( Math module ))
MODULE MATH
@double 2 * ;

(( Main program ))
MODULE MAIN
IMPORT GFX
IMPORT MATH AS M

5 M::DOUBLE .
GFX::SPACE
10 M::DOUBLE .
GFX::NEWLINE
```

### Example 4: Namespace Isolation

```lux
MODULE A
@double 2 * ;

MODULE B  
@double dup + ;

(( Both can coexist ))
MODULE MAIN
10 A::DOUBLE .  (( 20 ))
10 B::DOUBLE .  (( 20, but different implementation ))
```

## Design Benefits

### 1. **No Runtime Overhead**
All module resolution happens at compile time. The bytecode contains direct addresses - no name lookup at runtime.

### 2. **Backward Compatible**
Programs without modules work exactly as before. Modules are purely additive.

### 3. **Simple Implementation**
- No new opcodes needed
- No runtime module tables
- Dictionary keys are simply prefixed strings
- Import map is just compile-time alias resolution

### 4. **Flexible Organization**
You can organize code by:
- Functionality (MATH, STRING, IO)
- Layers (UI, LOGIC, DATA)
- Features (AUTH, PARSER, RENDERER)

## Implementation Details

### Compiler State

```go
type Compiler struct {
    dictionary    map[string]Word  // "MODULE::WORD" -> Word
    currentModule string            // Active module name
    imports       map[string]string // Alias -> Full module name
}
```

### Word Storage

Words are stored with their full qualified name:
```
"MATH::SQUARE" -> Word{Name: "MATH::SQUARE", Address: 123, Module: "MATH"}
"STRING::LEN"  -> Word{Name: "STRING::LEN", Address: 456, Module: "STRING"}
```

### Resolution Example

When compiling `M::SQUARE` after `IMPORT MATH AS M`:

1. Compiler sees `M::SQUARE`
2. Checks imports: `M` → `MATH`
3. Constructs full name: `MATH::SQUARE`
4. Looks up in dictionary
5. Emits CALL to the address

## Best Practices

### 1. Use Modules for Organization
```lux
MODULE PARSER
@tokenize ... ;
@parse ... ;

MODULE EVALUATOR  
@eval ... ;
@apply ... ;
```

### 2. Import with Meaningful Aliases
```lux
IMPORT UTILITIES AS UTIL
IMPORT MATHEMATICS AS MATH
IMPORT INPUT-OUTPUT AS IO
```

### 3. Keep Module Names Short
```lux
MODULE STR    (( Better than STRING-UTILITIES ))
MODULE NUM    (( Better than NUMERIC-OPERATIONS ))
```

### 4. Group Related Functions
```lux
MODULE LIST
@push ... ;
@pop ... ;
@length ... ;
@map ... ;
```

## Limitations

1. **No Circular Dependencies**: Module A can't import module B if B imports A
2. **Single-Level Namespaces**: Can't have nested modules like `MATH::TRIG::SIN`
3. **Case Insensitive**: `math::square` and `MATH::SQUARE` are the same
4. **Static Resolution**: All modules must be in same compilation unit

## Future Extensions

Possible enhancements while maintaining the symbol-based approach:

- **Private Words**: `@_private` convention for module-internal words
- **Re-exports**: `IMPORT X EXPORT Y` to re-export under different name
- **Module Comments**: Structured documentation in comments
- **Visibility Markers**: `@PUBLIC word`, `@PRIVATE word`

