// OneParse mono_block tests
//
// Verifies OneParse combinators under the mono_block HAPI chain:
//   1. Char / AnyOf / Satisfy   — single-char leaf parsers
//   2. Seq<P1,P2>               — sequential pair
//   3. Or<P1,P2>                — first-match alternation
//   4. Many / Some / ManyN      — repetition
//   5. Skip / Opt               — zero-width and optional
//   6. ParseDef Build/App/Ins   — chain manipulation meta-ops
//   7. hapi::query on ParseDef  — tag detection through HAPI
//
// Note: lambdas in template arguments require C++20; all predicates are
// named functions at namespace scope to stay compatible with C++17.

#include <cassert>
#include <iostream>
using namespace std;

#include <hapi/hapi.h>
#include <oneParse/oneParse.h>
using namespace hapi;
using namespace oneParse;

// ─── Shared predicates ───────────────────────────────────────────────────────

constexpr bool isDigitP(char c)  { return c>='0' && c<='9'; }
constexpr bool isAlphaP(char c)  { return (c>='a'&&c<='z') || (c>='A'&&c<='Z'); }
constexpr bool isAlNumP(char c)  { return isDigitP(c) || isAlphaP(c); }

// ─── Namespace-scope parser types (needed for static_assert in tests 6 & 7) ──

using DigitCharNS  = ParseDef<char, Satisfy<isDigitP>>;
using WsDigitNS    = ParseDef<char, Many<Space>, Satisfy<isDigitP>>;

using BaseDigitNS  = ParseDef<char, Satisfy<isDigitP>>;
using WithWsNS     = BaseDigitNS::App<Many<Space>>;
using Components6  = BaseDigitNS::Build<hapi::Chain>;
static_assert(Components6::size == 1, "Build should extract exactly one component");

// ─── Test 1: Char / AnyOf / Satisfy ─────────────────────────────────────────

void test_leaf_parsers() {
  using HashP   = ParseDef<char, Char<'#'>>;
  using DigitP  = ParseDef<char, Satisfy<isDigitP>>;
  using ABP     = ParseDef<char, AnyOf<'a','b'>>;

  auto r1 = HashP::run("#ok");
  assert(r1.ok && r1.val=='#');

  auto r2 = HashP::run("nope");
  assert(!r2.ok);

  auto r3 = DigitP::run("7x");
  assert(r3.ok && r3.val=='7');

  auto r4 = ABP::run("b!");
  assert(r4.ok && r4.val=='b');

  auto r5 = ABP::run("c!");
  assert(!r5.ok);

  cout << "PASS test_leaf_parsers\n";
}

// ─── Test 2: Seq<P1,P2> ──────────────────────────────────────────────────────

void test_seq() {
  using DotDigit = ParseDef<Pair<char,char>,
      Seq<ParseDef<char,Char<'.'>>, ParseDef<char,Satisfy<isDigitP>>>>;

  auto r = DotDigit::run(".3x");
  assert(r.ok && r.val.fst=='.' && r.val.snd=='3');
  assert(*r.rest == 'x');

  auto bad = DotDigit::run("3.x");
  assert(!bad.ok);

  cout << "PASS test_seq\n";
}

// ─── Test 3: Or<P1,P2> ───────────────────────────────────────────────────────

void test_or() {
  using DotOrHash = ParseDef<char, oneParse::Or<Char<'.'>, Char<'#'>>>;

  auto r1 = DotOrHash::run(".x");
  assert(r1.ok && r1.val=='.');

  auto r2 = DotOrHash::run("#x");
  assert(r2.ok && r2.val=='#');

  auto r3 = DotOrHash::run("ax");
  assert(!r3.ok);

  cout << "PASS test_or\n";
}

// ─── Test 4: Many / Some / ManyN ─────────────────────────────────────────────

void test_repetition() {
  using DigitP  = ParseDef<char,  Satisfy<isDigitP>>;
  using DigitsP = ParseDef<Arr<char,8>, ManyN<DigitP, 8>>;
  using SomeP   = ParseDef<Arr<char,8>, SomeN<DigitP, 8>>;

  auto r1 = DigitsP::run("123abc");
  assert(r1.ok && r1.val.len==3 && r1.val.data[0]=='1');

  auto r2 = DigitsP::run("abc");
  assert(r2.ok && r2.val.len==0);   // Many succeeds with zero matches

  auto r3 = SomeP::run("42!");
  assert(r3.ok && r3.val.len==2);

  auto r4 = SomeP::run("abc");
  assert(!r4.ok);                   // Some requires at least one

  cout << "PASS test_repetition\n";
}

// ─── Test 5: Skip / Opt ──────────────────────────────────────────────────────

void test_skip_opt() {
  using WsP    = ParseDef<char, Many<Space>>;
  using OptDot = ParseDef<char, Opt<Char<'.'>>>;

  auto r1 = WsP::run("   hello");
  assert(r1.ok && *r1.rest=='h');

  auto r2 = OptDot::run(".x");
  assert(r2.ok && r2.val=='.');

  auto r3 = OptDot::run("x");
  assert(r3.ok && r3.val=='\0');    // Opt succeeds with zero value

  cout << "PASS test_skip_opt\n";
}

// ─── Test 6: ParseDef Build / App / Ins ──────────────────────────────────────

void test_parsedef_chain_ops() {
  // App<XX...> prepends components — WithWsNS = ParseDef<char, Many<Space>, Satisfy<isDigitP>>
  auto r1 = WithWsNS::run("  5x");
  assert(r1.ok && r1.val=='5');

  // Ins<XX...> appends components after existing ones (closer to terminal)
  using InsP = BaseDigitNS::Ins<Eof>;
  auto r2 = InsP::run("5");    // digit then EOF
  assert(r2.ok && r2.val=='5');
  auto r3 = InsP::run("5x");   // digit then non-EOF — fails
  assert(!r3.ok);

  cout << "PASS test_parsedef_chain_ops\n";
}

// ─── Test 7: hapi::query tag detection on ParseDef ───────────────────────────

void test_query_on_parsedef() {
  // ZeroWidthTag is inherited by Many<Space> (zero-width combinator)
  static_assert( query<TagIs<ZeroWidthTag>, WsDigitNS>,
    "ZeroWidthTag should be detectable in WsDigitNS");
  static_assert(!query<TagIs<ZeroWidthTag>, DigitCharNS>,
    "ZeroWidthTag should not appear in DigitCharNS");

  cout << "PASS test_query_on_parsedef\n";
}

// ─────────────────────────────────────────────────────────────────────────────

#ifdef ARDUINO
  void setup() {
    Serial.begin(115200);
    while (!Serial);
    test_leaf_parsers();
    test_seq();
    test_or();
    test_repetition();
    test_skip_opt();
    test_parsedef_chain_ops();
    test_query_on_parsedef();
  }
  void loop() {}
#else
  int main() {
    test_leaf_parsers();
    test_seq();
    test_or();
    test_repetition();
    test_skip_opt();
    test_parsedef_chain_ops();
    test_query_on_parsedef();
    cout << "\nAll tests passed.\n";
    return 0;
  }
#endif
