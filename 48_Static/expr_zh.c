#include "defs.h"
#include "data.h"
#include "decl.h"

// 表达式解析
// Copyright (c) 2019 Warren Toomey, GPL3

// expression_list: <null>
//        | expression
//        | expression ',' expression_list
//        ;

// 解析零个或多个逗号分隔的表达式，返回由 A_GLUE 节点组成的 AST，
// 左子节点是先前表达式的子树（或 NULL），右子节点是下一个表达式。
// 每个 A_GLUE 节点的大小字段设置为此时树中表达式的数量。
// 如果没有解析表达式，则返回 NULL
struct ASTnode *expression_list(int endtoken) {
  struct ASTnode *tree = NULL;
  struct ASTnode *child = NULL;
  int exprcount = 0;

  // 循环直到结束标记
  while (Token.token != endtoken) {

    // 解析下一个表达式并增加表达式计数
    child = binexpr(0);
    exprcount++;

    // 构建一个 A_GLUE AST 节点，左子节点是之前的树，
    // 右子节点是新表达式。存储表达式计数。
    tree = mkastnode(A_GLUE, P_NONE, tree, NULL, child, NULL, exprcount);

    // 当到达结束标记时停止
    if (Token.token == endtoken) break;

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

  // 检查标识符是否已定义为函数，
  // 然后为其创建一个叶子节点。
  if ((funcptr = findsymbol(Text)) == NULL || funcptr->stype != S_FUNCTION) {
    fatals("Undeclared function", Text);
  }
  // 获取 '('
  lparen();

  // 解析参数表达式列表
  tree = expression_list(T_RPAREN);

  // XXX 检查每个参数与函数原型的类型

  // 构建函数调用 AST 节点。将函数的
  // 返回类型存储为该节点的类型。
  // 同时记录函数的符号 id
  tree = mkastunary(A_FUNCCALL, funcptr->type, tree, funcptr, 0);

  // 获取 ')'
  rparen();
  return (tree);
}

// 解析数组索引并返回其 AST 树
static struct ASTnode *array_access(void) {
  struct ASTnode *left, *right;
  struct symtable *aryptr;

  // 检查标识符是否已定义为数组，
  // 然后创建一个指向基址的叶子节点。
  if ((aryptr = findsymbol(Text)) == NULL || aryptr->stype != S_ARRAY) {
    fatals("Undeclared array", Text);
  }
  left = mkastleaf(A_ADDR, aryptr->type, aryptr, 0);

  // 获取 '['
  scan(&Token);

  // 解析后续表达式
  right = binexpr(0);

  // 获取 ']'
  match(T_RBRACKET, "]");

  // 确保这是 int 类型
  if (!inttype(right->type))
    fatal("Array index is not of integer type");

  // 按元素类型的大小缩放索引
  right = modify_type(right, left->type, A_ADD);

  // 返回一个 AST 树，其中数组基址加上偏移量，
  // 并解引用元素。此时仍是左值。
  left = mkastnode(A_ADD, aryptr->type, left, NULL, right, NULL, 0);
  left = mkastunary(A_DEREF, value_at(left->type), left, NULL, 0);
  return (left);
}

// 解析结构体或联合的成员引用，
// 并返回其 AST 树。如果 withpointer 为 true，
// 则通过指针访问成员。
static struct ASTnode *member_access(int withpointer) {
  struct ASTnode *left, *right;
  struct symtable *compvar;
  struct symtable *typeptr;
  struct symtable *m;

  // 检查标识符是否已声明为 struct/union
  // 或 struct/union 指针
  if ((compvar = findsymbol(Text)) == NULL)
    fatals("Undeclared variable", Text);
  if (withpointer && compvar->type != pointer_to(P_STRUCT)
      && compvar->type != pointer_to(P_UNION))
    fatals("Undeclared variable", Text);
  if (!withpointer && compvar->type != P_STRUCT && compvar->type != P_UNION)
    fatals("Undeclared variable", Text);

  // 如果是指向 struct/union 的指针，获取指针的值。
  // 否则，创建一个指向基址的叶子节点。
  // 无论哪种方式，都是右值
  if (withpointer) {
    left = mkastleaf(A_IDENT, pointer_to(compvar->type), compvar, 0);
  } else
    left = mkastleaf(A_ADDR, compvar->type, compvar, 0);
  left->rvalue = 1;

  // 获取复合类型的详细信息
  typeptr = compvar->ctype;

  // 跳过 '.' 或 '->' 标记并获取成员名称
  scan(&Token);
  ident();

  // 在类型中查找匹配的成员名称。
  // 如果找不到则报错
  for (m = typeptr->member; m != NULL; m = m->next)
    if (!strcmp(m->name, Text))
      break;

  if (m == NULL)
    fatals("No member found in struct/union: ", Text);

  // 构建一个带有偏移量的 A_INTLIT 节点
  right = mkastleaf(A_INTLIT, P_INT, NULL, m->st_posn);

  // 将成员的偏移量加到 struct/union 的基址上，
  // 并解引用它。此时仍是左值
  left = mkastnode(A_ADD, pointer_to(m->type), left, NULL, right, NULL, 0);
  left = mkastunary(A_DEREF, m->type, left, NULL, 0);
  return (left);
}

// 解析后缀表达式并返回
// 表示它的 AST 节点。标识符已在 Text 中。
static struct ASTnode *postfix(void) {
  struct ASTnode *n;
  struct symtable *varptr;
  struct symtable *enumptr;

  // 如果标识符匹配枚举值，
  // 返回一个 A_INTLIT 节点
  if ((enumptr = findenumval(Text)) != NULL) {
    scan(&Token);
    return (mkastleaf(A_INTLIT, P_INT, NULL, enumptr->st_posn));
  }
  // 扫描下一个标记以查看是否有后缀表达式
  scan(&Token);

  // 函数调用
  if (Token.token == T_LPAREN)
    return (funccall());

  // 数组引用
  if (Token.token == T_LBRACKET)
    return (array_access());

  // 访问 struct 或 union
  if (Token.token == T_DOT)
    return (member_access(0));
  if (Token.token == T_ARROW)
    return (member_access(1));

  // 一个变量。检查变量是否存在。
  if ((varptr = findsymbol(Text)) == NULL || varptr->stype != S_VARIABLE)
    fatals("Unknown variable", Text);

  switch (Token.token) {
    // 后递增：跳过标记
  case T_INC:
    scan(&Token);
    n = mkastleaf(A_POSTINC, varptr->type, varptr, 0);
    break;

    // 后递减：跳过标记
  case T_DEC:
    scan(&Token);
    n = mkastleaf(A_POSTDEC, varptr->type, varptr, 0);
    break;

    // 只是变量引用
  default:
    n = mkastleaf(A_IDENT, varptr->type, varptr, 0);
  }
  return (n);
}

// 解析基本因子并返回表示它的 AST 节点。
static struct ASTnode *primary(void) {
  struct ASTnode *n;
  int id;
  int type=0;
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
    type= parse_stars(parse_type(&ctype, &class));
    // 获取类型的大小
    size= typesize(type, ctype);
    rparen();
    // 返回带有大小的叶子节点整数常量
    return (mkastleaf(A_INTLIT, P_INT, NULL, size));

  case T_INTLIT:
    // 对于 INTLIT 标记，创建一个叶子 AST 节点。
    // 如果在 P_CHAR 范围内，则使其为 P_CHAR
    if (Token.intvalue >= 0 && Token.intvalue < 256)
      n = mkastleaf(A_INTLIT, P_CHAR, NULL, Token.intvalue);
    else
      n = mkastleaf(A_INTLIT, P_INT, NULL, Token.intvalue);
    break;

  case T_STRLIT:
    // 对于 STRLIT 标记，为其生成汇编代码。
    // 然后为其创建一个叶子 AST 节点。id 是字符串的标签。
    id = genglobstr(Text);
    n = mkastleaf(A_STRLIT, pointer_to(P_CHAR), NULL, id);
    break;

  case T_IDENT:
    return (postfix());

  case T_LPAREN:
    // 括号表达式的开头，跳过 '('。
    scan(&Token);


    // 如果下一个标记是类型标识符，这是强制转换表达式
    switch (Token.token) {
      case T_IDENT:
        // 我们需要查看标识符是否匹配 typedef。
        // 如果不是，则当作表达式处理。
        if (findtypedef(Text) == NULL) {
          n = binexpr(0); break;
        }
      case T_VOID:
      case T_CHAR:
      case T_INT:
      case T_LONG:
      case T_STRUCT:
      case T_UNION:
      case T_ENUM:
	// 获取括号内的类型
        type= parse_cast();
        // 跳过 ')' 然后解析后续表达式
        rparen();

      default: n = binexpr(0); // 扫描表达式
    }

    // 现在我们在 n 中至少有一个表达式，如果有强制转换，
    // type 中可能有非零类型。
    // 如果没有强制转换，跳过 ')'；如果有，则
    if (type == 0)
      rparen();
    else
      // 否则，为强制转换创建一个一元 AST 节点
      n= mkastunary(A_CAST, type, n, NULL, 0);
    return (n);

  default:
    fatals("Expecting a primary expression, got token", Token.tokstr);
  }

  // 扫描下一个标记并返回叶子节点
  scan(&Token);
  return (n);
}

// 将二元运算符标记转换为二元 AST 操作。
// 我们依赖于从标记到 AST 操作的 1:1 映射
static int binastop(int tokentype) {
  if (tokentype > T_EOF && tokentype <= T_SLASH)
    return (tokentype);
  fatald("Syntax error, token", tokentype);
  return (0);			// 保持 -Wall 愉快
}

// 如果标记是右结合的则返回 true，否则返回 false。
static int rightassoc(int tokentype) {
  if (tokentype >= T_ASSIGN && tokentype <= T_ASSLASH)
    return (1);
  return (0);
}

// 每个标记的运算符优先级。必须与 defs.h
// 中的标记顺序相匹配
static int OpPrec[] = {
  0, 10, 10,			// T_EOF, T_ASSIGN, T_ASPLUS,
  10, 10, 10,			// T_ASMINUS, T_ASSTAR, T_ASSLASH,
  20, 30,			// T_LOGOR, T_LOGAND
  40, 50, 60,			// T_OR, T_XOR, T_AMPER 
  70, 70,			// T_EQ, T_NE
  80, 80, 80, 80,		// T_LT, T_GT, T_LE, T_GE
  90, 90,			// T_LSHIFT, T_RSHIFT
  100, 100,			// T_PLUS, T_MINUS
  110, 110			// T_STAR, T_SLASH
};

// 检查我们有二元运算符并返回其优先级。
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

// 解析前缀表达式并返回表示它的子树。
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

    // 现在将操作码改为 A_ADDR，类型改为
    // 指向原始类型的指针
    tree->op = A_ADDR;
    tree->type = pointer_to(tree->type);
    break;
  case T_STAR:
    // 获取下一个标记并递归解析为前缀表达式
    scan(&Token);
    tree = prefix();

    // 目前，确保它是另一个解引用或
    // 标识符
    if (tree->op != A_IDENT && tree->op != A_DEREF)
      fatal("* operator must be followed by an identifier or *");

    // 在树前添加一个 A_DEREF 操作
    tree = mkastunary(A_DEREF, value_at(tree->type), tree, NULL, 0);
    break;
  case T_MINUS:
    // 获取下一个标记并递归解析为前缀表达式
    scan(&Token);
    tree = prefix();

    // 在树前添加一个 A_NEGATE 操作，并使
    // 子节点成为右值。因为 char 是无符号的，
    // 还要将其扩展为 int 以使其为有符号
    tree->rvalue = 1;
    tree = modify_type(tree, P_INT, 0);
    tree = mkastunary(A_NEGATE, tree->type, tree, NULL, 0);
    break;
  case T_INVERT:
    // 获取下一个标记并递归解析为前缀表达式
    scan(&Token);
    tree = prefix();

    // 在树前添加一个 A_INVERT 操作，并使
    // 子节点成为右值。
    tree->rvalue = 1;
    tree = mkastunary(A_INVERT, tree->type, tree, NULL, 0);
    break;
  case T_LOGNOT:
    // 获取下一个标记并递归解析为前缀表达式
    scan(&Token);
    tree = prefix();

    // 在树前添加一个 A_LOGNOT 操作，并使
    // 子节点成为右值。
    tree->rvalue = 1;
    tree = mkastunary(A_LOGNOT, tree->type, tree, NULL, 0);
    break;
  case T_INC:
    // 获取下一个标记并递归解析为前缀表达式
    scan(&Token);
    tree = prefix();

    // 目前，确保它是一个标识符
    if (tree->op != A_IDENT)
      fatal("++ operator must be followed by an identifier");

    // 在树前添加一个 A_PREINC 操作
    tree = mkastunary(A_PREINC, tree->type, tree, NULL, 0);
    break;
  case T_DEC:
    // 获取下一个标记并递归解析为前缀表达式
    scan(&Token);
    tree = prefix();

    // 目前，确保它是一个标识符
    if (tree->op != A_IDENT)
      fatal("-- operator must be followed by an identifier");

    // 在树前添加一个 A_PREDEC 操作
    tree = mkastunary(A_PREDEC, tree->type, tree, NULL, 0);
    break;
  default:
    tree = primary();
  }
  return (tree);
}

// 返回以二元运算符为根的 AST 树。
// 参数 ptp 是前一个标记的优先级。
struct ASTnode *binexpr(int ptp) {
  struct ASTnode *left, *right;
  struct ASTnode *ltemp, *rtemp;
  int ASTop;
  int tokentype;

  // 获取左边的树。同时获取下一个标记。
  left = prefix();

  // 如果遇到多个终止标记之一，只返回左节点
  tokentype = Token.token;
  if (tokentype == T_SEMI || tokentype == T_RPAREN ||
      tokentype == T_RBRACKET || tokentype == T_COMMA ||
      tokentype == T_COLON || tokentype == T_RBRACE) {
    left->rvalue = 1;
    return (left);
  }
  // 当这个标记的优先级大于前一个标记的优先级时，
  // 或者它是右结合且等于前一个标记的优先级时
  while ((op_precedence(tokentype) > ptp) ||
	 (rightassoc(tokentype) && op_precedence(tokentype) == ptp)) {
    // 获取下一个整数常量
    scan(&Token);

    // 用我们标记的优先级递归调用 binexpr() 构建子树
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

      // 创建一个赋值 AST 树。但是，交换
      // 左右，这样右表达式的代码将在左表达式之前生成
      ltemp = left;
      left = right;
      right = ltemp;
    } else {

      // 我们不是在做赋值，所以两个树都应该是右值
      // 如果它们是左值树，则转换为右值
      left->rvalue = 1;
      right->rvalue = 1;

      // 确保两种类型兼容，尝试
      // 修改每个树以匹配对方的类型。
      ltemp = modify_type(left, right->type, ASTop);
      rtemp = modify_type(right, left->type, ASTop);
      if (ltemp == NULL && rtemp == NULL)
	fatal("Incompatible types in binary expression");
      if (ltemp != NULL)
	left = ltemp;
      if (rtemp != NULL)
	right = rtemp;
    }

    // 将子树与我们连接起来。同时将
    // 标记转换为 AST 操作。
    left =
      mkastnode(binastop(tokentype), left->type, left, NULL, right, NULL, 0);

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