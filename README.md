# <img src="logo.png" alt="OneParse logo" width="32" height="32"> OneParse

**HAPI Compatibility:** Works with new Check/Apply/ApplyPack API (2026-Q2)

Parser combinator components for embedded C++ — zero heap, zero virtual dispatch, embedded-friendly.

Part of the [InternetOfPins](https://github.com/InternetOfPins) family.

## Design

Parsers are assembled at compile time by composing `Part` types into a `ParserDef` chain. A parser call returns a `Res<T>` — a plain value struct (ok flag, matched value, remaining input pointer) that fits in registers.

```cpp
template<typename T>
struct Res { bool ok; T val; Src rest; operator bool() const { return ok; } };
```

Each component is a struct with an inner `Part<O>` that wraps the next link in the chain. `ParserDef<T, OO...>` wires the chain together.

The usual TMP complaints — exponential compile times, unreadable errors, impenetrable extension model — come from recursive template evaluation and opaque internal types. `ParserDef` avoids both: chain folding uses pack expansion (linear in chain length), and errors name the user's own component types in chain order (`Skip<Space>::Part<Alpha::Part<ParseAPI<char>>>`), not library internals. Adding a component is one struct with one `Part<O>`.

## Components

| Component | Matches |
|-----------|---------|
| `Char<C>` | exactly the character `C` |
| `Satisfy<F>` | any char where `F(c)` is true |
| `Range<Lo,Hi>` | any char in `[Lo, Hi]` (delegates to `Quick::Range`) |
| `Ranges<RR...>` | any char accepted by any of the given `Quick::Range` types |
| `AnyOf<Cs...>` | any char in the compile-time character set |
| `Not<P>` | any char where atomic `P` does **not** match |
| `NoneOf<Cs...>` | any char **not** in the set — alias for `Not<AnyOf<Cs...>>` |

Built-in aliases: `Digit`, `Alpha`, `Space`.

## Meta parsers

All meta-parsers are components and compose inside `ParserDef` chains. Internally they use `Chain<PP...>::Part<ParseAPI<T>>` directly — no `APIOf` overhead, no validation pass.

`Or` and `Not` take **single components**.  
`Many<P>` takes a **single component** — use `Many<Or<P1,P2>>` to match alternatives.  
`Skip<PP...>` takes a **variadic component chain** — `Chain` is built internally.  
`To<T_in, F, PP...>` takes an explicit input type, a transform, and a **variadic component chain**.  
`ManyTill<P,End>` takes two **single components**.  
`Between<Open,P,Close>` takes single components `Open`/`Close` and a **complete parser** `P`.  
`SepBy<P,Sep,N>` / `SepBy1<P,Sep,N>` / `Verify<P,F>` / `Defer<T,F>` take **complete parsers**.  
`Seq<P1,P2>`, `ManyFn<P,F>`, and `ManyN<P,N>` take **complete parsers** (`ParserDef<T,...>`) so their result types are statically known.

| Component | Description |
|-----------|-------------|
| `Opt<P>` | zero or one of `P` — always succeeds; on match advances and sets `r.val`, otherwise leaves both unchanged |
| `Some<P>` | one or more of `P` — fails if zero matches; span implicit in `r.rest` |
| `Many<P>` | zero or more of component `P` — always succeeds; use `Many<Or<P1,P2>>` for alternatives |
| `Or<P1,P2>` | try component `P1`, fall back to `P2`; both share the chain's `T` |
| `Skip<PP...>` | advance past component chain `PP...`, discard its value |
| `To<T_in,F,PP...>` | parse `PP...` as `T_in`, apply `F(val)` to produce the outer `T` |
| `As<T_out,P>` | run complete parser `P`; construct `T_out{P::Type}` — type-directed, no explicit transform |
| `Seq<P1,P2>` | run complete parsers `P1` then `P2`, yield `Pair<P1::Type, P2::Type>` |
| `ManyFn<P,F>` | call `F(val)` on each match of complete parser `P` — visitor, zero alloc |
| `ManyN<P,N>` | collect up to `N` matches of complete parser `P` into `Arr<T,N>` |
| `Any` | match any single non-null character |
| `Eof` | succeed only at end of input |
| `ManyTill<P,End>` | advance past `P` zero or more times, stopping when `End` matches; fails if `End` never found |
| `Between<Open,P,Close>` | skip component `Open`, run complete parser `P`, skip component `Close`; yield `P`'s result |
| `SepBy<P,Sep,N>` | parse complete parser `P` zero or more times, separated by component `Sep`; up to `N` items |
| `SepBy1<P,Sep,N>` | same as `SepBy` but requires at least one item |
| `Verify<P,F>` | run complete parser `P`; fail without consuming if `F(val)` returns false |
| `Defer<T,F>` | call `F(src) → Res<T>` at runtime — breaks template recursion for self-referential grammars |

Utility types: `Pair<A,B>` (`fst`, `snd`) and `Arr<T,N>` (`data`, `len`, iterator pair).

## Usage

```cpp
#include <oneParse/oneParse.h>
using namespace oneParse;

// single character
using Hash = ParserDef<char, Char<'#'>>;

// predicate
using ADigit = ParserDef<char, Digit>;

// range
using Lower = ParserDef<char, Range<'a','z'>>;

// union of ranges
using AlphaNum = ParserDef<char, Ranges<
    Quick::Range<char,'a','z'>,
    Quick::Range<char,'A','Z'>,
    Quick::Range<char,'0','9'>>>;

// character set
using HexDigit = ParserDef<char, AnyOf<
    '0','1','2','3','4','5','6','7','8','9',
    'a','b','c','d','e','f',
    'A','B','C','D','E','F'>>;

// sequential composition — '#' then '!'
using HashBang = ParserDef<char, Char<'#'>, Char<'!'>>;

auto r = HashBang::run("#!ok");
if (r) { /* r.val, r.rest */ }

// Or — digit or lowercase letter
using DigitOrLower = ParserDef<char, Or<Digit, Range<'a','z'>>>;
auto r2 = DigitOrLower::run("x!");   // r2.val == 'x'
auto r3 = DigitOrLower::run("3!");   // r3.val == '3'

// Skip — consume '=' but keep the following char as result
using Ws = ParserDef<char, Many<Space>>;
using EqThenAlpha = ParserDef<char, Skip<Ws>, Char<'='>, Alpha>;
// Skip<Ws> advances past spaces; Char<'='> then Alpha set val

// Seq — key=value pair, both chars
using KeyVal = ParserDef<Pair<char,char>,
    Seq<ParserDef<char, Alpha>,
        ParserDef<char, Skip<ParserDef<char, Char<'='>>>, Alpha>>>;
auto kv = KeyVal::run("k=v");  // kv.val == {.fst='k', .snd='v'}

// To — build a struct from a Seq result
struct Entry { char key; char val; };
constexpr Entry makeEntry(Pair<char,char> p) { return {p.fst, p.snd}; }
using EntryP = ParserDef<Entry, To<Pair<char,char>, makeEntry, Seq<KeyAlpha, KeyDigit>>>;
auto e = EntryP::run("k=v");   // e.val == Entry{'k','v'}

// ManyN — collect up to 8 digits into a fixed array
using Digits8 = ParserDef<Arr<char,8>, ManyN<ParserDef<char, Digit>, 8>>;
auto digits = Digits8::run("1234abc");
// digits.val.len == 4, digits.val.data == {'1','2','3','4'}

// ManyFn — visitor; write each matched char into a caller-owned buffer
char buf[64]; int pos = 0;
auto sink = [](char c) { buf[pos++] = c; };  // or a plain function pointer
// ParserDef<char, ManyFn<ParserDef<char, Alpha>, &sinkFn>>

// skip leading whitespace — r.rest points past the spaces
using LeadingWs = ParserDef<char, Many<Space>>;
auto ws = LeadingWs::run("   hello");
// ws.rest == "hello"

// consume an integer token — span is [src, r.rest)
const char* src = "42abc";
auto tok = ParserDef<char, Many<Digit>>::run(src);
// tok.rest == "abc"; span length = tok.rest - src (== 2)

// Many over an explicit Or — digit or alpha, zero or more
using AlNum = ParserDef<char, Many<Or<Digit, Alpha>>>;

// skip whitespace then match '='  (Skip is also a variadic component chain)
using EqAfterWs = ParserDef<char, Skip<Space>, Char<'='>>;
auto eq = EqAfterWs::run(" =val");
// eq.val == '=', eq.rest == "val"

// skip several tokens at once
using SkipQuote = ParserDef<char, Skip<Char<'"'>>, Alpha>;  // skip '"' then match alpha
```

## AST strategy (paco style)

In paco, `.as(fn)` maps a parser's result to a node type, and combinators like `.then`/`.or`/`.skip` compose typed results into trees. The same pattern can be layered on top of `ParserDef`:

### 1. To / As

A `To<T_in, F, PP...>` meta-combinator that parses component chain `PP...` as `T_in`, applies functor `F`, and yields the outer `T`. This is the `.as()` equivalent — each node type is a plain struct, and `To` is the only place a struct is constructed.

### 2. Or / Choice

An `Or<P1, P2>` combinator that tries `P1` first and falls back to `P2`. Both branches must yield the same `T` (or a tagged union for heterogeneous AST nodes). On embedded, same-type branches are the primary case; a `Variant<N, T1, T2...>` placed on the stack can cover mixed-type alternatives if kept small.

### 3. Sequence with distinct results

The current chain (`ParseDef<T, P1, P2, ...>`) already sequences components but collapses everything into one `T`. For AST nodes that need both sub-results (e.g. `key` + `value`), a `Seq<P1, P2>` combinator yielding a `Pair<T1, T2>` is the natural extension. `To` then turns the pair into the target struct.

### 4. Many with accumulation

`Many<P>` advances past N chars; for an AST list node it needs to write into storage. Two zero-heap options:

- **Visitor / callback**: `ManyFn<P, F>` calls `F(Res<T>)` on each match — the caller owns the buffer, zero overhead.
- **Fixed-capacity array**: `ManyN<P, N>` fills a stack-allocated `array<T, N>`, fails if more than `N` items arrive.

### 5. Skip

`Skip<PP...>` consumes input but does not pass its result to the chain — the `.skip()` equivalent.

### Assembly pattern

```
node type    ←  To < Seq<FieldA, Skip<Sep>, FieldB>,  buildNode >
list type    ←  To < ManyFn<NodeP, accumulate>,       buildList >
root         ←  To < Or<TypeA, TypeB>,                tag        >
```

This keeps all allocation decisions at the call site (stack arrays, static buffers, or user-provided storage), matching the zero-heap guarantee of the rest of the library.

## Also provides

`wcw.h` — `Quick::Range<T,l,h>` / `Quick::Ranges<OO...>` check-only utilities, plus the `WCWidthCJK` wide-character width table used by OneMenu.

## License

MIT

---

Built on [HAPI](https://github.com/InternetOfPins/HAPI).
