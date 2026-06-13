/**
 * @file oneParse.h
 * @author Rui Azevedo (neu-rah) (ruihfazevedo@gmail.com)
 * @brief OneParse - Parser combinator components for HAPI and embedded systems
 */

#pragma once

#include <cstddef>
#include <hapi/hapi.h>
#include <oneParse/wcw.h>
using hapi::APIOf;
using hapi::Chain;

namespace oneParse {

  using Src = const char*;

  // Parse result — value type, no heap, fits in registers
  template<typename T>
  struct Res {
    bool ok;
    T    val;
    Src  rest;
    operator bool() const { return ok; }
  };

  // Base parser API — end of chain, all components matched
  // T is the result type surfaced to the caller
  template<typename T>
  struct ParseAPI {
    using Type   = T;
    using Result = Res<T>;
    static Result run(Src src) { return {true, T{}, src}; }
  };

  // Composition wrapper — mirrors OneData's DataDef pattern
  // Exposes Chain-style Build/App/Ins so ParseDef participates in HAPI's meta-world:
  //   SomeParser::Ins<NewComp>   — append component at end
  //   SomeParser::App<NewComp>   — prepend component at front
  //   SomeParser::Build<W>       — unpack OO... into W<OO...>
  template<typename T, typename... OO>
  struct ParseDef : APIOf<ParseAPI<T>, OO...> {
    using Base = APIOf<ParseAPI<T>, OO...>;
    using Base::Base;
    using Type   = T;
    using Result = Res<T>;
    template<template<typename...> class W> using Build = W<OO...>;
    template<typename... XX> using App = ParseDef<T, XX..., OO...>;
    template<typename... XX> using Ins = ParseDef<T, OO..., XX...>;
  };

  // --- Utilities -------------------------------------------------------------

  template<typename A, typename B>
  struct Pair { A fst; B snd; };

  template<typename T, size_t N>
  struct Arr {
    T      data[N];
    size_t len = 0;
    bool   push(T v)         { if (len >= N) return false; data[len++] = v; return true; }
    T*       begin()         { return data; }
    T*       end()           { return data + len; }
    const T* begin()   const { return data; }
    const T* end()     const { return data + len; }
  };

  // --- Zero-width tag ---------------------------------------------------------
  // Components that can succeed without consuming input inherit from this.
  // Many<P> and Some<P> declare rules() that reject ZeroWidth inner components.
  struct ZeroWidthTag {};

  // --- String literal component ----------------------------------------------

  // Match the null-terminated string S exactly; on failure does not consume input
  // S must be a constexpr const char[] at namespace scope (C++17 NTTP requirement)
  template<const char* S>
  struct Str {
    template<typename O>
    struct Part : O {
      using Base = O;
      using Base::Base;
      static auto run(Src src) -> typename Base::Result {
        Src orig = src;
        for (const char* s = S; *s; ++s, ++src)
          if (!src || *src != *s) return {false, {}, orig};
        return Base::run(src);
      }
    };
  };

  // --- Leaf components -------------------------------------------------------

  // Match one specific character
  template<char C>
  struct Char {
    template<typename O>
    struct Part : O {
      using Base = O;
      using Base::Base;
      static auto run(Src src) -> typename Base::Result {
        if (src && *src == C) {
          auto r = Base::run(src + 1);   // consume, continue chain
          if (r.ok) r.val = C;
          return r;
        }
        return {false, {}, src};
      }
    };
  };

  // Match any character satisfying a predicate
  template<bool(*F)(char)>
  struct Satisfy {
    template<typename O>
    struct Part : O {
      using Base = O;
      using Base::Base;
      static auto run(Src src) -> typename Base::Result {
        if (src && F(*src)) {
          auto r = Base::run(src + 1);   // consume, continue chain
          if (r.ok) r.val = *src;
          return r;
        }
        return {false, {}, src};
      }
    };
  };

  // Match a character in the inclusive range [Lo, Hi] — delegates to Quick::Range
  template<char Lo, char Hi>
  struct Range {
    template<typename O>
    struct Part : O {
      using Base = O;
      using Base::Base;
      static auto run(Src src) -> typename Base::Result {
        if (src && Quick::Range<char,Lo,Hi>::chk(*src)) {
          auto r = Base::run(src + 1);
          if (r.ok) r.val = *src;
          return r;
        }
        return {false, {}, src};
      }
    };
  };

  // Match any character accepted by any of the Quick::Range/Ranges-compatible types
  template<typename... RR>
  struct Ranges {
    template<typename O>
    struct Part : O {
      using Base = O;
      using Base::Base;
      static auto run(Src src) -> typename Base::Result {
        if (src && Quick::Ranges<RR...>::chk(*src)) {
          auto r = Base::run(src + 1);
          if (r.ok) r.val = *src;
          return r;
        }
        return {false, {}, src};
      }
    };
  };

  // Match any character in the compile-time character set
  template<char... Cs>
  struct AnyOf {
    static constexpr bool contains(char c) { return ((c == Cs) || ...); }
    template<typename O>
    struct Part : O {
      using Base = O;
      using Base::Base;
      static auto run(Src src) -> typename Base::Result {
        if (src && contains(*src)) {
          auto r = Base::run(src + 1);
          if (r.ok) r.val = *src;
          return r;
        }
        return {false, {}, src};
      }
    };
  };

  // --- Negation --------------------------------------------------------------

  // Match any single character where P does NOT match
  template<typename P>
  struct Not {
    template<typename O>
    struct Part : O {
      using Base = O;
      using Base::Base;
      static auto run(Src src) -> typename Base::Result {
        if (!src || !*src) return {false, {}, src};
        if (Chain<P>::template Part<ParseAPI<char>>::run(src).ok) return {false, {}, src};
        auto r = Base::run(src + 1);
        if (r.ok) r.val = *src;
        return r;
      }
    };
  };

  // Match any char NOT in the compile-time character set
  template<char... Cs>
  using NoneOf = Not<AnyOf<Cs...>>;

  // --- Meta parsers ----------------------------------------------------------

  // Match zero or one occurrence of component P; always succeeds
  // On match: advances input and sets r.val; on no match: leaves input and val untouched
  template<typename P>
  struct Opt : ZeroWidthTag {
    template<typename O>
    struct Part : O {
      using Base = O;
      using Base::Base;
      static auto run(Src src) -> typename Base::Result {
        auto probe = Chain<P>::template Part<ParseAPI<char>>::run(src);
        if (probe.ok) {
          auto r = Base::run(probe.rest);
          if (r.ok) r.val = probe.val;
          return r;
        }
        return Base::run(src);
      }
    };
  };

  // Match one or more occurrences of component P; fails if zero matches
  // r.val is left for the chain to fill — use r.rest vs original src for the span
  template<typename P>
  struct Some {
    static_assert(!std::is_base_of_v<ZeroWidthTag, P>, "Some<P>: P is zero-width — infinite loop");
    template<typename O>
    struct Part : O {
      using Base = O;
      using Base::Base;
      static auto run(Src src) -> typename Base::Result {
        auto first = Chain<P>::template Part<ParseAPI<char>>::run(src);
        if (!first.ok) return {false, {}, src};
        src = first.rest;
        while (src && *src) {
          auto probe = Chain<P>::template Part<ParseAPI<char>>::run(src);
          if (!probe.ok) break;
          src = probe.rest;
        }
        return Base::run(src);
      }
    };
  };

  // Match zero or more occurrences of component P (Kleene star); always succeeds
  // r.val is left for the chain to fill — use r.rest vs original src for the span
  // For alternatives use Many<Or<P1,P2>>
  template<typename P>
  struct Many : ZeroWidthTag {
    static_assert(!std::is_base_of_v<ZeroWidthTag, P>, "Many<P>: P is zero-width — infinite loop");
    template<typename O>
    struct Part : O {
      using Base = O;
      using Base::Base;
      static auto run(Src src) -> typename Base::Result {
        while (src && *src) {
          auto probe = Chain<P>::template Part<ParseAPI<char>>::run(src);
          if (!probe.ok) break;
          src = probe.rest;
        }
        return Base::run(src);
      }
    };
  };


  // Advance past component chain PP... without contributing a value to the chain
  template<typename... PP>
  struct Skip {
    template<typename O>
    struct Part : O {
      using Base = O;
      using Base::Base;
      static auto run(Src src) -> typename Base::Result {
        auto probe = Chain<PP...>::template Part<ParseAPI<char>>::run(src);
        if (!probe.ok) return {false, {}, src};
        return Base::run(probe.rest);
      }
    };
  };

  // Try component P1; on failure try component P2; both share the chain's T
  // Inherits ZeroWidthTag if either branch is zero-width — propagates recursively
  template<typename P1, typename P2>
  struct Or : std::conditional_t<
      std::is_base_of_v<ZeroWidthTag,P1> || std::is_base_of_v<ZeroWidthTag,P2>,
      ZeroWidthTag, hapi::Nil> {
    template<typename O>
    struct Part : O {
      using Base = O;
      using Base::Base;
      using T = typename Base::Type;
      static auto run(Src src) -> typename Base::Result {
        auto r1 = Chain<P1>::template Part<ParseAPI<T>>::run(src);
        if (r1.ok) { auto r = Base::run(r1.rest); if (r.ok) r.val = r1.val; return r; }
        auto r2 = Chain<P2>::template Part<ParseAPI<T>>::run(src);
        if (r2.ok) { auto r = Base::run(r2.rest); if (r.ok) r.val = r2.val; return r; }
        return {false, {}, src};
      }
    };
  };

  // Sequential composition of two complete parsers yielding Pair<P1::Type, P2::Type>
  template<typename P1, typename P2>
  struct Seq {
    template<typename O>
    struct Part : O {
      using Base = O;
      using Base::Base;
      static auto run(Src src) -> typename Base::Result {
        auto r1 = P1::run(src);
        if (!r1.ok) return {false, {}, src};
        auto r2 = P2::run(r1.rest);
        if (!r2.ok) return {false, {}, src};
        auto r = Base::run(r2.rest);
        if (r.ok) r.val = {r1.val, r2.val};
        return r;
      }
    };
  };

  // Parse component chain PP... as T_in, apply F(val) to produce the outer T
  template<typename T_in, auto F, typename... PP>
  struct To {
    template<typename O>
    struct Part : O {
      using Base = O;
      using Base::Base;
      static auto run(Src src) -> typename Base::Result {
        auto probe = Chain<PP...>::template Part<ParseAPI<T_in>>::run(src);
        if (!probe.ok) return {false, {}, src};
        auto r = Base::run(probe.rest);
        if (r.ok) r.val = F(probe.val);
        return r;
      }
    };
  };

  // Run complete parser P; construct T_out{P::Type} to produce the outer T
  // Type-directed variant of To — no explicit transform; T_out must be constructible from P::Type
  template<typename T_out, typename P>
  struct As {
    template<typename O>
    struct Part : O {
      using Base = O;
      using Base::Base;
      static auto run(Src src) -> typename Base::Result {
        auto probe = P::run(src);
        if (!probe.ok) return {false, {}, src};
        auto r = Base::run(probe.rest);
        if (r.ok) r.val = T_out{probe.val};
        return r;
      }
    };
  };

  // Call F(val) on each match of complete parser P; always succeeds (zero or more)
  template<typename P, auto F>
  struct ManyFn {
    template<typename O>
    struct Part : O {
      using Base = O;
      using Base::Base;
      static auto run(Src src) -> typename Base::Result {
        while (src && *src) {
          auto probe = P::run(src);
          if (!probe.ok) break;
          F(probe.val);
          src = probe.rest;
        }
        return Base::run(src);
      }
    };
  };

  // Collect 1..N matches of complete parser P into Arr<P::Type, N>; fails if zero or overflow
  template<typename P, size_t N>
  struct SomeN {
    using ElemT = typename P::Type;
    template<typename O>
    struct Part : O {
      using Base = O;
      using Base::Base;
      static auto run(Src src) -> typename Base::Result {
        auto first = P::run(src);
        if (!first.ok) return {false, {}, src};
        Arr<ElemT, N> arr{};
        arr.push(first.val);
        src = first.rest;
        while (src && *src) {
          auto probe = P::run(src);
          if (!probe.ok) break;
          if (!arr.push(probe.val)) return {false, {}, src};
          src = probe.rest;
        }
        auto r = Base::run(src);
        if (r.ok) r.val = arr;
        return r;
      }
    };
  };

  // Collect up to N matches of complete parser P into Arr<P::Type, N>; fails on overflow
  template<typename P, size_t N>
  struct ManyN {
    using ElemT = typename P::Type;
    template<typename O>
    struct Part : O {
      using Base = O;
      using Base::Base;
      static auto run(Src src) -> typename Base::Result {
        Arr<ElemT, N> arr{};
        while (src && *src) {
          auto probe = P::run(src);
          if (!probe.ok) break;
          if (!arr.push(probe.val)) return {false, {}, src};
          src = probe.rest;
        }
        auto r = Base::run(src);
        if (r.ok) r.val = arr;
        return r;
      }
    };
  };

  // Call F(src) at runtime, breaking template recursion for self-referential parsers
  // F must be declared before this template is instantiated (forward declaration is enough)
  template<typename T, auto F>
  struct Defer {
    template<typename O>
    struct Part : O {
      using Base = O;
      using Base::Base;
      static auto run(Src src) -> typename Base::Result {
        auto probe = F(src);
        if (!probe.ok) return {false, {}, src};
        auto r = Base::run(probe.rest);
        if (r.ok) r.val = probe.val;
        return r;
      }
    };
  };

  // Match any single non-null character
  struct Any {
    template<typename O>
    struct Part : O {
      using Base = O;
      using Base::Base;
      static auto run(Src src) -> typename Base::Result {
        if (src && *src) {
          auto r = Base::run(src + 1);
          if (r.ok) r.val = *src;
          return r;
        }
        return {false, {}, src};
      }
    };
  };

  // Succeed only at end of input
  struct Eof : ZeroWidthTag {
    template<typename O>
    struct Part : O {
      using Base = O;
      using Base::Base;
      static auto run(Src src) -> typename Base::Result {
        if (!src || !*src) return Base::run(src);
        return {false, {}, src};
      }
    };
  };


  // Advance past component P zero or more times, stopping when End matches
  // End is checked before P on each step; does not consume End
  // Fails if End never matches before the input is exhausted
  template<typename P, typename End>
  struct ManyTill {
    template<typename O>
    struct Part : O {
      using Base = O;
      using Base::Base;
      static auto run(Src src) -> typename Base::Result {
        Src cur = src;
        while (true) {
          if (Chain<End>::template Part<ParseAPI<char>>::run(cur).ok) break;
          if (!cur || !*cur) return {false, {}, src};
          auto probe = Chain<P>::template Part<ParseAPI<char>>::run(cur);
          if (!probe.ok) return {false, {}, src};
          cur = probe.rest;
        }
        return Base::run(cur);
      }
    };
  };

  // Skip Open component, run complete parser P, skip Close component; yield P's result
  template<typename Open, typename P, typename Close>
  struct Between {
    template<typename O>
    struct Part : O {
      using Base = O;
      using Base::Base;
      static auto run(Src src) -> typename Base::Result {
        Src orig = src;
        auto open = Chain<Open>::template Part<ParseAPI<char>>::run(src);
        if (!open.ok) return {false, {}, orig};
        auto inner = P::run(open.rest);
        if (!inner.ok) return {false, {}, orig};
        auto close = Chain<Close>::template Part<ParseAPI<char>>::run(inner.rest);
        if (!close.ok) return {false, {}, orig};
        auto r = Base::run(close.rest);
        if (r.ok) r.val = inner.val;
        return r;
      }
    };
  };

  // Parse complete parser P separated by component Sep; collect up to N items; zero or more
  // Backtracks if item fails after a matched separator, stops collecting (does not fail)
  template<typename P, typename Sep, size_t N>
  struct SepBy {
    using ElemT = typename P::Type;
    template<typename O>
    struct Part : O {
      using Base = O;
      using Base::Base;
      static auto run(Src src) -> typename Base::Result {
        Arr<ElemT, N> arr{};
        auto first = P::run(src);
        if (!first.ok) {
          auto r = Base::run(src);
          if (r.ok) r.val = arr;
          return r;
        }
        if (!arr.push(first.val)) return {false, {}, src};
        src = first.rest;
        while (src) {
          auto sep = Chain<Sep>::template Part<ParseAPI<char>>::run(src);
          if (!sep.ok) break;
          auto item = P::run(sep.rest);
          if (!item.ok) break;
          if (!arr.push(item.val)) return {false, {}, src};
          src = item.rest;
        }
        auto r = Base::run(src);
        if (r.ok) r.val = arr;
        return r;
      }
    };
  };

  // Same as SepBy but fails if zero items are parsed
  template<typename P, typename Sep, size_t N>
  struct SepBy1 {
    using ElemT = typename P::Type;
    template<typename O>
    struct Part : O {
      using Base = O;
      using Base::Base;
      static auto run(Src src) -> typename Base::Result {
        Arr<ElemT, N> arr{};
        auto first = P::run(src);
        if (!first.ok) return {false, {}, src};
        if (!arr.push(first.val)) return {false, {}, src};
        src = first.rest;
        while (src) {
          auto sep = Chain<Sep>::template Part<ParseAPI<char>>::run(src);
          if (!sep.ok) break;
          auto item = P::run(sep.rest);
          if (!item.ok) break;
          if (!arr.push(item.val)) return {false, {}, src};
          src = item.rest;
        }
        auto r = Base::run(src);
        if (r.ok) r.val = arr;
        return r;
      }
    };
  };

  // Run complete parser P; if F(val) returns false, fail without consuming input
  template<typename P, auto F>
  struct Verify {
    template<typename O>
    struct Part : O {
      using Base = O;
      using Base::Base;
      static auto run(Src src) -> typename Base::Result {
        auto probe = P::run(src);
        if (!probe.ok || !F(probe.val)) return {false, {}, src};
        auto r = Base::run(probe.rest);
        if (r.ok) r.val = probe.val;
        return r;
      }
    };
  };

  // --- Sugar ------------------------------------------------------------------

  constexpr bool isDigit(char c) { return c >= '0' && c <= '9'; }
  constexpr bool isAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
  constexpr bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

  using Digit = Satisfy<isDigit>;
  using Alpha = Satisfy<isAlpha>;
  using Space = Satisfy<isSpace>;

}; // namespace oneParse

// ParseDef is a first-class HAPI citizen — all meta-tools traverse it like APIOf
namespace hapi {
  template<typename F, typename T, typename... OO>
  struct Map<F, oneParse::ParseDef<T, OO...>> {
    using Expr = oneParse::ParseDef<T, typename Map<F, OO>::Expr...>;
  };

  template<typename P, typename T, typename... OO>
  struct FilterIf<P, oneParse::ParseDef<T, OO...>> {
  private:
    using Filtered = typename FilterIf<P, Chain<OO...>>::Expr;
    template<typename C> struct Rebuild;
    template<typename... XX> struct Rebuild<Chain<XX...>> {
      using type = oneParse::ParseDef<T, XX...>;
    };
  public:
    using Expr = typename Rebuild<Filtered>::type;
  };
};
