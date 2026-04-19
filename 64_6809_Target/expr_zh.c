#include "defs.h"
#include "data.h"
#include "decl.h"
#include "expr.h"
#include "gen.h"
#include "misc.h"
#include "parse.h"
#include "sym.h"
#include "target.h"
#include "tree.h"
#include "types.h"

// 表达式解析
// Copyright (c) 2019 Warren Toomey, GPL3

// expression_list: <null>
//        | expression
//        | expression ',' expression_list
//        ;

// 解析零个或多个逗号分隔的表达式的列表，并返回由 A_GLUE 节点组成的 AST，
// 左子节点是先前表达式的子树（或 NULL），右子节点是下一个表达式。
// 每个 A_GLUE 节点的 size 字段将设置为此时树中表达式的数量。
// 如果没有解析表达式，则返回 NULL
struct ASTnode *expression_list(int endtoken) {
  struct ASTnode *tree = NULL;
  struct ASTnode *child = NULL;
  int exprcount = 0;

  // 循环直到结束 token
  while (Token.token != endtoken) {

    // 解析下一个表达式并增加表达式计数
    child = binexpr(0);
    exprcount++;

    // 使用先前的树作为左子节点，新表达式作为右子节点构建一个 A_GLUE AST 节点。
    // 存储表达式计数。
    tree =
      mkastnode(A_GLUE, P_NONE, NULL, tree, NULL, child, NULL, exprcount);

    // 当我们到达结束 token 时停止
    if (Token.token == endtoken)
      break;

    // 此时必须有 ','
    match(T_COMMA, ",");
  }

  // 返回表达式树
  return (tree);
}

// 递归检查函数调用的参数与函数的参数。
// 我们使用参数和指向函数第一个参数或局部变量的指针获取一个带有参数的 AST 子树。
// 我们遍历 AST 树并返回指向下一个要处理的参数的指针。
struct symtable *check_arg_vs_param(struct ASTnode *tree,
				    struct symtable *param,
				    struct symtable *funcptr) {

  // 没有树但有参数，参数不够。否则，什么都不做。
  if (tree == NULL) {
    if (param != NULL && param->class==V_PARAM)
      fatal("Not enough arguments in function call A");
    return (NULL);
  }

  // 如果有左 AST 子节点，递归处理它
  if (tree->left != NULL)
    param = check_arg_vs_param(tree->left, param, funcptr);

  // 我们已经到达递归的底部
  if (tree->right == NULL)
    fatal("Not enough arguments in function call B");

  if (param == NULL) {
    // 如果函数允许任意数量的参数，我们可以处理这个参数。否则就是错误。
    if (funcptr->has_ellipsis) {

      // 如果树的类型是 P_CHAR，将其加宽到 P_INT。
      // 这主要用于执行 printf("%d", 'x');
      if (tree->right->type == P_CHAR) {
	tree->right= mkastunary(A_WIDEN, P_INT, NULL, tree->right, NULL, 0);
	tree->rightid= tree->right->nodeid;
      }
      return (NULL);
    }
    fatal("Too many arguments in function call");
  }

  // 稍微脏的技巧：在我们求值之前，将任何 INTLIT 类型更改为与函数参数相同的类型。
  if (tree->right->op == A_INTLIT)
    tree->right->type = param->type;

  // 确保 arg/param 类型兼容。如有必要，加宽参数
  tree->right = modify_type(tree->right, param->type, param->ctype, 0);
  tree->rightid= tree->right->nodeid;
  if (tree->right == NULL)
    fatal("Incompatible argument type in function call");

  // 现在为我们调用者返回下一个要处理的参数。
  // 当我们遇到第一个局部变量时返回 NULL，因为它们在所有参数之后。
  if (param->next != NULL && param->next->class==V_LOCAL) return(NULL);
  return (param->next);
}

// 解析函数调用并返回其 AST
static struct ASTnode *funccall(void) {
  struct ASTnode *tree;
  struct symtable *funcptr;

  // 检查标识符是否已定义为函数，然后为其创建一个叶子节点。
  if ((funcptr = findSymbol(Text, S_NOTATYPE, 0)) == NULL || funcptr->stype != S_FUNCTION) {
    fatals("Undeclared function", Text);
  }
  // 获取 '('
  lparen();

  // 解析参数表达式列表
  tree = expression_list(T_RPAREN);

  // 根据函数原型检查每个参数的类型
  check_arg_vs_param(tree, funcptr->member, funcptr);

  // 构建函数调用 AST 节点。将函数的返回类型存储为该节点的类型。
  // 还记录函数的符号 id
  tree =
    mkastunary(A_FUNCCALL, funcptr->type, funcptr->ctype, tree, funcptr, 0);

  // 获取 ')'
  rparen();
  return (tree);
}

// 解析数组索引并为其返回 AST 树
static struct ASTnode *array_access(struct ASTnode *left) {
  struct ASTnode *right;

  // 检查子树是否是指针
  if (!ptrtype(left->type))
    fatal("Not an array or pointer");

  // 获取 '['
  scan(&Token);

  // 解析随后的表达式
  right = binexpr(0);

  // 获取 ']'
  match(T_RBRACKET, "]");

  // 确保这是 int 类型
  if (!inttype(right->type))
    fatal("Array index is not of integer type");

  // 将左树设为右值
  left->rvalue = 1;

  // 按元素类型的大小缩放索引
  right = modify_type(right, left->type, left->ctype, A_ADD);

  // 返回一个 AST 树，其中数组基址加上偏移量，
  // 并解引用元素。此时仍是左值。
  left =
    mkastnode(A_ADD, left->type, left->ctype, left, NULL, right, NULL, 0);
  left =
    mkastunary(A_DEREF, value_at(left->type), left->ctype, left, NULL, 0);
  return (left);
}

// 解析 struct 或 union 的成员引用并为其返回 AST 树。
// 如果 withpointer 为真，则通过指针访问成员。
static struct ASTnode *member_access(struct ASTnode *left, int withpointer) {
  struct ASTnode *right;
  struct symtable *typeptr;
  struct symtable *m;

  // 检查左 AST 树是否是指向 struct 或 union 的指针
  if (withpointer && left->type != pointer_to(P_STRUCT)
      && left->type != pointer_to(P_UNION))
    fatal("Expression is not a pointer to a struct/union");

  // 或者，检查左 AST 树是否是 struct 或 union。
  // 如果是，则将其从 A_IDENT 更改为 A_ADDR，以便我们获得基地址，
  // 而不是该地址处的值。
  if (!withpointer) {
    if (left->type == P_STRUCT || left->type == P_UNION)
      left->op = A_ADDR;
    else
      fatal("Expression is not a struct/union");
  }
  // 获取组合类型的详细信息
  typeptr = left->ctype;

  // 跳过 '.' 或 '->' token 并获取成员的名称
  scan(&Token);
  ident();

  // 在类型中查找匹配的成员名称
  // 如果找不到则出错
  for (m = typeptr->member; m != NULL; m = m->next)
    if (!strcmp(m->name, Text))
      break;
  if (m == NULL)
    fatals("No member found in struct/union: ", Text);

  // 将左树设为右值
  left->rvalue = 1;

  // 使用偏移量构建一个 A_INTLIT 节点。使用可以添加到地址的右 int 大小。
  right = mkastleaf(A_INTLIT, cgaddrint(), NULL, NULL, m->st_posn);

  // 将成员的偏移量添加到 struct/union 的基址
  // 并解引用它。此时仍是左值
  left =
    mkastnode(A_ADD, pointer_to(m->type), m->ctype, left, NULL, right, NULL,
	      0);
  left = mkastunary(A_DEREF, m->type, m->ctype, left, NULL, 0);
  return (left);
}

// 解析括号表达式并返回表示它的 AST 节点。
static struct ASTnode *paren_expression(int ptp) {
  struct ASTnode *n;
  int type = 0;
  struct symtable *ctype = NULL;

  // 括号表达式的开始，跳过 '('。
  scan(&Token);

  // 如果之后的 token 是类型标识符，这是强制转换表达式
  switch (Token.token) {
  case T_IDENT:
    // 我们必须查看标识符是否匹配 typedef。
    // 如果不是，则将其视为表达式。
    if (findtypedef(Text) == NULL) {
      n = binexpr(0);		// ptp 是 0，因为括号内的表达式
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

    // 跳过 ')' 然后解析随后的表达式
    rparen();

  default:
    n = binexpr(ptp);		// 扫描表达式。我们传入 ptp，
    // 因为强制转换不会改变表达式的优先级
  }

  // 我们现在在 n 中至少有一个表达式，如果存在强制转换，则可能在 type 中有非零类型。
  // 如果没有强制转换，则跳过 ')'。
  if (type == 0)
    rparen();
  else
    // 否则，为强制转换创建一个一元 AST 节点
    n = mkastunary(A_CAST, type, ctype, n, NULL, 0);
  return (n);
}

// 解析一个基本因子并返回表示它的 AST 节点。
static struct ASTnode *primary(int ptp) {
  struct ASTnode *n;
  struct symtable *varptr;
  int type = 0;
  int size, class, totalsize, prevsize;
  struct symtable *ctype;
  char *litval, *litend;

  switch (Token.token) {
  case T_STATIC:
  case T_EXTERN:
    fatal("Compiler doesn't support static or extern local declarations");
  case T_SIZEOF:
    // 跳过 T_SIZEOF 并确保我们有一个左括号
    scan(&Token);
    if (Token.token != T_LPAREN)
      fatal("Left parenthesis expected after sizeof");
    scan(&Token);

    // 获取括号内的类型
    type = parse_stars(parse_type(&ctype, &class));

    // 获取类型的大小
    size = typesize(type, ctype);
    rparen();

    // 创建一个带有大小的叶子节点整数字面量
    return (mkastleaf(A_INTLIT, P_INT, NULL, NULL, size));

  case T_CHARLIT:
    // 对于 CHARLIT token，为其创建一个叶子 AST 节点。
    n = mkastleaf(A_INTLIT, P_CHAR, NULL, NULL, Token.intvalue);

  case T_INTLIT:
    // 对于 INTLIT token，为其创建一个叶子 AST 节点。
    n = mkastleaf(A_INTLIT, P_INT, NULL, NULL, Token.intvalue);
    break;

  case T_STRLIT:
    // 对于 STRLIT token，构建字面量字符串并存储在 name 中
    totalsize= strlen(Text);
    litval= (char *)malloc(totalsize+1);
    strcpy(litval, Text);

    // 对于连续的 STRLIT token，
    // 将它们的内容附加到 litval
    while (1) {
      scan(&Peektoken);
      if (Peektoken.token != T_STRLIT)
	break;

      // 在保存先前大小的同时增加总字符串大小
      size = strlen(Text);
      prevsize= totalsize;
      totalsize += size;

      // 用这个总大小分配新内存
      litval= (char *)realloc(litval, totalsize+1);

      // 找到当前字符串的末尾
      litend= litval + prevsize;

      // 将新字面量复制到末尾
      strcpy(litend, Text);

      scan(&Token);		// 以正确跳过它
    }

    // 现在为其创建一个叶子 AST 节点。id 是字符串的标签。
    n = mkastleaf(A_STRLIT, pointer_to(P_CHAR), NULL, NULL, 0);
    n->name= litval;
    break;

  case T_IDENT:
    // 查看此标识符是否存在作为符号。对于数组，将 rvalue 设置为 1。
    if ((varptr = findSymbol(Text, S_NOTATYPE, 0)) == NULL) {
      fatals("Unknown variable or function", Text);
    }

    switch (varptr->stype) {
    case S_ENUMVAL:
      // 如果标识符匹配枚举值，返回带有值的 A_INTLIT 节点
      n = mkastleaf(A_INTLIT, P_INT, NULL, NULL, varptr->st_posn);
      break;
    case S_VARIABLE:
      n = mkastleaf(A_IDENT, varptr->type, varptr->ctype, varptr, 0);
      break;
    case S_ARRAY:
      n = mkastleaf(A_ADDR, varptr->type, varptr->ctype, varptr, 0);
      n->rvalue = 1;
      break;
    case S_FUNCTION:
      // 函数调用，查看下一个 token 是否是左括号
      scan(&Token);
      if (Token.token != T_LPAREN)
	fatals("Function name used without parentheses", Text);
      return (funccall());
    default:
      fatals("Identifier not a scalar or array variable", Text);
    }

    break;

  case T_LPAREN:
    return (paren_expression(ptp));

  default:
    fatals("Expecting a primary expression, got token",
	Tstring[Token.token]);
  }

  // 扫描下一个 token 并返回叶子节点
  scan(&Token);
  return (n);
}

// 解析一个后缀表达式并返回表示它的 AST 节点。
// 标识符已在 Text 中。
static struct ASTnode *postfix(int ptp) {
  struct ASTnode *n;

  // 获取基本表达式
  n = primary(ptp);

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
      // 后递增：跳过 token
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
      // 后递减：跳过 token
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

  return (NULL);		// 保持 -Wall 高兴
}


// 将二元运算符 token 转换为二元 AST 操作。
// 我们依赖于从 token 到 AST 操作的 1:1 映射
static int binastop(int tokentype) {
  if (tokentype > T_EOF && tokentype <= T_MOD)
    return (tokentype);
  fatals("Syntax error, token", Tstring[tokentype]);
  return (0);			// 保持 -Wall 高兴
}

// 如果 token 是右结合则返回真，否则返回假。
static int rightassoc(int tokentype) {
  if (tokentype >= T_ASSIGN && tokentype <= T_ASSLASH)
    return (1);
  return (0);
}

// 每个 token 的运算符优先级。必须与 defs.h 中的 token 顺序匹配
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

// 解析前缀表达式并返回表示它的子树。
static struct ASTnode *prefix(int ptp) {
  struct ASTnode *tree = NULL;
  switch (Token.token) {
  case T_AMPER:
    // 获取下一个 token 并递归解析为前缀表达式
    scan(&Token);
    tree = prefix(ptp);

    // 确保它是一个标识符
    if (tree->op != A_IDENT)
      fatal("& operator must be followed by an identifier");

    // 防止 '&' 在数组上执行
    if (tree->sym->stype == S_ARRAY)
      fatal("& operator cannot be performed on an array");

    // 现在将操作符更改为 A_ADDR，类型更改为指向原始类型的指针。
    // 将标识符标记为需要真实内存地址
    tree->op = A_ADDR;
    tree->type = pointer_to(tree->type);
    tree->sym->st_hasaddr = 1;
    break;
  case T_STAR:
    // 获取下一个 token 并递归解析为前缀表达式。
    // 使其成为右值
    scan(&Token);
    tree = prefix(ptp);
    tree->rvalue = 1;

    // 确保树的类型是指针
    if (!ptrtype(tree->type))
      fatal("* operator must be followed by an expression of pointer type");

    // 将 A_DEREF 操作前置到树
    tree =
      mkastunary(A_DEREF, value_at(tree->type), tree->ctype, tree, NULL, 0);
    break;
  case T_MINUS:
    // 获取下一个 token 并递归解析为前缀表达式
    scan(&Token);
    tree = prefix(ptp);

    // 将 A_NEGATE 操作前置到树并使子节点成为右值。
    // 因为 char 是无符号的，如果需要也加宽到 int 以使其有符号
    tree->rvalue = 1;
    if (tree->type == P_CHAR)
      tree->type = P_INT;
    tree = mkastunary(A_NEGATE, tree->type, tree->ctype, tree, NULL, 0);
    break;
  case T_INVERT:
    // 获取下一个 token 并递归解析为前缀表达式
    scan(&Token);
    tree = prefix(ptp);

    // 将 A_INVERT 操作前置到树并使子节点成为右值。
    tree->rvalue = 1;
    tree = mkastunary(A_INVERT, tree->type, tree->ctype, tree, NULL, 0);
    break;
  case T_LOGNOT:
    // 获取下一个 token 并递归解析为前缀表达式
    scan(&Token);
    tree = prefix(ptp);

    // 将 A_LOGNOT 操作前置到树并使子节点成为右值。
    tree->rvalue = 1;
    tree = mkastunary(A_LOGNOT, tree->type, tree->ctype, tree, NULL, 0);
    break;
  case T_INC:
    // 获取下一个 token 并递归解析为前缀表达式
    scan(&Token);
    tree = prefix(ptp);

    // 目前，确保它是一个标识符
    if (tree->op != A_IDENT)
      fatal("++ operator must be followed by an identifier");

    // 将 A_PREINC 操作前置到树
    tree = mkastunary(A_PREINC, tree->type, tree->ctype, tree, NULL, 0);
    break;
  case T_DEC:
    // 获取下一个 token 并递归解析为前缀表达式
    scan(&Token);
    tree = prefix(ptp);

    // 目前，确保它是一个标识符
    if (tree->op != A_IDENT)
      fatal("-- operator must be followed by an identifier");

    // 将 A_PREDEC 操作前置到树
    tree = mkastunary(A_PREDEC, tree->type, tree->ctype, tree, NULL, 0);
    break;
  default:
    tree = postfix(ptp);
  }
  return (tree);
}

// 返回一个以二元运算符为根的 AST 树。
// 参数 ptp 是前一个 token 的优先级。
struct ASTnode *binexpr(int ptp) {
  struct ASTnode *left, *right;
  struct ASTnode *ltemp, *rtemp;
  int ASTop;
  int tokentype;

  // 获取左边的树。
  // 同时获取下一个 token。
  left = prefix(ptp);

  // 如果我们遇到几个终止 token 中的一个，只返回左节点
  tokentype = Token.token;
  if (tokentype == T_SEMI || tokentype == T_RPAREN ||
      tokentype == T_RBRACKET || tokentype == T_COMMA ||
      tokentype == T_COLON || tokentype == T_RBRACE) {
    left->rvalue = 1;
    return (left);
  }
  // 当这个 token 的优先级高于前一个 token 的优先级时，
  // 或者它是右结合且等于前一个 token 的优先级时
  while ((op_precedence(tokentype) > ptp) ||
	 (rightassoc(tokentype) && op_precedence(tokentype) == ptp)) {
    // 获取下一个整数字面量
    scan(&Token);

    // 递归调用 binexpr() 并使用我们的 token 优先级来构建子树
    right = binexpr(OpPrec[tokentype]);

    // 确定要对子树执行的操作
    ASTop = binastop(tokentype);

    switch (ASTop) {
    case A_TERNARY:
      // 确保我们有一个 ':' token，扫描它之后的表达式
      match(T_COLON, ":");
      ltemp = binexpr(0);

      // 如果三元条件不是布尔操作，则强制其为布尔
      if (left->op != A_LOGOR && left->op != A_LOGAND &&
				(left->op < A_EQ || left->op > A_GE))
        left = mkastunary(A_TOBOOL, left->type, left->ctype, left, NULL, 0);

      // 为这个语句构建并返回 AST。使用中间表达式的类型作为返回类型。
      // 我们还应该考虑第三个表达式的类型。
      return (mkastnode
	      (A_TERNARY, right->type, right->ctype, left, right, ltemp,
	       NULL, 0));

    case A_ASSIGN:
      // 赋值
      // 将右树设为右值
      right->rvalue = 1;

      // 如果右树是 A_INTLIT 且左类型是 P_CHAR，
      // 且 INTLIT 在 0 到 255 范围内，将右的类型更改为 PCHAR 以确保我们可以执行赋值
      if ((right->op == A_INTLIT) && (left->type == P_CHAR) &&
	  (right->a_intvalue >= 0) && (right->a_intvalue < 256))
	right->type = P_CHAR;

      // 确保右的类型与左匹配
      right = modify_type(right, left->type, left->ctype, 0);
      if (right == NULL)
	fatal("Incompatible expression in assignment");

      // 构建一个赋值 AST 树。但是，交换左右，
      // 以便右表达式的代码将在左表达式之前生成
      ltemp = left;
      left = right;
      right = ltemp;
      break;

    default:
      // 我们不是在做三元或赋值，所以两个树都应该是右值。
      // 如果它们是左值树，则将它们转换为右值
      left->rvalue = 1;
      right->rvalue = 1;

      // 如果右树是 A_INTLIT 且左类型是 P_CHAR，
      // 且 INTLIT 在 0 到 255 范围内，将右的类型更改为 PCHAR 以确保我们可以执行操作
      if ((right->op == A_INTLIT) && (left->type == P_CHAR) &&
	  (right->a_intvalue >= 0) && (right->a_intvalue < 256))
	right->type = P_CHAR;

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

    // 将该子树与我们的树连接起来。同时将 token 转换为 AST 操作。
    left =
      mkastnode(binastop(tokentype), left->type, left->ctype, left, NULL,
		right, NULL, 0);

    // 一些操作符无论其操作数如何都会产生 int 结果
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

    // 更新当前 token 的详细信息。
    // 如果我们遇到终止 token，只返回左节点
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