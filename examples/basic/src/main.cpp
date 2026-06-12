/**
 * @file main.cpp
 * @brief OneParse basic example — leaf parsers and sequential composition
 */

#ifdef ARDUINO
  #include <Arduino.h>
#endif

#include <iostream>
using namespace std;

#include <oneParse/oneParse.h>
using namespace oneParse;

// helpers
static void report(const char* label, bool ok, const char* rest) {
  cout << label << ": " << (ok ? "ok" : "fail") << "  rest=\"" << (rest ? rest : "") << "\"" << endl;
}

// --- Parser definitions -------------------------------------------------------

// single char
using Hash   = ParseDef<char, Char<'#'>>;
using Bang   = ParseDef<char, Char<'!'>>;

// predicate-based
using ADigit = ParseDef<char, Digit>;
using AAlpha = ParseDef<char, Alpha>;
using ASpace = ParseDef<char, Space>;

// sequential: '#' then '!'
using HashBang = ParseDef<char, Char<'#'>, Char<'!'>>;

// sequential: digit then alpha
using DigitAlpha = ParseDef<char, Digit, Alpha>;

// -----------------------------------------------------------------------------

void run() {
  auto r1 = Hash::run("#hello");
  report("Hash   \"#hello\"", r1.ok, r1.rest);

  auto r2 = Hash::run(".hello");
  report("Hash   \".hello\"", r2.ok, r2.rest);

  auto r3 = ADigit::run("3abc");
  report("Digit  \"3abc\"  ", r3.ok, r3.rest);

  auto r4 = AAlpha::run("3abc");
  report("Alpha  \"3abc\"  ", r4.ok, r4.rest);

  auto r5 = HashBang::run("#!ok");
  report("HashBang \"#!ok\"", r5.ok, r5.rest);

  auto r6 = HashBang::run("#.ok");
  report("HashBang \"#.ok\"", r6.ok, r6.rest);

  auto r7 = DigitAlpha::run("3a!!");
  report("DigitAlpha \"3a!!\"", r7.ok, r7.rest);
}

#ifdef ARDUINO
  void setup() {
    Serial.begin(115200);
    while (!Serial);
    run();
  }
  void loop() {}
#else
  int main() {
    run();
    return 0;
  }
#endif
