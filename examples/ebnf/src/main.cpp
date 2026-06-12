// OneParse ebnf example — paco-ebnf-inspired combinators
//
// Inspired by paco-ebnf (github.com/neu-rah/paco-ebnf):
//   Comment = skip-open + manyTill(anyChar, close) + skip-close
//   TagList = skip('[') + sepBy(ident, ',') + skip(']')
//   Byte    = digits.verify(v => v >= 0 && v <= 255)
//
// Demonstrates: Any, ManyTill, Between, SepBy, SepBy1, Verify, Eof

#ifdef ARDUINO
  #include <Arduino.h>
#endif

#include <iostream>
using namespace std;

#include <oneParse/oneParse.h>
using namespace oneParse;

// --- Block comment ------------------------------------------------------------

constexpr const char kCOpen[]  = "/*";
constexpr const char kCClose[] = "*/";

// Skips a /* ... */ comment; fails if unterminated
// Written as a component chain so it can be nested inside Opt<>
using CommentSkip = Skip<Str<kCOpen>, ManyTill<Any, Str<kCClose>>, Str<kCClose>>;

// --- Tag list  ----------------------------------------------------------------

// One tag: one or more alphanumerics, with optional leading whitespace
using TagP = ParseDef<Arr<char,16>,
    Skip<Many<Space>>,
    SomeN<ParseDef<char, Or<Alpha,Digit>>, 16>>;

// Comma separator (tolerates surrounding spaces)
using CommaP = Skip<Many<Space>, Char<','>, Many<Space>>;

// Closing ']' tolerates leading whitespace (handles "[ a, b ]")
using CloseP = Skip<Many<Space>, Char<']'>>;

// Comma-separated tags inside brackets, preceded by optional comment + whitespace
using TagsInner = ParseDef<Arr<Arr<char,16>, 8>, SepBy<TagP, CommaP, 8>>;
using TagListP  = ParseDef<Arr<Arr<char,16>, 8>,
    Skip<Opt<CommentSkip>, Many<Space>>,
    Between<Char<'['>, TagsInner, CloseP>>;

// --- Byte value (Verify) ------------------------------------------------------

static int digitsToInt(Arr<char,3> a) {
  int n = 0;
  for (size_t i = 0; i < a.len; i++) n = n * 10 + (a.data[i] - '0');
  return n;
}

constexpr bool isByte(int v) { return v >= 0 && v <= 255; }

using RawIntP = ParseDef<int, To<Arr<char,3>, digitsToInt, SomeN<ParseDef<char,Digit>,3>>>;
using ByteP   = ParseDef<int, Verify<RawIntP, isByte>>;

// --- Eof ----------------------------------------------------------------------

// Matches only when the entire input has been consumed
using FullTagListP = ParseDef<Arr<Arr<char,16>, 8>,
    Skip<Opt<CommentSkip>, Many<Space>>,
    Between<Char<'['>, TagsInner, CloseP>,
    Skip<Many<Space>, Eof>>;

// --- Runners ------------------------------------------------------------------

static void runTags(const char* label, const char* input) {
  cout << label << ":\n  \"" << input << "\"\n";
  auto r = TagListP::run(input);
  if (!r.ok) { cout << "  -> error\n\n"; return; }
  cout << "  -> [";
  for (size_t i = 0; i < r.val.len; i++) {
    if (i) cout << ", ";
    for (char c : r.val.data[i]) cout << c;
  }
  cout << "]  (" << r.val.len << " items)\n\n";
}

static void runFullTags(const char* label, const char* input) {
  cout << label << ":\n  \"" << input << "\"\n";
  auto r = FullTagListP::run(input);
  if (!r.ok) { cout << "  -> error (trailing garbage or unterminated)\n\n"; return; }
  cout << "  -> ok, consumed all input\n\n";
}

static void runByte(const char* label, const char* input) {
  cout << label << ":\n  \"" << input << "\"\n";
  auto r = ByteP::run(input);
  cout << "  -> " << (r.ok ? to_string(r.val) + " (ok)" : "error (out of range or invalid)") << "\n\n";
}

// -----------------------------------------------------------------------------

void run() {
  cout << "=== Between + SepBy ===\n\n";
  runTags("simple list",          "[red, green, blue]");
  runTags("single item",          "[alpha]");
  runTags("empty list (SepBy)",   "[]");
  runTags("extra spaces",         "[ one ,  two ,   three ]");
  runTags("overflow > 8 items",   "[a,b,c,d,e,f,g,h,i]");

  cout << "=== ManyTill + Any (block comment) ===\n\n";
  runTags("comment before list",  "/* color channels */ [red, green, blue]");
  runTags("comment only prefix",  "/* sensors */ [temp, humid, co2]");
  runTags("unterminated comment", "/* oops [red, green]");

  cout << "=== SepBy1 (at least one) ===\n\n";
  {
    using Inner1 = ParseDef<Arr<Arr<char,16>,8>, SepBy1<TagP, CommaP, 8>>;
    using List1  = ParseDef<Arr<Arr<char,16>,8>,
        Skip<Opt<CommentSkip>, Many<Space>>,
        Between<Char<'['>, Inner1, CloseP>>;
    cout << "SepBy1 non-empty:\n";
    { auto r = List1::run("[a, b]");
      cout << "  \"[a, b]\" -> " << (r.ok ? "ok" : "error") << "\n"; }
    { auto r = List1::run("[]");
      cout << "  \"[]\"    -> " << (r.ok ? "ok" : "error (empty — SepBy1 requires ≥1)") << "\n"; }
    cout << "\n";
  }

  cout << "=== Eof ===\n\n";
  runFullTags("exact match",      "[x, y, z]");
  runFullTags("trailing garbage", "[x, y, z] extra");

  cout << "=== Verify ===\n\n";
  runByte("0 (boundary)",   "0");
  runByte("255 (boundary)", "255");
  runByte("256 (over)",     "256");
  runByte("42 (valid)",     "42");
  runByte("999 (over)",     "999");
}

// -----------------------------------------------------------------------------

#ifdef ARDUINO
  void setup() { Serial.begin(115200); while (!Serial); run(); }
  void loop() {}
#else
  int main() { run(); return 0; }
#endif
