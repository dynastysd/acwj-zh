#include "defs.h"
#include "data.h"
#include "decl.h"

// 表达式的解析
// Copyright (c) 2019 Warren Toomey, GPL3

// 解析具有单个表达式
// 参数的函数调用并返回其 AST
struct ASTnode *funccall(void) {
  struct ASTnode *tree;
  int id;

  // 检查标识符是否已定义，
  // 然后为其制作叶子节点。XXX 添加结构类型测试
  if ((id = findglob(Text)) == -1) {
    fatals("Undeclared function", Text);
  }
  // 获取 '('
  lparen();

  // 解析后续表达式
  tree = binexpr(0);

  // 构造函数调用 AST 节点。存储
  // 函数的返回类型作为此节点的类型。
  // 同时记录函数的符号 id
  tree = mkastunary(A_FUNCCALL, Gsym[id].type, tree, id);

  // 获取 ')'
  rparen();
  return (tree);
}

// 解析一个初等因子并返回
// 表示它的 AST 节点。
static struct ASTnode *primary(void) {
  struct ASTnode *n;
  int id;

  switch (Token.token) {
    case T_INTLIT:
      // 对于 INTLIT 词法单元，为其制作一个叶子 AST 节点。
      // 如果它在 P_CHAR 范围内则将其设为 P_CHAR
      if ((Token.intvalue) >= 0 && (Token.intvalue < 256))
	n = mkastleaf(A_INTLIT, P_CHAR, Token.intvalue);
      else
	n = mkastleaf(A_INTLIT, P_INT, Token.intvalue);
      break;

    case T_IDENT:
      // 这可能是变量或函数调用。
      // 扫描下一个词法单元来确定
      scan(&Token);

      // 如果是 '('，则是函数调用
      if (Token.token == T_LPAREN)
	return (funccall());

      // 不是函数调用，所以拒绝新词法单元
      reject_token(&Token);

      // 检查变量是否存在。XXX 添加结构类型测试
      id = findglob(Text);
      if (id == -1)
	fatals("Unknown variable", Text);

      // 为其制作一个叶子 AST 节点
      n = mkastleaf(A_IDENT, Gsym[id].type, id);
      break;

    default:
      fatals("Syntax error, token", Token.token);
  }

  // 扫描下一个词法单元并返回叶子节点
  scan(&Token);
  return (n);
}


// 将二元运算符词法单元转换为 AST 操作。
// 我们依赖于从词法单元到 AST 操作的 1:1 映射
static int arithop(int tokentype) {
  if (tokentype > T_EOF && tokentype < T_INTLIT)
    return (tokentype);
  fatals("Syntax error, token", tokentype);
  return (0);			// 保持 -Wall 愉快
}

// 每个词法单元的运算符优先级。必须
// 与 defs.h 中词法单元的顺序匹配
static int OpPrec[] = {
  0, 10, 10,			// T_EOF, T_PLUS, T_MINUS
  20, 20,			// T_STAR, T_SLASH
  30, 30,			// T_EQ, T_NE
  40, 40, 40, 40		// T_LT, T_GT, T_LE, T_GE
};

// 检查我们是否有二元运算符并
// 返回其优先级
static int op_precedence(int tokentype) {
  int prec = OpPrec[tokentype];
  if (prec == 0)
    fatals("Syntax error, token", tokentype);
  return (prec);
}

// prefix_expression: primary
//     | '*' prefix_expression
//     | '&' prefix_expression
//     ;

// 解析前缀表达式并返回
// 表示它的子树
struct ASTnode *prefix(void) {
  struct ASTnode *tree;
  switch (Token.token) {
    case T_AMPER:
      // 获取下一个词法单元并递归地
      // 将其解析为前缀表达式
      scan(&Token);
      tree = prefix();

      // 确保它是一个标识符
      if (tree->op != A_IDENT)
	fatal("& operator must be followed by an identifier");

      // 现在将运算符改为 A_ADDR，类型改为
      // 原始类型的指针类型
      tree->op = A_ADDR;
      tree->type = pointer_to(tree->type);
      break;
    case T_STAR:
      // 获取下一个词法单元并递归地
      // 将其解析为前缀表达式
      scan(&Token);
      tree = prefix();

      // 目前，确保它是另一个解引用或
      // 标识符
      if (tree->op != A_IDENT && tree->op != A_DEREF)
	fatal("* operator must be followed by an identifier or *");

      // 在树前添加一个 A_DEREF 操作
      tree = mkastunary(A_DEREF, value_at(tree->type), tree, 0);
      break;
    default:
      tree = primary();
  }
  return (tree);
}

// 返回一个以二元运算符为根的 AST 树。
// 参数 ptp 是前一个词法单元的优先级。
struct ASTnode *binexpr(int ptp) {
  struct ASTnode *left, *right;
  int lefttype, righttype;
  int tokentype;

  // 获取左边的树。
  // 同时获取下一个词法单元。
  left = prefix();

  // 如果遇到分号或 ')'，则仅返回左节点
  tokentype = Token.token;
  if (tokentype == T_SEMI || tokentype == T_RPAREN)
    return (left);

  // 当此词法单元的优先级
  // 大于前一个词法单元的优先级时
  while (op_precedence(tokentype) > ptp) {
    // 读取下一个整数字面量
    scan(&Token);

    // 递归调用 binexpr() 并使用
    // 我们词法单元的优先级来构建子树
    right = binexpr(OpPrec[tokentype]);

    // 确保两种类型兼容
    lefttype = left->type;
    righttype = right->type;
    if (!type_compatible(&lefttype, &righttype, 0))
      fatal("Incompatible types");

    // 如有需要，加宽任一侧。类型变量现在是 A_WIDEN
    if (lefttype)
      left = mkastunary(lefttype, right->type, left, 0);
    if (righttype)
      right = mkastunary(righttype, left->type, right, 0);

    // 将该子树与我们当前的树连接起来。同时
    // 将词法单元转换为 AST 操作。
    left = mkastnode(arithop(tokentype), left->type, left, NULL, right, 0);

    // 更新当前词法单元的详细信息。
    // 如果遇到分号或 ')'，则仅返回左节点
    tokentype = Token.token;
    if (tokentype == T_SEMI || tokentype == T_RPAREN)
      return (left);
  }

  // 当优先级相同或更低时，
  // 返回我们拥有的树
  return (left);
}