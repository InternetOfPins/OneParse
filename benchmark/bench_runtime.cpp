// bench_runtime.cpp — OneParse runtime parsing benchmark
//
// Build (handled by bench.py):
//   g++ -std=c++17 -O2 -I../include -I../../HAPI/include -DPARSER_ONEPARSE \
//       -o build/bench_op bench_runtime.cpp
//   g++ -std=c++17 -O2 -DPARSER_SPIRIT  -o build/bench_sp bench_runtime.cpp
//   g++ -std=c++17 -O2 -DPARSER_STRLEN  -o build/bench_sl bench_runtime.cpp
//
// Usage:  ./bench_op <input_file> <iterations> <runs>
// Stdout: parser_name,input_path,bytes,iterations,median_ms

// Guard: empty translation unit when compiled without -DPARSER_XXX (e.g. by a library scanner)
#if defined(PARSER_STRLEN) || defined(PARSER_ONEPARSE) || defined(PARSER_SPIRIT)

#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <string>

static std::string read_file(const char* path) {
    std::ifstream f(path);
    if (!f) { std::cerr << "cannot open: " << path << "\n"; std::exit(1); }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// ─── Baseline: strlen ──────────────────────────────────────────────────────

#if defined(PARSER_STRLEN)

static const char PARSER_NAME[] = "strlen";
static void parse(const char* s) { volatile std::size_t n = std::strlen(s); (void)n; }

// ─── OneParse ──────────────────────────────────────────────────────────────
//
// Stores results in Arr<Pair<string,Val>,8> — stack-allocated container,
// heap-allocated std::string per key and string value.

#elif defined(PARSER_ONEPARSE)

#include <hapi/hapi.h>
#include <oneParse/oneParse.h>
using namespace oneParse;

static const char PARSER_NAME[] = "oneParse";

struct Val {
    enum class Kind : uint8_t { None, Null, Bool, Int, Str } kind = Kind::None;
    std::string_view str{};  // view into source buffer — no heap allocation
    int  i = 0;
    bool b = false;
};

struct QuotedBody {
    template<typename O> struct Part : O {
        using Base = O;
        using Base::Base;
        static auto run(Src src) -> typename Base::Result {
            const char* start = src;
            while (src && *src && *src != '"') ++src;
            auto r = Base::run(src);
            if (r.ok) r.val = std::string_view(start, src - start);
            return r;
        }
    };
};
using QuotedP = ParseDef<std::string_view, QuotedBody>;

static int digitsToInt(Arr<char,10> a) {
    int n = 0;
    for (std::size_t i = 0; i < a.len; i++) n = n * 10 + (a.data[i] - '0');
    return n;
}
static Val asNull (char)                     { Val v; v.kind = Val::Kind::Null;              return v; }
static Val asTrue (char)                     { Val v; v.kind = Val::Kind::Bool; v.b = true;  return v; }
static Val asFalse(char)                     { Val v; v.kind = Val::Kind::Bool; v.b = false; return v; }
static Val asStr  (std::string_view s)       { Val v; v.kind = Val::Kind::Str;  v.str = s;   return v; }
static Val asInt  (Pair<char,Arr<char,10>> p){ Val v; v.kind = Val::Kind::Int;
                                               v.i = (p.fst == '-' ? -1 : 1) * digitsToInt(p.snd); return v; }

constexpr const char kNull[]  = "null";
constexpr const char kTrue[]  = "true";
constexpr const char kFalse[] = "false";

using KeyP     = ParseDef<std::string_view, SkipWs,
                           Between<Char<'"'>, QuotedP, Char<'"'>>>;
using MagP     = ParseDef<Arr<char,10>, SomeN<ParseDef<char,Digit>,10>>;
using SignP    = ParseDef<char, Opt<Or<Char<'+'>, Char<'-'>>>>;
using StrComp  = To<std::string_view, asStr, Between<Char<'"'>, QuotedP, Char<'"'>>>;
using NullComp = To<char,        asNull,  Str<kNull>>;
using TrueComp = To<char,        asTrue,  Str<kTrue>>;
using FalseComp= To<char,        asFalse, Str<kFalse>>;
using IntComp  = To<Pair<char,Arr<char,10>>, asInt, Seq<SignP, MagP>>;
using ValComp  = Or<FirstChar<'"', StrComp>,
                Or<FirstChar<'n', NullComp>,
                Or<FirstChar<'t', TrueComp>,
                Or<FirstChar<'f', FalseComp>,
                                  IntComp>>>>;
using ColonP   = ParseDef<Val, Skip<SkipWs, Char<':'>, SkipWs>, ValComp>;
using MemberP  = ParseDef<Pair<std::string_view,Val>, Seq<KeyP, ColonP>>;
using CommaP   = Skip<SkipWs, Char<','>, SkipWs>;
using CloseP   = Skip<SkipWs, Char<'}'>>;
using BodyP    = ParseDef<Arr<Pair<std::string_view,Val>,8>, SepBy<MemberP, CommaP, 8>>;
using ObjectP  = ParseDef<Arr<Pair<std::string_view,Val>,8>,
    Skip<Many<Space>>, Between<Char<'{'>, BodyP, CloseP>>;

static void parse(const char* s) { (void)ObjectP::run(s); }

// ─── Spirit.X3 ─────────────────────────────────────────────────────────────
//
// Stores keys as std::vector<std::string>; values are scanned but omitted.
// Spirit.X3 uses phrase_parse with space skipper.

#elif defined(PARSER_SPIRIT)

#include <boost/spirit/home/x3.hpp>
namespace x3 = boost::spirit::x3;

static const char PARSER_NAME[] = "spirit.x3";

static void parse(const char* s) {
    const char* end = s + std::strlen(s);

    // key: quoted string → std::string content (quotes stripped)
    auto const quoted_key = x3::lexeme[
        x3::lit('"') >> *(x3::char_ - '"') >> x3::lit('"')
    ];

    // value: quoted or unquoted — scan and discard
    auto const quoted_val = x3::lit('"') >> *(x3::char_ - '"') >> x3::lit('"');
    auto const word_val   = x3::lexeme[+(x3::char_ - x3::char_(",} \t\n\r"))];
    auto const value      = x3::omit[quoted_val | word_val];

    // member: key ":" value  →  std::string (key only)
    auto const member = quoted_key >> x3::lit(':') >> value;

    // object: { member (, member)* }
    std::vector<std::string> keys;
    x3::phrase_parse(s, end,
        x3::lit('{') >> -(member % x3::lit(',')) >> x3::lit('}'),
        x3::space, keys);
}

#endif // inner parser selection

// ─── Timing harness ────────────────────────────────────────────────────────

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

    // CSV line: parser,file,bytes,iters,median_ms
    std::cout << PARSER_NAME << ","
              << path        << ","
              << content.size() << ","
              << iters          << ","
              << median         << "\n";
    return 0;
}

#endif // PARSER_STRLEN || PARSER_ONEPARSE || PARSER_SPIRIT
