/**
 * @file test.cpp
 * @brief OneParse unit tests
 *
 * Tests:
 *   Char<C>          — single character match
 *   Str<S>           — literal string match
 *   Digit/Alpha/Space — character class predicates
 *   Range<Lo,Hi>     — inclusive character range
 *   AnyOf<Cs...>     — character set match
 *   Not<P>           — negation
 *   Opt<P>           — optional (always succeeds)
 *   Some<P>          — one-or-more
 *   Many<P>          — zero-or-more
 *   Or<P1,P2>        — alternation
 *   Seq<P1,P2>       — sequential pair
 *   Skip<PP...>      — consume without contributing value
 *   To<T,F,PP...>    — transform result type
 *   Eof              — end-of-input
 *   Between          — bracketed match
 *   SepBy<P,Sep,N>   — separated list
 */

#include <iostream>
#include <cassert>
#include <string>
#include <oneParse/oneParse.h>

#ifdef ARDUINO
  #define cout Serial
  #define assert(x) do { if(!(x)) { Serial.print("FAIL: "); Serial.println(#x); } } while(0)
#else
  #include <cassert>
  using namespace std;
#endif

using namespace oneParse;

void test_char() {
  using P = ParseDef<char, Char<'a'>>;
  auto ok = P::run("abc");
  assert(ok.ok && ok.val == 'a' && string(ok.rest) == "bc");
  auto no = P::run("xyz");
  assert(!no.ok);
  auto empty = P::run("");
  assert(!empty.ok);
  cout << "Char<'a'>: ok" << endl;
}

static constexpr const char hello_str[] = "hello";

void test_str() {
  using P = ParseDef<char, Str<hello_str>>;
  auto ok = P::run("helloworld");
  assert(ok.ok && string(ok.rest) == "world");
  auto no = P::run("world");
  assert(!no.ok);
  cout << "Str<\"hello\">: ok" << endl;
}

void test_digit_alpha_space() {
  using D = ParseDef<char, Digit>;
  using A = ParseDef<char, Alpha>;
  using S = ParseDef<char, Space>;
  assert( D::run("5x").ok);
  assert(!D::run("x5").ok);
  assert( A::run("abc").ok);
  assert(!A::run("123").ok);
  assert( S::run(" x").ok);
  assert(!S::run("x ").ok);
  cout << "Digit/Alpha/Space: ok" << endl;
}

void test_range() {
  using Lower = ParseDef<char, Range<'a','z'>>;
  assert( Lower::run("foo").ok && Lower::run("foo").val == 'f');
  assert(!Lower::run("FOO").ok);
  cout << "Range<'a','z'>: ok" << endl;
}

void test_anyof() {
  using Vowel = ParseDef<char, AnyOf<'a','e','i','o','u'>>;
  assert( Vowel::run("apple").ok && Vowel::run("apple").val == 'a');
  assert(!Vowel::run("xyz").ok);
  cout << "AnyOf vowels: ok" << endl;
}

void test_not() {
  using NotDigit = ParseDef<char, Not<Digit>>;
  assert( NotDigit::run("abc").ok && NotDigit::run("abc").val == 'a');
  assert(!NotDigit::run("123").ok);
  assert(!NotDigit::run("").ok);
  cout << "Not<Digit>: ok" << endl;
}

void test_opt() {
  using MaybeSign = ParseDef<char, Opt<Char<'+'>>>;
  auto with = MaybeSign::run("+5");
  assert(with.ok && with.val == '+' && string(with.rest) == "5");
  auto without = MaybeSign::run("5");
  assert(without.ok && string(without.rest) == "5");  // always succeeds
  cout << "Opt<Char<'+'>: ok" << endl;
}

void test_some() {
  using Digits = ParseDef<char, Some<Digit>, Eof>;
  assert( Digits::run("123").ok);
  assert(!Digits::run("").ok);    // Some requires at least one
  assert(!Digits::run("1a2").ok); // Eof fails on trailing non-digit
  cout << "Some<Digit> + Eof: ok" << endl;
}

void test_many() {
  using Spaces = ParseDef<char, Many<Space>, Char<'x'>>;
  assert( Spaces::run("   x").ok);
  assert( Spaces::run("x").ok);   // Many zero matches → succeeds, Char<'x'> matches
  assert(!Spaces::run("   y").ok);
  cout << "Many<Space>: ok" << endl;
}

void test_or() {
  using DigOrAlpha = ParseDef<char, Or<Digit, Alpha>>;
  auto r1 = DigOrAlpha::run("3x");
  assert(r1.ok && r1.val == '3');
  auto r2 = DigOrAlpha::run("x3");
  assert(r2.ok && r2.val == 'x');
  assert(!DigOrAlpha::run(" ").ok);
  cout << "Or<Digit, Alpha>: ok" << endl;
}

void test_seq() {
  using AB = ParseDef<Pair<char,char>,
    Seq<ParseDef<char,Char<'a'>>, ParseDef<char,Char<'b'>>>>;
  auto r = AB::run("abcd");
  assert(r.ok && r.val.fst == 'a' && r.val.snd == 'b');
  assert(string(r.rest) == "cd");
  assert(!AB::run("axcd").ok);
  cout << "Seq<Char<'a'>, Char<'b'>>: ok" << endl;
}

void test_skip() {
  using EqTok = ParseDef<char, Skip<Many<Space>>, Char<'='>>;
  auto r1 = EqTok::run("   =5");
  assert(r1.ok && r1.val == '=' && string(r1.rest) == "5");
  auto r2 = EqTok::run("=5");
  assert(r2.ok);
  assert(!EqTok::run("  x").ok);
  cout << "Skip<Many<Space>> + Char<'='>: ok" << endl;
}

static constexpr int charToInt(char c) { return c - '0'; }

void test_to() {
  using DigInt = ParseDef<int, To<char, charToInt, Digit>>;
  auto r = DigInt::run("7abc");
  assert(r.ok && r.val == 7 && string(r.rest) == "abc");
  assert(!DigInt::run("abc").ok);
  cout << "To<char, charToInt, Digit>: ok" << endl;
}

void test_eof() {
  using End = ParseDef<char, Eof>;
  assert( End::run("").ok);
  assert(!End::run("x").ok);
  cout << "Eof: ok" << endl;
}

void test_between() {
  using Parens = ParseDef<char,
    Between<Char<'('>, ParseDef<char,Digit>, Char<')'>>>;
  auto r = Parens::run("(5)rest");
  assert(r.ok && r.val == '5' && string(r.rest) == "rest");
  assert(!Parens::run("(5x)").ok);
  assert(!Parens::run("5").ok);
  cout << "Between<'(', Digit, ')'>: ok" << endl;
}

void test_sepby() {
  using CsvDigits = ParseDef<Arr<char,8>,
    SepBy<ParseDef<char,Digit>, Char<','>, 8>>;
  auto r = CsvDigits::run("1,2,3end");
  assert(r.ok && r.val.len == 3);
  assert(r.val.data[0]=='1' && r.val.data[1]=='2' && r.val.data[2]=='3');
  assert(string(r.rest) == "end");

  auto empty = CsvDigits::run("end");   // SepBy allows zero matches
  assert(empty.ok && empty.val.len == 0);
  cout << "SepBy<Digit, ',', 8>: ok" << endl;
}

void doTests() {
  test_char();
  test_str();
  test_digit_alpha_space();
  test_range();
  test_anyof();
  test_not();
  test_opt();
  test_some();
  test_many();
  test_or();
  test_seq();
  test_skip();
  test_to();
  test_eof();
  test_between();
  test_sepby();
  cout << "all OneParse tests passed" << endl;
}

#ifdef ARDUINO
  void setup() { Serial.begin(115200); while(!Serial); doTests(); }
  void loop() {}
#else
  int main() { doTests(); return 0; }
#endif
