#include "defs.h"
#include "data.h"
#include "decl.h"

// 表达式解析
// Copyright (c) 2019 Warren Toomey, GPL3

// expression_list: <null>
//        | expression
//        | expression ',' expression_list
//        ;

// 解析零个或多个逗号分隔的表达式，
// 返回由 A_GLUE 节点组成的 AST，左子节点
// 是先前表达式的子树（或 NULL），右子节点
// 是下一个表达式。每个 A_GLUE 节点的 size 字段
// 设置为此时树中表达式的数量。如果没有
// 解析表达式，则返回 NULL
struct ASTnode *expression_list(int endtoken) {
  struct ASTnode *tree = NULL;
  struct ASTnode *child = NULL;
  int exprcount = 0;

  // 循环直到结束标记
  while (Token.token != endtoken) {

    // 解析下一个表达式并增加表达式计数
    child = binexpr(0);
    exprcount++;

    // 使用先前的树作为左子节点和新的表达式
    // 作为右子节点构建一个 A_GLUE AST 节点。存储表达式计数。
    tree =
      mkastnode(A_GLUE, P_NONE, NULL, tree, NULL, child, NULL, exprcount);

    // 当达到结束标记时停止
    if (Token.token == endtoken)
      break;

    // 此时必须有 ','
    match(T_COMMA, ",");
  }

  // 返回表达式树
  return (tree);
}

// 解析函数调用并返回其 AST
static struct ASTnode *funccall(void) {
  struct ASTnode *tree;
  struct symtable *funcptr;

  // 检查标识符是否已定义为函数，然后为其创建一个叶子节点。
  if ((funcptr = findsymbol(Text)) == NULL || funcptr->stype != S_FUNCTION) {
    fatals("Undeclared function", Text);
  }
  // 获取 '('
  lparen();

  // 解析参数表达式列表
  tree = expression_list(T_RPAREN);

  // XXX 检查每个参数相对于函数原型的类型

  // 构建函数调用 AST 节点。存储
  // 函数的返回类型作为此节点的类型。
  // 同时记录函数的符号 id
  tree =
    mkastunary(A_FUNCCALL, funcptr->type, funcptr->ctype, tree, funcptr, 0);

  // 获取 ')'
  rparen();
  return (tree);
}

// 解析数组索引并返回其 AST 树
static struct ASTnode *array_access(struct ASTnode *left) {
  struct ASTnode *right;

  // 检查子树是否是指针
  if (!ptrtype(left->type))
    fatal("Not an array or pointer");

  // 获取 '['
  scan(&Token);

  // 解析后续表达式
  right = binexpr(0);

  // 获取 ']'
  match(T_RBRACKET, "]");

  // 确保这是 int 类型
  if (!inttype(right->type))
    fatal("Array index is not of integer type");

  // 使左树成为 rvalue
  left->rvalue = 1;

  // 按元素类型的大小缩放索引
  right = modify_type(right, left->type, left->ctype, A_ADD);

  // 返回一个 AST 树，其中数组基址加上偏移量，
  // 并解引用元素。此时仍是 lvalue。
  left =
    mkastnode(A_ADD, left->type, left->ctype, left, NULL, right, NULL, 0);
  left =
    mkastunary(A_DEREF, value_at(left->type), left->ctype, left, NULL, 0);
  return (left);
}

// 解析 struct 或 union 的成员引用
// 并返回其 AST 树。如果 withpointer 为 true，
// 则通过指针访问成员。
static struct ASTnode *member_access(struct ASTnode *left, int withpointer) {
  struct ASTnode *right;
  struct symtable *typeptr;
  struct symtable *m;

  // 检查左 AST 树是否是指向 struct 或 union 的指针
  if (withpointer && left->type != pointer_to(P_STRUCT)
      && left->type != pointer_to(P_UNION))
    fatal("Expression is not a pointer to a struct/union");

  // 或者，检查左 AST 树是否是 struct 或 union。
  // 如果是，则将其从 A_IDENT 更改为 A_ADDR 以便
  // 我们获取基地址，而不是该地址处的值。
  if (!withpointer) {
    if (left->type == P_STRUCT || left->type == P_UNION)
      left->op = A_ADDR;
    else
      fatal("Expression is not a struct/union");
  }

  // 获取组合类型的详细信息
  typeptr = left->ctype;

  // 跳过 '.' 或 '->' 标记并获取成员的名称
  scan(&Token);
  ident();

  // 在类型中查找匹配的成员名称
  // 如果找不到则报错
  for (m = typeptr->member; m != NULL; m = m->next)
    if (!strcmp(m->name, Text))
      break;
  if (m == NULL)
    fatals("No member found in struct/union: ", Text);

  // 使左树成为 rvalue
  left->rvalue = 1;

  // 使用偏移量构建一个 A_INTLIT 节点
  right = mkastleaf(A_INTLIT, P_INT, NULL, NULL, m->st_posn);

  // 将成员的偏移量加到 struct/union 的基址上
  // 并解引用它。此时仍是 lvalue
  left =
    mkastnode(A_ADD, pointer_to(m->type), m->ctype, left, NULL, right, NULL,
	      0);
  left = mkastunary(A_DEREF, m->type, m->ctype, left, NULL, 0);
  return (left);
}

// 解析括号表达式并返回表示它的 AST 节点。
static struct ASTnode *paren_expression(void) {
  struct ASTnode *n;
  int type = 0;
  struct symtable *ctype = NULL;

  // 括号表达式的开始，跳过 '('。
  scan(&Token);

  // 如果之后的标记是类型标识符，这是强制转换表达式
  switch (Token.token) {
  case T_IDENT:
    // 我们需要查看标识符是否匹配 typedef。
    // 如果不是，则视为表达式。
    if (findtypedef(Text) == NULL) {
      n = binexpr(0);
      break;
    }
  case T_VOID:
  case T_CHAR:
  case T_INT:
  case T_LONG:
  case T_STRUCT:
  case T_UNION:
  case T_ENUM:
    // 获取括号内的类型
    type = parse_cast(&ctype);

    // 跳过 ')' 然后解析后续表达式
    rparen();

  default:
    n = binexpr(0);		// 扫描表达式
  }

  // 我们现在在 n 中至少有一个表达式，如果存在强制转换，
  // 并且在 type 中有非零类型。跳过 ')'（如果没有强制转换），
  // 否则，为强制转换创建一个一元 AST 节点
  if (type == 0)
    rparen();
  else
    n = mkastunary(A_CAST, type, ctype, n, NULL, 0);
  return (n);
}

// 解析一个因子并返回表示它的 AST 节点。
static struct ASTnode *primary(void) {
  struct ASTnode *n;
  struct symtable *enumptr;
  struct symtable *varptr;
  int id;
  int type = 0;
  int size, class;
  struct symtable *ctype;

  switch (Token.token) {
  case T_STATIC:
  case T_EXTERN:
    fatal("Compiler doesn't support static or extern local declarations");
  case T_SIZEOF:
    // 跳过 T_SIZEOF 并确保有左括号
    scan(&Token);
    if (Token.token != T_LPAREN)
      fatal("Left parenthesis expected after sizeof");
    scan(&Token);

    // 获取括号内的类型
    type = parse_stars(parse_type(&ctype, &class));

    // 获取类型的大小
    size = typesize(type, ctype);
    rparen();

    // 用大小创建一个整数叶子节点
    return (mkastleaf(A_INTLIT, P_INT, NULL, NULL, size));

  case T_INTLIT:
    // 对于 INTLIT 标记，为其创建一个叶子 AST 节点。
    // 如果在 P_CHAR 范围内，则使其为 P_CHAR
    if (Token.intvalue >= 0 && Token.intvalue < 256)
      n = mkastleaf(A_INTLIT, P_CHAR, NULL, NULL, Token.intvalue);
    else
      n = mkastleaf(A_INTLIT, P_INT, NULL, NULL, Token.intvalue);
    break;

  case T_STRLIT:
    // 对于 STRLIT 标记，为其生成汇编代码。
    id = genglobstr(Text, 0);

    // 对于连续的 STRLIT 标记，将其内容追加到此标记
    while (1) {
      scan(&Peektoken);
      if (Peektoken.token != T_STRLIT) break;
      genglobstr(Text, 1);
      scan(&Token);	// 正确跳过它
    }

    // 现在为其创建一个叶子 AST 节点。id 是字符串的标签。
    genglobstrend();
    n = mkastleaf(A_STRLIT, pointer_to(P_CHAR), NULL, NULL, id);
    break;

  case T_IDENT:
    // 如果标识符匹配枚举值，
    // 返回一个 A_INTLIT 节点
    if ((enumptr = findenumval(Text)) != NULL) {
      n = mkastleaf(A_INTLIT, P_INT, NULL, NULL, enumptr->st_posn);
      break;
    }
    // 查看此标识符是否作为符号存在。对于数组，设置 rvalue 为 1。
    if ((varptr = findsymbol(Text)) == NULL)
      fatals("Unknown variable or function", Text);
    switch (varptr->stype) {
    case S_VARIABLE:
      n = mkastleaf(A_IDENT, varptr->type, varptr->ctype, varptr, 0);
      break;
    case S_ARRAY:
      n = mkastleaf(A_ADDR, varptr->type, varptr->ctype, varptr, 0);
      n->rvalue = 1;
      break;
    case S_FUNCTION:
      // 函数调用，查看下一个标记是否是左括号
      scan(&Token);
      if (Token.token != T_LPAREN)
	fatals("Function name used without parentheses", Text);
      return (funccall());
    default:
      fatals("Identifier not a scalar or array variable", Text);
    }
    break;

  case T_LPAREN:
    return (paren_expression());

  default:
    fatals("Expecting a primary expression, got token", Token.tokstr);
  }

  // 扫描下一个标记并返回叶子节点
  scan(&Token);
  return (n);
}

// 解析后缀表达式并返回表示它的 AST 节点。
// 标识符已在 Text 中。
static struct ASTnode *postfix(void) {
  struct ASTnode *n;

  // 获取主表达式
  n = primary();

  // 循环直到没有更多的后缀运算符
  while (1) {
    switch (Token.token) {
    case T_LBRACKET:
      // 数组引用
      n = array_access(n);
      break;

    case T_DOT:
      // 访问 struct 或 union
      n = member_access(n, 0);
      break;

    case T_ARROW:
      // 通过指针访问 struct 或 union
      n = member_access(n, 1);
      break;

    case T_INC:
      // 后增：跳过标记
      if (n->rvalue == 1)
	fatal("Cannot ++ on rvalue");
      scan(&Token);

      // 不能做两次
      if (n->op == A_POSTINC || n->op == A_POSTDEC)
	fatal("Cannot ++ and/or -- more than once");

      // 更改 AST 操作
      n->op = A_POSTINC;
      break;

    case T_DEC:
      // 后减：跳过标记
      if (n->rvalue == 1)
	fatal("Cannot -- on rvalue");
      scan(&Token);

      // 不能做两次
      if (n->op == A_POSTINC || n->op == A_POSTDEC)
	fatal("Cannot ++ and/or -- more than once");

      // 更改 AST 操作
      n->op = A_POSTDEC;
      break;

    default:
      return (n);
    }
  }

  return (NULL);		// 保持 -Wall 愉快
}


// 将二元运算符标记转换为二元 AST 操作。
// 我们依赖于从标记到 AST 操作的 1:1 映射
static int binastop(int tokentype) {
  if (tokentype > T_EOF && tokentype <= T_SLASH)
    return (tokentype);
  fatals("Syntax error, token", Tstring[tokentype]);
  return (0);			// 保持 -Wall 愉快
}

// 如果标记是右结合则返回 true，否则返回 false。
static int rightassoc(int tokentype) {
  if (tokentype >= T_ASSIGN && tokentype <= T_ASSLASH)
    return (1);
  return (0);
}

// 每个标记的运算符优先级。必须
// 与 defs.h 中的标记顺序匹配
static int OpPrec[] = {
  0, 10, 10,			// T_EOF, T_ASSIGN, T_ASPLUS,
  10, 10, 10,			// T_ASMINUS, T_ASSTAR, T_ASSLASH,
  15,				// T_QUESTION,
  20, 30,			// T_LOGOR, T_LOGAND
  40, 50, 60,			// T_OR, T_XOR, T_AMPER 
  70, 70,			// T_EQ, T_NE
  80, 80, 80, 80,		// T_LT, T_GT, T_LE, T_GE
  90, 90,			// T_LSHIFT, T_RSHIFT
  100, 100,			// T_PLUS, T_MINUS
  110, 110			// T_STAR, T_SLASH
};

// 检查我们是否有二元运算符并返回其优先级。
static int op_precedence(int tokentype) {
  int prec;
  if (tokentype > T_SLASH)
    fatals("Token with no precedence in op_precedence:", Tstring[tokentype]);
  prec = OpPrec[tokentype];
  if (prec == 0)
    fatals("Syntax error, token", Tstring[tokentype]);
  return (prec);
}

// prefix_expression: postfix_expression
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
    // 获取下一个标记并递归解析为前缀表达式
    scan(&Token);
    tree = prefix();

    // 确保它是一个标识符
    if (tree->op != A_IDENT)
      fatal("& operator must be followed by an identifier");

    // 防止在数组上执行 '&'
    if (tree->sym->stype == S_ARRAY)
      fatal("& operator cannot be performed on an array");

    // 现在将操作更改为 A_ADDR，类型更改为
    // 原始类型的指针
    tree->op = A_ADDR;
    tree->type = pointer_to(tree->type);
    break;
  case T_STAR:
    // 获取下一个标记并递归解析为前缀表达式
    scan(&Token);
    tree = prefix();

    // 目前，确保它是另一个解引用或标识符
    if (tree->op != A_IDENT && tree->op != A_DEREF)
      fatal("* operator must be followed by an identifier or *");

    // 将 A_DEREF 操作添加到树的开头
    tree =
      mkastunary(A_DEREF, value_at(tree->type), tree->ctype, tree, NULL, 0);
    break;
  case T_MINUS:
    // 获取下一个标记并递归解析为前缀表达式
    scan(&Token);
    tree = prefix();

    // 将 A_NEGATE 操作添加到树的开头，并
    // 使子节点成为 rvalue。因为 char 是无符号的，
    // 如果需要也将其扩展为 int 以使其为有符号
    tree->rvalue = 1;
    if (tree->type == P_CHAR)
      tree->type = P_INT;
    tree = mkastunary(A_NEGATE, tree->type, tree->ctype, tree, NULL, 0);
    break;
  case T_INVERT:
    // 获取下一个标记并递归解析为前缀表达式
    scan(&Token);
    tree = prefix();

    // 将 A_INVERT 操作添加到树的开头，并使子节点成为 rvalue。
    tree->rvalue = 1;
    tree = mkastunary(A_INVERT, tree->type, tree->ctype, tree, NULL, 0);
    break;
  case T_LOGNOT:
    // 获取下一个标记并递归解析为前缀表达式
    scan(&Token);
    tree = prefix();

    // 将 A_LOGNOT 操作添加到树的开头，并使子节点成为 rvalue。
    tree->rvalue = 1;
    tree = mkastunary(A_LOGNOT, tree->type, tree->ctype, tree, NULL, 0);
    break;
  case T_INC:
    // 获取下一个标记并递归解析为前缀表达式
    scan(&Token);
    tree = prefix();

    // 目前，确保它是一个标识符
    if (tree->op != A_IDENT)
      fatal("++ operator must be followed by an identifier");

    // 将 A_PREINC 操作添加到树的开头
    tree = mkastunary(A_PREINC, tree->type, tree->ctype, tree, NULL, 0);
    break;
  case T_DEC:
    // 获取下一个标记并递归解析为前缀表达式
    scan(&Token);
    tree = prefix();

    // 目前，确保它是一个标识符
    if (tree->op != A_IDENT)
      fatal("-- operator must be followed by an identifier");

    // 将 A_PREDEC 操作添加到树的开头
    tree = mkastunary(A_PREDEC, tree->type, tree->ctype, tree, NULL, 0);
    break;
  default:
    tree = postfix();
  }
  return (tree);
}

// 返回一个以二元运算符为根的 AST 树。
// 参数 ptp 是前一个标记的优先级。
struct ASTnode *binexpr(int ptp) {
  struct ASTnode *left, *right;
  struct ASTnode *ltemp, *rtemp;
  int ASTop;
  int tokentype;

  // 获取左边的树。
  // 同时获取下一个标记。
  left = prefix();

  // 如果遇到几个终止标记之一，只返回左节点
  tokentype = Token.token;
  if (tokentype == T_SEMI || tokentype == T_RPAREN ||
      tokentype == T_RBRACKET || tokentype == T_COMMA ||
      tokentype == T_COLON || tokentype == T_RBRACE) {
    left->rvalue = 1;
    return (left);
  }
  // 当这个标记的优先级高于前一个标记的优先级时，
  // 或者它是右结合且等于前一个标记的优先级时
  while ((op_precedence(tokentype) > ptp) ||
	 (rightassoc(tokentype) && op_precedence(tokentype) == ptp)) {
    // 获取下一个整数常量
    scan(&Token);

    // 用我们标记的优先级递归调用 binexpr() 来构建子树
    right = binexpr(OpPrec[tokentype]);

    // 确定要对子树执行的操作
    ASTop = binastop(tokentype);

    switch (ASTop) {
    case A_TERNARY:
      // 确保有 ':' 标记，扫描其后的表达式
      match(T_COLON, ":");
      ltemp = binexpr(0);

      // 为此语句构建并返回 AST。使用中间
      // 表达式的类型作为返回类型。XXX 我们也应该
      // 考虑第三个表达式的类型。
      return (mkastnode
	      (A_TERNARY, right->type, right->ctype, left, right, ltemp,
	       NULL, 0));

    case A_ASSIGN:
      // 赋值
      // 使右树成为 rvalue
      right->rvalue = 1;

      // 确保右边的类型与左边匹配
      right = modify_type(right, left->type, left->ctype, 0);
      if (right == NULL)
	fatal("Incompatible expression in assignment");

      // 创建一个赋值 AST 树。但是，切换
      // 左右，以便右表达式的代码
      // 将在左表达式之前生成
      ltemp = left;
      left = right;
      right = ltemp;
      break;

    default:
      // 我们不是在做三元或赋值，所以两个树都应该
      // 是 rvalue。如果它们是 lvalue 树，则将它们转换为 rvalue
      left->rvalue = 1;
      right->rvalue = 1;

      // 通过尝试修改每个树以匹配另一方的类型来确保两种类型兼容。
      ltemp = modify_type(left, right->type, right->ctype, ASTop);
      rtemp = modify_type(right, left->type, left->ctype, ASTop);
      if (ltemp == NULL && rtemp == NULL)
	fatal("Incompatible types in binary expression");
      if (ltemp != NULL)
	left = ltemp;
      if (rtemp != NULL)
	right = rtemp;
    }

    // 将该子树与我们的连接起来。同时将标记转换为 AST 操作。
    left =
      mkastnode(binastop(tokentype), left->type, left->ctype, left, NULL,
		right, NULL, 0);

    // 一些运算符产生 int 结果，无论其操作数如何
    switch (binastop(tokentype)) {
    case A_LOGOR:
    case A_LOGAND:
    case A_EQ:
    case A_NE:
    case A_LT:
    case A_GT:
    case A_LE:
    case A_GE:
      left->type = P_INT;
    }

    // 更新当前标记的详细信息。
    // 如果遇到终止标记，只返回左节点
    tokentype = Token.token;
    if (tokentype == T_SEMI || tokentype == T_RPAREN ||
	tokentype == T_RBRACKET || tokentype == T_COMMA ||
	tokentype == T_COLON || tokentype == T_RBRACE) {
      left->rvalue = 1;
      return (left);
    }
  }

  // 当优先级相同或更低时返回我们拥有的树
  left->rvalue = 1;
  return (left);
}