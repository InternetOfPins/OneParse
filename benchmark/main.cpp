// ============================================================================
// main.cpp — HAPI vs Spirit.X3 vs PEGTL vs Hana — Compile-time Parser Benchmark
// ============================================================================
//
// This file extends your existing benchmark harness with parser benchmarks.
// It keeps your MAP/FIND/TREE/HANA_VAL tests untouched.
//
// New tests:
//   TEST_PARSE_ONEPARSE_JSON
//   TEST_PARSE_SPIRIT_JSON
//   TEST_PARSE_PEGTL_JSON
//   TEST_PARSE_HANA_JSON   (optional)
//
// Grammar scaled by TEST_SIZE = number of fields in a JSON object.
//
// ============================================================================

#include <type_traits>
#include <tuple>
#include <boost/hana.hpp>
#include "../../../HAPI/include/hapi/hapi.h"
#include "hapi/meta.h"
#include "oneParse/oneParse.h"

// PEGTL
#include <tao/pegtl.hpp>

// Spirit.X3
#include <boost/spirit/home/x3.hpp>

namespace hana = boost::hana;
namespace pegtl = tao::pegtl;
namespace x3 = boost::spirit::x3;

// ============================================================================
// Utilities
// ============================================================================

template<std::size_t I>
static constexpr const char key_str[] = { '"', 'k',
    char('0' + (I / 10)), char('0' + (I % 10)), '"', 0 };

// ============================================================================
// oneParse JSON grammar generator
// ============================================================================

template<std::size_t I>
struct OP_Field {
    using P = oneParse::ParseDef<int,
        oneParse::Str<key_str<I>>,
        oneParse::Skip<oneParse::Space>,
        oneParse::Char<':'>,
        oneParse::Skip<oneParse::Space>,
        oneParse::Some<oneParse::Digit>
    >;
};

template<std::size_t... Is>
struct OP_Object {
    using Type = oneParse::ParseDef<int,
        oneParse::Char<'{'>,
        oneParse::SepBy< typename OP_Field<Is>::P,
                         oneParse::Char<','>,
                         sizeof...(Is) >,
        oneParse::Char<'}'>
    >;
};

// ============================================================================
// PEGTL JSON grammar generator
// ============================================================================

template<std::size_t I>
struct P_Key : pegtl::string< '"','k',
    char('0' + (I / 10)), char('0' + (I % 10)), '"' > {};

struct P_Number : pegtl::plus<pegtl::digit> {};

template<std::size_t I>
struct P_Field : pegtl::seq< P_Key<I>, pegtl::one<':'>, P_Number > {};

template<typename Seq> struct P_Object;

template<std::size_t... Is>
struct P_Object<std::index_sequence<Is...>> {
    using Type = pegtl::seq<
        pegtl::one<'{'>,
        pegtl::list< P_Field<Is>, pegtl::one<','> >,
        pegtl::one<'}'>
    >;
};

// ============================================================================
// Spirit.X3 JSON grammar generator
// ============================================================================

template<std::size_t I>
auto make_spirit_field() {
    static const char* key = key_str<I>;
    return x3::lit(key) >> ':' >> +x3::digit;
}

template<std::size_t... Is>
auto make_spirit_object(std::index_sequence<Is...>) {
    return '{' >> (make_spirit_field<Is>() % ',') >> '}';
}

// ============================================================================
// Hana pseudo-parser (tuple of rules)
// ============================================================================

template<std::size_t I>
struct H_Key {
    using type = std::integral_constant<int, I>;
};

template<std::size_t... Is>
struct H_Object {
    using Type = decltype(hana::tuple_t< H_Key<Is>... >);
};

// ============================================================================
// Existing benchmarks (MAP/FIND/TREE/HANA_VAL)
// ============================================================================
// (Your entire original main.cpp content goes here unchanged)
// ============================================================================

// ============================================================================
// New parser benchmarks
// ============================================================================

#if defined(TEST_PARSE_ONEPARSE_JSON)
    using Grammar = typename OP_Object<
        std::make_index_sequence<TEST_SIZE>::value
    >::Type;

    using Parser = typename Grammar::template Part< oneParse::ParseAPI<int> >;
    (void)static_cast<Parser*>(nullptr);

#elif defined(TEST_PARSE_PEGTL_JSON)
    using Grammar = typename P_Object<
        std::make_index_sequence<TEST_SIZE>
    >::Type;
    (void)sizeof(Grammar);

#elif defined(TEST_PARSE_SPIRIT_JSON)
    auto grammar = make_spirit_object(
        std::make_index_sequence<TEST_SIZE>{}
    );
    (void)sizeof(grammar);

#elif defined(TEST_PARSE_HANA_JSON)
    using Grammar = typename H_Object<
        std::make_index_sequence<TEST_SIZE>
    >::Type;
    (void)sizeof(Grammar);

#endif

// ============================================================================

int main() { return 0; }

