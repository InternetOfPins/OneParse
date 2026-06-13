// OneParse EBNF parser — follows paco-ebnf (github.com/neu-rah/paco-ebnf)
//
// Parses grammar text in the form:
//   NCName ::= NameStartChar (NameChar)*
//   Expr   ::= Term (('+' | '-') Term)*
//
// Recursion cycle: PrimaryP → ChoiceP → SequenceP → ItemP → PrimaryP
//
// Broken by: parseChoiceImpl() is forward-declared before PrimaryCollect is defined;
//            its body is defined after ChoiceP is complete (the Defer pattern).
//
// Leaf parsers and PrimaryCollect / SequenceCollect remain as custom structs
// because they contain complex branching / lookahead logic.
// ItemP, ChoiceP, ProductionP, GrammarP are pure combinators.

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
  Arr<Sequence, 6> alts;
  Choice() = default;
  explicit Choice(Arr<Sequence, 6> a) : alts(a) {}  // needed by As<Choice, AltArrP>
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
        if (!arr.push(*src)) return {false,{},src};
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

// CharCode: #xHH...
constexpr bool isHexD(char c) {
  return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');
}
constexpr const char kHashX[] = "#x";
using CharCodeP = ParseDef<Arr<char,32>,
    Skip<Str<kHashX>>,
    SomeN<ParseDef<char,Satisfy<isHexD>>,32>>;

// CharClass: [...] content captured verbatim
using CharClassBody = ParseDef<Arr<char,32>, ManyN<ParseDef<char, Not<Char<']'>>>, 32>>;
using CharClassP    = ParseDef<Arr<char,32>, Between<Char<'['>, CharClassBody, Char<']'>>>;

// Whitespace component and complete parser
constexpr const char kCOpen[]  = "/*";
constexpr const char kCClose[] = "*/";
using CommentSkip = Skip<Str<kCOpen>, ManyTill<Any, Str<kCClose>>, Str<kCClose>>;
using Ws          = Many<Or<Space, CommentSkip>>;   // component: zero-width ws skip
using WsP         = ParseDef<char, Ws>;             // complete parser for skipWs()

static Src skipWs(Src src) { return WsP::run(src).rest; }

// Production separator literal
constexpr const char kDef[] = "::=";
using DefP = ParseDef<char, Str<kDef>>;

// ─── Forward declaration — breaks PrimaryCollect → ChoiceP template cycle ────
//
// PrimaryCollect calls parseChoiceImpl at runtime (group case: '(' Choice ')').
// parseChoiceImpl is forward-declared here so PrimaryCollect can reference it.
// Its body is defined after ChoiceP is complete — this is the Defer pattern
// expressed at the function level rather than the combinator level.

static Res<Choice> parseChoiceImpl(Src src);

// ─── PrimaryCollect ───────────────────────────────────────────────────────────
// Custom struct: 5 distinct output kinds, pool allocation for Group.
// Calls parseChoiceImpl (forward-declared above) for the '(' Choice ')' case.

struct PrimaryCollect {
  template<typename O>
  struct Part : O {
    using Base = O;
    using Base::Base;
    static auto run(Src src) -> typename Base::Result {
      Primary p{};

      // CharCode before NCName — '#' is not a valid name start anyway
      { auto r = CharCodeP::run(src);
        if (r.ok) { p.kind=Primary::Kind::CharCode; p.text=r.val;
                    auto res=Base::run(r.rest); if(res.ok) res.val=p; return res; } }

      { auto r = CharClassP::run(src);
        if (r.ok) { p.kind=Primary::Kind::CharClass; p.text=r.val;
                    auto res=Base::run(r.rest); if(res.ok) res.val=p; return res; } }

      { auto r = StringLiteralP::run(src);
        if (r.ok) { p.kind=Primary::Kind::Str; p.text=r.val;
                    auto res=Base::run(r.rest); if(res.ok) res.val=p; return res; } }

      // Group: '(' Choice ')' — parseChoiceImpl is the deferred function
      if (src && *src == '(') {
        auto inner = parseChoiceImpl(src + 1);
        if (inner.ok) {
          Src ws = skipWs(inner.rest);
          if (ws && *ws == ')') {
            Choice* c = allocGroup();
            if (!c) return {false,{},src};
            *c = inner.val;
            p.kind = Primary::Kind::Group; p.group = c;
            auto res = Base::run(ws + 1); if(res.ok) res.val=p; return res;
          }
        }
        return {false,{},src};
      }

      { auto r = NCNameP::run(src);
        if (r.ok) { p.kind=Primary::Kind::Name; p.text=r.val;
                    auto res=Base::run(r.rest); if(res.ok) res.val=p; return res; } }

      return {false,{},src};
    }
  };
};
using PrimaryP = ParseDef<Primary, Skip<Ws>, PrimaryCollect>;

// ─── Item = Primary + optional quantifier ─────────────────────────────────────

static Item makeItem(Pair<Primary,char> p) {
  return {p.fst, p.snd ? (Quant)p.snd : Quant::None};
}
using QuantCharP = ParseDef<char, Opt<AnyOf<'?','*','+'>>>;
using ItemP = ParseDef<Item,
    To<Pair<Primary,char>, makeItem,
        Seq<PrimaryP, QuantCharP>>>;

// ─── Sequence: items until '|', ')', production boundary, or EOF ──────────────
// Custom struct: stop condition requires lookahead (production boundary: NCName ::=)

struct SequenceCollect {
  template<typename O>
  struct Part : O {
    using Base = O;
    using Base::Base;
    static auto run(Src src) -> typename Base::Result {
      Sequence seq{};
      while (true) {
        Src ws = skipWs(src);
        if (!ws || !*ws || *ws=='|' || *ws==')') break;
        // production boundary: NCName immediately followed by ::=
        { auto n = NCNameP::run(ws);
          if (n.ok && DefP::run(skipWs(n.rest)).ok) break; }
        auto ir = ItemP::run(src);
        if (!ir.ok) break;
        if (!seq.push(ir.val)) break;
        src = ir.rest;
      }
      auto r = Base::run(src);
      if (r.ok) r.val = seq;
      return r;
    }
  };
};
using SequenceP = ParseDef<Sequence, SequenceCollect>;

// ─── Choice = alternatives separated by '|' ──────────────────────────────────
//
// SepBy1<SequenceP, Sep, 6>: collect SequenceP-separated-by-'|' into Arr<Sequence,6>
// As<Choice, AltArrP>: construct Choice{arr} using the constructor added above

using AltArrP = ParseDef<Arr<Sequence,6>, SepBy1<SequenceP, Skip<Ws, Char<'|'>>, 6>>;
using ChoiceP = ParseDef<Choice, As<Choice, AltArrP>>;

// ─── Defer pattern: body defined here — ChoiceP is now complete ──────────────

static Res<Choice> parseChoiceImpl(Src src) { return ChoiceP::run(src); }

// ─── Production = NCName '::=' Choice ────────────────────────────────────────

static Production makeProduction(Pair<Arr<char,32>,Choice> p) {
  Production prod{}; prod.name=p.fst; prod.rhs=p.snd; return prod;
}
using WsNCNameP      = ParseDef<Arr<char,32>, Skip<Ws>, NCNameCollect>;
using ChoiceAfterDefP = ParseDef<Choice, Skip<Ws, Str<kDef>, Ws>, As<Choice, AltArrP>>;
using ProductionP    = ParseDef<Production,
    To<Pair<Arr<char,32>,Choice>, makeProduction,
        Seq<WsNCNameP, ChoiceAfterDefP>>>;

// ─── Grammar = many productions ──────────────────────────────────────────────

using GrammarP = ParseDef<Grammar, ManyN<ProductionP, 16>>;

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
  resetPool();
  auto g = GrammarP::run(kGrammar);
  cout << "Grammar: " << g.val.len << " productions"
       << "  (groups in pool: " << gPoolN << ")\n\n";
  for (size_t i = 0; i < g.val.len; i++) {
    printArr(g.val.data[i].name);
    cout << "  ::=  ";
    printChoice(g.val.data[i].rhs);
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
