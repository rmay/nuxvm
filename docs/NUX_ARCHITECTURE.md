# LUX Module System Architecture

## Visual Overview

```
┌─────────────────────────────────────────────────────────────┐
│                      LUX SOURCE CODE                        │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  MODULE MATH                                                │
│  @square dup * ;                                            │
│  @cube dup square * ;                                       │
│                                                             │
│  MODULE STRING                                              │
│  @len ... ;                                                 │
│  @concat ... ;                                              │
│                                                             │
│  MODULE MAIN                                                │
│  IMPORT MATH AS M                                           │
│  IMPORT STRING AS S                                         │
│                                                             │
│  5 M::SQUARE .                                              │
│  "hello" S::LEN .                                           │
│                                                             │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│                      LEXER (lexer.go)                       │
├─────────────────────────────────────────────────────────────┤
│  Tokenizes source → [MODULE][MATH][@][square][dup][*][;]   │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│                COMPILER (compiler_modules.go)               │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  ┌─────────────────────────────────────────────┐           │
│  │         COMPILATION STATE                   │           │
│  ├─────────────────────────────────────────────┤           │
│  │  currentModule: "MAIN"                      │           │
│  │  imports: {"M" → "MATH", "S" → "STRING"}    │           │
│  │  dictionary: {                              │           │
│  │    "MATH::SQUARE"  → Word{addr: 0x100}     │           │
│  │    "MATH::CUBE"    → Word{addr: 0x120}     │           │
│  │    "STRING::LEN"   → Word{addr: 0x140}     │           │
│  │    "STRING::CONCAT"→ Word{addr: 0x160}     │           │
│  │  }                                          │           │
│  └─────────────────────────────────────────────┘           │
│                                                             │
│  RESOLUTION PROCESS:                                        │
│  ┌──────────────────────────────────────────┐              │
│  │ Input: "M::SQUARE"                       │              │
│  │   ↓                                      │              │
│  │ Check imports: M → MATH                  │              │
│  │   ↓                                      │              │
│  │ Construct: "MATH::SQUARE"                │              │
│  │   ↓                                      │              │
│  │ Lookup: dictionary["MATH::SQUARE"]       │              │
│  │   ↓                                      │              │
│  │ Emit: CALL 0x100                         │              │
│  └──────────────────────────────────────────┘              │
│                                                             │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│                        BYTECODE                             │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  [JMP 0x200]          ← Skip to main                        │
│                                                             │
│  [0x100] PUSH 5       ← MATH::SQUARE definition            │
│          DUP                                                │
│          MUL                                                │
│          RET                                                │
│                                                             │
│  [0x120] PUSH 3       ← MATH::CUBE definition              │
│          CALL 0x100   ← Calls MATH::SQUARE                 │
│          MUL                                                │
│          RET                                                │
│                                                             │
│  [0x200] PUSH 5       ← Main program                        │
│          CALL 0x100   ← Direct call, no name lookup!       │
│          PUSH 0                                             │
│          OUT                                                │
│          HALT                                               │
│                                                             │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│                  VIRTUAL MACHINE (vm.go)                    │
├─────────────────────────────────────────────────────────────┤
│  Execute bytecode normally - no module awareness needed!    │
│  CALL instructions use direct addresses                     │
│  → Zero runtime overhead                                    │
└─────────────────────────────────────────────────────────────┘
```

## Data Flow Diagram

```
Source Code
    │
    ├─→ MODULE directive  → Sets currentModule
    │                       Qualifies subsequent words
    │
    ├─→ IMPORT directive  → Adds to imports map
    │                       Enables alias resolution
    │
    ├─→ Word definition   → Store as "MODULE::WORD"
    │   (@word ... ;)       Add to dictionary
    │
    └─→ Word reference    → 1. Try exact match
        (word or M::word)   2. Try current module
                            3. Try import expansion
                            4. Try builtins
                            → Emit CALL to address
```

## Resolution Algorithm

```
function resolveWord(name):
    upperName = toUpper(name)
    
    // 1. Exact match
    if dictionary.contains(upperName):
        return dictionary[upperName]
    
    // 2. Current module prefix
    if not upperName.contains("::") and currentModule != "":
        qualified = currentModule + "::" + upperName
        if dictionary.contains(qualified):
            return dictionary[qualified]
    
    // 3. Import alias expansion
    if upperName.contains("::"):
        [prefix, word] = upperName.split("::")
        if imports.contains(prefix):
            fullModule = imports[prefix]
            qualified = fullModule + "::" + word
            if dictionary.contains(qualified):
                return dictionary[qualified]
    
    // 4. Not found
    return null
```

## Memory Layout

```
┌──────────────────────────────────────┐
│         COMPILER MEMORY              │
├──────────────────────────────────────┤
│                                      │
│  Dictionary (map[string]Word):       │
│  ┌────────────────────────────────┐ │
│  │ "MATH::SQUARE"  → 0x100        │ │
│  │ "MATH::CUBE"    → 0x120        │ │
│  │ "STRING::LEN"   → 0x140        │ │
│  │ "MAIN::TEST"    → 0x180        │ │
│  └────────────────────────────────┘ │
│                                      │
│  Imports (map[string]string):        │
│  ┌────────────────────────────────┐ │
│  │ "M"  → "MATH"                  │ │
│  │ "S"  → "STRING"                │ │
│  └────────────────────────────────┘ │
│                                      │
│  Bytecode ([]byte):                  │
│  ┌────────────────────────────────┐ │
│  │ [0x00] JMP 0x200               │ │
│  │ [0x05] PUSH 5                  │ │
│  │ [0x0A] DUP                     │ │
│  │ [0x0B] MUL                     │ │
│  │ ...                            │ │
│  └────────────────────────────────┘ │
└──────────────────────────────────────┘

NO MODULE DATA IN VM MEMORY!
(Everything resolved at compile time)
```

## Compilation Phases

```
┌─────────────────────────────────────────┐
│         PHASE 1: FIRST PASS             │
├─────────────────────────────────────────┤
│                                         │
│  • Reserve JMP space                    │
│  • Process MODULE directives            │
│  • Process IMPORT directives            │
│  • Compile word definitions             │
│    - Store with qualified names         │
│  • Patch initial JMP                    │
│                                         │
└─────────────────────────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────┐
│         PHASE 2: SECOND PASS            │
├─────────────────────────────────────────┤
│                                         │
│  • Skip MODULE/IMPORT directives        │
│  • Skip word definitions                │
│  • Compile main program                 │
│    - Resolve word references            │
│    - Emit CALL instructions             │
│  • Add HALT                             │
│                                         │
└─────────────────────────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────┐
│             OUTPUT BYTECODE             │
└─────────────────────────────────────────┘
```

## Comparison: With vs Without Modules

```
WITHOUT MODULES:
┌──────────────────────┐
│ Dictionary:          │
│ "square" → 0x100     │    ← Name collision risk!
│ "cube"   → 0x120     │
│ "square" → 0x140     │    ← Can't have two!
└──────────────────────┘

WITH MODULES:
┌──────────────────────────┐
│ Dictionary:              │
│ "MATH::square" → 0x100   │  ← No collision
│ "MATH::cube"   → 0x120   │
│ "GFX::square"  → 0x140   │  ← Different namespace
└──────────────────────────┘
```

## Call Stack at Runtime

```
Runtime execution is IDENTICAL with or without modules:

┌─────────────────────────┐
│    Return Stack         │
├─────────────────────────┤
│  0x210  (from main)     │
│  0x105  (from cube)     │
└─────────────────────────┘
        
┌─────────────────────────┐
│    Data Stack           │
├─────────────────────────┤
│  25                     │
│  5                      │
└─────────────────────────┘

The VM never sees module names!
```

## Benefits Summary

```
┌──────────────────────────────────────────────┐
│           COMPILE TIME                       │
├──────────────────────────────────────────────┤
│  ✓ Namespace organization                    │
│  ✓ Import alias resolution                   │
│  ✓ Name collision prevention                 │
│  ✓ Code organization                         │
│  ✓ Better error messages                     │
├──────────────────────────────────────────────┤
│  Cost: +Minimal string operations            │
└──────────────────────────────────────────────┘

┌──────────────────────────────────────────────┐
│            RUNTIME                           │
├──────────────────────────────────────────────┤
│  ✓ Zero overhead                             │
│  ✓ Same bytecode structure                   │
│  ✓ No module tables                          │
│  ✓ No name lookups                           │
│  ✓ Direct address calls                      │
├──────────────────────────────────────────────┤
│  Cost: NONE                                  │
└──────────────────────────────────────────────┘
```
