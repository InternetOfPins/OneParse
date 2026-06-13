// OneParse json example — flat JSON object parser
//
// Inspired by paco json-paco-ast (github.com/neu-rah/paco):
//   _null  = string("null").as(_ => null)
//   _true  = string("true").as(_ => true)
//   _false = string("false").as(_ => false)
//   string = skip('"') + many(noneOf('"')) + skip('"')
//   number = optional('-') + int + optional(frac)
//   member = string + skip(':') + value
//   object = skip('{') + sepBy(member, ',') + skip('}')
//
// Parses flat JSON objects: null, bool, int, string values.
// Demonstrates: Between, SepBy, Not, Or, To, Str (keyword matching)

#ifdef ARDUINO
  #include <Arduino.h>
#endif

#include <iostream>
using namespace std;

#include <oneParse/oneParse.h>
using namespace oneParse;

// --- Value type ---------------------------------------------------------------

struct Val {
  enum class Kind : uint8_t { None, Null, Bool, Int, Str } kind = Kind::None;
  Arr<char,32> str{};
  int          i = 0;
  bool         b = false;
};

// --- Helpers ------------------------------------------------------------------

static int digitsToInt(Arr<char,10> a) {
  int n = 0;
  for (size_t i = 0; i < a.len; i++) n = n * 10 + (a.data[i] - '0');
  return n;
}

static Val asNull(char)          { Val v; v.kind = Val::Kind::Null;        return v; }
static Val asTrue(char)          { Val v; v.kind = Val::Kind::Bool; v.b = true;  return v; }
static Val asFalse(char)         { Val v; v.kind = Val::Kind::Bool; v.b = false; return v; }
static Val asStr(Arr<char,32> s) { Val v; v.kind = Val::Kind::Str;  v.str = s;  return v; }

static Val signedToVal(Pair<char,Arr<char,10>> p) {
  Val v;
  v.kind = Val::Kind::Int;
  v.i = (p.fst == '-' ? -1 : 1) * digitsToInt(p.snd);
  return v;
}

// --- Keyword literals (Str<S> requires constexpr const char[] at namespace scope) ---

constexpr const char kNull[]  = "null";
constexpr const char kTrue[]  = "true";
constexpr const char kFalse[] = "false";

// --- String parser ------------------------------------------------------------

using QuotedBodyP = ParseDef<Arr<char,32>, ManyN<ParseDef<char, Not<Char<'"'>>>, 32>>;
using KeyBodyP    = ParseDef<Arr<char,16>, ManyN<ParseDef<char, Not<Char<'"'>>>, 16>>;
using KeyP        = ParseDef<Arr<char,16>,
    Skip<Many<Space>>,
    Between<Char<'"'>, KeyBodyP, Char<'"'>>>;

// --- Value components (each lifts its result to Val) --------------------------

// Keywords — Str<S> matches the literal, To maps the dummy char result to Val
using NullComp  = To<char, asNull,  Str<kNull>>;
using TrueComp  = To<char, asTrue,  Str<kTrue>>;
using FalseComp = To<char, asFalse, Str<kFalse>>;

// String — Between<'"', body, '"'> is a component; To lifts Arr<char,32> → Val
using StrValComp = To<Arr<char,32>, asStr,
    Between<Char<'"'>, QuotedBodyP, Char<'"'>>>;

// Integer — [+-]? digits → Val
using MagP = ParseDef<Arr<char,10>, SomeN<ParseDef<char,Digit>,10>>;
using SignP = ParseDef<char, Opt<Or<Char<'+'>, Char<'-'>>>>;
using IntValComp = To<Pair<char,Arr<char,10>>, signedToVal, Seq<SignP, MagP>>;

// All alternatives — tried left to right; Or<P1,P2> takes components
// null / true / false must come before int (to avoid "null" being parsed as NCName)
using AnyValComp = Or<NullComp,
                   Or<TrueComp,
                   Or<FalseComp,
                   Or<StrValComp, IntValComp>>>>;

// --- Member: "key" : value ----------------------------------------------------

using ValAfterColonP = ParseDef<Val,
    Skip<Many<Space>, Char<':'>, Many<Space>>,
    AnyValComp>;

using MemberP = ParseDef<Pair<Arr<char,16>,Val>, Seq<KeyP, ValAfterColonP>>;

// --- Object -------------------------------------------------------------------

using CommaP    = Skip<Many<Space>, Char<','>, Many<Space>>;
using CloseObjP = Skip<Many<Space>, Char<'}'>>;

using MembersP = ParseDef<Arr<Pair<Arr<char,16>,Val>, 8>,
    SepBy<MemberP, CommaP, 8>>;

using ObjectP = ParseDef<Arr<Pair<Arr<char,16>,Val>, 8>,
    Skip<Many<Space>>,
    Between<Char<'{'>, MembersP, CloseObjP>>;

// --- Printer ------------------------------------------------------------------

static void printVal(const Val& v) {
  switch (v.kind) {
    case Val::Kind::Null: cout << "null";                              break;
    case Val::Kind::Bool: cout << (v.b ? "true" : "false");           break;
    case Val::Kind::Int:  cout << v.i;                                 break;
    case Val::Kind::Str:
      cout << '"';
      for (char c : v.str) cout << c;
      cout << '"';
      break;
    default: cout << "?"; break;
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
  runObj(R"({"active":true,"count":0,"name":"Alice","ref":null})");
  runObj(R"({"host":"192.168.1.1","port":8080,"tls":false})");
  runObj(R"({"x":-7,"y":0,"z":255})");
  runObj(R"({ "name" : "Alice" , "age" : 30 , "admin" : false })");
  runObj(R"({})");

  cout << "=== edge cases ===\n\n";
  runObj(R"(not json)");
  runObj(R"({"unclosed":42)");
  runObj(R"({"k":})");
  runObj(R"({"tricky":"nullified","flag":true,"n":null})");
}

// -----------------------------------------------------------------------------

#ifdef ARDUINO
  void setup() { Serial.begin(115200); while (!Serial); run(); }
  void loop() {}
#else
  int main() { run(); return 0; }
#endif
