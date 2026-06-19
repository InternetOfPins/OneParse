#pragma once

#include <stddef.h>
#include <stdint.h>
#include <tuple>
#include <type_traits>
#include <hapi/hapi.h>
#include <oneOutput/oneOutput.h>
using hapi::APIOf;
using hapi::Chain;
using hapi::Nil;

namespace oneParse {

  // ── Shared source type ────────────────────────────────────────────────────────
  using Src = const char*;

  // ── Typed result ──────────────────────────────────────────────────────────────

  template<typename A, typename B>
  struct Pair { A fst{}; B snd{}; };

  struct err_tag {};
  static constexpr err_tag err_v{};

  template<typename T>
  struct Res {
    bool ok{false};
    T    val{};
    Src  rest{nullptr};
    Src  err{nullptr};
    constexpr Res() = default;
    constexpr Res(bool o, T v, Src r, Src e=nullptr) : ok(o), val(v), rest(r), err(e) {}
    constexpr Res(err_tag, Src at) : ok(false), val{}, rest(at), err(at) {}

    // fmap: transform val through fn; position and error propagate
    template<typename Fn>
    auto fmap(Fn fn) const {
      using U = decltype(fn(val));
      if (!ok) return Res<U>(false, U{}, rest, err);
      return Res<U>(true, fn(val), rest);
    }

    // operator%: alias for fmap (r % fn)
    template<typename Fn>
    auto operator%(Fn fn) const { return fmap(fn); }

    // operator>>: sequential — run P at rest, combine as Pair<T, P::ValType>
    template<typename P>
    auto operator>>(P) const {
      using U = typename P::ValType;
      if (!ok) return Res<Pair<T,U>>(false, Pair<T,U>{}, rest, err);
      auto r2 = P::run(rest);
      if (!r2.ok) return Res<Pair<T,U>>(false, Pair<T,U>{}, rest, r2.err);
      return Res<Pair<T,U>>(true, Pair<T,U>{val, r2.val}, r2.rest);
    }

    // skip<P>: run P at rest, discard P's val, keep our val at P's new position
    template<typename P>
    Res<T> skip() const {
      if (!ok) return *this;
      auto r2 = P::run(rest);
      if (!r2.ok) return Res<T>(false, T{}, rest, r2.err);
      return Res<T>(true, val, r2.rest);
    }
  };

  // ── Streaming result (character-level) ───────────────────────────────────────

  enum class St : uint8_t { ok, partial, fail };

  struct StreamRes {
    St   state;
    char val{0};
    constexpr operator bool() const { return state == St::ok; }
    constexpr bool partial()  const { return state == St::partial; }
    static constexpr StreamRes Ok(char c) { return {St::ok,      c}; }
    static constexpr StreamRes Partial()  { return {St::partial, 0}; }
    static constexpr StreamRes Fail()     { return {St::fail,    0}; }
  };

  // ── Streaming API (character-by-character, output-chain model) ─────────────
  template<typename Cfg=Nil>
  struct ParseAPI : oneOutput::OutAPI<Cfg> {
    StreamRes run(char) { return StreamRes::Fail(); }
  };

  // ── Typed leaf API (used as HAPI chain base for typed parsers) ───────────────
  template<typename T>
  struct TypedParseAPI {
    using Result = Res<T>;
    static Result run(Src s)                    { return Result(true, T{}, s); }
    static Result run()                         { return Result(true, T{}, nullptr); }
    static Result ok(Src s, T v)               { return Result(true, v, s); }
    static Result fail(Src s, Src msg=nullptr) { return Result(false, T{}, s, msg); }
  };

  // ── Fixed-capacity array — no heap, MCU-safe ──────────────────────────────────
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

  // ── Tag for zero-width typed components (don't contribute to Result::val) ────
  struct ZeroWidthTag {};

  // ── Tag for in-place val transformers (no source advance) ────────────────────
  struct TransformTag {};

  // Mutate<Fn>: Fn(T&) — modify val in-place; position unchanged
  template<auto Fn>
  struct Mutate : TransformTag {
    template<typename T>
    static void apply(T& v) { Fn(v); }
  };

  // Trans<Fn>: val = Fn(val) — same-type transform; position unchanged
  template<auto Fn>
  struct Trans : TransformTag {
    template<typename T>
    static T apply(T v) { return Fn(v); }
  };

  // ── Source component — embeds a const char* buffer in the parser type ───────
  //
  // From<Buf> sets the source pointer at the start of the component fold,
  // enabling a zero-arg ParseDef::run() when From<Buf> is among the Comps.
  //
  // Usage:
  //   constexpr char kJson[] = "{\"x\":1}";
  //   using P = ParseDef<Val, From<kJson>, ObjP>;
  //   constexpr auto r = P::run();   // zero-arg; source comes from kJson

  struct FromTag {};

  template<const char* Buf>
  struct From : FromTag {
    static constexpr const char* source = Buf;
  };

  // ── Single-char stateless ─────────────────────────────────────────────────────

  template<char q>
  struct Char {
    template<typename O> struct Part : O {
      using Base=O; using Base::Base; using Base::put;
      static constexpr bool chk(char c) { return c==q; }
      StreamRes run(char c) {
        if (chk(c)) { put(c); return StreamRes::Ok(c); }
        return StreamRes::Fail();
      }
    };
    static constexpr bool chk(char c) { return c==q; }
    static Res<char> run(Src s) {
      if (!*s || *s!=q) return {false, {}, s, s};
      return {true, *s, s+1};
    }
  };

  template<char a, char b>
  struct Range {
    template<typename O> struct Part : O {
      using Base=O; using Base::Base; using Base::put;
      static constexpr bool chk(char c) { return a<=c&&c<=b; }
      StreamRes run(char c) {
        if (chk(c)) { put(c); return StreamRes::Ok(c); }
        return StreamRes::Fail();
      }
    };
    static constexpr bool chk(char c) { return a<=c&&c<=b; }
    static Res<char> run(Src s) {
      if (!*s || !(a<=*s&&*s<=b)) return {false, {}, s, s};
      return {true, *s, s+1};
    }
  };

  template<char... CC>
  struct AnyOf {
    template<typename O> struct Part : O {
      using Base=O; using Base::Base; using Base::put;
      static constexpr bool chk(char c) { return ((c==CC)||...); }
      StreamRes run(char c) {
        if (chk(c)) { put(c); return StreamRes::Ok(c); }
        return StreamRes::Fail();
      }
    };
    static constexpr bool chk(char c) { return ((c==CC)||...); }
    static Res<char> run(Src s) {
      if (!*s || !chk(*s)) return {false, {}, s, s};
      return {true, *s, s+1};
    }
  };

  template<bool(*F)(char)>
  struct Satisfy {
    template<typename O> struct Part : O {
      using Base=O; using Base::Base; using Base::put;
      static bool chk(char c) { return F(c); }
      StreamRes run(char c) {
        if (F(c)) { put(c); return StreamRes::Ok(c); }
        return StreamRes::Fail();
      }
    };
    static constexpr bool chk(char c) { return F(c); }
    static Res<char> run(Src s) {
      if (!*s || !F(*s)) return {false, {}, s, s};
      return {true, *s, s+1};
    }
  };

  struct Any {
    template<typename O> struct Part : O {
      using Base=O; using Base::Base; using Base::put;
      static constexpr bool chk(char c) { return c!='\0'; }
      StreamRes run(char c) {
        if (c) { put(c); return StreamRes::Ok(c); }
        return StreamRes::Fail();
      }
    };
    static constexpr bool chk(char c) { return c!='\0'; }
    static Res<char> run(Src s) {
      if (!*s) return {false, {}, s, s};
      return {true, *s, s+1};
    }
  };

  // ── Single-char combinators ───────────────────────────────────────────────────

  template<typename... PP>
  struct Or {
    template<typename O> struct Part : O {
      using Base=O; using Base::Base; using Base::put;
      static constexpr bool chk(char c) {
        return (PP::template Part<ParseAPI<Nil>>::chk(c)||...);
      }
      StreamRes run(char c) {
        if (chk(c)) { put(c); return StreamRes::Ok(c); }
        return StreamRes::Fail();
      }
    };
    static constexpr bool chk(char c) {
      return (PP::template Part<ParseAPI<Nil>>::chk(c)||...);
    }
    static Res<char> run(Src s) {
      if (!*s || !chk(*s)) return {false, {}, s, s};
      return {true, *s, s+1};
    }
  };

  template<typename... PP>
  struct And {
    template<typename O> struct Part : O {
      using Base=O; using Base::Base; using Base::put;
      static constexpr bool chk(char c) {
        return (PP::template Part<ParseAPI<Nil>>::chk(c)&&...);
      }
      StreamRes run(char c) {
        if (chk(c)) { put(c); return StreamRes::Ok(c); }
        return StreamRes::Fail();
      }
    };
    static constexpr bool chk(char c) {
      return (PP::template Part<ParseAPI<Nil>>::chk(c)&&...);
    }
    static Res<char> run(Src s) {
      if (!*s || !chk(*s)) return {false, {}, s, s};
      return {true, *s, s+1};
    }
  };

  template<typename P>
  struct Not {
    template<typename O> struct Part : O {
      using Base=O; using Base::Base; using Base::put;
      static constexpr bool chk(char c) {
        return !P::template Part<ParseAPI<Nil>>::chk(c);
      }
      StreamRes run(char c) {
        if (chk(c)) { put(c); return StreamRes::Ok(c); }
        return StreamRes::Fail();
      }
    };
    static constexpr bool chk(char c) {
      return !P::template Part<ParseAPI<Nil>>::chk(c);
    }
    static Res<char> run(Src s) {
      if (!*s || !chk(*s)) return {false, {}, s, s};
      return {true, *s, s+1};
    }
  };

  template<char... CC>
  using NoneOf = Not<AnyOf<CC...>>;

  // ── Multi-char streaming ─────────────────────────────────────────────────────

  template<typename P>
  struct String {
    template<typename O> struct Part : O {
      using Base=O; using Base::Base; using Base::put;
      static constexpr bool chk(char c) {
        return P::template Part<ParseAPI<Nil>>::chk(c);
      }
      StreamRes run(char c) {
        if (chk(c)) { put(c); return StreamRes::Ok(c); }
        return StreamRes::Fail();
      }
    };
  };

  template<char... CC>
  struct Lit {
    template<typename O> struct Part : O {
      using Base=O; using Base::Base; using Base::put;
      static constexpr char chars[]{CC...};
      char   buf[sizeof...(CC)]{};
      size_t pos{0};
      static constexpr bool chk(char c) { return sizeof...(CC)>0 && chars[0]==c; }

      StreamRes run(char c) {
        if (c!=chars[pos]) { pos=0; return StreamRes::Fail(); }
        buf[pos]=c;
        if (++pos==sizeof...(CC)) {
          pos=0;
          for (char x:buf) put(x);
          return StreamRes::Ok(c);
        }
        return StreamRes::Partial();
      }
    };
  };

  // ── Repetition (streaming) ────────────────────────────────────────────────────

  template<typename P>
  struct Some {
    template<typename O> struct Part : O {
      using Base=O; using Base::Base; using Base::put;
      bool matched{false};
      static constexpr bool chk(char c) {
        return P::template Part<ParseAPI<Nil>>::chk(c);
      }
      StreamRes run(char c) {
        if (chk(c)) { put(c); matched=true; return StreamRes::Ok(c); }
        if (!matched) return StreamRes::Fail();
        matched=false;
        return StreamRes::Fail();
      }
    };
    // Typed: match 1+ chars, return last
    static Res<char> run(Src s) {
      if (!*s || !P::chk(*s)) return {false, {}, s, s};
      char last = *s++;
      while (*s && P::chk(*s)) { last = *s++; }
      return {true, last, s};
    }
  };

  template<typename P>
  struct Many : ZeroWidthTag {
    template<typename O> struct Part : O {
      using Base=O; using Base::Base; using Base::put;
      static constexpr bool chk(char c) {
        return P::template Part<ParseAPI<Nil>>::chk(c);
      }
      StreamRes run(char c) {
        if (chk(c)) { put(c); return StreamRes::Ok(c); }
        return StreamRes::Fail();
      }
    };
    // Typed: skip 0+ chars matching P (zero-width — val is '\0')
    static Res<char> run(Src s) {
      while (*s && P::chk(*s)) ++s;
      return {true, '\0', s};
    }
  };

  template<typename P>
  struct Opt {
    template<typename O> struct Part : O {
      using Base=O; using Base::Base; using Base::put;
      static constexpr bool chk(char c) {
        return P::template Part<ParseAPI<Nil>>::chk(c);
      }
      StreamRes run(char c) {
        if (chk(c)) { put(c); return StreamRes::Ok(c); }
        return StreamRes::Ok(0);
      }
    };
    // Typed: match 0 or 1 — always succeeds; '\0' if no match
    static Res<char> run(Src s) {
      if (*s && P::chk(*s)) return {true, *s, s+1};
      return {true, '\0', s};
    }
  };

  struct SkipWs : ZeroWidthTag {
    template<typename O> struct Part : O {
      using Base=O; using Base::Base;
      static constexpr bool chk(char c) { return (unsigned char)c<=' '; }
      StreamRes run(char c) {
        if (chk(c)) return StreamRes::Ok(c);
        return StreamRes::Fail();
      }
    };
    static Res<char> run(Src s) {
      while (*s && (unsigned char)*s<=' ') ++s;
      return {true, '\0', s};
    }
  };

  // ── Streaming sequence / alternation ─────────────────────────────────────────

  namespace detail {
    template<size_t I, size_t N, typename Tuple>
    StreamRes metaRun(Tuple& parts, size_t& cur, char c) {
      if constexpr (I >= N) { cur = 0; return StreamRes::Fail(); }
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
    StreamRes altRun(Tuple& parts, int8_t idx, char c) {
      if constexpr (I >= N) return StreamRes::Fail();
      else {
        if (idx != (int8_t)I) return altRun<I+1, N>(parts, idx, c);
        return std::get<I>(parts).run(c);
      }
    }
  } // namespace detail

  template<typename... PP>
  struct Meta {
    template<typename O> struct Part : O {
      using Base=O; using Base::Base;
      size_t cur{0};
      std::tuple<typename PP::template Part<Base>...> parts;
      static constexpr bool chk(char c) {
        using First = std::tuple_element_t<0, std::tuple<PP...>>;
        return First::template Part<ParseAPI<Nil>>::chk(c);
      }
      StreamRes run(char c) {
        return detail::metaRun<0, sizeof...(PP)>(parts, cur, c);
      }
    };
  };

  template<typename... PP>
  struct Alt {
    template<typename O> struct Part : O {
      using Base=O; using Base::Base;
      int8_t committed{-1};
      std::tuple<typename PP::template Part<Base>...> parts;

      static constexpr bool chk(char c) {
        return (PP::template Part<ParseAPI<Nil>>::chk(c)||...);
      }

      StreamRes run(char c) {
        if (committed < 0) {
          committed = detail::altFind<0, PP...>(c);
          if (committed < 0) return StreamRes::Fail();
        }
        auto r = detail::altRun<0, sizeof...(PP)>(parts, committed, c);
        if (r.state == St::fail) committed = -1;
        return r;
      }
    };
  };

  // ── Sugar ─────────────────────────────────────────────────────────────────────

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

  // ── Typed-only combinators ────────────────────────────────────────────────────

  struct Eof : ZeroWidthTag {
    static constexpr bool chk(char c) { return c=='\0'; }
    static Res<char> run(Src s) {
      if (*s) return {false, '\0', s, s};
      return {true, '\0', s};
    }
  };

  // Seq<P1, P2> — typed sequential pair: P1 then P2 → Pair<T1,T2>
  template<typename P1, typename P2>
  struct Seq {
    using T1 = typename P1::ValType;
    using T2 = typename P2::ValType;
    using ValType = Pair<T1,T2>;

    static Res<Pair<T1,T2>> run(Src s) {
      auto r1 = P1::run(s);
      if (!r1.ok) return Res<Pair<T1,T2>>(false, {}, s, r1.err);
      auto r2 = P2::run(r1.rest);
      if (!r2.ok) return Res<Pair<T1,T2>>(false, {}, s, r2.err);
      return Res<Pair<T1,T2>>(true, {r1.val, r2.val}, r2.rest);
    }
  };

  // ManyN<P,N> — collect 0+ typed-parser results into Arr<P::ValType,N>
  template<typename P, size_t N>
  struct ManyN {
    using ValType = Arr<typename P::ValType, N>;

    static Res<ValType> run(Src s) {
      ValType buf{};
      while (*s) {
        auto r = P::run(s);
        if (!r.ok) break;
        if (!buf.push(r.val)) break;
        s = r.rest;
      }
      return {true, buf, s};
    }
  };

  // SomeN<P,N> — collect 1+ typed-parser results into Arr<P::ValType,N>
  template<typename P, size_t N>
  struct SomeN {
    using ValType = Arr<typename P::ValType, N>;

    static Res<ValType> run(Src s) {
      ValType buf{};
      while (*s) {
        auto r = P::run(s);
        if (!r.ok) break;
        if (!buf.push(r.val)) break;
        s = r.rest;
      }
      if (!buf.len) return Res<ValType>(false, ValType{}, s, s);
      return Res<ValType>(true, buf, s);
    }
  };

  // Skip<Comps...> — run all comps, discard result (zero-width)
  template<typename... Comps>
  struct Skip : ZeroWidthTag {
    static Res<char> run(Src s) {
      const char* start = s;
      char dummy{};
      bool ok = true;
      ((ok = ok && ([&]() -> bool {
        auto r = Comps::run(s);
        if (!r.ok) return false;
        s = r.rest;
        return true;
      }())) && ...);
      if (!ok) return {false, '\0', start, start};
      return {true, '\0', s};
    }
  };

  // ManyFn<P, Fn> — call Fn(char) for each match (zero-width, side-effect)
  template<typename P, void(*Fn)(char)>
  struct ManyFn : ZeroWidthTag {
    static Res<char> run(Src s) {
      while (*s) {
        auto r = P::run(s);
        if (!r.ok) break;
        Fn(r.val);
        s = r.rest;
      }
      return {true, '\0', s};
    }
  };

  // Str<S> — match exact string literal (zero-width; consumes but discards)
  template<const char* S>
  struct Str : ZeroWidthTag {
    static constexpr int Len = [] { int n = 0; while (S[n]) ++n; return n; }();

    static Res<char> run(Src s) {
      if constexpr (Len < 8) {
        // char-by-char — branch predictor wins for short literals (benchmark: 2× faster at ≤7 chars)
        const char* p = S;
        while (*p) {
          if (*s != *p) return {false, '\0', s, s};
          ++s; ++p;
        }
        return {true, '\0', s};
      } else {
        // memcmp — single call, no per-char branch; wins for literals ≥8 chars (fits qword)
        if (__builtin_memcmp(s, S, Len) != 0) return {false, '\0', s, s};
        return {true, '\0', s + Len};
      }
    }
  };

  // To<InputT, Fn, P> — run P, transform result through Fn
  template<typename InputT, auto Fn, typename P>
  struct To {
    using OutT = std::invoke_result_t<decltype(Fn), InputT>;
    using ValType = OutT;

    static Res<OutT> run(Src s) {
      auto r = P::run(s);
      if (!r.ok) return Res<OutT>(false, OutT{}, s, r.err);
      return Res<OutT>(true, Fn(r.val), r.rest);
    }
  };

  // As<T, P> — construct T from P's result
  template<typename T, typename P>
  struct As {
    using ValType = T;

    static Res<T> run(Src s) {
      auto r = P::run(s);
      if (!r.ok) return Res<T>(false, T{}, s, r.err);
      return Res<T>(true, T(r.val), r.rest);
    }
  };

  // Verify<P, Pred> — run P, fail if Pred rejects the value
  template<typename P, auto Pred>
  struct Verify {
    using ValType = typename P::ValType;

    static Res<ValType> run(Src s) {
      auto r = P::run(s);
      if (!r.ok) return r;
      if (!Pred(r.val)) return {false, r.val, s, s};
      return r;
    }
  };

  // Between<Open,Content,Close> — consume Open, run Content, consume Close, return Content::val
  template<typename Open, typename Content, typename Close>
  struct Between {
    using ValType = typename Content::ValType;

    static Res<ValType> run(Src s) {
      auto r1 = Open::run(s);
      if (!r1.ok) return Res<ValType>(false, ValType{}, s, r1.err);
      auto r2 = Content::run(r1.rest);
      if (!r2.ok) return Res<ValType>(false, ValType{}, s, r2.err);
      auto r3 = Close::run(r2.rest);
      if (!r3.ok) return Res<ValType>(false, ValType{}, s, r3.err);
      return Res<ValType>(true, r2.val, r3.rest);
    }
  };

  // SepBy1<P,Sep,N> — P separated by Sep, 1 or more, into Arr<P::ValType,N>
  template<typename P, typename Sep, size_t N>
  struct SepBy1 {
    using ValType = Arr<typename P::ValType, N>;

    static Res<ValType> run(Src s) {
      ValType arr{};
      auto r = P::run(s);
      if (!r.ok) return Res<ValType>(false, ValType{}, s, r.err);
      arr.push(r.val); s = r.rest;
      while (true) {
        auto sep = Sep::run(s); if (!sep.ok) break;
        auto r2  = P::run(sep.rest); if (!r2.ok) break;
        if (!arr.push(r2.val)) break;
        s = r2.rest;
      }
      return {true, arr, s};
    }
  };

  // ManyTill<P,End> — consume P* until End matches (peek; End not consumed)
  template<typename P, typename End>
  struct ManyTill : ZeroWidthTag {
    static Res<char> run(Src s) {
      while (*s) {
        if (End::run(s).ok) return {true, '\0', s};
        auto r = P::run(s); if (!r.ok) return {false, '\0', s, s};
        s = r.rest;
      }
      return {false, '\0', s, s};
    }
  };

  // TryOr<PP...> — typed alternation: try each P::run in order, return first ok
  //   All PP must share the same ValType.
  template<typename... PP>
  struct TryOr {
    using ValType = typename std::tuple_element_t<0, std::tuple<PP...>>::ValType;

    static Res<ValType> run(Src s) {
      Res<ValType> result{};
      bool found = (([&]() -> bool {
        auto r = PP::run(s);
        if (r.ok) { result = r; return true; }
        return false;
      }()) || ...);
      if (!found) return Res<ValType>(false, ValType{}, s, "tryOr: no alternative");
      return result;
    }
  };

  // ── TypedDef — typed parser built from a result type + component list ─────────
  //
  // RunComp helper: call Comp::run(s), assign to val if non-zero-width.
  // Uses is_component to detect HAPI components without static run.

  namespace detail {
    template<typename T, typename=void>
    struct is_component : std::false_type {};
    template<typename T>
    struct is_component<T, std::void_t<typename T::template Part<ParseAPI<>>>> : std::true_type {};

    template<typename T, typename=void>
    struct has_static_run : std::false_type {};
    template<typename T>
    struct has_static_run<T, std::void_t<decltype(T::run(std::declval<const char*>()))>>
      : std::true_type {};

    template<typename T, typename Comp>
    bool runComp(Src& s, T& val, Src& err) {
      if constexpr (std::is_base_of_v<FromTag, Comp>) {
        s = Comp::source;
        return true;
      } else if constexpr (std::is_base_of_v<TransformTag, Comp>) {
        // Mutate: Fn(val&) → void;  Trans: Fn(val) → val (same T)
        if constexpr (std::is_void_v<decltype(Comp::apply(val))>)
          Comp::apply(val);
        else
          val = Comp::apply(val);
        return true;
      } else if constexpr (has_static_run<Comp>::value) {
        auto r = Comp::run(s);
        if (!r.ok) { err = r.err; return false; }
        if constexpr (!std::is_base_of_v<ZeroWidthTag, Comp>)
          val = r.val;
        s = r.rest;
        return true;
      } else {
        // HAPI component without static run: instantiate with TypedParseAPI<T>
        typename Comp::template Part<TypedParseAPI<T>> part{};
        auto r = part.run(s);
        if (!r.ok) { err = r.err; return false; }
        if constexpr (!std::is_base_of_v<ZeroWidthTag, Comp>)
          val = r.val;
        s = r.rest;
        return true;
      }
    }
  } // namespace detail

  template<typename T, typename... Comps>
  struct TypedDef {
    using ValType = T;
    using Types   = Chain<ParseAPI<>, Comps...>;   // HAPI introspection

    template<template<typename...> class W> using Build = W<Comps...>;
    template<typename... XX> using App = TypedDef<T, XX..., Comps...>;
    template<typename... XX> using Ins = TypedDef<T, Comps..., XX...>;

    static Res<T> run(Src s) {
      T val{};
      Src start = s;
      Src err   = nullptr;
      bool ok = (detail::runComp<T, Comps>(s, val, err) && ...);
      if (!ok) return Res<T>(false, T{}, start, err);
      return Res<T>(true, val, s);
    }

    // zero-arg run(): available when From<Buf> is among Comps
    // Function template so enable_if SFINAE fires at call site, not class instantiation
    template<bool _En = (std::is_base_of_v<FromTag, Comps> || ...)>
    static auto run() -> std::enable_if_t<_En, Res<T>>
    { return run(nullptr); }
  };

  // ── ParseDef — dispatches to streaming (APIOf) or typed (TypedDef) ──────────
  //   If first arg is a HAPI component, use the streaming HAPI chain.
  //   Otherwise treat first arg as result type → TypedDef.

  template<typename T, typename... Comps>
  using ParseDef = std::conditional_t<
    detail::is_component<T>::value,
    APIOf<ParseAPI<>, T, Comps...>,
    TypedDef<T, Comps...>
  >;

  // ── ParseDef convenience (streaming, no result type prefix) ─────────────────
  //   StreamDef<Comps...> = always streaming, first arg is a component.
  template<typename... OO>
  using StreamDef = APIOf<ParseAPI<>, OO...>;

} // namespace oneParse
