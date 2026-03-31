#include "defs.h"
#include "data.h"
#include "decl.h"

// 表达式解析
// Copyright (c) 2019 Warren Toomey, GPL3

// 解析一个主因子并返回代表它的 AST 节点。
static struct ASTnode *primary(void) {
  struct ASTnode *n;

  // 对于 INTLIT token，为它创建一个叶子 AST 节点
  // 并扫描下一个 token。否则，对于任何其他 token 类型，
  // 这是一个语法错误。
  switch (Token.token) {
    case T_INTLIT:
      n = mkastleaf(A_INTLIT, Token.intvalue);
      scan(&Token);
      return (n);
    default:
      fprintf(stderr, "syntax error on line %d, token %d\n", Line,
              Token.token);
      exit(1);
  }
}


// 将二元运算符 token 转换为一个 AST 操作。
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
      fprintf(stderr, "syntax error on line %d, token %d\n", Line, tokentype);
      exit(1);
  }
}

// 每个 token 的运算符优先级
static int OpPrec[] = { 0, 10, 10, 20, 20, 0 };

// 检查我们是否有二元运算符并返回其优先级。
static int op_precedence(int tokentype) {
  int prec = OpPrec[tokentype];
  if (prec == 0) {
    fprintf(stderr, "syntax error on line %d, token %d\n", Line, tokentype);
    exit(1);
  }
  return (prec);
}

// 返回一个以二元运算符为根的 AST 树。
// 参数 ptp 是前一个 token 的优先级。
struct ASTnode *binexpr(int ptp) {
  struct ASTnode *left, *right;
  int tokentype;

  // 获取左边的整数字面量。
  // 同时获取下一个 token。
  left = primary();

  // 如果遇到分号，仅返回左节点
  tokentype = Token.token;
  if (tokentype == T_SEMI)
    return (left);

  // 当这个 token 的优先级
  // 高于前一个 token 优先级时
  while (op_precedence(tokentype) > ptp) {
    // 获取下一个整数字面量
    scan(&Token);

    // 递归调用 binexpr()，用我们 token 的
    // 优先级来构建一棵子树
    right = binexpr(OpPrec[tokentype]);

    // 将那棵子树与我们的连接起来。同时将 token
    // 转换为一个 AST 操作。
    left = mkastnode(arithop(tokentype), left, right, 0);

    // 更新当前 token 的详情。
    // 如果遇到分号，仅返回左节点
    tokentype = Token.token;
    if (tokentype == T_SEMI)
      return (left);
  }

  // 当优先级相同或更低时，返回我们拥有的树
  return (left);
}
