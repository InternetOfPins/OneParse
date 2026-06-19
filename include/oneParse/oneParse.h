#pragma once

#include <stddef.h>
#include <stdint.h>
#include <tuple>
#include <hapi/hapi.h>
#include <oneOutput/oneOutput.h>
using hapi::APIOf;
using hapi::Chain;
using hapi::Nil;

namespace oneParse {

  // ── Result ─────────────────────────────────────────────────────────────────
  // run(char) returns Res — signal only, no stored value.
  // Matched values flow downstream via put(T) through the output chain.

  enum class St : uint8_t { ok, partial, fail };

  struct Res {
    St   state;
    char val{0};
    constexpr operator bool() const { return state == St::ok; }
    constexpr bool partial()  const { return state == St::partial; }
    static constexpr Res Ok(char c) { return {St::ok,      c}; }
    static constexpr Res Partial()  { return {St::partial, 0}; }
    static constexpr Res Fail()     { return {St::fail,    0}; }
  };

  // ── Base ───────────────────────────────────────────────────────────────────

  template<typename Cfg=Nil>
  struct ParseAPI : oneOutput::OutAPI<Cfg> {
    Res run(char) { return Res::Fail(); }
  };

  // ParseDef — compose parser components + output chain tail
  // e.g. ParseDef<Some<Digit>, ConsoleOut>
  //      ParseDef<Or<Digit,Sign>, MyUartOut>
  template<typename... OO>
  using ParseDef = APIOf<ParseAPI<>, OO...>;

  // ── Single-char stateless ──────────────────────────────────────────────────

  template<char q>
  struct Char {
    template<typename O> struct Part : O {
      using Base=O; using Base::Base; using Base::put;
      static constexpr bool chk(char c) { return c==q; }
      Res run(char c) {
        if (chk(c)) { put(c); return Res::Ok(c); }
        return Res::Fail();
      }
    };
  };

  template<char a, char b>
  struct Range {
    template<typename O> struct Part : O {
      using Base=O; using Base::Base; using Base::put;
      static constexpr bool chk(char c) { return a<=c&&c<=b; }
      Res run(char c) {
        if (chk(c)) { put(c); return Res::Ok(c); }
        return Res::Fail();
      }
    };
  };

  template<char... CC>
  struct AnyOf {
    template<typename O> struct Part : O {
      using Base=O; using Base::Base; using Base::put;
      static constexpr bool chk(char c) { return ((c==CC)||...); }
      Res run(char c) {
        if (chk(c)) { put(c); return Res::Ok(c); }
        return Res::Fail();
      }
    };
  };

  template<bool(*F)(char)>
  struct Satisfy {
    template<typename O> struct Part : O {
      using Base=O; using Base::Base; using Base::put;
      static bool chk(char c) { return F(c); }
      Res run(char c) {
        if (F(c)) { put(c); return Res::Ok(c); }
        return Res::Fail();
      }
    };
  };

  struct Any {
    template<typename O> struct Part : O {
      using Base=O; using Base::Base; using Base::put;
      static constexpr bool chk(char c) { return c!='\0'; }
      Res run(char c) {
        if (c) { put(c); return Res::Ok(c); }
        return Res::Fail();
      }
    };
  };

  // ── Combinators (single-char, stateless) ───────────────────────────────────

  template<typename... PP>
  struct Or {
    template<typename O> struct Part : O {
      using Base=O; using Base::Base; using Base::put;
      static constexpr bool chk(char c) {
        return (PP::template Part<ParseAPI<Nil>>::chk(c)||...);
      }
      Res run(char c) {
        if (chk(c)) { put(c); return Res::Ok(c); }
        return Res::Fail();
      }
    };
  };

  template<typename... PP>
  struct And {
    template<typename O> struct Part : O {
      using Base=O; using Base::Base; using Base::put;
      static constexpr bool chk(char c) {
        return (PP::template Part<ParseAPI<Nil>>::chk(c)&&...);
      }
      Res run(char c) {
        if (chk(c)) { put(c); return Res::Ok(c); }
        return Res::Fail();
      }
    };
  };

  template<typename P>
  struct Not {
    template<typename O> struct Part : O {
      using Base=O; using Base::Base; using Base::put;
      static constexpr bool chk(char c) {
        return !P::template Part<ParseAPI<Nil>>::chk(c);
      }
      Res run(char c) {
        if (chk(c)) { put(c); return Res::Ok(c); }
        return Res::Fail();
      }
    };
  };

  template<char... CC>
  using NoneOf = Not<AnyOf<CC...>>;

  // ── Multi-char ────────────────────────────────────────────────────────────

  // String<P> — stateless: match any char P allows, put it, keep running.
  // Fail on non-match (caller re-feeds). No state — auto-resets every call.
  template<typename P>
  struct String {
    template<typename O> struct Part : O {
      using Base=O; using Base::Base; using Base::put;
      static constexpr bool chk(char c) {
        return P::template Part<ParseAPI<Nil>>::chk(c);
      }
      Res run(char c) {
        if (chk(c)) { put(c); return Res::Ok(c); }
        return Res::Fail();
      }
    };
  };

  // Lit<CC...> — exact char sequence; puts all chars only on complete match
  template<char... CC>
  struct Lit {
    template<typename O> struct Part : O {
      using Base=O; using Base::Base; using Base::put;
      static constexpr char chars[]{CC...};
      char   buf[sizeof...(CC)]{};
      size_t pos{0};
      static constexpr bool chk(char c) { return sizeof...(CC)>0 && chars[0]==c; }

      Res run(char c) {
        if (c!=chars[pos]) { pos=0; return Res::Fail(); }
        buf[pos]=c;
        if (++pos==sizeof...(CC)) {
          pos=0;
          for (char x:buf) put(x);
          return Res::Ok(c);
        }
        return Res::Partial();
      }
    };
  };

  // 1+ matches of P; ok per matched char; fail on first non-match
  // On natural end (fail after ≥1 match) caller must re-feed the non-matching char
  template<typename P>
  struct Some {
    template<typename O> struct Part : O {
      using Base=O; using Base::Base; using Base::put;
      bool matched{false};
      static constexpr bool chk(char c) {
        return P::template Part<ParseAPI<Nil>>::chk(c);
      }
      Res run(char c) {
        if (chk(c)) { put(c); matched=true; return Res::Ok(c); }
        if (!matched) return Res::Fail();
        matched=false;
        return Res::Fail(); // natural end — caller re-feeds c
      }
    };
  };

  // 0+ matches of P; fail on non-match (caller re-feeds); zero matches is ok
  template<typename P>
  struct Many {
    template<typename O> struct Part : O {
      using Base=O; using Base::Base; using Base::put;
      static constexpr bool chk(char c) {
        return P::template Part<ParseAPI<Nil>>::chk(c);
      }
      Res run(char c) {
        if (chk(c)) { put(c); return Res::Ok(c); }
        return Res::Fail(); // caller re-feeds c
      }
    };
  };

  // Optional single char; ok whether P matched or not; non-match not consumed
  template<typename P>
  struct Opt {
    template<typename O> struct Part : O {
      using Base=O; using Base::Base; using Base::put;
      static constexpr bool chk(char c) {
        return P::template Part<ParseAPI<Nil>>::chk(c);
      }
      Res run(char c) {
        if (chk(c)) { put(c); return Res::Ok(c); }
        return Res::Ok(0); // always ok — caller re-feeds c
      }
    };
  };

  // Consume whitespace, do not put; fail on non-ws (caller re-feeds)
  struct SkipWs {
    template<typename O> struct Part : O {
      using Base=O; using Base::Base;
      static constexpr bool chk(char c) { return (unsigned char)c<=' '; }
      Res run(char c) {
        if (chk(c)) return Res::Ok(c);
        return Res::Fail();
      }
    };
  };

  // ── Meta ───────────────────────────────────────────────────────────────────
  // Meta<PP...> — drives sub-parsers in sequence.
  // Each sub-parser runs until it returns Fail; then Meta re-feeds that char
  // to the next sub-parser. cur tracks which parser is active. Resets to 0
  // and returns Fail when the last parser fails (sequence complete).

  namespace detail {
    template<size_t I, size_t N, typename Tuple>
    Res metaRun(Tuple& parts, size_t& cur, char c) {
      if constexpr (I >= N) { cur = 0; return Res::Fail(); }
      else {
        if (cur != I) return metaRun<I+1, N>(parts, cur, c);
        auto r = std::get<I>(parts).run(c);
        if (r.state != St::fail) return r;
        ++cur;
        return metaRun<I+1, N>(parts, cur, c);
      }
    }

    template<size_t I, typename... PP>
    int8_t altFind(char c) {
      if constexpr (I >= sizeof...(PP)) return -1;
      else {
        using P = std::tuple_element_t<I, std::tuple<PP...>>;
        if (P::template Part<ParseAPI<Nil>>::chk(c)) return (int8_t)I;
        return altFind<I+1, PP...>(c);
      }
    }

    template<size_t I, size_t N, typename Tuple>
    Res altRun(Tuple& parts, int8_t idx, char c) {
      if constexpr (I >= N) return Res::Fail();
      else {
        if (idx != (int8_t)I) return altRun<I+1, N>(parts, idx, c);
        return std::get<I>(parts).run(c);
      }
    }
  }

  template<typename... PP>
  struct Meta {
    template<typename O> struct Part : O {
      using Base=O; using Base::Base;
      size_t cur{0};
      std::tuple<typename PP::template Part<Base>...> parts;
      // first-char check delegates to the first parser
      static constexpr bool chk(char c) {
        using First = std::tuple_element_t<0, std::tuple<PP...>>;
        return First::template Part<ParseAPI<Nil>>::chk(c);
      }
      Res run(char c) {
        return detail::metaRun<0, sizeof...(PP)>(parts, cur, c);
      }
    };
  };

  // Alt<PP...> — committed alternation.
  // On first char: chk() selects the branch; all subsequent chars go to that
  // branch until it fails. On fail: resets, returns Fail (caller re-feeds).
  template<typename... PP>
  struct Alt {
    template<typename O> struct Part : O {
      using Base=O; using Base::Base;
      int8_t committed{-1};
      std::tuple<typename PP::template Part<Base>...> parts;

      static constexpr bool chk(char c) {
        return (PP::template Part<ParseAPI<Nil>>::chk(c)||...);
      }

      Res run(char c) {
        if (committed < 0) {
          committed = detail::altFind<0, PP...>(c);
          if (committed < 0) return Res::Fail();
        }
        auto r = detail::altRun<0, sizeof...(PP)>(parts, committed, c);
        if (r.state == St::fail) committed = -1;
        return r;
      }
    };
  };

  // ── Sugar ──────────────────────────────────────────────────────────────────

  constexpr bool isDigit(char c) { return c>='0'&&c<='9'; }
  constexpr bool isAlpha(char c) { return (c>='a'&&c<='z')||(c>='A'&&c<='Z'); }
  constexpr bool isAlNum(char c) { return isDigit(c)||isAlpha(c); }
  constexpr bool isSpace(char c) { return c==' '||c=='\t'||c=='\n'||c=='\r'; }

  using Digit   = Range<'0','9'>;
  using Alpha   = Satisfy<isAlpha>;
  using AlNum   = Satisfy<isAlNum>;
  using Space   = Satisfy<isSpace>;
  using Sign    = Or<Char<'-'>,Char<'+'>>;
  using Numeric = Or<Digit,Sign>;

  // ── Utility ────────────────────────────────────────────────────────────────

  // Fixed-capacity array — no heap, MCU-safe
  template<typename T, size_t N>
  struct Arr {
    T      data[N]{};
    size_t len{0};
    bool  push(T v) { if (len>=N) return false; data[len++]=v; return true; }
    void  reset()   { len=0; }
    T*       begin()       { return data; }
    T*       end()         { return data+len; }
    const T* begin() const { return data; }
    const T* end()   const { return data+len; }
  };

} // namespace oneParse
