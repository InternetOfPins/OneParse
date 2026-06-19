// bench_stream.cpp — streaming char-by-char (rParser) vs pointer-based (oneParse)
//
// Same task: classify every char in the input as Digit/Numeric or not.
// Isolates dispatch + Res return-type cost, one char at a time.
//
// Build:
//   g++ -std=c++17 -O2 -I../include -I../../HAPI/include \
//       -DPARSER_RPARSER   -o build/bench_rparser   bench_stream.cpp
//   g++ -std=c++17 -O2 -I../include -I../../HAPI/include \
//       -DPARSER_OP_STREAM -o build/bench_op_stream  bench_stream.cpp
//
// Usage:  ./bench_rparser <input_file> <iterations> <runs>
// Stdout: parser_name,input_path,bytes,iterations,median_ms

#if defined(PARSER_RPARSER) || defined(PARSER_OP_STREAM)

#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <string>

static std::string read_file(const char* path) {
    std::ifstream f(path);
    if (!f) { std::cerr << "cannot open: " << path << "\n"; std::exit(1); }
    std::ostringstream ss; ss << f.rdbuf(); return ss.str();
}

// ─── rParser streaming ────────────────────────────────────────────────────────
//
// Inlined (no oneOutput dep), NullOut sink.
// Res{St,char,ptr} — 3-state, minimal return.

#if defined(PARSER_RPARSER)

#include <hapi/hapi.h>
using namespace hapi;

static const char PARSER_NAME[] = "rParser";

namespace rParser {

  enum class St : uint8_t { ok, partial, fail };

  struct Res {
    St   state;
    char val{0};
    constexpr operator bool() const { return state == St::ok; }
    static constexpr Res Ok(char c) { return {St::ok,   c}; }
    static constexpr Res Fail()     { return {St::fail, 0}; }
  };

  struct NullOut {
    template<typename O>
    struct Part : O {
      using Base = O; using Base::Base;
      static constexpr void put(char) {}
    };
  };

  struct ParserBase {
    Res run(char) { return Res::Fail(); }
    static constexpr void put(char) {}
  };

  template<typename... OO>
  using ParserDef = APIOf<ParserBase, OO..., NullOut>;

  template<char a, char b>
  struct Range {
    template<typename O>
    struct Part : O {
      using Base = O; using Base::Base;
      static constexpr bool chk(char c) { return a <= c && c <= b; }
      Res run(char c) {
        if (chk(c)) { Base::put(c); return Res::Ok(c); }
        return Res::Fail();
      }
    };
  };

  template<char q>
  struct Char {
    template<typename O>
    struct Part : O {
      using Base = O; using Base::Base;
      static constexpr bool chk(char c) { return c == q; }
      Res run(char c) {
        if (chk(c)) { Base::put(c); return Res::Ok(c); }
        return Res::Fail();
      }
    };
  };

  template<typename... PP>
  struct Or {
    template<typename O>
    struct Part : O {
      using Base = O; using Base::Base;
      static constexpr bool chk(char c) {
        return (PP::template Part<ParserBase>::chk(c) || ...);
      }
      Res run(char c) {
        if (chk(c)) { Base::put(c); return Res::Ok(c); }
        return Res::Fail();
      }
    };
  };

  using Digit   = Range<'0','9'>;
  using Sign    = Or<Char<'-'>, Char<'+'>>;
  using Numeric = Or<Digit, Sign>;
}

using namespace rParser;

static ParserDef<Numeric> p;
static volatile uint32_t sink = 0;
static void parse(const char* s) {
    for (; *s; ++s) sink += (uint32_t)p.run(*s).state;
}

// ─── old oneParse pointer-based, inlined ──────────────────────────────────────
//
// run(const char*) → Res<char>{bool, rest, union<char, std::string>}
// Inlined here so the benchmark remains valid after the new oneParse.h replaced
// the old pointer-based API. This is what we replaced — kept for comparison.

#elif defined(PARSER_OP_STREAM)

#include <hapi/hapi.h>
#include <string>
#include <new>
using namespace hapi;

static const char PARSER_NAME[] = "op-stream";

namespace op_old {
  using Src = const char*;
  struct ok_t {}; struct err_t {};
  constexpr ok_t ok_v{}; constexpr err_t err_v{};

  template<typename T>
  struct Res {
    bool ok; Src rest;
    union { T val; std::string err; };
    Res(ok_t,  T v, Src r)              : ok(true),  rest(r) { new(&val) T(v); }
    Res(err_t, Src r, std::string e={}) : ok(false), rest(r) { new(&err) std::string(std::move(e)); }
    Res(Res&& o) noexcept : ok(o.ok), rest(o.rest) {
      if (ok) new(&val) T(std::move(o.val));
      else    new(&err) std::string(std::move(o.err));
    }
    ~Res() { if (ok) val.~T(); else err.~basic_string(); }
    operator bool() const { return ok; }
  };

  template<typename T>
  struct ParseAPI {
    using Type=T; using Result=Res<T>;
    static Result run(Src src) { return {ok_v, T{}, src}; }
  };

  template<typename T, typename... OO>
  using ParseDef = APIOf<ParseAPI<T>, OO...>;

  template<char a, char b>
  struct Range {
    template<typename O> struct Part : O {
      using Base=O; using Base::Base;
      static auto run(Src src) -> typename Base::Result {
        if (src && *src>=a && *src<=b) {
          auto r=Base::run(src+1); if (r.ok) r.val=*src; return std::move(r);
        }
        // mirror original oneParse error string cost — forces heap allocation
        return {err_v, src, std::string("expected ['") + a + "'-'" + b + "'] at " + (src&&*src?std::string(src,1):"<end>")};
      }
    };
  };
  using Digit = Range<'0','9'>;
}

using DigitP = op_old::ParseDef<char, op_old::Digit>;

static volatile uint32_t sink = 0;
static void parse(const char* s) {
    for (; *s; ++s) sink += (uint32_t)DigitP::run(s).ok;
}

#endif

// ─── Timing harness ──────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    const char* path = (argc > 1) ? argv[1] : "data/small.json";
    int iters = (argc > 2) ? std::atoi(argv[2]) : 10000;
    int runs  = (argc > 3) ? std::atoi(argv[3]) : 5;

    std::string content = read_file(path);
    const char* src     = content.c_str();

    std::vector<double> times;
    times.reserve(runs);
    for (int r = 0; r < runs; r++) {
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < iters; i++) parse(src);
        auto t1 = std::chrono::steady_clock::now();
        times.push_back(
            std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    std::sort(times.begin(), times.end());
    double median = times[runs / 2];

    std::cout << PARSER_NAME << ","
              << path           << ","
              << content.size() << ","
              << iters          << ","
              << median         << "\n";
    return 0;
}

#endif // PARSER_RPARSER || PARSER_OP_STREAM
