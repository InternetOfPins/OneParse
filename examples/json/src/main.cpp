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
// Note: streaming result type is StreamRes (not Res<T> which is for typed parsers).

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

    // Loop instead of self-recursive `return run(c);` on phase transitions
    // (Key->Colon, Val->Sep) -- a HAPI/HLS-portability constraint, not a
    // style choice: the recursive-call form creates a self-edge in the
    // function's call graph, which triggers a real bug in Bambu HLS's
    // clang16_plugin_topfname.so (a missing-memoization reachability
    // traversal -- confirmed against Bambu's own source). A loop is
    // intra-procedural control flow, invisible to a call-graph pass, and
    // produces byte-identical
    // output under both g++ and clang++ on every fixture this example
    // demonstrates.
    StreamRes run(char c) {
      while (true) {
        switch (phase) {

          case Phase::Open:
            if (c=='{') { phase=Phase::AfterOpen; return StreamRes::Ok(c); }
            return StreamRes::Fail();

          case Phase::AfterOpen:
            if (isSpace(c)) return StreamRes::Ok(c);
            if (c=='}')     { phase=Phase::Done; return StreamRes::Ok(c); }
            phase=Phase::Key;
            continue;

          case Phase::Key: {
            auto r = key.run(c);
            if (r.state==St::fail) { phase=Phase::Colon; continue; }
            return r;
          }

          case Phase::Colon:
            if (isSpace(c)) return StreamRes::Ok(c);
            if (c==':') { put(':'); put(' '); phase=Phase::ValStart; return StreamRes::Ok(c); }
            return StreamRes::Fail();

          case Phase::ValStart:
            if (isSpace(c)) return StreamRes::Ok(c);
            phase=Phase::Val;
            continue;

          case Phase::Val: {
            auto r = val.run(c);
            if (r.state==St::fail) { phase=Phase::Sep; continue; }
            return r;
          }

          case Phase::Sep:
            if (isSpace(c)) return StreamRes::Ok(c);
            if (c==',') {
              key = decltype(key){}; val = decltype(val){};
              phase=Phase::AfterOpen;
              put('\n');
              return StreamRes::Ok(c);
            }
            if (c=='}') { phase=Phase::Done; return StreamRes::Ok(c); }
            return StreamRes::Fail();

          case Phase::Done:
            return StreamRes::Fail();
        }
      }
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
