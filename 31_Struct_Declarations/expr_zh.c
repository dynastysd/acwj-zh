#include "defs.h"
#include "data.h"
#include "decl.h"

// 表达式解析
// Copyright (c) 2019 Warren Toomey, GPL3

// expression_list: <null>
//        | expression
//        | expression ',' expression_list
//        ;

// 解析零个或多个逗号分隔的表达式列表，
// 返回由A_GLUE节点组成的AST，左子节点
// 是先前表达式的子树(或NULL)，右子节点
// 是下一个表达式。每个A_GLUE节点的size字段
// 设置为此时树中表达式的数量。如果没有
// 解析到表达式，则返回NULL
static struct ASTnode *expression_list(void) {
  struct ASTnode *tree = NULL;
  struct ASTnode *child = NULL;
  int exprcount = 0;

  // 循环直到最终的右括号
  while (Token.token != T_RPAREN) {

    // 解析下一个表达式并增加表达式计数
    child = binexpr(0);
    exprcount++;

    // 使用先前的树作为左子节点，
    // 新表达式作为右子节点构建A_GLUE AST节点。
    // 存储表达式计数
    tree = mkastnode(A_GLUE, P_NONE, tree, NULL, child, NULL, exprcount);

    // 此时必须是','或')'
    switch (Token.token) {
      case T_COMMA:
	scan(&Token);
	break;
      case T_RPAREN:
	break;
      default:
	fatald("表达式列表中意外的标记", Token.token);
    }
  }

  // 返回表达式树
  return (tree);
}

// 解析函数调用并返回其AST
static struct ASTnode *funccall(void) {
  struct ASTnode *tree;
  struct symtable *funcptr;

  // 检查标识符是否已定义为函数，
  // 然后为其创建一个叶子节点
  if ((funcptr = findsymbol(Text)) == NULL || funcptr->stype != S_FUNCTION) {
    fatals("未声明的函数", Text);
  }
  // 获取'('
  lparen();

  // 解析参数表达式列表
  tree = expression_list();

  // XXX 检查每个参数的类型与函数的原型

  // 构建函数调用AST节点。将
  // 函数的返回类型存储为该节点的类型。
  // 同时记录函数的符号id
  tree = mkastunary(A_FUNCCALL, funcptr->type, tree, funcptr, 0);

  // 获取')'
  rparen();
  return (tree);
}

// 解析数组索引并返回其AST树
static struct ASTnode *array_access(void) {
  struct ASTnode *left, *right;
  struct symtable *aryptr;

  // 检查标识符是否已定义为数组，
  // 然后创建一个指向其基址的叶子节点
  if ((aryptr = findsymbol(Text)) == NULL || aryptr->stype != S_ARRAY) {
    fatals("未声明的数组", Text);
  }
  left = mkastleaf(A_ADDR, aryptr->type, aryptr, 0);

  // 获取'['
  scan(&Token);

  // 解析后续表达式
  right = binexpr(0);

  // 获取']'
  match(T_RBRACKET, "]");

  // 确保这是int类型
  if (!inttype(right->type))
    fatal("数组索引不是整数类型");

  // 按元素类型的大小缩放索引
  right = modify_type(right, left->type, A_ADD);

  // 返回AST树，其中数组基址加上了偏移量，
  // 并解引用元素。此时仍然是左值
  left = mkastnode(A_ADD, aryptr->type, left, NULL, right, NULL, 0);
  left = mkastunary(A_DEREF, value_at(left->type), left, NULL, 0);
  return (left);
}

// 解析后缀表达式并返回
// 表示它的AST节点。标识符已在Text中
static struct ASTnode *postfix(void) {
  struct ASTnode *n;
  struct symtable *varptr;

  // 扫描下一个标记以查看是否有后缀表达式
  scan(&Token);

  // 函数调用
  if (Token.token == T_LPAREN)
    return (funccall());

  // 数组引用
  if (Token.token == T_LBRACKET)
    return (array_access());

  // 变量。检查变量是否存在
  if ((varptr = findsymbol(Text)) == NULL || varptr->stype != S_VARIABLE)
    fatals("未知的变量", Text);

  switch (Token.token) {
      // 后增:跳过标记
    case T_INC:
      scan(&Token);
      n = mkastleaf(A_POSTINC, varptr->type, varptr, 0);
      break;

      // 后减:跳过标记
    case T_DEC:
      scan(&Token);
      n = mkastleaf(A_POSTDEC, varptr->type, varptr, 0);
      break;

      // 只是一个变量引用
    default:
      n = mkastleaf(A_IDENT, varptr->type, varptr, 0);
  }
  return (n);
}

// 解析基本因子并返回
// 表示它的AST节点
static struct ASTnode *primary(void) {
  struct ASTnode *n;
  int id;

  switch (Token.token) {
    case T_INTLIT:
      // 对于INTLIT标记，为其创建一个叶子AST节点。
      // 如果在P_CHAR范围内则使其为P_CHAR类型
      if ((Token.intvalue) >= 0 && (Token.intvalue < 256))
	n = mkastleaf(A_INTLIT, P_CHAR, NULL, Token.intvalue);
      else
	n = mkastleaf(A_INTLIT, P_INT, NULL, Token.intvalue);
      break;

    case T_STRLIT:
      // 对于STRLIT标记，为其生成汇编代码。
      // 然后为其创建一个叶子AST节点。id是字符串的标签
      id = genglobstr(Text);
      n = mkastleaf(A_STRLIT, pointer_to(P_CHAR), NULL, id);
      break;

    case T_IDENT:
      return (postfix());

    case T_LPAREN:
      // 圆括号表达式的开始，跳过'('。
      // 扫描表达式和右括号
      scan(&Token);
      n = binexpr(0);
      rparen();
      return (n);

    default:
      fatald("期望基本表达式，得到标记", Token.token);
  }

  // 扫描下一个标记并返回叶子节点
  scan(&Token);
  return (n);
}

// 将二元操作符标记转换为二元AST操作
// 我们依赖于从标记到AST操作的一对一映射
static int binastop(int tokentype) {
  if (tokentype > T_EOF && tokentype <= T_SLASH)
    return (tokentype);
  fatald("语法错误，标记", tokentype);
  return (0);			// 保持 -Wall 不报错
}

// 如果标记是右结合的则返回true，
// 否则返回false
static int rightassoc(int tokentype) {
  if (tokentype == T_ASSIGN)
    return (1);
  return (0);
}

// 每个标记的操作符优先级。
// 必须与defs.h中的标记顺序一致
static int OpPrec[] = {
  0, 10, 20, 30,		// T_EOF, T_ASSIGN, T_LOGOR, T_LOGAND
  40, 50, 60,			// T_OR, T_XOR, T_AMPER
  70, 70,			// T_EQ, T_NE
  80, 80, 80, 80,		// T_LT, T_GT, T_LE, T_GE
  90, 90,			// T_LSHIFT, T_RSHIFT
  100, 100,			// T_PLUS, T_MINUS
  110, 110			// T_STAR, T_SLASH
};

// 检查我们是否有二元操作符并
// 返回其优先级
static int op_precedence(int tokentype) {
  int prec;
  if (tokentype > T_SLASH)
    fatald("标记在op_precedence中没有优先级:", tokentype);
  prec = OpPrec[tokentype];
  if (prec == 0)
    fatald("语法错误，标记", tokentype);
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
// 表示它的子树
struct ASTnode *prefix(void) {
  struct ASTnode *tree;
  switch (Token.token) {
    case T_AMPER:
      // 获取下一个标记并递归解析
      // 作为前缀表达式
      scan(&Token);
      tree = prefix();

      // 确保它是一个标识符
      if (tree->op != A_IDENT)
	fatal("&操作符后面必须是标识符");

      // 现在将操作符改为A_ADDR，类型改为
      // 原始类型的指针
      tree->op = A_ADDR;
      tree->type = pointer_to(tree->type);
      break;
    case T_STAR:
      // 获取下一个标记并递归解析
      // 作为前缀表达式
      scan(&Token);
      tree = prefix();

      // 目前，确保它是另一个解引用或
      // 标识符
      if (tree->op != A_IDENT && tree->op != A_DEREF)
	fatal("*操作符后面必须是标识符或*");

      // 在树前添加A_DEREF操作
      tree = mkastunary(A_DEREF, value_at(tree->type), tree, NULL, 0);
      break;
    case T_MINUS:
      // 获取下一个标记并递归解析
      // 作为前缀表达式
      scan(&Token);
      tree = prefix();

      // 在树前添加A_NEGATE操作并
      // 使子节点成为右值。因为char是无符号的，
      // 还要将其扩展为int使其为有符号
      tree->rvalue = 1;
      tree = modify_type(tree, P_INT, 0);
      tree = mkastunary(A_NEGATE, tree->type, tree, NULL, 0);
      break;
    case T_INVERT:
      // 获取下一个标记并递归解析
      // 作为前缀表达式
      scan(&Token);
      tree = prefix();

      // 在树前添加A_INVERT操作并
      // 使子节点成为右值
      tree->rvalue = 1;
      tree = mkastunary(A_INVERT, tree->type, tree, NULL, 0);
      break;
    case T_LOGNOT:
      // 获取下一个标记并递归解析
      // 作为前缀表达式
      scan(&Token);
      tree = prefix();

      // 在树前添加A_LOGNOT操作并
      // 使子节点成为右值
      tree->rvalue = 1;
      tree = mkastunary(A_LOGNOT, tree->type, tree, NULL, 0);
      break;
    case T_INC:
      // 获取下一个标记并递归解析
      // 作为前缀表达式
      scan(&Token);
      tree = prefix();

      // 目前，确保它是标识符
      if (tree->op != A_IDENT)
	fatal("++操作符后面必须是标识符");

      // 在树前添加A_PREINC操作
      tree = mkastunary(A_PREINC, tree->type, tree, NULL, 0);
      break;
    case T_DEC:
      // 获取下一个标记并递归解析
      // 作为前缀表达式
      scan(&Token);
      tree = prefix();

      // 目前，确保它是标识符
      if (tree->op != A_IDENT)
	fatal("--操作符后面必须是标识符");

      // 在树前添加A_PREDEC操作
      tree = mkastunary(A_PREDEC, tree->type, tree, NULL, 0);
      break;
    default:
      tree = primary();
  }
  return (tree);
}

// 返回以二元操作符为根的AST树。
// 参数ptp是前一个标记的优先级
struct ASTnode *binexpr(int ptp) {
  struct ASTnode *left, *right;
  struct ASTnode *ltemp, *rtemp;
  int ASTop;
  int tokentype;

  // 获取左边的树
  // 同时获取下一个标记
  left = prefix();

  // 如果遇到多个终止标记之一，只返回左节点
  tokentype = Token.token;
  if (tokentype == T_SEMI || tokentype == T_RPAREN ||
      tokentype == T_RBRACKET || tokentype == T_COMMA) {
    left->rvalue = 1;
    return (left);
  }
  // 当此标记的优先级大于前一个标记的优先级时，
  // 或者它是右结合且等于前一个标记的优先级时
  while ((op_precedence(tokentype) > ptp) ||
	 (rightassoc(tokentype) && op_precedence(tokentype) == ptp)) {
    // 获取下一个整数字面量
    scan(&Token);

    // 使用我们标记的优先级递归调用binexpr()
    // 来构建子树
    right = binexpr(OpPrec[tokentype]);

    // 确定要对子树执行的操作
    ASTop = binastop(tokentype);

    if (ASTop == A_ASSIGN) {
      // 赋值
      // 使右边的树成为右值
      right->rvalue = 1;

      // 确保右边的类型与左边匹配
      right = modify_type(right, left->type, 0);
      if (right == NULL)
	fatal("赋值中表达式类型不兼容");

      // 构建赋值AST树。但是，
      // 交换左右，这样右边表达式的代码
      // 将在左边表达式之前生成
      ltemp = left;
      left = right;
      right = ltemp;
    } else {

      // 我们不是在做赋值，所以两个树都应该是右值
      // 如果它们是左值树则转换为右值
      left->rvalue = 1;
      right->rvalue = 1;

      // 确保两种类型兼容，尝试
      // 修改每个树以匹配对方的类型
      ltemp = modify_type(left, right->type, ASTop);
      rtemp = modify_type(right, left->type, ASTop);
      if (ltemp == NULL && rtemp == NULL)
	fatal("二元表达式中类型不兼容");
      if (ltemp != NULL)
	left = ltemp;
      if (rtemp != NULL)
	right = rtemp;
    }

    // 将该子树与我们连接。同时将标记
    // 转换为AST操作
    left =
      mkastnode(binastop(tokentype), left->type, left, NULL, right, NULL, 0);

    // 更新当前标记的详细信息
    // 如果遇到终止标记，只返回左节点
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
