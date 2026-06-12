// OneParse EBNF parser — follows paco-ebnf (github.com/neu-rah/paco-ebnf)
//
// Parses grammar text in the form:
//   NCName ::= NameStartChar (NameChar)*
//   Expr   ::= Term (('+' | '-') Term)*
//
// AST types follow paco-ebnf naming: Grammar, Production, Choice, Item, Primary
//
// Recursion: Primary can contain a Group which holds a Choice.
// Template cycle is broken via Defer<Choice, parseChoice> (library combinator)
// and a static arena for Group Choice nodes.

#ifdef ARDUINO
  #include <Arduino.h>
#endif

#include <iostream>
using namespace std;

#include <oneParse/oneParse.h>
using namespace oneParse;

// ─── AST (paco-ebnf naming) ───────────────────────────────────────────────────

enum class Quant : uint8_t { None=0, Opt='?', Star='*', Plus='+' };

struct Choice; // forward — Primary stores Choice* for groups

struct Primary {
  enum class Kind : uint8_t { None, Name, Str, CharCode, CharClass, Group } kind = Kind::None;
  Arr<char, 32> text{};   // for Name / Str / CharCode / CharClass
  Choice*       group = nullptr; // for Group — points into gPool arena
};

struct Item {
  Primary prim{};
  Quant   quant = Quant::None;
};

using Sequence = Arr<Item, 8>;   // items in one sequential alternative

struct Choice {
  Arr<Sequence, 6> alts;         // alternatives separated by '|'
};

struct Production {
  Arr<char, 32> name{};
  Choice        rhs{};
};

using Grammar = Arr<Production, 16>;

// ─── Arena for recursive Group Choice nodes ───────────────────────────────────

static constexpr size_t kMaxGroups = 32;
static Choice gPool[kMaxGroups];
static size_t gPoolN = 0;

static Choice* allocGroup() { return gPoolN < kMaxGroups ? &gPool[gPoolN++] : nullptr; }
static void    resetPool()  { gPoolN = 0; }

// ─── Leaf parsers (template-based, no recursion) ─────────────────────────────

// NCName: starts with letter or '_', continues with letter/digit/'-'/'.'
constexpr bool isNameStart(char c) {
  return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_';
}
constexpr bool isNameChar(char c) {
  return isNameStart(c)||(c>='0'&&c<='9')||c=='-'||c=='.';
}

struct NCNameCollect {
  template<typename O>
  struct Part : O {
    using Base = O;
    using Base::Base;
    static auto run(Src src) -> typename Base::Result {
      if (!src || !isNameStart(*src)) return {false,{},src};
      Arr<char,32> arr{};
      while (src && *src && isNameChar(*src)) {
        if (!arr.push(*src)) return {false,{},src}; // overflow
        ++src;
      }
      auto r = Base::run(src);
      if (r.ok) r.val = arr;
      return r;
    }
  };
};
using NCNameP = ParseDef<Arr<char,32>, NCNameCollect>;

// StringLiteral: 'content' or "content"
using SQBody = ParseDef<Arr<char,32>, ManyN<ParseDef<char, Not<Char<'\''>>>, 32>>;
using DQBody = ParseDef<Arr<char,32>, ManyN<ParseDef<char, Not<Char<'"'>>>,  32>>;
using StringLiteralP = ParseDef<Arr<char,32>,
    Or<Between<Char<'\''>, SQBody, Char<'\''>>,
       Between<Char<'"'>,  DQBody, Char<'"'>>>>;

// CharCode: #xHH... (hex char reference)
constexpr bool isHexD(char c) {
  return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');
}
constexpr const char kHashX[] = "#x";
using CharCodeP = ParseDef<Arr<char,32>,
    Skip<Str<kHashX>>,
    SomeN<ParseDef<char,Satisfy<isHexD>>,32>>;

// CharClass: [...] content captured verbatim (e.g. "0-9", "a-zA-Z_")
using CharClassBody = ParseDef<Arr<char,32>, ManyN<ParseDef<char, Not<Char<']'>>>, 32>>;
using CharClassP    = ParseDef<Arr<char,32>, Between<Char<'['>, CharClassBody, Char<']'>>>;

// Whitespace + block comments (/* ... */)
constexpr const char kCOpen[]  = "/*";
constexpr const char kCClose[] = "*/";
using CommentSkip = Skip<Str<kCOpen>, ManyTill<Any, Str<kCClose>>, Str<kCClose>>;
using WsP         = ParseDef<char, Many<Or<Space, CommentSkip>>>;

static Src skipWs(Src src) { return WsP::run(src).rest; } // Many always ok

// Production separator
constexpr const char kDef[] = "::=";
using DefP = ParseDef<char, Str<kDef>>;

// ─── Recursive parsers ────────────────────────────────────────────────────────
// parseChoice is forward-declared so parsePrimary can use it for '(' Choice ')'
// Defer<Choice, parseChoice> makes this usable inside any ParseDef chain too.

static Res<Choice> parseChoice(Src src); // forward

static Quant parseQuant(Src& src) {
  if (src && (*src=='?'||*src=='*'||*src=='+')) return (Quant)*src++;
  return Quant::None;
}

static Res<Primary> parsePrimary(Src src) {
  Primary p{};
  src = skipWs(src);
  if (!src || !*src) return {false,{},src};

  // Group: '(' Choice ')' — uses Defer concept: parseChoice called at runtime
  if (*src == '(') {
    auto inner = parseChoice(src + 1);
    if (inner.ok) {
      Src ws = skipWs(inner.rest);
      if (ws && *ws == ')') {
        Choice* c = allocGroup();
        if (!c) return {false,{},src}; // pool exhausted
        *c = inner.val;
        p.kind  = Primary::Kind::Group;
        p.group = c;
        return {true, p, ws + 1};
      }
    }
    return {false,{},src}; // '(' without ')' — fail, don't try other alternatives
  }

  // CharCode: #xNN (before NCName — '#' not a valid name start anyway)
  { auto r = CharCodeP::run(src);
    if (r.ok) { p.kind=Primary::Kind::CharCode; p.text=r.val; return {true,p,r.rest}; } }

  // CharClass: [...]
  { auto r = CharClassP::run(src);
    if (r.ok) { p.kind=Primary::Kind::CharClass; p.text=r.val; return {true,p,r.rest}; } }

  // StringLiteral: '...' or "..."
  { auto r = StringLiteralP::run(src);
    if (r.ok) { p.kind=Primary::Kind::Str; p.text=r.val; return {true,p,r.rest}; } }

  // NCName — last, catches identifiers and rule references
  { auto r = NCNameP::run(src);
    if (r.ok) { p.kind=Primary::Kind::Name; p.text=r.val; return {true,p,r.rest}; } }

  return {false,{},src};
}

static Res<Item> parseItem(Src src) {
  auto pr = parsePrimary(src);
  if (!pr.ok) return {false,{},src};
  Item it;
  it.prim  = pr.val;
  it.quant = parseQuant(pr.rest);
  return {true, it, pr.rest};
}

static Res<Sequence> parseSequence(Src src) {
  Sequence seq{};
  while (true) {
    Src ws = skipWs(src);
    // natural stop tokens
    if (!ws || !*ws || *ws=='|' || *ws==')') break;
    // production boundary: NCName followed by ::= is a new rule, not an item
    { auto n = NCNameP::run(ws);
      if (n.ok && DefP::run(skipWs(n.rest)).ok) break; }
    auto ir = parseItem(ws);
    if (!ir.ok) break;
    if (!seq.push(ir.val)) break; // overflow
    src = ir.rest;
  }
  return {true, seq, src};
}

static Res<Choice> parseChoice(Src src) {
  Choice ch{};
  auto seq = parseSequence(src);
  if (!ch.alts.push(seq.val)) return {false,{},src};
  src = seq.rest;
  while (src) {
    Src ws = skipWs(src);
    if (!ws || *ws != '|') break;
    ws++;
    auto seq2 = parseSequence(ws);
    if (!ch.alts.push(seq2.val)) break; // overflow (max 6 alts)
    src = seq2.rest;
  }
  return {true, ch, src};
}

static Res<Production> parseProduction(Src src) {
  src = skipWs(src);
  auto nameR = NCNameP::run(src);
  if (!nameR.ok) return {false,{},src};
  src = skipWs(nameR.rest);
  auto defR = DefP::run(src);
  if (!defR.ok) return {false,{},src};
  src = skipWs(defR.rest);
  auto choiceR = parseChoice(src);
  Production prod{};
  prod.name = nameR.val;
  prod.rhs  = choiceR.val;
  return {true, prod, choiceR.rest};
}

static Grammar parseGrammar(Src src) {
  Grammar g{};
  resetPool();
  while (src && *src) {
    src = skipWs(src);
    if (!src || !*src) break;
    auto pr = parseProduction(src);
    if (!pr.ok) break; // unrecognised token — stop
    if (!g.push(pr.val)) break; // grammar overflow
    src = pr.rest;
  }
  return g;
}

// ─── Printer ─────────────────────────────────────────────────────────────────

static void printArr(const Arr<char,32>& a) { for (char c : a) cout << c; }

static void printChoice(const Choice& ch); // forward

static void printPrimary(const Primary& p) {
  switch (p.kind) {
    case Primary::Kind::Name:      printArr(p.text); break;
    case Primary::Kind::Str:       cout << '\''; printArr(p.text); cout << '\''; break;
    case Primary::Kind::CharCode:  cout << "#x"; printArr(p.text); break;
    case Primary::Kind::CharClass: cout << '['; printArr(p.text); cout << ']'; break;
    case Primary::Kind::Group:
      cout << '(';
      if (p.group) printChoice(*p.group);
      cout << ')';
      break;
    default: break;
  }
}

static void printItem(const Item& it) {
  printPrimary(it.prim);
  if (it.quant != Quant::None) cout << (char)it.quant;
}

static void printSeq(const Sequence& seq) {
  for (size_t i = 0; i < seq.len; i++) {
    if (i) cout << ' ';
    printItem(seq.data[i]);
  }
}

static void printChoice(const Choice& ch) {
  for (size_t i = 0; i < ch.alts.len; i++) {
    if (i) cout << " | ";
    printSeq(ch.alts.data[i]);
  }
}

// ─── Test grammar ─────────────────────────────────────────────────────────────

static const char* kGrammar = R"(
/* Simple expression grammar — paco-ebnf inspired */
Digit  ::= [0-9]
Letter ::= [a-zA-Z_]
Ident  ::= Letter (Letter | Digit)*
Int    ::= Digit+
Expr   ::= Term (('+' | '-') Term)*
Term   ::= Factor (('*' | '/') Factor)*
Factor ::= Int | Ident | '(' Expr ')'
)";

void run() {
  auto g = parseGrammar(kGrammar);
  cout << "Grammar: " << g.len << " productions"
       << "  (groups in pool: " << gPoolN << ")\n\n";
  for (size_t i = 0; i < g.len; i++) {
    printArr(g.data[i].name);
    cout << "  ::=  ";
    printChoice(g.data[i].rhs);
    cout << "\n";
  }
}

// ─────────────────────────────────────────────────────────────────────────────

#ifdef ARDUINO
  void setup() { Serial.begin(115200); while (!Serial); run(); }
  void loop() {}
#else
  int main() { run(); return 0; }
#endif
