#include "defs.h"
#include "data.h"
#include "decl.h"

// 表达式解析
// Copyright (c) 2019 Warren Toomey, GPL3

// expression_list: <null>
//        | expression
//        | expression ',' expression_list
//        ;

// 解析零个或多个逗号分隔的表达式，返回一个由
// A_GLUE 节点组成的 AST，左子节点是之前表达式的
// 子树（或 NULL），右子节点是下一个表达式。每个 A_GLUE
// 节点的 size 字段被设置为此时树中表达式的数量。如果没有
// 解析表达式，则返回 NULL
static struct ASTnode *expression_list(void) {
  struct ASTnode *tree = NULL;
  struct ASTnode *child = NULL;
  int exprcount = 0;

  // 循环直到遇到最终的右括号
  while (Token.token != T_RPAREN) {

    // 解析下一个表达式并增加表达式计数
    child = binexpr(0);
    exprcount++;

    // 使用之前的树作为左子节点，新表达式作为右子节点
    // 构建一个 A_GLUE AST 节点。存储表达式计数。
    tree = mkastnode(A_GLUE, P_NONE, tree, NULL, child, exprcount);

    // 此时必须是 ',' 或 ')'
    switch (Token.token) {
    case T_COMMA:
      scan(&Token);
      break;
    case T_RPAREN:
      break;
    default:
      fatald("Unexpected token in expression list", Token.token);
    }
  }

  // 返回表达式树
  return (tree);
}

// 解析函数调用并返回其 AST
static struct ASTnode *funccall(void) {
  struct ASTnode *tree;
  int id;

  // 检查标识符是否已定义为函数，
  // 然后为其创建一个叶子节点。
  if ((id = findsymbol(Text)) == -1 || Symtable[id].stype != S_FUNCTION) {
    fatals("Undeclared function", Text);
  }
  // 获取 '('
  lparen();

  // 解析参数表达式列表
  tree = expression_list();

  // XXX 检查每个参数的类型与函数原型的匹配

  // 构建函数调用 AST 节点。存储
  // 函数的返回类型作为此节点的类型。
  // 同时记录函数的符号 id
  tree = mkastunary(A_FUNCCALL, Symtable[id].type, tree, id);

  // 获取 ')'
  rparen();
  return (tree);
}

// 解析数组索引并返回其 AST 树
static struct ASTnode *array_access(void) {
  struct ASTnode *left, *right;
  int id;

  // 检查标识符是否已定义为数组，
  // 然后创建一个指向基址的叶子节点。
  if ((id = findsymbol(Text)) == -1 || Symtable[id].stype != S_ARRAY) {
    fatals("Undeclared array", Text);
  }
  left = mkastleaf(A_ADDR, Symtable[id].type, id);

  // 获取 '['
  scan(&Token);

  // 解析后续表达式
  right = binexpr(0);

  // 获取 ']'
  match(T_RBRACKET, "]");

  // 确保是 int 类型
  if (!inttype(right->type))
    fatal("Array index is not of integer type");

  // 按元素类型的大小缩放索引
  right = modify_type(right, left->type, A_ADD);

  // 返回一个 AST 树，其中数组的基址加上了偏移量，
  // 并解引用元素。此时仍是左值。
  left = mkastnode(A_ADD, Symtable[id].type, left, NULL, right, 0);
  left = mkastunary(A_DEREF, value_at(left->type), left, 0);
  return (left);
}

// 解析后缀表达式并返回
// 表示它的 AST 节点。标识符已在 Text 中。
static struct ASTnode *postfix(void) {
  struct ASTnode *n;
  int id;

  // 扫描下一个词法单元以查看是否有后缀表达式
  scan(&Token);

  // 函数调用
  if (Token.token == T_LPAREN)
    return (funccall());

  // 数组引用
  if (Token.token == T_LBRACKET)
    return (array_access());

  // 一个变量。检查变量是否存在。
  id = findsymbol(Text);
  if (id == -1 || Symtable[id].stype != S_VARIABLE)
    fatals("Unknown variable", Text);

  switch (Token.token) {
    // 后增量：跳过词法单元
  case T_INC:
    scan(&Token);
    n = mkastleaf(A_POSTINC, Symtable[id].type, id);
    break;

    // 后减量：跳过词法单元
  case T_DEC:
    scan(&Token);
    n = mkastleaf(A_POSTDEC, Symtable[id].type, id);
    break;

    // 只是一个变量引用
  default:
    n = mkastleaf(A_IDENT, Symtable[id].type, id);
  }
  return (n);
}

// 解析基本因子并返回
// 表示它的 AST 节点。
static struct ASTnode *primary(void) {
  struct ASTnode *n;
  int id;

  switch (Token.token) {
  case T_INTLIT:
    // 对于 INTLIT 词法单元，为其创建一个叶子 AST 节点。
    // 如果在 P_CHAR 范围内则使其为 P_CHAR 类型
    if ((Token.intvalue) >= 0 && (Token.intvalue < 256))
      n = mkastleaf(A_INTLIT, P_CHAR, Token.intvalue);
    else
      n = mkastleaf(A_INTLIT, P_INT, Token.intvalue);
    break;

  case T_STRLIT:
    // 对于 STRLIT 词法单元，为其生成汇编代码。
    // 然后为其创建一个叶子 AST 节点。id 是字符串的标签。
    id = genglobstr(Text);
    n = mkastleaf(A_STRLIT, P_CHARPTR, id);
    break;

  case T_IDENT:
    return (postfix());

  case T_LPAREN:
    // 带括号表达式的开始，跳过 '('。
    // 扫描表达式和右括号
    scan(&Token);
    n = binexpr(0);
    rparen();
    return (n);

  default:
    fatald("Expecting a primary expression, got token", Token.token);
  }

  // 扫描下一个词法单元并返回叶子节点
  scan(&Token);
  return (n);
}

// 将二元运算符词法单元转换为二元 AST 操作。
// 我们依赖于从词法单元到 AST 操作的 1:1 映射
static int binastop(int tokentype) {
  if (tokentype > T_EOF && tokentype <= T_SLASH)
    return (tokentype);
  fatald("Syntax error, token", tokentype);
  return (0);			// 保持 -Wall 高兴
}

// 如果词法单元是右结合则返回 true，
// 否则返回 false。
static int rightassoc(int tokentype) {
  if (tokentype == T_ASSIGN)
    return (1);
  return (0);
}

// 每个词法单元的运算符优先级。必须与 defs.h
// 中的词法单元顺序相匹配
static int OpPrec[] = {
  0, 10, 20, 30,		// T_EOF, T_ASSIGN, T_LOGOR, T_LOGAND
  40, 50, 60,			// T_OR, T_XOR, T_AMPER
  70, 70,			// T_EQ, T_NE
  80, 80, 80, 80,		// T_LT, T_GT, T_LE, T_GE
  90, 90,			// T_LSHIFT, T_RSHIFT
  100, 100,			// T_PLUS, T_MINUS
  110, 110			// T_STAR, T_SLASH
};

// 检查是否有二元运算符并返回其优先级。
static int op_precedence(int tokentype) {
  int prec;
  if (tokentype > T_SLASH)
    fatald("Token with no precedence in op_precedence:", tokentype);
  prec = OpPrec[tokentype];
  if (prec == 0)
    fatald("Syntax error, token", tokentype);
  return (prec);
}

// prefix_expression: primary
//     | '*'  prefix_expression
//     | '&'  prefix_expression
//     | '-'  prefix_expression
//     | '++' prefix_expression
//     | '--' prefix_expression
//     ;

// 解析前缀表达式并返回
// 表示它的子树。
struct ASTnode *prefix(void) {
  struct ASTnode *tree;
  switch (Token.token) {
  case T_AMPER:
    // 获取下一个词法单元并递归
    // 解析为前缀表达式
    scan(&Token);
    tree = prefix();

    // 确保它是一个标识符
    if (tree->op != A_IDENT)
      fatal("& operator must be followed by an identifier");

    // 现在将运算符改为 A_ADDR，类型改为
    // 指向原始类型的指针
    tree->op = A_ADDR;
    tree->type = pointer_to(tree->type);
    break;
  case T_STAR:
    // 获取下一个词法单元并递归
    // 解析为前缀表达式
    scan(&Token);
    tree = prefix();

    // 目前，确保它是另一个解引用或
    // 标识符
    if (tree->op != A_IDENT && tree->op != A_DEREF)
      fatal("* operator must be followed by an identifier or *");

    // 将 A_DEREF 操作添加到树的前面
    tree = mkastunary(A_DEREF, value_at(tree->type), tree, 0);
    break;
  case T_MINUS:
    // 获取下一个词法单元并递归
    // 解析为前缀表达式
    scan(&Token);
    tree = prefix();

    // 在树前添加 A_NEGATE 操作并
    // 使子节点成为右值。因为 char 是无符号的，
    // 还要将其扩展为 int 以便有符号
    tree->rvalue = 1;
    tree = modify_type(tree, P_INT, 0);
    tree = mkastunary(A_NEGATE, tree->type, tree, 0);
    break;
  case T_INVERT:
    // 获取下一个词法单元并递归
    // 解析为前缀表达式
    scan(&Token);
    tree = prefix();

    // 在树前添加 A_INVERT 操作并
    // 使子节点成为右值。
    tree->rvalue = 1;
    tree = mkastunary(A_INVERT, tree->type, tree, 0);
    break;
  case T_LOGNOT:
    // 获取下一个词法单元并递归
    // 解析为前缀表达式
    scan(&Token);
    tree = prefix();

    // 在树前添加 A_LOGNOT 操作并
    // 使子节点成为右值。
    tree->rvalue = 1;
    tree = mkastunary(A_LOGNOT, tree->type, tree, 0);
    break;
  case T_INC:
    // 获取下一个词法单元并递归
    // 解析为前缀表达式
    scan(&Token);
    tree = prefix();

    // 目前，确保它是一个标识符
    if (tree->op != A_IDENT)
      fatal("++ operator must be followed by an identifier");

    // 在树前添加 A_PREINC 操作
    tree = mkastunary(A_PREINC, tree->type, tree, 0);
    break;
  case T_DEC:
    // 获取下一个词法单元并递归
    // 解析为前缀表达式
    scan(&Token);
    tree = prefix();

    // 目前，确保它是一个标识符
    if (tree->op != A_IDENT)
      fatal("-- operator must be followed by an identifier");

    // 在树前添加 A_PREDEC 操作
    tree = mkastunary(A_PREDEC, tree->type, tree, 0);
    break;
  default:
    tree = primary();
  }
  return (tree);
}

// 返回以二元运算符为根的 AST 树。
// 参数 ptp 是前一个词法单元的优先级。
struct ASTnode *binexpr(int ptp) {
  struct ASTnode *left, *right;
  struct ASTnode *ltemp, *rtemp;
  int ASTop;
  int tokentype;

  // 获取左边的树。
  // 同时获取下一个词法单元。
  left = prefix();

  // 如果遇到终止词法单元之一，则只返回左节点
  tokentype = Token.token;
  if (tokentype == T_SEMI || tokentype == T_RPAREN ||
      tokentype == T_RBRACKET || tokentype == T_COMMA) {
    left->rvalue = 1;
    return (left);
  }
  // 当此词法单元的优先级高于
  // 前一个词法单元优先级时，或它是右结合且
  // 等于前一个词法单元的优先级时
  while ((op_precedence(tokentype) > ptp) ||
	 (rightassoc(tokentype) && op_precedence(tokentype) == ptp)) {
    // 获取下一个整数字面量
    scan(&Token);

    // 递归调用 binexpr() 并使用我们词法单元的
    // 优先级来构建子树
    right = binexpr(OpPrec[tokentype]);

    // 确定要对子树执行的操作
    ASTop = binastop(tokentype);

    if (ASTop == A_ASSIGN) {
      // 赋值
      // 使右树成为右值
      right->rvalue = 1;

      // 确保右边的类型与左边匹配
      right = modify_type(right, left->type, 0);
      if (right == NULL)
	fatal("Incompatible expression in assignment");

      // 创建一个赋值 AST 树。但是，
      // 交换左右，这样右表达式的代码
      // 将在左表达式之前生成
      ltemp = left;
      left = right;
      right = ltemp;
    } else {

      // 我们不是在做赋值，所以两个树都应该是右值
      // 如果它们是左值树则转换为右值
      left->rvalue = 1;
      right->rvalue = 1;

      // 通过尝试修改每个树以匹配对方的类型来
      // 确保两种类型兼容。
      ltemp = modify_type(left, right->type, ASTop);
      rtemp = modify_type(right, left->type, ASTop);
      if (ltemp == NULL && rtemp == NULL)
	fatal("Incompatible types in binary expression");
      if (ltemp != NULL)
	left = ltemp;
      if (rtemp != NULL)
	right = rtemp;
    }

    // 将子树与我们连接。同时将词法单元
    // 转换为 AST 操作。
    left = mkastnode(binastop(tokentype), left->type, left, NULL, right, 0);

    // 更新当前词法单元的详情。
    // 如果遇到终止词法单元，则只返回左节点
    tokentype = Token.token;
    if (tokentype == T_SEMI || tokentype == T_RPAREN ||
	tokentype == T_RBRACKET || tokentype == T_COMMA) {
      left->rvalue = 1;
      return (left);
    }
  }

  // 当优先级相同或更低时返回我们拥有的树
  left->rvalue = 1;
  return (left);
}