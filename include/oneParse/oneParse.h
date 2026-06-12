/**
 * @file oneParse.h
 * @author Rui Azevedo (neu-rah) (ruihfazevedo@gmail.com)
 * @brief OneParse - Parser combinator components for HAPI and embedded systems
 */

#pragma once

#include <hapi/hapi.h>
#include <oneParse/wcw.h>
using hapi::APIOf;

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
  template<typename T, typename... OO>
  struct ParseDef : APIOf<ParseAPI<T>, OO...> {
    using Base = APIOf<ParseAPI<T>, OO...>;
    using Base::Base;
    using Type   = T;
    using Result = Res<T>;
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

  // --- Sugar ------------------------------------------------------------------

  constexpr bool isDigit(char c) { return c >= '0' && c <= '9'; }
  constexpr bool isAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
  constexpr bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

  using Digit = Satisfy<isDigit>;
  using Alpha = Satisfy<isAlpha>;
  using Space = Satisfy<isSpace>;

}; // namespace oneParse
