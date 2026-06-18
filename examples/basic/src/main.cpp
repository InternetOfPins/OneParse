/**
 * @file main.cpp
 * @brief OneParse basic example — leaf, negation, and meta parsers
 */

#include <iostream>
using namespace std;

#include <oneParse/oneParse.h>
using namespace oneParse;

static void report(const char* label, bool ok, char val, const char* rest) {
  cout << label << ": " << (ok ? "ok" : "FAIL")
       << "  val='" << (ok ? val : '?') << "'"
       << "  rest=\"" << (rest ? rest : "") << "\"" << endl;
}

// --- leaf ---------------------------------------------------------------------

using Hash      = ParseDef<char, Char<'#'>>;
using ADigit    = ParseDef<char, Digit>;
using AAlpha    = ParseDef<char, Alpha>;
using HashBang  = ParseDef<char, Char<'#'>, Char<'!'>>;

// --- negation -----------------------------------------------------------------

using NotHash   = ParseDef<char, Not<Char<'#'>>>;
using NotDigit  = ParseDef<char, NoneOf<'0','1','2','3','4','5','6','7','8','9'>>;

// --- Or -----------------------------------------------------------------------

using DigOrLow  = ParseDef<char, Or<Digit, Range<'a','z'>>>;

// --- Opt / Some / Many --------------------------------------------------------

using MaybeSign = ParseDef<char, Opt<Or<Char<'+'>, Char<'-'>>>>;
using Spaces    = ParseDef<char, Many<Space>>;
using Digits1   = ParseDef<char, Some<Digit>>;

// --- Skip ---------------------------------------------------------------------

using EqToken   = ParseDef<char, Skip<Space>, Char<'='>>;

// --- Seq + To ----------------------------------------------------------------

using KeyP      = ParseDef<char, Alpha>;
using ValP      = ParseDef<char, Digit>;
using KVPair    = ParseDef<Pair<char,char>, Seq<KeyP, ValP>>;

struct Entry { char key; char val; };
static Entry makeEntry(Pair<char,char> p) { return {p.fst, p.snd}; }
using EntryP    = ParseDef<Entry, To<Pair<char,char>, makeEntry, Seq<KeyP, ValP>>>;

// --- ManyN / ManyFn -----------------------------------------------------------

using ADigitP   = ParseDef<char, Digit>;
using Digits8   = ParseDef<Arr<char,8>, ManyN<ADigitP, 8>>;

static char sinkBuf[64]; static int sinkPos = 0;
static void sinkFn(char c) { sinkBuf[sinkPos++] = c; }
using AlphaSink = ParseDef<char, ManyFn<AAlpha, sinkFn>>;

// -----------------------------------------------------------------------------

void run() {
  cout << "\n--- leaf ---\n";
  { auto r = Hash::run("#hello");     report("Hash      \"#hello\"",  r.ok, r.val, r.rest); }
  { auto r = Hash::run(".hello");     report("Hash      \".hello\"",  r.ok, r.val, r.rest); }
  { auto r = ADigit::run("3abc");     report("Digit     \"3abc\"",    r.ok, r.val, r.rest); }
  { auto r = AAlpha::run("3abc");     report("Alpha     \"3abc\"",    r.ok, r.val, r.rest); }
  { auto r = HashBang::run("#!ok");   report("HashBang  \"#!ok\"",    r.ok, r.val, r.rest); }
  { auto r = HashBang::run("#.ok");   report("HashBang  \"#.ok\"",    r.ok, r.val, r.rest); }

  cout << "\n--- negation ---\n";
  { auto r = NotHash::run("hello");   report("NotHash   \"hello\"",   r.ok, r.val, r.rest); }
  { auto r = NotHash::run("#hello");  report("NotHash   \"#hello\"",  r.ok, r.val, r.rest); }
  { auto r = NotDigit::run("abc");    report("NotDigit  \"abc\"",     r.ok, r.val, r.rest); }
  { auto r = NotDigit::run("3abc");   report("NotDigit  \"3abc\"",    r.ok, r.val, r.rest); }

  cout << "\n--- Or ---\n";
  { auto r = DigOrLow::run("x!");     report("DigOrLow  \"x!\"",      r.ok, r.val, r.rest); }
  { auto r = DigOrLow::run("3!");     report("DigOrLow  \"3!\"",      r.ok, r.val, r.rest); }
  { auto r = DigOrLow::run("A!");     report("DigOrLow  \"A!\"",      r.ok, r.val, r.rest); }

  cout << "\n--- Opt / Some / Many ---\n";
  { auto r = MaybeSign::run("-5");    report("MaybeSign \"-5\"",      r.ok, r.val, r.rest); }
  { auto r = MaybeSign::run("5");     report("MaybeSign \"5\"",       r.ok, r.val, r.rest); }
  { auto r = Spaces::run("   hi");    report("Spaces    \"   hi\"",   r.ok, r.val, r.rest); }
  { auto r = Spaces::run("hi");       report("Spaces    \"hi\"",      r.ok, r.val, r.rest); }
  { auto r = Digits1::run("42abc");   report("Some<Dig> \"42abc\"",   r.ok, r.val, r.rest); }
  { auto r = Digits1::run("abc");     report("Some<Dig> \"abc\"",     r.ok, r.val, r.rest); }

  cout << "\n--- Skip ---\n";
  { auto r = EqToken::run(" =val");   report("EqToken   \" =val\"",   r.ok, r.val, r.rest); }
  { auto r = EqToken::run("=val");    report("EqToken   \"=val\"",    r.ok, r.val, r.rest); }

  cout << "\n--- Seq ---\n";
  { auto r = KVPair::run("a3!");
    cout << "KVPair    \"a3!\": " << (r.ok ? "ok" : "FAIL")
         << "  {'" << r.val.fst << "','" << r.val.snd << "'}"
         << "  rest=\"" << r.rest << "\"" << endl; }

  cout << "\n--- To ---\n";
  { auto r = EntryP::run("b7!");
    cout << "Entry     \"b7!\": " << (r.ok ? "ok" : "FAIL")
         << "  {key:'" << r.val.key << "' val:'" << r.val.val << "'}"
         << "  rest=\"" << r.rest << "\"" << endl; }

  cout << "\n--- ManyN ---\n";
  { auto r = Digits8::run("1234abc");
    cout << "Digits8   \"1234abc\": " << (r.ok ? "ok" : "FAIL")
         << "  len=" << r.val.len << " [";
    for (size_t i = 0; i < r.val.len; i++) cout << r.val.data[i];
    cout << "]  rest=\"" << r.rest << "\"" << endl; }

  cout << "\n--- ManyFn ---\n";
  sinkPos = 0;
  { auto r = AlphaSink::run("hello!");
    sinkBuf[sinkPos] = '\0';
    cout << "AlphaSink \"hello!\": " << (r.ok ? "ok" : "FAIL")
         << "  collected=\"" << sinkBuf << "\""
         << "  rest=\"" << r.rest << "\"" << endl; }
}

int main() { run(); return 0; }
