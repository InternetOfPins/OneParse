// bench_parse.cpp — compile-time parser benchmark: OneParse vs Spirit.X3 vs Hana
//
// Compile with:
//   g++ -std=c++17 -fsyntax-only -ftemplate-depth=2000 \
//       -I../include -DTEST_PARSE_ONEPARSE_JSON -DTEST_SIZE=8 bench_parse.cpp
//
// TEST_SIZE = number of distinct key fields in the grammar.

#include <cstddef>
#include <type_traits>

// Key strings at namespace scope — C++17 NTTPs require external linkage;
// template variables at namespace scope have it by default.
template<std::size_t I>
constexpr const char key_str[] = {
    '"', 'k', char('0' + (I / 10)), char('0' + (I % 10)), '"', '\0'
};

// Guard: empty translation unit when compiled without a test flag (e.g. by a library scanner)
#if defined(TEST_PARSE_BASELINE) || defined(TEST_PARSE_ONEPARSE_JSON) \
 || defined(TEST_PARSE_SPIRIT_JSON) || defined(TEST_PARSE_HANA_JSON)

// ─── Baseline — empty grammar, measures compiler startup cost ─────────────────

#if defined(TEST_PARSE_BASELINE)

void run() {}

// ─── oneParse ─────────────────────────────────────────────────────────────────
//
// Grammar: flat chain of N  Skip<Str<"kNN">, Char<':'>>  components + Digit
// Each Str<key_str<I>> is a distinct type — exercises HAPI Chain instantiation
// at O(N) template depth with N distinct leaf types.

#elif defined(TEST_PARSE_ONEPARSE_JSON)

#include <hapi/hapi.h>
#include <oneParse/oneParse.h>
using namespace oneParse;

template<std::size_t... Is>
auto grammar_helper(std::index_sequence<Is...>)
    -> ParseDef<char, Skip<Str<key_str<Is>>, Char<':'>>..., Digit>;

using Grammar = decltype(grammar_helper(std::make_index_sequence<TEST_SIZE>{}));

void run() { (void)Grammar::run(""); }

// ─── Spirit.X3 ────────────────────────────────────────────────────────────────
//
// Grammar: N rules  lit("kNN") >> ':' >> digit  composed with >> (sequence).
// Unary right fold  (f<0>() >> (f<1>() >> (...)))  forces N distinct
// instantiations of Spirit's sequence combinator.

#elif defined(TEST_PARSE_SPIRIT_JSON)

#include <boost/spirit/home/x3.hpp>
namespace x3 = boost::spirit::x3;

template<std::size_t I>
auto field() {
    return x3::lit(key_str<I>) >> x3::lit(':') >> x3::digit;
}

template<std::size_t... Is>
auto grammar_helper(std::index_sequence<Is...>) {
    return (field<Is>() >> ...);  // unary right fold: f0>>(f1>>(f2>>...))
}

void run() {
    auto g = grammar_helper(std::make_index_sequence<TEST_SIZE>{});
    (void)g;
}

// ─── Hana (type-level) ────────────────────────────────────────────────────────
//
// Not a parser — measures Hana's cost of building a tuple of N distinct types.
// Included as a reference point for pure type-level composition overhead.

#elif defined(TEST_PARSE_HANA_JSON)

#include <boost/hana.hpp>
namespace hana = boost::hana;

template<std::size_t I>
struct Key { using type = std::integral_constant<std::size_t, I>; };

template<std::size_t... Is>
auto grammar_helper(std::index_sequence<Is...>) {
    return hana::tuple_t<Key<Is>...>;
}

void run() {
    auto g = grammar_helper(std::make_index_sequence<TEST_SIZE>{});
    (void)g;
}

#endif

int main() { run(); return 0; }

#endif // test flag defined
