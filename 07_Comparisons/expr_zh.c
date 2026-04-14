#include "defs.h"
#include "data.h"
#include "decl.h"

// 表达式解析
// Copyright (c) 2019 Warren Toomey, GPL3

// 解析基本因子并返回表示它的抽象语法树节点
static struct ASTnode *primary(void) {
  struct ASTnode *n;
  int id;

  switch (Token.token) {
  case T_INTLIT:
    // 对于整数字面量词法单元，创建一个叶子抽象语法树节点
    n = mkastleaf(A_INTLIT, Token.intvalue);
    break;

  case T_IDENT:
    // 检查该标识符是否存在
    id = findglob(Text);
    if (id == -1)
      fatals("Unknown variable", Text);

    // 为该标识符创建一个叶子抽象语法树节点
    n = mkastleaf(A_IDENT, id);
    break;

  default:
    fatald("Syntax error, token", Token.token);
  }

  // 扫描下一个词法单元并返回叶子节点
  scan(&Token);
  return (n);
}


// 将二元运算符词法单元转换为抽象语法树操作
// 我们依赖于词法单元到抽象语法树操作的1:1映射
static int arithop(int tokentype) {
  if (tokentype > T_EOF && tokentype < T_INTLIT)
    return(tokentype);
  fatald("Syntax error, token", tokentype);
}

// 每个词法单元的运算符优先级
// 必须与defs.h中的词法单元顺序相匹配
static int OpPrec[] = {
  0, 10, 10,			// T_EOF, T_PLUS, T_MINUS
  20, 20,			// T_STAR, T_SLASH
  30, 30,			// T_EQ, T_NE
  40, 40, 40, 40		// T_LT, T_GT, T_LE, T_GE
};

// 检查我们是否有二元运算符并返回其优先级
static int op_precedence(int tokentype) {
  int prec = OpPrec[tokentype];
  if (prec == 0)
    fatald("Syntax error, token", tokentype);
  return (prec);
}

// 返回一棵以二元运算符为根节点的抽象语法树
// 参数ptp是前一个词法单元的优先级
struct ASTnode *binexpr(int ptp) {
  struct ASTnode *left, *right;
  int tokentype;

  // 获取左侧的基本语法树
  // 同时获取下一个词法单元
  left = primary();

  // 如果遇到分号，仅返回左侧节点
  tokentype = Token.token;
  if (tokentype == T_SEMI)
    return (left);

  // 当此词法单元的优先级高于前一个词法单元的优先级时
  while (op_precedence(tokentype) > ptp) {
    // 获取下一个整数字面量
    scan(&Token);

    // 使用我们词法单元的优先级递归调用binexpr()来构建子树
    right = binexpr(OpPrec[tokentype]);

    // 将该子树与我们的子树连接起来
    // 同时将词法单元转换为抽象语法树操作
    left = mkastnode(arithop(tokentype), left, right, 0);

    // 更新当前词法单元的详细信息
    // 如果遇到分号，仅返回左侧节点
    tokentype = Token.token;
    if (tokentype == T_SEMI)
      return (left);
  }

  // 当优先级相同或更低时返回我们拥有的语法树
  return (left);
}