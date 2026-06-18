/**
 * @file main.cpp
 * @brief Kelvin temperature parser — paco-style AST via Seq + To
 *
 * Parses "temp: [+|-]<digits>K" and rejects negative values.
 *
 * Inspired by paco:
 *   string("temp: ")
 *   .then(option("", oneOf("-+")).then(digits).join().as(parseInt).to("temp"))
 *   .then(char('K').to("unit"))
 *   .verify(o => o.temp >= 0)
 *   .failMsg("positive Kelvin!")
 */

#include <iostream>
using namespace std;

#include <oneParse/oneParse.h>
using namespace oneParse;

// --- AST node -----------------------------------------------------------------

struct Kelvin {
  int temp; char unit;
  Kelvin() = default;
  explicit Kelvin(Pair<int,char> p) : temp(p.fst), unit(p.snd) {}
};

// --- Helpers ------------------------------------------------------------------

static int arrToInt(Arr<char,10> a) {
  int n = 0;
  for (size_t i = 0; i < a.len; i++) n = n * 10 + (a.data[i] - '0');
  return n;
}

static int signedInt(Pair<char, Arr<char,10>> p) {
  return (p.fst == '-' ? -1 : 1) * arrToInt(p.snd);
}

// --- Parser definition --------------------------------------------------------

// optional sign  →  char ('+', '-', or '\0')
using SignP      = ParseDef<char, Opt<Or<Char<'+'>, Char<'-'>>>>;

// one or more digits  →  Arr<char,10>  (SomeN: fails on zero digits)
using MagP       = ParseDef<Arr<char,10>, SomeN<ParseDef<char, Digit>, 10>>;

// sign + digits  →  signed int
using SignedIntP = ParseDef<int,
    To<Pair<char,Arr<char,10>>, signedInt,
        Seq<SignP, MagP>>>;

// string literal for the prefix (constexpr variable required by C++17 NTTP rules)
constexpr const char kPrefix[] = "temp: ";

// int + 'K'  →  Pair<int,char> — input to As<Kelvin>
using KelvinPairP = ParseDef<Pair<int,char>, Seq<SignedIntP, ParseDef<char, Char<'K'>>>>;

// skip prefix, then As<Kelvin> constructs directly from Pair<int,char>
using RawKelvinP = ParseDef<Kelvin, Str<kPrefix>, As<Kelvin, KelvinPairP>>;

// reject below absolute zero inline — mirrors paco's .verify(o => o.temp >= 0)
constexpr bool isPositiveKelvin(Kelvin k) { return k.temp >= 0; }
using KelvinP = ParseDef<Kelvin, Verify<RawKelvinP, isPositiveKelvin>>;

// --- Runner -------------------------------------------------------------------

static void run(const char* input) {
  auto r = KelvinP::run(input);

  cout << "  \"" << input << "\"\n";

  if (!r.ok) {
    cout << "    -> error: parse failed at \"" << (r.rest ? r.rest : "") << "\"" << endl;
    return;
  }
  cout << "    -> Kelvin{ temp: " << r.val.temp
       << ", unit: '" << r.val.unit << "' }" << endl;
}

// -----------------------------------------------------------------------------

int main() {
  run("temp: 12K");
  run("temp: +273K");
  run("temp: 0K");
  run("temp: -5K");
  run("temp: 300C");
  run("temp: K");
  run("humidity: 50K");
  return 0;
}
