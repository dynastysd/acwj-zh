#include "defs.h"
#include "data.h"
#include "decl.h"

// 表达式的解析
// Copyright (c) 2019 Warren Toomey, GPL3

// expression_list: <null>
//        | expression
//        | expression ',' expression_list
//        ;

// 解析零个或多个逗号分隔的表达式列表，
// 返回由 A_GLUE 节点组成的 AST，左子树是先前表达式的子树（或 NULL），
// 右子树是下一个表达式。每个 A_GLUE 节点的 size 字段设置为此时树中表达式的数量。
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

    // 构建一个 A_GLUE AST 节点，左子树为之前的树，右子树为新表达式。
    // 存储表达式计数。
    tree =
      mkastnode(A_GLUE, P_NONE, NULL, tree, NULL, child, NULL, exprcount);

    // 当到达结束标记时停止
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

  // 检查标识符是否已定义为函数，
  // 然后为其创建一个叶子节点。
  if ((funcptr = findsymbol(Text)) == NULL || funcptr->stype != S_FUNCTION) {
    fatals("未声明的函数", Text);
  }

  // 获取 '('
  lparen();

  // 解析参数表达式列表
  tree = expression_list(T_RPAREN);

  // XXX 检查每个参数与函数原型的类型

  // 构建函数调用 AST 节点。将函数的返回类型存储为
  // 此节点的类型。还记录函数的符号 id
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
    fatal("不是数组或指针");

  // 获取 '['
  scan(&Token);

  // 解析后续表达式
  right = binexpr(0);

  // 获取 ']'
  match(T_RBRACKET, "]");

  // 确保这是 int 类型
  if (!inttype(right->type))
    fatal("数组索引不是整数类型");

  // 将左树设为 rvalue
  left->rvalue = 1;

  // 按元素类型的大小缩放索引
  right = modify_type(right, left->type, left->ctype, A_ADD);

  // 返回 AST 树，其中数组基址加上了偏移量，
  // 并解引用元素。此时仍为 lvalue。
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
    fatal("表达式不是指向 struct/union 的指针");

  // 或者，检查左 AST 树是否是 struct 或 union。
  // 如果是，则将其从 A_IDENT 更改为 A_ADDR，
  // 以便我们获取基址，而不是该地址的值。
  if (!withpointer) {
    if (left->type == P_STRUCT || left->type == P_UNION)
      left->op = A_ADDR;
    else
      fatal("表达式不是 struct/union");
  }

  // 获取复合类型的详细信息
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
    fatals("在 struct/union 中未找到成员：", Text);

  // 将左树设为 rvalue
  left->rvalue = 1;

  // 使用偏移量构建一个 A_INTLIT 节点
  right = mkastleaf(A_INTLIT, P_INT, NULL, NULL, m->st_posn);

  // 将成员的偏移量加到 struct/union 的基址上
  // 并解引用它。此时仍为 lvalue
  left =
    mkastnode(A_ADD, pointer_to(m->type), m->ctype, left, NULL, right, NULL,
	      0);
  left = mkastunary(A_DEREF, m->type, m->ctype, left, NULL, 0);
  return (left);
}

// 解析括号表达式并
// 返回表示它的 AST 节点。
static struct ASTnode *paren_expression(int ptp) {
  struct ASTnode *n;
  int type = 0;
  struct symtable *ctype = NULL;

  // 括号表达式的开头，跳过 '('。
  scan(&Token);

  // 如果标记后是类型标识符，则是强制转换表达式
  switch (Token.token) {
  case T_IDENT:
    // 我们必须查看标识符是否匹配 typedef。
    // 如果不是，则视为表达式。
    if (findtypedef(Text) == NULL) {
      n = binexpr(0);	// ptp 为零，因为括号内的表达式
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
    n = binexpr(ptp);		// 扫描表达式。我们传入 ptp
				// 因为强制转换不会改变表达式的优先级
  }

  // 我们现在在 n 中至少有一个表达式，如果有强制转换，还可能在 type 中有非零类型。
  // 如果没有强制转换，则跳过 ')'。
  if (type == 0)
    rparen();
  else
    // 否则，为强制转换创建一个一元 AST 节点
    n = mkastunary(A_CAST, type, ctype, n, NULL, 0);
  return (n);
}

// 解析基本因子并返回
// 表示它的 AST 节点。
static struct ASTnode *primary(int ptp) {
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
    fatal("编译器不支持 static 或 extern 局部声明");
  case T_SIZEOF:
    // 跳过 T_SIZEOF 并确保有左括号
    scan(&Token);
    if (Token.token != T_LPAREN)
      fatal("sizeof 后应有左括号");
    scan(&Token);

    // 获取括号内的类型
    type = parse_stars(parse_type(&ctype, &class));

    // 获取类型的大小
    size = typesize(type, ctype);
    rparen();

    // 创建一个包含大小的叶子节点整数字面量
    return (mkastleaf(A_INTLIT, P_INT, NULL, NULL, size));

  case T_INTLIT:
    // 对于 INTLIT 标记，为其创建一个叶子 AST 节点。
    // 如果在 P_CHAR 范围内，则将其设为 P_CHAR
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

    // 查看此标识符是否作为符号存在。对于数组，将 rvalue 设为 1。
    if ((varptr = findsymbol(Text)) == NULL)
      fatals("未知的变量或函数", Text);
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
	fatals("函数名使用但没有括号", Text);
      return (funccall());
    default:
      fatals("标识符不是标量或数组变量", Text);
    }

    break;

  case T_LPAREN:
    return (paren_expression(ptp));

  default:
    fatals("预期基本表达式，得到标记", Token.tokstr);
  }

  // 扫描下一个标记并返回叶子节点
  scan(&Token);
  return (n);
}

// 解析后缀表达式并返回
// 表示它的 AST 节点。标识符已在 Text 中。
static struct ASTnode *postfix(int ptp) {
  struct ASTnode *n;

  // 获取基本表达式
  n = primary(ptp);

  // 循环直到没有更多后缀运算符
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
      // 指针访问 struct 或 union
      n = member_access(n, 1);
      break;

    case T_INC:
      // 后自增：跳过标记
      if (n->rvalue == 1)
	fatal("不能在 rvalue 上 ++");
      scan(&Token);

      // 不能做两次
      if (n->op == A_POSTINC || n->op == A_POSTDEC)
	fatal("不能 ++ 和/或 -- 超过一次");

      // 更改 AST 操作
      n->op = A_POSTINC;
      break;

    case T_DEC:
      // 后自减：跳过标记
      if (n->rvalue == 1)
	fatal("不能在 rvalue 上 --");
      scan(&Token);

      // 不能做两次
      if (n->op == A_POSTINC || n->op == A_POSTDEC)
	fatal("不能 ++ 和/或 -- 超过一次");

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
// 我们依赖从标记到 AST 操作的 1:1 映射
static int binastop(int tokentype) {
  if (tokentype > T_EOF && tokentype <= T_MOD)
    return (tokentype);
  fatals("语法错误，标记", Tstring[tokentype]);
  return (0);			// 保持 -Wall 愉快
}

// 如果标记是右关联的则返回 true，否则返回 false。
static int rightassoc(int tokentype) {
  if (tokentype >= T_ASSIGN && tokentype <= T_ASSLASH)
    return (1);
  return (0);
}

// 每个标记的运算符优先级。必须
// 与 defs.h 中标记的顺序相匹配
static int OpPrec[] = {
  0, 10, 10,			// T_EOF, T_ASSIGN, T_ASPLUS,
  10, 10,			// T_ASMINUS, T_ASSTAR,
  10, 10,			// T_ASSLASH, T_ASMOD,
  15,				// T_QUESTION,
  20, 30,			// T_LOGOR, T_LOGAND
  40, 50, 60,			// T_OR, T_XOR, T_AMPER
  70, 70,			// T_EQ, T_NE
  80, 80, 80, 80,		// T_LT, T_GT, T_LE, T_GE
  90, 90,			// T_LSHIFT, T_RSHIFT
  100, 100,			// T_PLUS, T_MINUS
  110, 110, 110			// T_STAR, T_SLASH, T_MOD
};

// 检查我们是否有二元运算符并返回其优先级。
static int op_precedence(int tokentype) {
  int prec;
  if (tokentype > T_MOD)
    fatals("标记在 op_precedence 中没有优先级：", Tstring[tokentype]);
  prec = OpPrec[tokentype];
  if (prec == 0)
    fatals("语法错误，标记", Tstring[tokentype]);
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
static struct ASTnode *prefix(int ptp) {
  struct ASTnode *tree;
  switch (Token.token) {
  case T_AMPER:
    // 获取下一个标记并递归解析为前缀表达式
    scan(&Token);
    tree = prefix(ptp);

    // 确保它是一个标识符
    if (tree->op != A_IDENT)
      fatal("& 运算符后面必须是标识符");

    // 防止对数组执行 '&'
    if (tree->sym->stype == S_ARRAY)
      fatal("& 运算符不能对数组执行");

    // 现在将操作更改为 A_ADDR，类型更改为指向原始类型的指针
    tree->op = A_ADDR;
    tree->type = pointer_to(tree->type);
    break;
  case T_STAR:
    // 获取下一个标记并递归解析为前缀表达式。
    // 将其设为 rvalue
    scan(&Token);
    tree = prefix(ptp);
    tree->rvalue= 1;

    // 确保树的类型是指针
    if (!ptrtype(tree->type))
      fatal("* 运算符后面必须是指针类型的表达式");

    // 在树前添加一个 A_DEREF 操作
    tree =
      mkastunary(A_DEREF, value_at(tree->type), tree->ctype, tree, NULL, 0);
    break;
  case T_MINUS:
    // 获取下一个标记并递归解析为前缀表达式
    scan(&Token);
    tree = prefix(ptp);

    // 在树前添加一个 A_NEGATE 操作，
    // 并将子节点设为 rvalue。因为 char 是无符号的，
    // 还需要加宽为 int 以使其有符号
    tree->rvalue = 1;
    if (tree->type == P_CHAR)
      tree->type = P_INT;
    tree = mkastunary(A_NEGATE, tree->type, tree->ctype, tree, NULL, 0);
    break;
  case T_INVERT:
    // 获取下一个标记并递归解析为前缀表达式
    scan(&Token);
    tree = prefix(ptp);

    // 在树前添加一个 A_INVERT 操作，
    // 并将子节点设为 rvalue。
    tree->rvalue = 1;
    tree = mkastunary(A_INVERT, tree->type, tree->ctype, tree, NULL, 0);
    break;
  case T_LOGNOT:
    // 获取下一个标记并递归解析为前缀表达式
    scan(&Token);
    tree = prefix(ptp);

    // 在树前添加一个 A_LOGNOT 操作，
    // 并将子节点设为 rvalue。
    tree->rvalue = 1;
    tree = mkastunary(A_LOGNOT, tree->type, tree->ctype, tree, NULL, 0);
    break;
  case T_INC:
    // 获取下一个标记并递归解析为前缀表达式
    scan(&Token);
    tree = prefix(ptp);

    // 目前，确保它是一个标识符
    if (tree->op != A_IDENT)
      fatal("++ 运算符后面必须是标识符");

    // 在树前添加一个 A_PREINC 操作
    tree = mkastunary(A_PREINC, tree->type, tree->ctype, tree, NULL, 0);
    break;
  case T_DEC:
    // 获取下一个标记并递归解析为前缀表达式
    scan(&Token);
    tree = prefix(ptp);

    // 目前，确保它是一个标识符
    if (tree->op != A_IDENT)
      fatal("-- 运算符后面必须是标识符");

    // 在树前添加一个 A_PREDEC 操作
    tree = mkastunary(A_PREDEC, tree->type, tree->ctype, tree, NULL, 0);
    break;
  default:
    tree = postfix(ptp);
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
  left = prefix(ptp);

  // 如果我们遇到几个终止标记之一，则只返回左节点
  tokentype = Token.token;
  if (tokentype == T_SEMI || tokentype == T_RPAREN ||
      tokentype == T_RBRACKET || tokentype == T_COMMA ||
      tokentype == T_COLON || tokentype == T_RBRACE) {
    left->rvalue = 1;
    return (left);
  }

  // 当此标记的优先级高于前一个标记的优先级时，
  // 或者它是右关联的且等于前一个标记的优先级
  while ((op_precedence(tokentype) > ptp) ||
	 (rightassoc(tokentype) && op_precedence(tokentype) == ptp)) {
    // 扫描下一个整数字面量
    scan(&Token);

    // 使用我们标记的优先级递归调用 binexpr() 来构建子树
    right = binexpr(OpPrec[tokentype]);

    // 确定要对子树执行的操作
    ASTop = binastop(tokentype);

    switch (ASTop) {
    case A_TERNARY:
      // 确保我们有 ':' 标记，扫描其后的表达式
      match(T_COLON, ":");
      ltemp = binexpr(0);

      // 为此语句构建并返回 AST。使用中间
      // 表达式的类型作为返回类型。XXX 我们还应该考虑第三个表达式的类型。
      return (mkastnode
	      (A_TERNARY, right->type, right->ctype, left, right, ltemp,
	       NULL, 0));

    case A_ASSIGN:
      // 赋值
      // 将右树设为 rvalue
      right->rvalue = 1;

      // 确保右边的类型与左边匹配
      right = modify_type(right, left->type, left->ctype, 0);
      if (right == NULL)
	fatal("赋值中的表达式不兼容");

      // 创建一个赋值的 AST 树。但是，
      // 将左右交换，这样右表达式的代码将在左表达式之前生成
      ltemp = left;
      left = right;
      right = ltemp;
      break;

    default:
      // 我们不是在做三元运算或赋值，所以两个树都应该是 rvalue。
      // 如果它们是 lvalue 树，则将两者都转换为 rvalue
      left->rvalue = 1;
      right->rvalue = 1;

      // 通过尝试将每个树修改为与另一个树的类型相匹配来确保两种类型兼容。
      ltemp = modify_type(left, right->type, right->ctype, ASTop);
      rtemp = modify_type(right, left->type, left->ctype, ASTop);
      if (ltemp == NULL && rtemp == NULL)
	fatal("二元表达式中的类型不兼容");
      if (ltemp != NULL)
	left = ltemp;
      if (rtemp != NULL)
	right = rtemp;
    }

    // 将该子树与我们的树连接起来。同时将标记转换为 AST 操作。
    left =
      mkastnode(binastop(tokentype), left->type, left->ctype, left, NULL,
		right, NULL, 0);

    // 某些运算符无论其操作数如何都会产生 int 结果
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
    // 如果我们遇到终止标记，则只返回左节点
    tokentype = Token.token;
    if (tokentype == T_SEMI || tokentype == T_RPAREN ||
	tokentype == T_RBRACKET || tokentype == T_COMMA ||
	tokentype == T_COLON || tokentype == T_RBRACE) {
      left->rvalue = 1;
      return (left);
    }
  }

  // 当优先级相同或更低时返回我们得到的树
  left->rvalue = 1;
  return (left);
}