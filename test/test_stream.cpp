// test_stream.cpp — streaming API correctness tests
//
// Tests parsing boundaries: correct consumed-char counts, correct fail/ok,
// consistent results between char-by-char and run_n paths.
//
// Covers: String<NoneOf<C>>, Meta (quoted-string fast path), Alt (table N>=5),
//         TinyAlt (linear), and the memchr threshold boundary.
//
// Build:
//   g++ -std=c++17 -O2 -I../include -I../../HAPI/include -I../../OneOutput/include \
//       -o build/test_stream test/test_stream.cpp && ./build/test_stream

#include <oneParse/oneParse.h>
#include <cassert>
#include <cstring>
#include <iostream>
#include <random>
#include <string>

using namespace oneParse;

struct Sink {
    template<typename O> struct Part : O {
        using Base = O; using Base::Base;
        void put(char) {}
    };
};

// Feed char-by-char; return number consumed before first fail.
template<typename P>
static size_t feed_chars(P& p, const char* s, size_t n) {
    size_t i = 0;
    for (; i < n; ++i)
        if (p.run(s[i]).state == St::fail) break;
    return i;
}

// ── String<NoneOf<'"'>> ───────────────────────────────────────────────────────

using StrP = ParseDef<String<NoneOf<'"'>>, Sink>;

static void test_string(size_t body_len) {
    std::string body(body_len, 'x');
    std::string input = body + '"';      // body + stopper

    // run_n: should stop before the quote
    StrP p1;
    size_t r1 = p1.run_n(input.c_str(), input.size());
    assert(r1 == body_len && "String run_n: wrong count");

    // char-by-char: each body char ok, quote fails
    StrP p2;
    size_t r2 = feed_chars(p2, input.c_str(), input.size());
    assert(r2 == body_len && "String char: wrong count");

    assert(r1 == r2 && "String: run_n vs char mismatch");
}

// ── Meta<Char<'"'>, String<NoneOf<'"'>>, Char<'"'>> — quoted-string fast path ─

using JsonStr  = Meta<Char<'"'>, String<NoneOf<'"'>>, Char<'"'>>;
using MetaP    = ParseDef<JsonStr, Sink>;

static void test_meta(size_t body_len) {
    std::string body(body_len, 'a' + (char)(body_len % 26));
    std::string input = '"' + body + '"';
    size_t total = input.size();

    // run_n: should consume exactly the full quoted string
    MetaP p1;
    size_t r1 = p1.run_n(input.c_str(), total);
    assert(r1 == total && "Meta run_n: wrong count");

    // char-by-char: all chars ok
    MetaP p2;
    size_t r2 = feed_chars(p2, input.c_str(), total);
    assert(r2 == total && "Meta char: wrong count");

    assert(r1 == r2 && "Meta: run_n vs char mismatch");

    // run_n on body only (no closing quote) — should consume body chars, stop
    MetaP p3;
    std::string partial = '"' + body;
    size_t r3 = p3.run_n(partial.c_str(), partial.size());
    assert(r3 == partial.size() && "Meta partial run_n: wrong count");

    // run_n should not accept a second string — cur should be at 2 (done)
    MetaP p4;
    p4.run_n(input.c_str(), total);
    size_t r4 = p4.run_n(input.c_str(), total);  // second attempt must consume 0
    assert(r4 == 0 && "Meta: accepted second string after completion");
}

// ── memchr threshold boundary ─────────────────────────────────────────────────

static void test_boundary() {
    for (size_t L : {5u, 6u, 7u, 8u, 9u, 15u, 16u, 17u}) {
        test_string(L);
        test_meta(L);
    }
}

// ── Alt<5> (table path) and TinyAlt<5> (linear) ──────────────────────────────

using JsonNum   = String<Or<Digit, Sign, Char<'.'>>>;
using JsonNull  = Lit<'n','u','l','l'>;
using JsonTrue  = Lit<'t','r','u','e'>;
using JsonFalse = Lit<'f','a','l','s','e'>;

using AltP     = ParseDef<Alt    <JsonStr, JsonNum, JsonNull, JsonTrue, JsonFalse>, Sink>;
using TinyAltP = ParseDef<TinyAlt<JsonStr, JsonNum, JsonNull, JsonTrue, JsonFalse>, Sink>;

struct AltCase { const char* input; size_t expect; };
static const AltCase ALT_CASES[] = {
    { "\"hello\"", 7  },
    { "42",        2  },
    { "null",      4  },
    { "true",      4  },
    { "false",     5  },
    { "-3.14",     5  },
    { "xyz",       0  },    // no match — consumed 0
};

// Note: Alt::run_n is a bulk-continuation path — it requires commitment via
// run(first_char) first, and Char<C>'s stateless design means cur advances
// only on failure, so the fast path fires starting from body state (cur==1),
// not immediately after the opening delimiter. Tests here use char-by-char,
// which exercises commitment + full dispatch without the state-split ambiguity.
template<typename P>
static void test_alt_impl(const char* label) {
    for (auto& c : ALT_CASES) {
        size_t n = std::strlen(c.input);
        P pc;
        size_t rc = feed_chars(pc, c.input, n);
        if (rc != c.expect) {
            std::cerr << label << " char: input=\"" << c.input
                      << "\" expected=" << c.expect << " got=" << rc << "\n";
            assert(false);
        }
    }
}

// ── quoted-string with non-uniform body ───────────────────────────────────────

static void test_meta_mixed_body() {
    // body contains all printable ASCII except '"' — exercises every byte value
    std::string body;
    for (char c = 32; c < 127; ++c) if (c != '"') body += c;
    std::string input = '"' + body + '"';

    MetaP p1, p2;
    size_t r1 = p1.run_n(input.c_str(), input.size());
    size_t r2 = feed_chars(p2, input.c_str(), input.size());
    assert(r1 == input.size() && "mixed run_n: wrong count");
    assert(r2 == input.size() && "mixed char: wrong count");
    assert(r1 == r2            && "mixed: run_n vs char mismatch");
}

// ── Range<a,b> — SIMD bulk scan correctness (fuzzed) ──────────────────────────
// Regression check for the SSE2/AVX2 run_n path: exercises the two-arg bulk
// put() (needs a sink that actually implements it, unlike Sink above), full
// byte range (including >127, i.e. negative char), and randomized start
// offsets/lengths to hit every SIMD alignment/tail boundary.

struct Collect {
    std::string out;
    void put(char c) { out.push_back(c); }
    void put(const char* s, size_t n) { out.append(s, n); }
};

template<char a, char b>
static size_t ref_range_run_n(const char* s, size_t n, std::string& out) {
    size_t i = 0;
    while (i < n && a <= s[i] && s[i] <= b) { out.push_back(s[i]); ++i; }
    return i;
}

template<char a, char b>
static void test_range_fuzz(unsigned seed, int trials) {
    std::mt19937 rng(seed);
    for (int trial = 0; trial < trials; ++trial) {
        int len = (int)(rng() % 80) + 1;
        std::string s;
        for (int i = 0; i < len; ++i) s.push_back((char)(rng() % 256));
        int start = (int)(rng() % (unsigned)len);   // vary start to hit every alignment/tail case
        std::string sub = s.substr((size_t)start);

        std::string expOut;
        size_t exp = ref_range_run_n<a,b>(sub.c_str(), sub.size(), expOut);

        typename Range<a,b>::template Part<Collect> p{};
        size_t got = p.run_n(sub.c_str(), sub.size());

        assert(got == exp && "Range run_n: wrong count");
        assert(p.out == expOut && "Range run_n: wrong bytes");
    }
}

// ── JsonNum = String<Or<Digit,Sign,Char<'.'>>> — run_n correctness (fuzzed) ──
// General regression coverage for JsonNum's run_n (currently the plain tight
// loop -- a digit-run fast path via range_scan_n was tried and reverted, see
// the note on String::Part::run_n). Mixes digit runs with sign/dot chars,
// including "-3.14"-shaped input: a case that would silently mis-parse if this
// were ever routed through Alt<>'s commit-to-one-alternative semantics instead
// of Or<>'s per-char union (Alt would commit to Sign on '-' and then fail on
// the following digit) -- kept as a guard against that mistake resurfacing.

static size_t ref_jsonnum_run_n(const char* s, size_t n, std::string& out) {
    auto chk = [](char c){ return (c>='0'&&c<='9') || c=='-' || c=='+' || c=='.'; };
    size_t i = 0;
    while (i < n && chk(s[i])) { out.push_back(s[i]); ++i; }
    return i;
}

static void test_jsonnum_fuzz(unsigned seed, int trials) {
    std::mt19937 rng(seed);
    static const char alphabet[] = "0000000000123456789+-.xyz, "; // digit-heavy, like real numbers
    for (int trial = 0; trial < trials; ++trial) {
        int len = (int)(rng() % 100) + 1;   // up to 100 -- crosses the 16/32-byte vector stages
        std::string s;
        for (int i = 0; i < len; ++i) s.push_back(alphabet[rng() % (sizeof(alphabet) - 1)]);

        std::string expOut;
        size_t exp = ref_jsonnum_run_n(s.c_str(), s.size(), expOut);

        typename JsonNum::template Part<Collect> p{};
        size_t got = p.run_n(s.c_str(), s.size());

        assert(got == exp && "JsonNum run_n: wrong count");
        assert(p.out == expOut && "JsonNum run_n: wrong bytes");
    }
}

static void test_jsonnum_cases() {
    static const char* CASES[] = {
        "-3.14", "42", "+7", "0.001", "-0", "3.", ".5",
        "123456789012345678901234567890123456789", // > 1 AVX2 vector of pure digits
        "-123456789012345678901234567890123456789.987654321",
    };
    for (auto* in : CASES) {
        size_t n = std::strlen(in);
        std::string expOut;
        size_t exp = ref_jsonnum_run_n(in, n, expOut);
        typename JsonNum::template Part<Collect> p{};
        size_t got = p.run_n(in, n);
        if (got != exp || p.out != expOut) {
            std::cerr << "JsonNum case \"" << in << "\": expected=" << exp
                       << " got=" << got << "\n";
            assert(false);
        }
    }
}

// ── runner ────────────────────────────────────────────────────────────────────

void doStreamTests() {
    static const size_t LENGTHS[] = {10, 20, 30, 50, 80};

    std::cout << "String<NoneOf<'\"'>>  [10,20,30,50,80]:\n";
    for (size_t L : LENGTHS) test_string(L);
    std::cout << "  ok\n";

    std::cout << "Meta quoted-string fast path  [10,20,30,50,80]:\n";
    for (size_t L : LENGTHS) test_meta(L);
    std::cout << "  ok\n";

    std::cout << "memchr threshold boundary  [5,6,7,8,9,15,16,17]:\n";
    test_boundary();
    std::cout << "  ok\n";

    std::cout << "Meta mixed body (all printable ASCII):\n";
    test_meta_mixed_body();
    std::cout << "  ok\n";

    std::cout << "Alt<5> (table path):\n";
    test_alt_impl<AltP>("Alt");
    std::cout << "  ok\n";

    std::cout << "TinyAlt<5> (linear path):\n";
    test_alt_impl<TinyAltP>("TinyAlt");
    std::cout << "  ok\n";

    std::cout << "Range<a,b> SIMD bulk scan (fuzzed, 50000 trials x 4 ranges):\n";
    test_range_fuzz<'0','9'>(1234, 50000);              // digits -- Range's most common real use
    test_range_fuzz<'a','z'>(1234, 50000);               // lowercase alpha
    test_range_fuzz<0,0>(1234, 50000);                   // degenerate: a==b
    test_range_fuzz<(char)-128,(char)127>(1234, 50000);  // full valid range, span=255 edge
    std::cout << "  ok\n";

    std::cout << "JsonNum run_n correctness (fixed cases + fuzzed, 50000 trials):\n";
    test_jsonnum_cases();
    test_jsonnum_fuzz(4321, 50000);
    std::cout << "  ok\n";

    std::cout << "all streaming tests passed\n";
}

#ifdef ARDUINO
  void setup() { Serial.begin(115200); while(!Serial); doStreamTests(); }
  void loop() {}
#else
  int main() { doStreamTests(); return 0; }
#endif
