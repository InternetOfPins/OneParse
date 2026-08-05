// OneParse HLS smoke test -- the real, full JSON grammar
// (JsonVal = Alt<JsonStr, JsonNum, JsonNull, JsonTrue, JsonFalse>), same as
// examples/json, synthesized through PandA-Bambu HLS. Demonstrates the
// same composed grammar at two entry-point shapes:
//
//   jsonCharTop(char c)              -- "tiny": one char in, run() alone,
//                                        no memory interface at all.
//   jsonBufTop(const char* buf, n)   -- "on steroids": a real buffer + the
//                                        run_n() bulk-scan path (the same
//                                        memchr-optimized fast path
//                                        oneParse.h uses internally, not a
//                                        wrapper loop around run()).
//
// Both are real Bambu synthesis targets (--top-fname=jsonCharTop /
// --top-fname=jsonBufTop). See README.md for the verified footprint
// numbers -- same grammar, same composition machinery, genuinely
// different hardware cost depending on which entry point is used.
//
// ConsoleOut swapped for a synthesizable accumulating Out (same pattern as
// every other .../hls_smoke example) -- see HAPI/examples/hls_smoke and
// OneParse/.RnD/hls/FINDINGS.md for why (iostream has no synthesis
// target). run()'s self-recursion was also rewritten as a loop -- see
// FINDINGS.md's root-cause writeup (a real Bambu topfname bug, worked
// around here, not a HAPI/OneParse issue).

#include <oneParse/oneParse.h>
#include <oneOutput/oneOutput.h>
using namespace oneParse;
using namespace oneOutput;

static int total = 0;
struct CountOut {
  template<typename O>
  struct Part : O {
    static void put(char c) { total += (int)c; }
    template<typename T> static constexpr void put(const T&) {}
    static constexpr void put(const char*, SizeT) {}
  };
};

using JsonStr   = Meta<Char<'"'>, String<NoneOf<'"'>>, Char<'"'>>;
using JsonNum   = String<Or<Digit, Sign, Char<'.'>>>;
using JsonNull  = Lit<'n','u','l','l'>;
using JsonTrue  = Lit<'t','r','u','e'>;
using JsonFalse = Lit<'f','a','l','s','e'>;
using JsonVal   = Alt<JsonStr, JsonNum, JsonNull, JsonTrue, JsonFalse>;

struct JsonObj {
  template<typename O> struct Part : O {
    using Base=O; using Base::Base; using Base::put;

    enum class Phase : uint8_t {
      Open, AfterOpen, Key, Colon, ValStart, Val, Sep, Done
    } phase{Phase::Open};

    typename JsonStr::template Part<O> key;
    typename JsonVal::template Part<O> val;

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

    // Bulk path: fast-scan string bodies, char-by-char only for phase
    // transitions -- the "steroids" entry point's real machinery, not a
    // synthetic wrapper.
    size_t run_n(const char* s, size_t n) {
      size_t total_ = 0;
      while (total_ < n) {
        size_t k = 0;
        if      (phase == Phase::Key) k = key.run_n(s + total_, n - total_);
        else if (phase == Phase::Val) k = val.run_n(s + total_, n - total_);
        if (k > 0) { total_ += k; }
        else {
          if (run(s[total_]).state == St::fail) break;
          ++total_;
        }
      }
      return total_;
    }
  };
};

using P = ParseDef<JsonObj, CountOut>;

// ── "tiny": one genuine char in, run() alone ──────────────────────────────
P pChar;
int jsonCharTop(char c) {
  auto r = pChar.run(c);
  return r.state == St::ok ? 1 : 0;
}

// ── "on steroids": a real buffer + the run_n() bulk-scan path ────────────
P pBuf;
size_t jsonBufTop(const char* buf, size_t n) {
  return pBuf.run_n(buf, n);
}

#include <iostream>
// host-side sanity check only -- not part of the synthesized design.
int main() {
  std::cout << jsonCharTop('{') << std::endl;
  const char* s = "{\"x\":1}";
  std::cout << jsonBufTop(s, 7) << std::endl;
  return 0;
}
