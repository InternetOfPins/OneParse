/**
 * @file oneParse.h
 * @author Rui Azevedo (neu-rah) (ruihfazevedo@gmail.com)
 * @brief OneParse - Parser combinator components for HAPI and embedded systems
 */

#pragma once

#include <cstddef>
#include <cstring>
#include <string>
#include <hapi/hapi.h>
#include <oneParse/wcw.h>
using hapi::APIOf;
using hapi::Chain;

namespace oneParse {

  using Src = const char*;

  // Short snippet of remaining input for error messages
  inline std::string snip(Src s, size_t n = 16) {
    if (!s || !*s) return "<end>";
    size_t len = std::strlen(s);
    return '"' + std::string(s, len < n ? len : n) + (len > n ? "..." : "") + '"';
  }

  // Parse result — value + position + error trace
  // err: empty on success; innermost failure message first, each outer caller appends
  template<typename T>
  struct Res {
    bool        ok;
    T           val;
    Src         rest;
    std::string err;
    operator bool() const { return ok; }
  };

  // Base parser API — end of chain, all components matched
  template<typename T>
  struct ParseAPI {
    using Type   = T;
    using Result = Res<T>;
    static Result run(Src src) { return {true, T{}, src}; }
  };

  // Composition wrapper — mirrors OneData's DataDef pattern
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
  // Many<P> and Some<P> static_assert against ZeroWidth inner components.
  struct ZeroWidthTag {};

  // --- Value-leaf tag ---------------------------------------------------------
  // Components that overwrite r.val on the way back up the call stack inherit from this.
  // Only one ValueLeafTag component may appear as a direct member of any flat Chain;
  // additional value sources must be structural (Or, Opt, To, As, Seq, ...).
  struct ValueLeafTag {};

  // --- String literal component ----------------------------------------------

  template<const char* S>
  struct Str {
    template<typename O>
    struct Part : O {
      using Base = O;
      using Base::Base;
      static auto run(Src src) -> typename Base::Result {
        static constexpr std::size_t N = std::char_traits<char>::length(S);
        if (!src || std::strncmp(src, S, N) != 0)
          return {false, {}, src, std::string("expected \"") + S + "\" at " + snip(src)};
        auto r = Base::run(src + N);
        if (!r.ok) r.err += "\n  <- Str at " + snip(src);
        return r;
      }
    };
  };

  // --- Leaf components -------------------------------------------------------

  template<char C>
  struct Char : ValueLeafTag {
    template<typename O>
    struct Part : O {
      using Base = O;
      using Base::Base;
      static auto run(Src src) -> typename Base::Result {
        if (src && *src == C) {
          auto r = Base::run(src + 1);
          if (r.ok) r.val = C;
          else      r.err += "\n  <- Char at " + snip(src);
          return r;
        }
        return {false, {}, src, std::string("expected '") + C + "' at " + snip(src)};
      }
    };
  };

  template<bool(*F)(char)>
  struct Satisfy : ValueLeafTag {
    template<typename O>
    struct Part : O {
      using Base = O;
      using Base::Base;
      static auto run(Src src) -> typename Base::Result {
        if (src && F(*src)) {
          auto r = Base::run(src + 1);
          if (r.ok) r.val = *src;
          else      r.err += "\n  <- Satisfy at " + snip(src);
          return r;
        }
        return {false, {}, src, std::string("predicate failed at ") + snip(src)};
      }
    };
  };

  template<char Lo, char Hi>
  struct Range : ValueLeafTag {
    template<typename O>
    struct Part : O {
      using Base = O;
      using Base::Base;
      static auto run(Src src) -> typename Base::Result {
        if (src && Quick::Range<char,Lo,Hi>::chk(*src)) {
          auto r = Base::run(src + 1);
          if (r.ok) r.val = *src;
          else      r.err += "\n  <- Range at " + snip(src);
          return r;
        }
        return {false, {}, src,
          std::string("expected ['") + Lo + "'-'" + Hi + "'] at " + snip(src)};
      }
    };
  };

  template<typename... RR>
  struct Ranges : ValueLeafTag {
    template<typename O>
    struct Part : O {
      using Base = O;
      using Base::Base;
      static auto run(Src src) -> typename Base::Result {
        if (src && Quick::Ranges<RR...>::chk(*src)) {
          auto r = Base::run(src + 1);
          if (r.ok) r.val = *src;
          else      r.err += "\n  <- Ranges at " + snip(src);
          return r;
        }
        return {false, {}, src, std::string("ranges mismatch at ") + snip(src)};
      }
    };
  };

  template<char... Cs>
  struct AnyOf : ValueLeafTag {
    static constexpr bool contains(char c) { return ((c == Cs) || ...); }
    template<typename O>
    struct Part : O {
      using Base = O;
      using Base::Base;
      static auto run(Src src) -> typename Base::Result {
        if (src && contains(*src)) {
          auto r = Base::run(src + 1);
          if (r.ok) r.val = *src;
          else      r.err += "\n  <- AnyOf at " + snip(src);
          return r;
        }
        return {false, {}, src, std::string("none matched at ") + snip(src)};
      }
    };
  };

  // --- Negation --------------------------------------------------------------

  template<typename P>
  struct Not : ValueLeafTag {
    template<typename O>
    struct Part : O {
      using Base = O;
      using Base::Base;
      static auto run(Src src) -> typename Base::Result {
        if (!src || !*src)
          return {false, {}, src, std::string("not: unexpected end at ") + snip(src)};
        if (Chain<P>::template Part<ParseAPI<char>>::run(src).ok)
          return {false, {}, src, std::string("not: unexpected match at ") + snip(src)};
        auto r = Base::run(src + 1);
        if (r.ok) r.val = *src;
        else      r.err += "\n  <- Not at " + snip(src);
        return r;
      }
    };
  };

  template<char... Cs>
  using NoneOf = Not<AnyOf<Cs...>>;

  // --- Meta parsers ----------------------------------------------------------

  // Match zero or one occurrence of P; always succeeds
  template<typename P>
  struct Opt : ZeroWidthTag {
    template<typename O>
    struct Part : O {
      using Base = O;
      using Base::Base;
      using T = typename Base::Type;
      static auto run(Src src) -> typename Base::Result {
        auto probe = Chain<P>::template Part<ParseAPI<T>>::run(src);
        if (probe.ok) {
          auto r = Base::run(probe.rest);
          if (r.ok) r.val = probe.val;
          else      r.err += "\n  <- Opt (matched) at " + snip(src);
          return r;
        }
        auto r = Base::run(src);
        if (!r.ok) r.err += "\n  <- Opt (unmatched) at " + snip(src);
        return r;
      }
    };
  };

  // Match one or more occurrences of P; fails if zero matches
  template<typename P>
  struct Some {
    static_assert(!std::is_base_of_v<ZeroWidthTag, P>, "Some<P>: P is zero-width — infinite loop");
    template<typename O>
    struct Part : O {
      using Base = O;
      using Base::Base;
      static auto run(Src src) -> typename Base::Result {
        Src orig = src;
        auto first = Chain<P>::template Part<ParseAPI<char>>::run(src);
        if (!first.ok) {
          typename Base::Result r{false, {}, orig,
            first.err + "\n  <- Some: at least one match required at " + snip(orig)};
          return r;
        }
        src = first.rest;
        while (src && *src) {
          auto probe = Chain<P>::template Part<ParseAPI<char>>::run(src);
          if (!probe.ok) break;
          src = probe.rest;
        }
        auto r = Base::run(src);
        if (!r.ok) r.err += "\n  <- Some at " + snip(orig);
        return r;
      }
    };
  };

  // Match zero or more occurrences of P (star / *); always succeeds
  template<typename P>
  struct Many : ZeroWidthTag {
    static_assert(!std::is_base_of_v<ZeroWidthTag, P>, "Many<P>: P is zero-width — infinite loop");
    template<typename O>
    struct Part : O {
      using Base = O;
      using Base::Base;
      static auto run(Src src) -> typename Base::Result {
        Src orig = src;
        while (src && *src) {
          auto probe = Chain<P>::template Part<ParseAPI<char>>::run(src);
          if (!probe.ok) break;
          src = probe.rest;
        }
        auto r = Base::run(src);
        if (!r.ok) r.err += "\n  <- Many at " + snip(orig);
        return r;
      }
    };
  };

  // Fast whitespace skip — tighter than Many<Space>; uses a single byte compare
  struct SkipWs : ZeroWidthTag {
    template<typename O>
    struct Part : O {
      using Base = O;
      using Base::Base;
      static auto run(Src src) -> typename Base::Result {
        while (src && (unsigned char)*src <= ' ') ++src;
        return Base::run(src);
      }
    };
  };

  // Advance past component chain PP... without contributing a value
  template<typename... PP>
  struct Skip {
    template<typename O>
    struct Part : O {
      using Base = O;
      using Base::Base;
      static auto run(Src src) -> typename Base::Result {
        auto probe = Chain<PP...>::template Part<ParseAPI<char>>::run(src);
        if (!probe.ok) {
          typename Base::Result r{false, {}, src,
            probe.err + "\n  <- Skip at " + snip(src)};
          return r;
        }
        auto r = Base::run(probe.rest);
        if (!r.ok) r.err += "\n  <- Skip at " + snip(src);
        return r;
      }
    };
  };

  // Try P1; on failure try P2; both share the chain's T
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
        if (r1.ok) {
          auto r = Base::run(r1.rest);
          if (r.ok) r.val = r1.val;
          else      r.err += "\n  <- Or (P1) at " + snip(src);
          return r;
        }
        auto r2 = Chain<P2>::template Part<ParseAPI<T>>::run(src);
        if (r2.ok) {
          auto r = Base::run(r2.rest);
          if (r.ok) r.val = r2.val;
          else      r.err += "\n  <- Or (P2) at " + snip(src);
          return r;
        }
        typename Base::Result r{false, {}, src,
          "Or: all branches failed at " + snip(src) +
          "\n  P1: " + r1.err +
          "\n  P2: " + r2.err};
        return r;
      }
    };
  };

  // First-char dispatch: run P only if *src == C, otherwise fail without touching input.
  // Use inside Or to avoid backtracking into P when input starts with the wrong char.
  template<char C, typename P>
  struct FirstChar
      : std::conditional_t<std::is_base_of_v<ZeroWidthTag,P>, ZeroWidthTag, hapi::Nil> {
    template<typename O>
    struct Part : O {
      using Base = O;
      using Base::Base;
      using T = typename Base::Type;
      static auto run(Src src) -> typename Base::Result {
        if (!src || *src != C)
          return {false, {}, src};  // empty err — Or discards it if another branch succeeds
        return Chain<P>::template Part<ParseAPI<T>>::run(src);
      }
    };
  };

  // Sequential composition of two complete parsers yielding Pair<T1,T2>
  template<typename P1, typename P2>
  struct Seq {
    template<typename O>
    struct Part : O {
      using Base = O;
      using Base::Base;
      static auto run(Src src) -> typename Base::Result {
        auto r1 = P1::run(src);
        if (!r1.ok) {
          typename Base::Result r{false, {}, src,
            r1.err + "\n  <- Seq (first) at " + snip(src)};
          return r;
        }
        auto r2 = P2::run(r1.rest);
        if (!r2.ok) {
          typename Base::Result r{false, {}, src,
            r2.err + "\n  <- Seq (second) at " + snip(r1.rest)};
          return r;
        }
        auto r = Base::run(r2.rest);
        if (r.ok) r.val = {r1.val, r2.val};
        else      r.err += "\n  <- Seq at " + snip(src);
        return r;
      }
    };
  };

  // Parse chain PP... as T_in, apply F(val) to produce the outer T
  template<typename T_in, auto F, typename... PP>
  struct To {
    template<typename O>
    struct Part : O {
      using Base = O;
      using Base::Base;
      static auto run(Src src) -> typename Base::Result {
        auto probe = Chain<PP...>::template Part<ParseAPI<T_in>>::run(src);
        if (!probe.ok) {
          typename Base::Result r{false, {}, src,
            probe.err + "\n  <- To at " + snip(src)};
          return r;
        }
        auto r = Base::run(probe.rest);
        if (r.ok) r.val = F(probe.val);
        else      r.err += "\n  <- To at " + snip(src);
        return r;
      }
    };
  };

  // Run complete parser P; construct T_out{P::Type}
  template<typename T_out, typename P>
  struct As {
    template<typename O>
    struct Part : O {
      using Base = O;
      using Base::Base;
      static auto run(Src src) -> typename Base::Result {
        auto probe = P::run(src);
        if (!probe.ok) {
          typename Base::Result r{false, {}, src,
            probe.err + "\n  <- As at " + snip(src)};
          return r;
        }
        auto r = Base::run(probe.rest);
        if (r.ok) r.val = T_out{probe.val};
        else      r.err += "\n  <- As at " + snip(src);
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
        Src orig = src;
        while (src && *src) {
          auto probe = P::run(src);
          if (!probe.ok) break;
          F(probe.val);
          src = probe.rest;
        }
        auto r = Base::run(src);
        if (!r.ok) r.err += "\n  <- ManyFn at " + snip(orig);
        return r;
      }
    };
  };

  // Collect 1..N matches of complete parser P into Arr<P::Type, N>
  template<typename P, size_t N>
  struct SomeN {
    using ElemT = typename P::Type;
    template<typename O>
    struct Part : O {
      using Base = O;
      using Base::Base;
      static auto run(Src src) -> typename Base::Result {
        Src orig = src;
        auto first = P::run(src);
        if (!first.ok) {
          typename Base::Result r{false, {}, orig,
            first.err + "\n  <- SomeN: at least one match required at " + snip(orig)};
          return r;
        }
        Arr<ElemT, N> arr{};
        arr.push(first.val);
        src = first.rest;
        while (src && *src) {
          auto probe = P::run(src);
          if (!probe.ok) break;
          if (!arr.push(probe.val)) {
            typename Base::Result r{false, {}, orig,
              "SomeN: overflow (>" + std::to_string(N) + ") at " + snip(src)};
            return r;
          }
          src = probe.rest;
        }
        auto r = Base::run(src);
        if (r.ok) r.val = arr;
        else      r.err += "\n  <- SomeN at " + snip(orig);
        return r;
      }
    };
  };

  // Collect up to N matches of complete parser P into Arr<P::Type, N>
  template<typename P, size_t N>
  struct ManyN {
    using ElemT = typename P::Type;
    template<typename O>
    struct Part : O {
      using Base = O;
      using Base::Base;
      static auto run(Src src) -> typename Base::Result {
        Src orig = src;
        Arr<ElemT, N> arr{};
        while (src && *src) {
          auto probe = P::run(src);
          if (!probe.ok) break;
          if (!arr.push(probe.val)) {
            typename Base::Result r{false, {}, orig,
              "ManyN: overflow (>" + std::to_string(N) + ") at " + snip(src)};
            return r;
          }
          src = probe.rest;
        }
        auto r = Base::run(src);
        if (r.ok) r.val = arr;
        else      r.err += "\n  <- ManyN at " + snip(orig);
        return r;
      }
    };
  };

  // Call F(src) at runtime, breaking template recursion for self-referential parsers
  template<typename T, auto F>
  struct Defer {
    template<typename O>
    struct Part : O {
      using Base = O;
      using Base::Base;
      static auto run(Src src) -> typename Base::Result {
        auto probe = F(src);
        if (!probe.ok) {
          typename Base::Result r{false, {}, src,
            probe.err + "\n  <- Defer at " + snip(src)};
          return r;
        }
        auto r = Base::run(probe.rest);
        if (r.ok) r.val = probe.val;
        else      r.err += "\n  <- Defer at " + snip(src);
        return r;
      }
    };
  };

  // Match any single non-null character
  struct Any : ValueLeafTag {
    template<typename O>
    struct Part : O {
      using Base = O;
      using Base::Base;
      static auto run(Src src) -> typename Base::Result {
        if (src && *src) {
          auto r = Base::run(src + 1);
          if (r.ok) r.val = *src;
          else      r.err += "\n  <- Any at " + snip(src);
          return r;
        }
        return {false, {}, src, std::string("unexpected end of input")};
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
        if (!src || !*src) {
          auto r = Base::run(src);
          if (!r.ok) r.err += "\n  <- Eof";
          return r;
        }
        return {false, {}, src, std::string("expected end of input at ") + snip(src)};
      }
    };
  };

  // Advance past P zero or more times, stopping when End matches; does not consume End
  template<typename P, typename End>
  struct ManyTill {
    template<typename O>
    struct Part : O {
      using Base = O;
      using Base::Base;
      static auto run(Src src) -> typename Base::Result {
        Src orig = src;
        Src cur = src;
        while (true) {
          if (Chain<End>::template Part<ParseAPI<char>>::run(cur).ok) break;
          if (!cur || !*cur)
            return {false, {}, orig,
              std::string("ManyTill: end condition never matched, reached end at ") + snip(orig)};
          auto probe = Chain<P>::template Part<ParseAPI<char>>::run(cur);
          if (!probe.ok) {
            typename Base::Result r{false, {}, orig,
              probe.err + "\n  <- ManyTill at " + snip(orig)};
            return r;
          }
          cur = probe.rest;
        }
        auto r = Base::run(cur);
        if (!r.ok) r.err += "\n  <- ManyTill at " + snip(orig);
        return r;
      }
    };
  };

  // Skip Open, run complete parser P, skip Close; yield P's result
  template<typename Open, typename P, typename Close>
  struct Between {
    template<typename O>
    struct Part : O {
      using Base = O;
      using Base::Base;
      static auto run(Src src) -> typename Base::Result {
        Src orig = src;
        auto open = Chain<Open>::template Part<ParseAPI<char>>::run(src);
        if (!open.ok) {
          typename Base::Result r{false, {}, orig,
            open.err + "\n  <- Between (open) at " + snip(orig)};
          return r;
        }
        auto inner = P::run(open.rest);
        if (!inner.ok) {
          typename Base::Result r{false, {}, orig,
            inner.err + "\n  <- Between (inner) at " + snip(open.rest)};
          return r;
        }
        auto close = Chain<Close>::template Part<ParseAPI<char>>::run(inner.rest);
        if (!close.ok) {
          typename Base::Result r{false, {}, orig,
            close.err + "\n  <- Between (close) at " + snip(inner.rest)};
          return r;
        }
        auto r = Base::run(close.rest);
        if (r.ok) r.val = inner.val;
        else      r.err += "\n  <- Between at " + snip(orig);
        return r;
      }
    };
  };

  // Parse complete parser P separated by Sep; collect up to N items; zero or more
  template<typename P, typename Sep, size_t N>
  struct SepBy {
    using ElemT = typename P::Type;
    template<typename O>
    struct Part : O {
      using Base = O;
      using Base::Base;
      static auto run(Src src) -> typename Base::Result {
        Src orig = src;
        Arr<ElemT, N> arr{};
        auto first = P::run(src);
        if (!first.ok) {
          auto r = Base::run(src);
          if (r.ok) r.val = arr;
          else      r.err += "\n  <- SepBy at " + snip(orig);
          return r;
        }
        if (!arr.push(first.val))
          return {false, {}, orig, "SepBy: overflow (>" + std::to_string(N) + ") at " + snip(src)};
        src = first.rest;
        while (src) {
          auto sep = Chain<Sep>::template Part<ParseAPI<char>>::run(src);
          if (!sep.ok) break;
          auto item = P::run(sep.rest);
          if (!item.ok) break;
          if (!arr.push(item.val))
            return {false, {}, orig, "SepBy: overflow (>" + std::to_string(N) + ") at " + snip(src)};
          src = item.rest;
        }
        auto r = Base::run(src);
        if (r.ok) r.val = arr;
        else      r.err += "\n  <- SepBy at " + snip(orig);
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
        Src orig = src;
        Arr<ElemT, N> arr{};
        auto first = P::run(src);
        if (!first.ok) {
          typename Base::Result r{false, {}, orig,
            first.err + "\n  <- SepBy1: at least one item required at " + snip(orig)};
          return r;
        }
        if (!arr.push(first.val))
          return {false, {}, orig, "SepBy1: overflow (>" + std::to_string(N) + ") at " + snip(src)};
        src = first.rest;
        while (src) {
          auto sep = Chain<Sep>::template Part<ParseAPI<char>>::run(src);
          if (!sep.ok) break;
          auto item = P::run(sep.rest);
          if (!item.ok) break;
          if (!arr.push(item.val))
            return {false, {}, orig, "SepBy1: overflow (>" + std::to_string(N) + ") at " + snip(src)};
          src = item.rest;
        }
        auto r = Base::run(src);
        if (r.ok) r.val = arr;
        else      r.err += "\n  <- SepBy1 at " + snip(orig);
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
        if (!probe.ok) {
          typename Base::Result r{false, {}, src,
            probe.err + "\n  <- Verify at " + snip(src)};
          return r;
        }
        if (!F(probe.val))
          return {false, {}, src, std::string("Verify: predicate rejected value at ") + snip(src)};
        auto r = Base::run(probe.rest);
        if (r.ok) r.val = probe.val;
        else      r.err += "\n  <- Verify at " + snip(src);
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
