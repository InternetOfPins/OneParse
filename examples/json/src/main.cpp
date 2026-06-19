// OneParse streaming JSON flat-object parser
//
// Grammar:
//   object = '{' member* '}'
//   member = ws '"' key '"' ws ':' ws value ws sep
//   value  = '"' chars '"'               (string — NoneOf<'"'>)
//            | [-0-9.]+                  (number — String<Or<Digit,Sign,Char<'.'>>>)
//            | "null" | "true" | "false" (keyword — Alt dispatch)
//
// No AST, no type conversion. Matched chars flow via put() to the output chain.
// Error on bad input: silent fail (streaming model — print at fail site if needed).

#include <oneParse/oneParse.h>
#include <oneOutput/oneOutput.h>
#include <iostream>
using namespace std;
using namespace oneOutput;
using namespace oneParse;

// ── JSON sub-parsers (composed from library primitives) ─────────────────────

// Quoted string: open-quote, content (anything except '"'), close-quote
using JsonStr = Meta<Char<'"'>, String<NoneOf<'"'>>, Char<'"'>>;

// Number: leading sign or digit, then digits and dot
using JsonNum = String<Or<Digit, Sign, Char<'.'>>>;

// Keywords matched as exact literals
using JsonNull  = Lit<'n','u','l','l'>;
using JsonTrue  = Lit<'t','r','u','e'>;
using JsonFalse = Lit<'f','a','l','s','e'>;

// Value: first-char dispatch commits to one branch, which runs until it fails
using JsonVal = Alt<JsonStr, JsonNum, JsonNull, JsonTrue, JsonFalse>;

// ── Top-level object state machine ──────────────────────────────────────────
// The grammar structure (members, commas, braces) is stateful across many chars
// — natural fit for a custom state machine. Sub-parsers above do the real work.

struct JsonObj {
  template<typename O> struct Part : O {
    using Base=O; using Base::Base; using Base::put;

    enum class Phase : uint8_t {
      Open, AfterOpen, Key, Colon, ValStart, Val, Sep, Done
    } phase{Phase::Open};

    // Sub-parsers share the same output chain (O) as this Part
    typename JsonStr::template Part<O> key;
    typename JsonVal::template Part<O> val;

    Res run(char c) {
      switch (phase) {

        case Phase::Open:
          if (c=='{') { phase=Phase::AfterOpen; return Res::Ok(c); }
          return Res::Fail();

        case Phase::AfterOpen:
          if (isSpace(c)) return Res::Ok(c);
          if (c=='}')     { phase=Phase::Done; return Res::Ok(c); }
          phase=Phase::Key;
          [[fallthrough]];

        case Phase::Key: {
          auto r = key.run(c);
          if (r.state==St::fail) { phase=Phase::Colon; return run(c); }
          return r;
        }

        case Phase::Colon:
          if (isSpace(c)) return Res::Ok(c);
          if (c==':') { put(':'); put(' '); phase=Phase::ValStart; return Res::Ok(c); }
          return Res::Fail();

        case Phase::ValStart:
          if (isSpace(c)) return Res::Ok(c);
          phase=Phase::Val;
          [[fallthrough]];

        case Phase::Val: {
          auto r = val.run(c);
          if (r.state==St::fail) { phase=Phase::Sep; return run(c); }
          return r;
        }

        case Phase::Sep:
          if (isSpace(c)) return Res::Ok(c);
          if (c==',') {
            key = decltype(key){}; val = decltype(val){};
            phase=Phase::AfterOpen;
            put('\n');
            return Res::Ok(c);
          }
          if (c=='}') { phase=Phase::Done; return Res::Ok(c); }
          return Res::Fail();

        case Phase::Done:
          return Res::Fail();
      }
      return Res::Fail();
    }
  };
};

// ── Wire up to output chain ─────────────────────────────────────────────────

using P = ParseDef<JsonObj, ConsoleOut>;

static void parse(const char* src) {
  P p;
  cout << src << "\n  -> ";
  for (const char* s=src; *s; ++s) p.run(*s);
  cout << "\n\n";
}

// ── Demo ─────────────────────────────────────────────────────────────────────

int main() {
  cout << "=== streaming JSON flat-object parser ===\n\n";
  parse(R"({"sensor":"temp","value":42,"unit":"C"})");
  parse(R"({"active":true,"count":0,"name":"Alice","ref":null})");
  parse(R"({"host":"192.168.1.1","port":8080,"tls":false})");
  parse(R"({"x":-7,"y":0,"z":255})");
  parse(R"({ "name" : "Alice" , "age" : 30 , "admin" : false })");
  parse(R"({})");
  cout << "=== edge cases (silent fail) ===\n\n";
  parse(R"(not json)");
  parse(R"({"unclosed":42)");
  return 0;
}
