#include "defs.h"
#include "data.h"
#include "decl.h"

// 表达式解析
// Copyright (c) 2019 Warren Toomey, GPL3

// 解析一个基本因子并返回代表它的
// 抽象语法树节点。
static struct ASTnode *primary(void) {
  struct ASTnode *n;
  int id;

  switch (Token.token) {
  case T_INTLIT:
    // 对于 INTLIT 词法单元，创建一个叶子抽象语法树节点。
    n = mkastleaf(A_INTLIT, Token.intvalue);
    break;

  case T_IDENT:
    // 检查该标识符是否存在
    id = findglob(Text);
    if (id == -1)
      fatals("Unknown variable", Text);

    // 为其创建一个叶子抽象语法树节点
    n = mkastleaf(A_IDENT, id);
    break;

  default:
    fatald("Syntax error, token", Token.token);
  }

  // 扫描下一个词法单元并返回叶子节点
  scan(&Token);
  return (n);
}


// 将二元运算符词法单元转换为抽象语法树操作。
static int arithop(int tokentype) {
  switch (tokentype) {
  case T_PLUS:
    return (A_ADD);
  case T_MINUS:
    return (A_SUBTRACT);
  case T_STAR:
    return (A_MULTIPLY);
  case T_SLASH:
    return (A_DIVIDE);
  default:
    fatald("Syntax error, token", tokentype);
  }
}

// 每个词法单元的运算符优先级
static int OpPrec[] = { 0, 10, 10, 20, 20, 0 };

// 检查我们是否有二元运算符并返回其优先级。
static int op_precedence(int tokentype) {
  int prec = OpPrec[tokentype];
  if (prec == 0)
    fatald("Syntax error, token", tokentype);
  return (prec);
}

// 返回一个以二元运算符为根的抽象语法树。
// 参数 ptp 是前一个词法单元的优先级。
struct ASTnode *binexpr(int ptp) {
  struct ASTnode *left, *right;
  int tokentype;

  // 获取左侧的基本因子树。
  // 同时获取下一个词法单元。
  left = primary();

  // 如果遇到分号，则只返回左侧节点
  tokentype = Token.token;
  if (tokentype == T_SEMI)
    return (left);

  // 当该词法单元的优先级高于前一个词法单元的优先级时
  while (op_precedence(tokentype) > ptp) {
    // 读取下一个整数常量
    scan(&Token);

    // 使用当前词法单元的优先级递归调用 binexpr() 来构建子树
    right = binexpr(OpPrec[tokentype]);

    // 将该子树与当前树连接。同时将词法单元转换为抽象语法树操作。
    left = mkastnode(arithop(tokentype), left, right, 0);

    // 更新当前词法单元的详细信息。
    // 如果遇到分号，则只返回左侧节点
    tokentype = Token.token;
    if (tokentype == T_SEMI)
      return (left);
  }

  // 当优先级相同或更低时返回我们得到的树
  return (left);
}