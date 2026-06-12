// OneParse json example — flat JSON object parser
//
// Inspired by paco json-paco-ast (github.com/neu-rah/paco):
//   string = skip('"') + many(noneOf('"')) + skip('"')
//   member = string + skip(':') + value
//   object = skip('{') + sepBy(member, ',') + skip('}')
//
// Parses flat JSON objects with string or integer values.
// Demonstrates: Between, SepBy, Not, Or, To, Verify

#ifdef ARDUINO
  #include <Arduino.h>
#endif

#include <iostream>
using namespace std;

#include <oneParse/oneParse.h>
using namespace oneParse;

// --- Value type ---------------------------------------------------------------

struct Val {
  enum class Kind : uint8_t { None, Str, Int } kind = Kind::None;
  Arr<char,32> str{};
  int          i = 0;
};

// --- Helpers ------------------------------------------------------------------

static int digitsToInt(Arr<char,10> a) {
  int n = 0;
  for (size_t i = 0; i < a.len; i++) n = n * 10 + (a.data[i] - '0');
  return n;
}

static Val asStr(Arr<char,32> s) { return {Val::Kind::Str, s, 0}; }

static Val signedToVal(Pair<char,Arr<char,10>> p) {
  int n = (p.fst == '-' ? -1 : 1) * digitsToInt(p.snd);
  Val v; v.kind = Val::Kind::Int; v.i = n; return v;
}

// --- String parsers -----------------------------------------------------------

// Body of a quoted string: any char except '"', up to 32 chars
using QuotedBodyP = ParseDef<Arr<char,32>,
    ManyN<ParseDef<char, Not<Char<'"'>>>, 32>>;

// Key body: same, 16 chars
using KeyBodyP = ParseDef<Arr<char,16>,
    ManyN<ParseDef<char, Not<Char<'"'>>>, 16>>;

// Key parser: leading whitespace + quoted string
using KeyP = ParseDef<Arr<char,16>,
    Skip<Many<Space>>,
    Between<Char<'"'>, KeyBodyP, Char<'"'>>>;

// --- Value parsers ------------------------------------------------------------

// Digits for int parsing
using MagP = ParseDef<Arr<char,10>, SomeN<ParseDef<char,Digit>,10>>;
using SignP = ParseDef<char, Opt<Or<Char<'+'>, Char<'-'>>>>;

// Component: match a quoted string and lift to Val
// To<T_in, F, PP...> probes PP... as T_in, applies F
using StrValComp = To<Arr<char,32>, asStr,
    Between<Char<'"'>, QuotedBodyP, Char<'"'>>>;

// Component: match [+-]?digits and lift to Val
using IntValComp = To<Pair<char,Arr<char,10>>, signedToVal,
    Seq<SignP, MagP>>;

// Complete parser for a value (string or int), with leading whitespace
// Or<P1,P2> takes components — both StrValComp and IntValComp are component-level To<>
using ValP = ParseDef<Val, Skip<Many<Space>>, Or<StrValComp, IntValComp>>;

// --- Member: "key" : value ----------------------------------------------------

// Value parser that also skips the leading colon separator
using ValAfterColonP = ParseDef<Val,
    Skip<Many<Space>, Char<':'>, Many<Space>>,
    Or<StrValComp, IntValComp>>;

using MemberP = ParseDef<Pair<Arr<char,16>,Val>, Seq<KeyP, ValAfterColonP>>;

// --- Object -------------------------------------------------------------------

using CommaP   = Skip<Many<Space>, Char<','>, Many<Space>>;
using CloseObjP= Skip<Many<Space>, Char<'}'>>;

using MembersP = ParseDef<Arr<Pair<Arr<char,16>,Val>, 8>,
    SepBy<MemberP, CommaP, 8>>;

using ObjectP  = ParseDef<Arr<Pair<Arr<char,16>,Val>, 8>,
    Skip<Many<Space>>,
    Between<Char<'{'>, MembersP, CloseObjP>>;

// --- Printer ------------------------------------------------------------------

static void printVal(const Val& v) {
  if (v.kind == Val::Kind::Str) {
    cout << '"';
    for (char c : v.str) cout << c;
    cout << '"';
  } else if (v.kind == Val::Kind::Int) {
    cout << v.i;
  } else {
    cout << "null";
  }
}

static void runObj(const char* input) {
  cout << input << "\n";
  auto r = ObjectP::run(input);
  if (!r.ok) { cout << "  -> parse error\n\n"; return; }
  cout << "  -> {\n";
  for (size_t i = 0; i < r.val.len; i++) {
    cout << "       \"";
    for (char c : r.val.data[i].fst) cout << c;
    cout << "\": ";
    printVal(r.val.data[i].snd);
    cout << "\n";
  }
  cout << "     }\n\n";
}

// -----------------------------------------------------------------------------

void run() {
  cout << "=== JSON flat object parser ===\n\n";

  runObj(R"({"sensor":"temp","value":42,"unit":"C"})");
  runObj(R"({"host":"192.168.1.1","port":8080,"tls":0})");
  runObj(R"({"x":-7,"y":0,"z":255})");
  runObj(R"({ "name" : "Alice" , "age" : 30 })");
  runObj(R"({})");
  runObj(R"({"ok":1,"msg":"ready"})");

  cout << "=== edge cases ===\n\n";
  runObj(R"(not json)");
  runObj(R"({"unclosed":42)");
  runObj(R"({"k":})");
}

// -----------------------------------------------------------------------------

#ifdef ARDUINO
  void setup() { Serial.begin(115200); while (!Serial); run(); }
  void loop() {}
#else
  int main() { run(); return 0; }
#endif
