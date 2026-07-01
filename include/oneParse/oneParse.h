#pragma once

#include <stddef.h>
#include <stdint.h>
#include <array>
#include <cstring>
#include <tuple>
#include <type_traits>
#if defined(__SSE2__) || defined(__AVX2__)
  #include <immintrin.h>
#endif
#include <hapi/hapi.h>
#include <oneOutput/oneOutput.h>
using hapi::APIOf;
using hapi::Chain;
using hapi::Nil;

namespace oneParse {

  // ── Shared source type ────────────────────────────────────────────────────────
  using Src = const char*;

  // ── Typed result ──────────────────────────────────────────────────────────────

  /// @brief parser result pair; fst holds first value, snd holds second
  template<typename A, typename B>
  struct Pair { A fst{}; B snd{}; };

  struct err_tag {};
  static constexpr err_tag err_v{};

  /// @brief typed parser result: ok flag, parsed value, remaining source, error position
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

  /// @brief character-level streaming parse result: ok/partial/fail + matched char
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
  /// @brief fixed-capacity array for MCU-safe parser output accumulation (no heap)
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

  /// @brief in-place value transformer: calls Fn(val&), position unchanged
  template<auto Fn>
  struct Mutate : TransformTag {
    template<typename T>
    static void apply(T& v) { Fn(v); }
  };

  /// @brief value transform: val = Fn(val), same type, position unchanged
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

  /// @brief embeds a compile-time source buffer; enables zero-arg ParseDef::run()
  template<const char* Buf>
  struct From : FromTag {
    static constexpr const char* source = Buf;
  };

  // ── Single-char stateless ─────────────────────────────────────────────────────

  /// @brief matches exactly the character q; fails on any other input
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

  namespace detail {
    // Shared bulk char-range scanner: consumes chars in [a,b] from s[0..n),
    // writing matched bytes to self.put(...). Used by Range<a,b>::Part::run_n.
    // (Also tried wrapping String<Or<Range<a,b>,Rest...>> bodies -- e.g. JSON's
    // Or<Digit,Sign,Char<'.'>> -- around this; reverted, see the note on
    // String::Part::run_n: per-call overhead doesn't amortize over JSON's
    // typical 1-12 digit field width.)
    //
    // Only ever declared with a live SIMD body where the compiler has already
    // defined __SSE2__/__AVX2__ (x86 hosts). AVR-GCC / arm-none-eabi-gcc /
    // xtensa-esp32-elf-gcc never define these, so on every small-MCU target
    // this falls straight to the plain per-char loop -- same API everywhere,
    // no target is left without a working bulk path. The two-arg bulk put()
    // is only ever called from inside the SIMD branch, so it's never reachable
    // on MCU output backends (e.g. UartOut) that don't implement that overload.
    template<char a, char b, typename Self>
    size_t range_scan_n(Self& self, const char* s, size_t n) {
      size_t i = 0;
#if !defined(ONE_PARSE_RANGE_TIGHT_LOOP) && (defined(__SSE2__) || defined(__AVX2__))
      constexpr unsigned char lo   = (unsigned char)a;
      constexpr unsigned char span = (unsigned char)(b - a);
#if defined(__AVX2__)
      {
        const __m256i vlo   = _mm256_set1_epi8((char)lo);
        const __m256i vspan = _mm256_set1_epi8((char)span);
        for (; i + 32 <= n; i += 32) {
          __m256i v    = _mm256_loadu_si256((const __m256i*)(s + i));
          __m256i t    = _mm256_sub_epi8(v, vlo);
          __m256i diff = _mm256_subs_epu8(t, vspan);
          unsigned mask = (unsigned)_mm256_movemask_epi8(
                             _mm256_cmpeq_epi8(diff, _mm256_setzero_si256()));
          if (mask != 0xFFFFFFFFu) {
            unsigned run = (unsigned)__builtin_ctz(~mask);
            if (run) self.put(s + i, run);
            return i + run;
          }
          self.put(s + i, 32);
        }
      }
#endif
#if defined(__SSE2__)
      {
        const __m128i vlo   = _mm_set1_epi8((char)lo);
        const __m128i vspan = _mm_set1_epi8((char)span);
        for (; i + 16 <= n; i += 16) {
          __m128i v    = _mm_loadu_si128((const __m128i*)(s + i));
          __m128i t    = _mm_sub_epi8(v, vlo);
          __m128i diff = _mm_subs_epu8(t, vspan);
          unsigned mask = (unsigned)_mm_movemask_epi8(
                             _mm_cmpeq_epi8(diff, _mm_setzero_si128())) & 0xFFFFu;
          if (mask != 0xFFFFu) {
            unsigned run = (unsigned)__builtin_ctz((~mask) & 0xFFFFu);
            if (run) self.put(s + i, run);
            return i + run;
          }
          self.put(s + i, 16);
        }
      }
#endif
#endif
      // remainder shorter than one vector (or no SIMD ISA at all): plain
      // per-char loop. A scan-then-bulk-put here measured ~0.56x (slower) on
      // isolated short fields -- the common real case for Digit (1-5 digit
      // JSON numbers never reach the 16/32-byte vector stages at all) -- so
      // this stays char-by-char.
      while (i < n && a <= s[i] && s[i] <= b) { self.put(s[i]); ++i; }
      return i;
    }
  }

  /// @brief matches characters in [a, b] inclusive
  template<char a, char b>
  struct Range {
    static_assert(a <= b, "Range<a,b>: requires a<=b -- chk() already assumes an "
                           "ascending range, and the SIMD run_n wraparound trick "
                           "(span=b-a as unsigned) silently matches everything "
                           "instead of nothing for an inverted range.");
    template<typename O> struct Part : O {
      using Base=O; using Base::Base; using Base::put;
      static constexpr bool chk(char c) { return a<=c&&c<=b; }
      StreamRes run(char c) {
        if (chk(c)) { put(c); return StreamRes::Ok(c); }
        return StreamRes::Fail();
      }
      size_t run_n(const char* s, size_t n) { return detail::range_scan_n<a,b>(*this, s, n); }
    };
    static constexpr bool chk(char c) { return a<=c&&c<=b; }
    static Res<char> run(Src s) {
      if (!*s || !(a<=*s&&*s<=b)) return {false, {}, s, s};
      return {true, *s, s+1};
    }
  };

  /// @brief matches any one of the listed characters
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

  /// @brief matches any character where F(c) is true
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

  /// @brief matches any single non-null character
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

  /// @brief ordered alternation: succeeds if any PP matches; tries left to right
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

  /// @brief character intersection: succeeds only if all PP match the same character
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

  /// @brief negation: matches any character that P does NOT match
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

  // ── memchr optimisation: detect Not<AnyOf<C>> (= NoneOf<C>, single excluded char) ──────────
  // Used by String::Part::run_n to replace the tight chk loop with memchr for longer bodies.
  namespace detail {
    template<typename P> struct is_single_reject {
      static constexpr bool value = false;
      static constexpr char ch = 0;
    };
    template<char C>
    struct is_single_reject<Not<AnyOf<C>>> {
      static constexpr bool value = true;
      static constexpr char ch = C;
    };
  }

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
      // bulk: memchr for single-excluded-char predicates above threshold; tight loop elsewhere.
      //
      // Tried (and reverted): delegating Or<Range<a,b>,Rest...> bodies (e.g. JSON
      // numbers' Or<Digit,Sign,Char<'.'>>) to range_scan_n per digit-run. Correct,
      // but measured ~1.3x *slower* on realistic 1-12 digit JSON number fields --
      // can't be fixed by gating on n like the memchr threshold above, because n
      // here is the remaining buffer, not the field length (same caveat as the
      // memchr note below), so the gate can't tell short fields from long ones.
      // The real cost: a field like "3.14" or "-42" needs 2+ separate vector-probe
      // calls (split at the sign/dot), and that per-call overhead doesn't amortize
      // over fields this short. Range<a,b>::Part::run_n itself is still a real win
      // used directly/standalone; it just doesn't pay off wrapped in String<Or<>>
      // at JSON's typical field width.
#ifndef ONE_PARSE_STRING_TIGHT_LOOP
      size_t run_n(const char* s, size_t n) {
        using RC = detail::is_single_reject<P>;
        if constexpr (RC::value) {
          // memchr: libc SIMD search — wins for string bodies ≥ 8 bytes (micro-bench verified)
          // n is remaining input, not body length — for quoted strings inside Meta the
          // Meta fast path calls memchr directly and never reaches here
          if (n >= 8) {
            const char* end = (const char*)std::memchr(s, (unsigned char)RC::ch, n);
            size_t count = end ? (size_t)(end - s) : n;
            for (size_t i = 0; i < count; ++i) put(s[i]);
            return count;
          }
        }
        // tight loop — short strings or multi-char predicates
        size_t i = 0;
        while (i < n && chk(s[i])) { put(s[i]); ++i; }
        return i;
      }
#else
      // ONE_PARSE_STRING_TIGHT_LOOP: original path — auto-vectorized by compiler at -O3
      size_t run_n(const char* s, size_t n) {
        size_t i = 0;
        while (i < n && chk(s[i])) { put(s[i]); ++i; }
        return i;
      }
#endif
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
    // detect Meta<Char<C>, String<Not<AnyOf<C>>>, Char<C>> — the quoted-string pattern
    template<typename... PP>
    struct is_quoted_string { static constexpr bool value = false; static constexpr char ch = 0; };
    template<char C>
    struct is_quoted_string<Char<C>, String<Not<AnyOf<C>>>, Char<C>> {
      static constexpr bool value = true;
      static constexpr char ch = C;
    };

    template<typename T, typename = void>
    struct has_run_n : std::false_type {};
    template<typename T>
    struct has_run_n<T, std::void_t<decltype(
        std::declval<T&>().run_n(std::declval<const char*>(), std::declval<size_t>()))>>
      : std::true_type {};

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

    // bulk path: call run_n on the component at cur if it supports it
    template<size_t I, size_t N, typename Tuple>
    size_t metaRunN(Tuple& parts, size_t cur, const char* s, size_t n) {
      if constexpr (I >= N) return 0;
      else {
        if (cur != I) return metaRunN<I+1, N>(parts, cur, s, n);
        auto& p = std::get<I>(parts);
        if constexpr (has_run_n<std::decay_t<decltype(p)>>::value) return p.run_n(s, n);
        else return 0;
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

    // compile-time 256-entry first-char dispatch table for Alt<PP...>
    // table[c] = index of first alternative whose chk(c) is true; 255 = no match
    template<size_t I, typename... PP>
    constexpr void alt_fill_table(std::array<uint8_t, 256>& t) {
      if constexpr (I < sizeof...(PP)) {
        using P = std::tuple_element_t<I, std::tuple<PP...>>;
        for (size_t c = 0; c < 256; ++c)
          if (t[c] == 255 && P::template Part<ParseAPI<Nil>>::chk((char)c))
            t[c] = (uint8_t)I;
        alt_fill_table<I+1, PP...>(t);
      }
    }
    template<typename... PP>
    constexpr std::array<uint8_t, 256> make_alt_table() {
      std::array<uint8_t, 256> t{};
      for (auto& x : t) x = 255;
      alt_fill_table<0, PP...>(t);
      return t;
    }

    // conditional storage: table only present (and ROM-allocated) when UseTable=true
    template<bool UseTable, typename... PP> struct AltTableStorage {};
    template<typename... PP>
    struct AltTableStorage<true, PP...> {
      static constexpr auto dispatch_table = make_alt_table<PP...>();
    };

    template<size_t I, size_t N, typename Tuple>
    StreamRes altRun(Tuple& parts, int8_t idx, char c) {
      if constexpr (I >= N) return StreamRes::Fail();
      else {
        if (idx != (int8_t)I) return altRun<I+1, N>(parts, idx, c);
        return std::get<I>(parts).run(c);
      }
    }

    template<size_t I, size_t N, typename Tuple>
    size_t altRunN(Tuple& parts, int8_t idx, const char* s, size_t n) {
      if constexpr (I >= N) return 0;
      else {
        if (idx != (int8_t)I) return altRunN<I+1, N>(parts, idx, s, n);
        auto& p = std::get<I>(parts);
        if constexpr (has_run_n<std::decay_t<decltype(p)>>::value) return p.run_n(s, n);
        else return 0;
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
      // bulk path for quoted-string pattern Meta<Char<C>, String<NoneOf<C>>, Char<C>>:
      // eliminates 3 char-by-char run() calls (open quote, first body char, close quote)
      // by using a direct char check + memchr + put loop per occurrence
#ifndef ONE_PARSE_META_SLOW
      size_t run_n(const char* s, size_t n) {
        using QS = detail::is_quoted_string<PP...>;
        if constexpr (QS::value) {
          size_t total = 0;
          while (total < n) {
            if (cur == 0) {
              if (s[total] != QS::ch) break;
              std::get<0>(parts).run(s[total]); // put opening quote
              cur = 1;
              ++total;
            } else if (cur == 1) {
              const char* end = (const char*)std::memchr(
                  s + total, (unsigned char)QS::ch, n - total);
              size_t count = end ? (size_t)(end - (s + total)) : (n - total);
              auto& body = std::get<1>(parts);
              for (size_t i = 0; i < count; ++i) body.put(s[total + i]);
              total += count;
              if (!end) break;
              std::get<2>(parts).run(s[total]); // put closing quote
              cur = 2;
              ++total;
              break; // this Meta occurrence is complete
            } else { break; }
          }
          return total;
        }
        // original: delegate to component run_n, char-by-char on transitions
        size_t total = 0;
        while (total < n) {
          size_t k = detail::metaRunN<0, sizeof...(PP)>(parts, cur, s + total, n - total);
          if (k > 0) { total += k; }
          else {
            StreamRes r = run(s[total]);
            if (r.state == St::fail) break;
            ++total;
          }
        }
        return total;
      }
#else
      // ONE_PARSE_META_SLOW: original path for comparison
      size_t run_n(const char* s, size_t n) {
        size_t total = 0;
        while (total < n) {
          size_t k = detail::metaRunN<0, sizeof...(PP)>(parts, cur, s + total, n - total);
          if (k > 0) { total += k; }
          else {
            StreamRes r = run(s[total]);
            if (r.state == St::fail) break;
            ++total;
          }
        }
        return total;
      }
#endif
    };
  };

  // TinyAlt: linear first-char scan — zero extra memory, always safe for MCUs
  template<typename... PP>
  struct TinyAlt {
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
      size_t run_n(const char* s, size_t n) {
        if (committed < 0) return 0;
        return detail::altRunN<0, sizeof...(PP)>(parts, committed, s, n);
      }
    };
  };

  // Alt: auto-selects strategy at compile time based on sizeof...(PP)
  //   N < 5  → linear scan (TinyAlt path, no table allocated)
  //   N >= 5 → 256-byte constexpr dispatch table (O(1) commit)
  // Table is in ROM only when needed; MCU-safe for small alternations.
  template<typename... PP>
  struct Alt : detail::AltTableStorage<(sizeof...(PP) >= 5), PP...> {
    template<typename O> struct Part : O {
      using Base=O; using Base::Base;
      int8_t committed{-1};
      std::tuple<typename PP::template Part<Base>...> parts;

      static constexpr bool use_table = (sizeof...(PP) >= 5);

      static constexpr bool chk(char c) {
        if constexpr (use_table)
          return detail::AltTableStorage<true, PP...>::dispatch_table[(unsigned char)c] != 255;
        else
          return (PP::template Part<ParseAPI<Nil>>::chk(c)||...);
      }
      StreamRes run(char c) {
        if (committed < 0) {
          if constexpr (use_table) {
            uint8_t idx = detail::AltTableStorage<true, PP...>::dispatch_table[(unsigned char)c];
            if (idx == 255) return StreamRes::Fail();
            committed = (int8_t)idx;
          } else {
            committed = detail::altFind<0, PP...>(c);
            if (committed < 0) return StreamRes::Fail();
          }
        }
        auto r = detail::altRun<0, sizeof...(PP)>(parts, committed, c);
        if (r.state == St::fail) committed = -1;
        return r;
      }
      size_t run_n(const char* s, size_t n) {
        if (committed < 0) return 0;
        return detail::altRunN<0, sizeof...(PP)>(parts, committed, s, n);
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
  // SkipTerminal/SkipStep: same Chain<>::Part composition as TypedStep,
  // scoped down to position-only (no val to carry) since Skip discards results.
  struct SkipTerminal {
    static bool step(Src&) { return true; }
  };

  template<typename Comp>
  struct SkipStep {
    template<typename O> struct Part : O {
      using Base = O; using Base::Base;
      static bool step(Src& s) {
        auto r = Comp::run(s);
        if (!r.ok) return false;
        s = r.rest;
        return Base::step(s);
      }
    };
  };

  template<typename... Comps>
  struct Skip : ZeroWidthTag {
    using Exec = typename Chain<SkipStep<Comps>...>::template Part<SkipTerminal>;
    static Res<char> run(Src s) {
      const char* start = s;
      if (!Exec::step(s)) return {false, '\0', start, start};
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

  // SepBy<P,Sep,N> — P separated by Sep, 0 or more, into Arr<P::ValType,N>
  //   Always succeeds; empty list if first P fails.
  template<typename P, typename Sep, size_t N>
  struct SepBy {
    using ValType = Arr<typename P::ValType, N>;

    static Res<ValType> run(Src s) {
      ValType arr{};
      auto r = P::run(s);
      if (!r.ok) return {true, arr, s};  // zero matches: ok, empty
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

  // TypedTerminal/TypedStep — real HAPI Chain<>::Part composition for TypedDef's
  // dispatch, mirroring the Mutate/Trans/Ref mono_block idiom in hapi/run.h:
  // each Comp becomes a component whose Part<O>::step() runs itself then
  // chains into Base::step(), instead of folding detail::runComp by hand.
  template<typename T>
  struct TypedTerminal {
    static bool step(Src&, T&, Src&) { return true; }
  };

  template<typename Comp, typename T>
  struct TypedStep {
    template<typename O> struct Part : O {
      using Base = O; using Base::Base;
      static bool step(Src& s, T& val, Src& err) {
        return detail::runComp<T, Comp>(s, val, err) && Base::step(s, val, err);
      }
    };
  };

  template<typename T, typename... Comps>
  struct TypedDef {
    using ValType = T;
    using Types   = Chain<ParseAPI<>, Comps...>;   // HAPI introspection
    using Exec    = typename Chain<TypedStep<Comps,T>...>::template Part<TypedTerminal<T>>;

    template<template<typename...> class W> using Build = W<Comps...>;
    template<typename... XX> using App = TypedDef<T, XX..., Comps...>;
    template<typename... XX> using Ins = TypedDef<T, Comps..., XX...>;

    static Res<T> run(Src s) {
      T val{};
      Src start = s;
      Src err   = nullptr;
      bool ok = Exec::step(s, val, err);
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
