#include <cctype>
#include <cstddef>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

//===----------------------------------------------------------------------===//
// Lexer
//===----------------------------------------------------------------------===//

// return its ASCII value([0-255]) if the character is unknown
enum Token {
  // end of file
  tok_eof = -1,

  // commands
  tok_def = -2,
  tok_extern = -3,

  // primary
  tok_identifier = -4,
  tok_number = -5,
};

static std::string IdentifierStr; // Fill in if tok_identifier
static double NumVal;             // Fill in if tok_number

static int get_tok() {
  static int LastChar = ' ';

  // skip whitespace
  while (isspace(LastChar)) {
    LastChar = getchar();
  }

  // for identifiers of keyword "def", "extern"
  if (isalpha(LastChar)) {
    IdentifierStr = LastChar;
    while (isalnum((LastChar = getchar()))) {
      IdentifierStr += LastChar;
    }

    if (IdentifierStr == "def") {
      return tok_def;
    }
    if (IdentifierStr == "extern") {
      return tok_extern;
    }

    return tok_identifier;
  }

  // for numbers, cannot handle inputs like "1.23.45.67"
  if (isdigit(LastChar) || LastChar == '.') {
    std::string NumStr;
    do {
      NumStr += LastChar;
      LastChar = getchar();
    } while (isdigit(LastChar) || LastChar == '.');

    NumVal = strtod(NumStr.c_str(), 0);
    return tok_number;
  }

  // for comments
  if (LastChar == '#') {
    do {
      LastChar = getchar();
    } while (LastChar != EOF && LastChar != '\n' && LastChar != '\r');

    if (LastChar != EOF) {
      return get_tok(); // read the next unit
    }
  }

  // if dont match, it is either operators like '+' or EOF
  if (LastChar == EOF) {
    return tok_eof;
  }

  int ThisChar = LastChar;
  LastChar = getchar();
  return ThisChar;
}

//===----------------------------------------------------------------------===//
// Abstract Syntax Tree
//===----------------------------------------------------------------------===//
namespace {

// 所有表达式节点的基类
class ExprAST {
public:
  virtual ~ExprAST() = default;
};

// 数字字面值（如123.0）的表达式类
class NumberExprAST : public ExprAST {
  double Val;

public:
  NumberExprAST(double Val) : Val(Val) {}
};

// 用于表示变量的表达式类
class VariableExprAST : public ExprAST {
  std::string Name;

public:
  VariableExprAST(const std::string &Name) : Name(Name) {}
};

// 用于表示二元操作符的类
class BinaryExprAST : public ExprAST {
  char Op;
  std::unique_ptr<ExprAST> LHS, RHS;

public:
  BinaryExprAST(char Op, std::unique_ptr<ExprAST> LHS,
                std::unique_ptr<ExprAST> RHS)
      : Op(Op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}
};

// 用于表示函数调用的类
class CallExprAST : public ExprAST {
  std::string Callee;
  std::vector<std::unique_ptr<ExprAST>> Args;

public:
  CallExprAST(const std::string &Callee,
              std::vector<std::unique_ptr<ExprAST>> Args)
      : Callee(Callee), Args(std::move(Args)) {}
};

// 表示函数的"prototype"，即函数的名称，函数变量的名称与数量
class PrototypeAST {
  std::string Name;
  std::vector<std::string> Args;

public:
  PrototypeAST(std::string Name, std::vector<std::string> Args)
      : Name(Name), Args(Args) {}

  const std::string &getName() const { return Name; }
};

// 表示函数的定义
class FunctionAST {
  std::unique_ptr<PrototypeAST> Proto;
  std::unique_ptr<ExprAST> Body; // TODO: 为什么一个ExprAST就可以表示body？

public:
  FunctionAST(std::unique_ptr<PrototypeAST> Proto,
              std::unique_ptr<ExprAST> Body)
      : Proto(std::move(Proto)), Body(std::move(Body)) {}
};
} // namespace

//===----------------------------------------------------------------------===//
// Parser
//===----------------------------------------------------------------------===//

static int CurTok;
static int getNextToken() { return CurTok = get_tok(); }

// 保存已定义的二元运算符的优先级
static std::map<char, int> BinopPrecedence;

std::unique_ptr<ExprAST> LogError(const char *Str) {
  fprintf(stderr, "Error: %s\n", Str);
  return nullptr;
}

std::unique_ptr<PrototypeAST> LogErrorP(const char *Str) {
  LogError(Str);
  return nullptr;
}

static std::unique_ptr<ExprAST> ParseExpression();

// numberexpr ::= number
// 当前token为tok_number时，新建一个NumberExprAST节点并返回
static std::unique_ptr<ExprAST> ParseNumberExpr() {
  auto Result = std::make_unique<NumberExprAST>(NumVal);
  getNextToken();
  return std::move(Result);
}

// parenexpr ::= ( expression )
// 处理括号。括号本身不会新建AST节点
static std::unique_ptr<ExprAST> ParseParenExpr() {
  getNextToken(); // eat '('
  auto V = ParseExpression();
  if (!V) {
    return nullptr;
  }
  // 这里展示了如何使用LogError
  if (CurTok != ')') {
    return LogError("expected ')'");
  }
  getNextToken(); // eat ')'

  return V;
}

// identifierexpr
//    ::= identifier
//    ::= identifier ( expression* )
// 处理变量引用（variable reference）与函数调用
static std::unique_ptr<ExprAST> ParseIdentifierExpr() {
  std::string IdName = IdentifierStr;
  getNextToken();

  // 普通的变量引用
  if (CurTok != '(') {
    return std::make_unique<VariableExprAST>(IdName);
  }

  // 函数调用
  getNextToken();
  std::vector<std::unique_ptr<ExprAST>> Args;
  if (CurTok != ')') {
    while (true) {
      if (auto Arg = ParseExpression()) {
        Args.push_back(std::move(Arg));
      } else {
        return nullptr;
      }

      if (CurTok == ')') {
        break;
      }

      if (CurTok != ',') {
        return LogError("expected ')' or ',' in argument list");
      }
      getNextToken(); // Eat ','
    }
  }

  getNextToken(); // Eat ')'

  return std::make_unique<CallExprAST>(IdName, std::move(Args));
}

// primary expression
//   ::= identifier expression
//   ::= number expression
//   ::= paren expression
static std::unique_ptr<ExprAST> ParsePrimary() {
  switch (CurTok) {
  case tok_identifier:
    return ParseIdentifierExpr();
  case tok_number:
    return ParseNumberExpr();
  case '(':
    return ParseParenExpr();
  default:
    return LogError("unknown token when expecting an expression");
  }
}

static int GetTokPrecedence() {
  if (!isascii(CurTok)) {
    return -1;
  }

  // 确保操作符已经声明在了map中
  int TokPrec = -1;
  if (BinopPrecedence.find(CurTok) != BinopPrecedence.end()) {
    TokPrec = BinopPrecedence[CurTok];
  }

  return TokPrec;
}

// binoprhs
//   ::= ()'+' primary)*
// Any sequence of pairs whose operators are all higher precedence
// than “+” should be parsed together and returned as “RHS”
static std::unique_ptr<ExprAST> ParseBinOpRHS(int ExprPrec,
                                              std::unique_ptr<ExprAST> LHS) {
  while (true) {
    int TokPrec = GetTokPrecedence();
    if (TokPrec < ExprPrec) {
      return LHS;
    }

    int BinOp = CurTok;
    getNextToken(); // eat the binary operator

    auto RHS = ParsePrimary();
    if (!RHS) {
      return nullptr;
    }

    int NextPrec = GetTokPrecedence();

    // 如果下一个运算符的优先级更高，需要优先处理
    if (TokPrec < NextPrec) {
      RHS = ParseBinOpRHS(TokPrec + 1, std::move(RHS)); // why +1?
      if (!RHS) {
        return nullptr;
      }
    }
    // TokPrec + 1的原因：
    // 在诸如 "a + b * c + d * e"的情况，如果不 + 1，则在解析完b * c之后，
    // 还会继续将后面的内容添加到RHS中

    LHS =
        std::make_unique<BinaryExprAST>(BinOp, std::move(LHS), std::move(RHS));
  }
}

// operator precedence parsing：将可能有具有二义性的操作符的表达式分解为多个部分
// 例如，对于表达式"a+b+(c+d)*e*f+g"，首先解析primary expr "a"，
// 之后它将看到多个pair：[+, b] [+, (c+d)] [*, e] [*, f] and [+, g]
// expression
//   ::=primary binoprhs
// binoprhs是一个pair [binary operator, primary expression]
static std::unique_ptr<ExprAST> ParseExpression() {
  auto LHS = ParsePrimary();
  if (!LHS) {
    return nullptr;
  }

  return ParseBinOpRHS(0, std::move(LHS));
}

// prototype
//   ::= id ( id * )
static std::unique_ptr<PrototypeAST> ParsePrototype() {
  if (CurTok != tok_identifier) {
    return LogErrorP("Expected function name in prototype");
  }

  std::string FuncName = IdentifierStr;
  getNextToken();

  if (CurTok != '(') {
    return LogErrorP("Expected '(' in function prototype");
  }

  std::vector<std::string> ArgNames;
  while (getNextToken() == tok_identifier) {
    ArgNames.push_back(IdentifierStr);
  }

  if (CurTok != ')') {
    return LogErrorP("expected ')'");
  }

  getNextToken(); // eat ')'

  return std::make_unique<PrototypeAST>(FuncName, std::move(ArgNames));
}

// function definition ::= 'def' prototype expression
// TODO: 目前只能parse函数只有一行的情况
static std::unique_ptr<FunctionAST> ParseDefinition() {
  getNextToken(); // eat 'def'

  auto Proto = ParsePrototype();
  if (!Proto) {
    return nullptr;
  }

  if (auto E = ParseExpression()) {
    return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
  }
  return nullptr;
}

// external ::= 'def' prototype
static std::unique_ptr<PrototypeAST> ParseExtern() {
  getNextToken();
  return ParsePrototype();
}

// toplevelexpr ::= expression
static std::unique_ptr<FunctionAST> ParseTopLevelExpr() {
  if (auto E = ParseExpression()) {
    // Make an anonymous proto
    auto Proto = std::make_unique<PrototypeAST>("__anon_expr",
                                                std::vector<std::string>());
    return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
  }

  return nullptr;
}

//===----------------------------------------------------------------------===//
// Top-Level parsing
//===----------------------------------------------------------------------===//

static void HandleDefinition() {
  if (ParseDefinition()) {
    fprintf(stderr, "Parsed a function definition.\n");
  } else {
    // Skip token for error recovery.
    getNextToken();
  }
}

static void HandleExtern() {
  if (ParseExtern()) {
    fprintf(stderr, "Parsed an extern\n");
  } else {
    // Skip token for error recovery.
    getNextToken();
  }
}

static void HandleTopLevelExpression() {
  // Evaluate a top-level expression into an anonymous function.
  if (ParseTopLevelExpr()) {
    fprintf(stderr, "Parsed a top-level expr\n");
  } else {
    // Skip token for error recovery.
    getNextToken();
  }
}

// top ::= definition | external | expression | ';'
// top-level expression：函数体外的表达式
static void MainLoop() {
  while (true) {
    fprintf(stderr, "ready> ");
    switch (CurTok) {
    case tok_eof:
      return;
    case ';':
      getNextToken();
      break;
    case tok_def:
      HandleDefinition();
      break;
    case tok_extern:
      HandleExtern();
      break;
    default:
      HandleTopLevelExpression();
      break;
    }
  }
}

int main() {
  // 声明支持的运算符以及优先级
  BinopPrecedence['<'] = 10;
  BinopPrecedence['-'] = 20;
  BinopPrecedence['+'] = 20;
  BinopPrecedence['*'] = 40; // 最高优先级

  // prepare for the first token.
  fprintf(stderr, "ready> ");
  getNextToken();

  MainLoop();

  return 0;
}