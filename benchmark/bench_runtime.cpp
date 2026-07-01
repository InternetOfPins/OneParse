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
#if defined(PARSER_STRLEN) || defined(PARSER_ONEPARSE) || defined(PARSER_ONEPARSE_NOKEY) \
 || defined(PARSER_SPIRIT) || defined(PARSER_LEXY) || defined(PARSER_ONEPARSE_STREAM) \
 || defined(PARSER_ONEPARSE_INDEX) \
 || defined(PARSER_PEGTL)  || defined(PARSER_SIMDJSON)

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

// ─── OneParse (full + nokey variants) ─────────────────────────────────────
//
// full    (PARSER_ONEPARSE):        Arr<Pair<string_view,Val>,8> — keys + typed values
// nokey   (PARSER_ONEPARSE_NOKEY):  Arr<Val,8>                  — typed values, keys discarded

#elif defined(PARSER_ONEPARSE) || defined(PARSER_ONEPARSE_NOKEY)

#include <hapi/hapi.h>
#include <oneParse/oneParse.h>
using namespace oneParse;

#if defined(PARSER_ONEPARSE)
static const char PARSER_NAME[] = "oneParse";
#else
static const char PARSER_NAME[] = "op-nokey";
#endif

struct Val {
    enum class Kind : uint8_t { None, Null, Bool, Int, Str } kind = Kind::None;
    std::string_view str{};
    int  i = 0;
    bool b = false;
};

// Store quoted string content as string_view into source buffer
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

// Advance past quoted string content without storing anything
struct QuotedSkip {
    template<typename O> struct Part : O {
        using Base = O;
        using Base::Base;
        static auto run(Src src) -> typename Base::Result {
            while (src && *src && *src != '"') ++src;
            return Base::run(src);
        }
    };
};

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
using CommaP   = Skip<SkipWs, Char<','>, SkipWs>;
using CloseP   = Skip<SkipWs, Char<'}'>>;

#if defined(PARSER_ONEPARSE)

using KeyP     = ParseDef<std::string_view, SkipWs,
                           Between<Char<'"'>, QuotedP, Char<'"'>>>;
using ColonP   = ParseDef<Val, Skip<SkipWs, Char<':'>, SkipWs>, ValComp>;
using MemberP  = ParseDef<Pair<std::string_view,Val>, Seq<KeyP, ColonP>>;
using BodyP    = ParseDef<Arr<Pair<std::string_view,Val>,8>, SepBy<MemberP, CommaP, 8>>;
using ObjectP  = ParseDef<Arr<Pair<std::string_view,Val>,8>,
    Skip<Many<Space>>, Between<Char<'{'>, BodyP, CloseP>>;

static void parse(const char* s) { (void)ObjectP::run(s); }

#else // PARSER_ONEPARSE_NOKEY

// Skip<SkipWs, Char<'"'>, QuotedSkip, Char<'"'>, SkipWs, Char<':'>, SkipWs>
// advances past the entire  "key" :  prefix; ValComp downstream provides the value.
using SkipKeyColon = Skip<SkipWs, Char<'"'>, QuotedSkip, Char<'"'>, SkipWs, Char<':'>, SkipWs>;
using NoKeyMemberP = ParseDef<Val, SkipKeyColon, ValComp>;
using BodyNoKeyP   = ParseDef<Arr<Val,8>, SepBy<NoKeyMemberP, CommaP, 8>>;
using ObjectNoKeyP = ParseDef<Arr<Val,8>,
    Skip<Many<Space>>, Between<Char<'{'>, BodyNoKeyP, CloseP>>;

static void parse(const char* s) { (void)ObjectNoKeyP::run(s); }

#endif

// ─── OneParse streaming ────────────────────────────────────────────────────
//
// Char-by-char streaming JSON flat-object parser.
// Uses Meta<>/Alt<>/String<>/Lit<> from oneParse.h.
// NullOut sink — output chars discarded; sink += state prevents DCE.

#elif defined(PARSER_ONEPARSE_STREAM)

#include <oneParse/oneParse.h>
#include <oneOutput/oneOutput.h>
using namespace oneParse;

static const char PARSER_NAME[] = "oneParse";

struct NullOut {
    template<typename O> struct Part : O {
        using Base=O; using Base::Base;
        void put(char) {}
    };
};

using JsonStr   = Meta<Char<'"'>, String<NoneOf<'"'>>, Char<'"'>>;
using JsonNum   = String<Or<Digit, Sign, Char<'.'>>>;
using JsonNull  = Lit<'n','u','l','l'>;
using JsonTrue  = Lit<'t','r','u','e'>;
using JsonFalse = Lit<'f','a','l','s','e'>;
using JsonVal   = Alt<JsonStr, JsonNum, JsonNull, JsonTrue, JsonFalse>;

struct JsonObj {
    template<typename O> struct Part : O {
        using Base=O; using Base::Base; using Base::put;
        enum class Phase : uint8_t {
            Open, AfterOpen, Key, Colon, ValStart, Val, Sep, Done
        } phase{Phase::Open};
        typename JsonStr::template Part<O> key;
        typename JsonVal::template Part<O> val;

        StreamRes run(char c) {
            switch (phase) {
                case Phase::Open:
                    if (c=='{') { phase=Phase::AfterOpen; return StreamRes::Ok(c); }
                    return StreamRes::Fail();
                case Phase::AfterOpen:
                    if (isSpace(c)) return StreamRes::Ok(c);
                    if (c=='}')     { phase=Phase::Done; return StreamRes::Ok(c); }
                    phase=Phase::Key;
                    [[fallthrough]];
                case Phase::Key: {
                    auto r = key.run(c);
                    if (r.state==St::fail) { phase=Phase::Colon; return run(c); }
                    return r;
                }
                case Phase::Colon:
                    if (isSpace(c)) return StreamRes::Ok(c);
                    if (c==':') { phase=Phase::ValStart; return StreamRes::Ok(c); }
                    return StreamRes::Fail();
                case Phase::ValStart:
                    if (isSpace(c)) return StreamRes::Ok(c);
                    phase=Phase::Val;
                    [[fallthrough]];
                case Phase::Val: {
                    auto r = val.run(c);
                    if (r.state==St::fail) { phase=Phase::Sep; return run(c); }
                    return r;
                }
                case Phase::Sep:
                    if (isSpace(c)) return StreamRes::Ok(c);
                    if (c==',') {
                        key=decltype(key){}; val=decltype(val){};
                        phase=Phase::AfterOpen;
                        return StreamRes::Ok(c);
                    }
                    if (c=='}') { phase=Phase::Done; return StreamRes::Ok(c); }
                    return StreamRes::Fail();
                case Phase::Done:
                    return StreamRes::Fail();
            }
            return StreamRes::Fail();
        }

        // bulk path: fast-scan string bodies, char-by-char only for phase transitions
        size_t run_n(const char* s, size_t n) {
            size_t total = 0;
            while (total < n) {
                size_t k = 0;
                if      (phase == Phase::Key) k = key.run_n(s + total, n - total);
                else if (phase == Phase::Val) k = val.run_n(s + total, n - total);
                if (k > 0) { total += k; }
                else {
                    if (run(s[total]).state == St::fail) break;
                    ++total;
                }
            }
            return total;
        }
    };
};

using P = ParseDef<JsonObj, NullOut>;

static volatile uint32_t sink = 0;
static void parse(const char* s) {
    P p;
    sink += (uint32_t)p.run_n(s, std::strlen(s));
}

// ─── OneParse two-stage structural-index scan (opt-in benchmark experiment) ──
//
// simdjson-style scan for this one grammar: stage 1 makes a single pass over
// the whole buffer recording structural byte offsets ({ } : , and quote
// pairs); stage 2 walks that offset list instead of driving a char-by-char
// phase state machine. Built as a real HAPI component (Part<O> shape, drop-in
// via ParseDef<JsonObjIndexed, NullOut> — identical usage to JsonObj above)
// so it composes the same way, not a parallel architecture. JsonObj and the
// rest of the streaming ::Part machinery are untouched by this.
//
// Known simplification vs JsonObj: unquoted value bodies (numbers/keywords)
// are treated as "whatever's between two structural offsets" without
// verifying they match null/true/false/a number — fine for a scan-only
// benchmark (nothing is validated or extracted either way), not something to
// promote into oneParse.h as-is.
//
// Toy-scale bound: Arr<uint16_t,64> caps input at 64KB / ~16 flat-object
// members — sized for these 4 benchmark files, not production-ready.

#elif defined(PARSER_ONEPARSE_INDEX)

#include <oneParse/oneParse.h>
#include <oneOutput/oneOutput.h>
#include <cstring>
#include <array>
using namespace oneParse;

static const char PARSER_NAME[] = "op-index";

struct NullOut {
    template<typename O> struct Part : O {
        using Base=O; using Base::Base;
        void put(char) {}
    };
};

namespace idx_detail {
    inline constexpr std::array<bool,256> make_structural_table() {
        std::array<bool,256> t{};
        for (unsigned char c : {'{','}',':',',','"'}) t[c] = true;
        return t;
    }
    static constexpr auto is_structural = make_structural_table();

    // Stage 1: one linear pass — record structural byte offsets. Quote opens
    // get their closing quote found via memchr (same technique as
    // detail::is_quoted_string's bulk path in oneParse.h) and both offsets
    // are pushed, so a string span is always two consecutive index entries.
    inline size_t scan(const char* s, size_t n, Arr<uint16_t,64>& idx) {
        size_t i = 0;
        while (i < n) {
            while (i < n && !is_structural[(unsigned char)s[i]]) ++i;
            if (i >= n) break;
            idx.push((uint16_t)i);
            if (s[i] == '"') {
                const char* end = (const char*)memchr(s+i+1, '"', n-i-1);
                i = end ? (size_t)(end - s) : n;
                idx.push((uint16_t)i);
            }
            ++i;
        }
        return idx.len;
    }

    // Stage 2: walk the index — same flat {"key":val, ...} grammar as JsonObj.
    // On success, `end` is set to one past the closing '}' (matches JsonObj's
    // run_n, which also stops at '}' rather than consuming trailing bytes).
    inline bool walk(const char* s, const Arr<uint16_t,64>& idx, size_t& end) {
        size_t k = 0;
        if (idx.len == 0 || s[idx.data[0]] != '{') return false;
        ++k;
        if (k < idx.len && s[idx.data[k]] == '}') { end = idx.data[k]+1; return true; }
        while (true) {
            if (k+1 >= idx.len || s[idx.data[k]] != '"') return false;
            k += 2; // key: opening + closing quote offsets
            if (k >= idx.len || s[idx.data[k]] != ':') return false;
            ++k;
            if (k < idx.len && s[idx.data[k]] == '"') {
                if (k+1 >= idx.len) return false;
                k += 2; // string value: opening + closing quote offsets
            }
            // else: unquoted value (number/keyword) is the implicit gap up
            // to the next structural offset — nothing indexed, nothing to walk.
            if (k >= idx.len) return false;
            char c = s[idx.data[k]];
            if (c == ',') { ++k; continue; }
            if (c == '}') { end = idx.data[k]+1; return true; }
            return false;
        }
    }
} // namespace idx_detail

struct JsonObjIndexed {
    template<typename O> struct Part : O {
        using Base=O; using Base::Base;

        size_t run_n(const char* s, size_t n) {
            Arr<uint16_t,64> idx{};
            idx_detail::scan(s, n, idx);
            size_t end = 0;
            return idx_detail::walk(s, idx, end) ? end : 0;
        }
        // run_n-only component: char-by-char path unused by this benchmark.
        StreamRes run(char) { return StreamRes::Fail(); }
    };
};

using PIdx = ParseDef<JsonObjIndexed, NullOut>;

static volatile uint32_t sink = 0;
static void parse(const char* s) {
    PIdx p;
    sink += (uint32_t)p.run_n(s, std::strlen(s));
}

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

// ─── lexy ──────────────────────────────────────────────────────────────────
//
// Stores keys as std::vector<std::string>; values are scanned but discarded.
// Grammar: quoted key + colon + (quoted|alnum+) value, whitespace-skipped.

#elif defined(PARSER_LEXY)

#include <lexy/action/parse.hpp>
#include <lexy/callback.hpp>
#include <lexy/dsl.hpp>
#include <lexy/input/string_input.hpp>
#include <vector>

static const char PARSER_NAME[] = "lexy";

namespace grammar {
    namespace dsl = lexy::dsl;

    // double-quoted string content → std::string
    struct key_prod : lexy::token_production {
        static constexpr auto rule  = dsl::quoted(dsl::ascii::print);
        static constexpr auto value = lexy::as_string<std::string>;
    };

    // value: quoted string | bare alnum+ (null/true/false/int) — discard
    struct val_prod : lexy::token_production {
        static constexpr auto rule = [] {
            auto q    = dsl::quoted(dsl::ascii::print);
            auto word = dsl::while_one(dsl::ascii::alnum);
            return q | word;
        }();
        static constexpr auto value = lexy::noop;
    };

    // member: "key" : value → key as std::string
    struct member {
        static constexpr auto whitespace = dsl::ascii::space;
        static constexpr auto rule =
            dsl::p<key_prod> + dsl::colon + dsl::p<val_prod>;
        static constexpr auto value = lexy::callback<std::string>(
            [](std::string key) { return key; }
        );
    };

    // object: { member (, member)* }
    struct object {
        static constexpr auto whitespace = dsl::ascii::space;
        static constexpr auto rule =
            dsl::curly_bracketed.opt_list(dsl::p<member>, dsl::sep(dsl::comma));
        static constexpr auto value = lexy::as_list<std::vector<std::string>>;
    };
}

static void parse(const char* s) {
    auto input  = lexy::zstring_input(s);
    auto result = lexy::parse<grammar::object>(input, lexy::noop);
    (void)result;
}

// ─── PEGTL ─────────────────────────────────────────────────────────────────
//
// Template PEG grammar — closest architectural match to OneParse.
// Keys stored in std::vector<std::string>; values scanned and discarded.

#elif defined(PARSER_PEGTL)

#include <tao/pegtl.hpp>
#include <vector>
#include <string>

static const char PARSER_NAME[] = "pegtl";

namespace pegtl = tao::pegtl;

namespace json_grammar {
    struct str_char  : pegtl::not_one<'"'>             {};
    struct str_body  : pegtl::star<str_char>            {};
    struct quoted    : pegtl::seq<pegtl::one<'"'>, str_body, pegtl::one<'"'>> {};
    struct num_char  : pegtl::sor<pegtl::digit,
                                  pegtl::one<'-'>, pegtl::one<'+'>, pegtl::one<'.'>> {};
    struct number    : pegtl::plus<num_char>            {};
    struct kw_null   : pegtl::string<'n','u','l','l'>   {};
    struct kw_true   : pegtl::string<'t','r','u','e'>   {};
    struct kw_false  : pegtl::string<'f','a','l','s','e'> {};
    struct value     : pegtl::sor<quoted, number, kw_null, kw_true, kw_false> {};
    struct ws        : pegtl::star<pegtl::space>        {};
    struct colon     : pegtl::seq<ws, pegtl::one<':'>, ws> {};
    struct comma     : pegtl::seq<ws, pegtl::one<','>, ws> {};
    struct member    : pegtl::seq<quoted, colon, value> {};
    struct members   : pegtl::list<member, comma>       {};
    struct object    : pegtl::seq<pegtl::one<'{'>, ws,
                                  pegtl::opt<members>,
                                  ws, pegtl::one<'}'>>  {};
    struct grammar   : pegtl::seq<object, pegtl::eof>  {};

    struct state { std::vector<std::string> keys; };

    template<typename Rule> struct action : pegtl::nothing<Rule> {};
    template<> struct action<str_body> {
        template<typename Input>
        static void apply(const Input& in, state& st) {
            st.keys.emplace_back(in.begin(), in.end());
        }
    };
}

static volatile size_t sink = 0;
static void parse(const char* s) {
    json_grammar::state st;
    pegtl::view_input input(s, s + std::strlen(s));
    pegtl::parse<json_grammar::grammar, json_grammar::action>(input, st);
    sink += st.keys.size();
}

// ─── simdjson ───────────────────────────────────────────────────────────────
//
// On-demand API over a pre-padded copy of the input.
// Parser object reused across calls; padded_string rebuilt each call
// (reflects real per-parse cost when input arrives fresh).

#elif defined(PARSER_SIMDJSON)

#include "simdjson_src/simdjson.h"
#include <vector>
#include <string>

static const char PARSER_NAME[] = "simdjson";

static simdjson::ondemand::parser sj_parser;
static volatile size_t sink = 0;

static void parse(const char* s) {
    auto padded = simdjson::padded_string(s, std::strlen(s));
    auto doc    = sj_parser.iterate(padded);
    size_t n    = 0;
    for (auto field : doc.get_object()) {
        (void)field.key();
        (void)field.value().raw_json_token();
        ++n;
    }
    sink += n;
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

#endif // PARSER_STRLEN || PARSER_ONEPARSE || PARSER_SPIRIT || PARSER_ONEPARSE_STREAM
