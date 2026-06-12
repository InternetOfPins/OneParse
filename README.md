# OneParse

Parser combinator components for [HAPI](https://github.com/InternetOfPins/HAPI) — zero heap, zero virtual dispatch, embedded-friendly.

Part of the [InternetOfPins](https://github.com/InternetOfPins) family.

## Design

Parsers are assembled at compile time by composing `Part` types through HAPI's `APIOf` chain. A parser call returns a `Res<T>` — a plain value struct (ok flag, matched value, remaining input pointer) that fits in registers.

```cpp
template<typename T>
struct Res { bool ok; T val; Src rest; operator bool() const { return ok; } };
```

Each component is a struct with an inner `Part<O>` that wraps the next link in the chain. `ParseDef<T, OO...>` wires the chain together.

## Components

| Component | Matches |
|-----------|---------|
| `Char<C>` | exactly the character `C` |
| `Satisfy<F>` | any char where `F(c)` is true |
| `Range<Lo,Hi>` | any char in `[Lo, Hi]` (delegates to `Quick::Range`) |
| `Ranges<RR...>` | any char accepted by any of the given `Quick::Range` types |
| `AnyOf<Cs...>` | any char in the compile-time character set |

Built-in aliases: `Digit`, `Alpha`, `Space`.

## Usage

```cpp
#include <oneParse/oneParse.h>
using namespace oneParse;

// single character
using Hash = ParseDef<char, Char<'#'>>;

// predicate
using ADigit = ParseDef<char, Digit>;

// range
using Lower = ParseDef<char, Range<'a','z'>>;

// union of ranges
using AlphaNum = ParseDef<char, Ranges<
    Quick::Range<char,'a','z'>,
    Quick::Range<char,'A','Z'>,
    Quick::Range<char,'0','9'>>>;

// character set
using HexDigit = ParseDef<char, AnyOf<
    '0','1','2','3','4','5','6','7','8','9',
    'a','b','c','d','e','f',
    'A','B','C','D','E','F'>>;

// sequential composition — '#' then '!'
using HashBang = ParseDef<char, Char<'#'>, Char<'!'>>;

auto r = HashBang::run("#!ok");
if (r) { /* r.val, r.rest */ }
```

## Also provides

`wcw.h` — `Quick::Range<T,l,h>` / `Quick::Ranges<OO...>` check-only utilities, plus the `WCWidthCJK` wide-character width table used by OneMenu.

## Dependencies

- [HAPI](https://github.com/InternetOfPins/HAPI)

## License

MIT
