// HLS synthesis target: "tiny" -- single-char run(), no memory interface.
// Isolated in its own translation unit (not part of src/, so PlatformIO's
// native build doesn't pick it up) deliberately: a second same-typed
// global parser object in the same TU (as in src/main.cpp, which has both
// jsonCharTop and jsonBufTop for the native demo) pulls extra
// static-initialization machinery into Bambu's output and inflates the
// footprint -- confirmed empirically, see README.md. This file is the
// real, isolated number.

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
  };
};

using P = ParseDef<JsonObj, CountOut>;
P p;

int jsonCharTop(char c) {
  auto r = p.run(c);
  return r.state == St::ok ? 1 : 0;
}
