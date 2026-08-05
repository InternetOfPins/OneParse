// HLS synthesis target: "on steroids" -- buffer + run_n() bulk-scan path.
// Isolated in its own translation unit -- see char_top.cpp's header
// comment for why.

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
P p;

size_t jsonBufTop(const char* buf, size_t n) {
  return p.run_n(buf, n);
}
