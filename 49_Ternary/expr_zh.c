#include "defs.h"
#include "data.h"
#include "decl.h"

// 表达式解析
// Copyright (c) 2019 Warren Toomey, GPL3

// expression_list: <null>
//        | expression
//        | expression ',' expression_list
//        ;

// 解析零个或多个逗号分隔的表达式列表，并
// 返回由A_GLUE节点组成的AST，左子节点
// 是先前表达式的子树（或NULL），右子节点
// 是下一个表达式。每个A_GLUE节点将具有大小字段
// 设置为此时树中表达式的数量。如果没有
// 解析表达式，则返回NULL
struct ASTnode *expression_list(int endtoken) {
  struct ASTnode *tree = NULL;
  struct ASTnode *child = NULL;
  int exprcount = 0;

  // 循环直到结束词素
  while (Token.token != endtoken) {

    // 解析下一个表达式并增加表达式计数
    child = binexpr(0);
    exprcount++;

    // 使用先前的树作为左子节点构建A_GLUE AST节点
    // 新的表达式作为右子节点。存储表达式计数。
    tree = mkastnode(A_GLUE, P_NONE, tree, NULL, child, NULL, exprcount);

    // 当达到结束词素时停止
    if (Token.token == endtoken)
      break;

    // 此时必须有','
    match(T_COMMA, ",");
  }

  // 返回表达式树
  return (tree);
}

// 解析函数调用并返回其AST
static struct ASTnode *funccall(void) {
  struct ASTnode *tree;
  struct symtable *funcptr;

  // 检查标识符是否已定义为函数，
  // 然后为其创建一个叶子节点。
  if ((funcptr = findsymbol(Text)) == NULL || funcptr->stype != S_FUNCTION) {
    fatals("Undeclared function", Text);
  }
  // 获取'('
  lparen();

  // 解析参数表达式列表
  tree = expression_list(T_RPAREN);

  // XXX 检查每个参数的类型与函数的原型匹配

  // 构建函数调用AST节点。存储
  // 函数的返回类型作为此节点的类型。
  // 还记录函数的符号id
  tree = mkastunary(A_FUNCCALL, funcptr->type, tree, funcptr, 0);

  // 获取')'
  rparen();
  return (tree);
}

// 解析数组索引并返回其AST树
static struct ASTnode *array_access(void) {
  struct ASTnode *left, *right;
  struct symtable *aryptr;

  // 检查标识符是否已定义为数组
  // 然后创建一个指向基址的叶子节点
  if ((aryptr = findsymbol(Text)) == NULL || aryptr->stype != S_ARRAY) {
    fatals("Undeclared array", Text);
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
    fatal("Array index is not of integer type");

  // 按元素类型的大小缩放索引
  right = modify_type(right, left->type, A_ADD);

  // 返回一个AST树，其中数组的基址加上偏移量
  // 并解引用元素。此时仍是左值。
  left = mkastnode(A_ADD, aryptr->type, left, NULL, right, NULL, 0);
  left = mkastunary(A_DEREF, value_at(left->type), left, NULL, 0);
  return (left);
}

// 解析struct或union的成员引用
// 并返回其AST树。如果withpointer为true，
// 则通过指针访问成员。
static struct ASTnode *member_access(int withpointer) {
  struct ASTnode *left, *right;
  struct symtable *compvar;
  struct symtable *typeptr;
  struct symtable *m;

  // 检查标识符是否已声明为
  // struct/union或struct/union指针
  if ((compvar = findsymbol(Text)) == NULL)
    fatals("Undeclared variable", Text);
  if (withpointer && compvar->type != pointer_to(P_STRUCT)
      && compvar->type != pointer_to(P_UNION))
    fatals("Undeclared variable", Text);
  if (!withpointer && compvar->type != P_STRUCT && compvar->type != P_UNION)
    fatals("Undeclared variable", Text);

  // 如果是指向struct/union的指针，获取指针的值。
  // 否则，创建一个指向基址的叶子节点。
  // 无论哪种方式，它都是一个右值
  if (withpointer) {
    left = mkastleaf(A_IDENT, pointer_to(compvar->type), compvar, 0);
  } else
    left = mkastleaf(A_ADDR, compvar->type, compvar, 0);
  left->rvalue = 1;

  // 获取组合类型的信息
  typeptr = compvar->ctype;

  // 跳过'.'或'->'词素并获取成员名称
  scan(&Token);
  ident();

  // 在类型中找到匹配的成员名称
  // 如果找不到则报错
  for (m = typeptr->member; m != NULL; m = m->next)
    if (!strcmp(m->name, Text))
      break;

  if (m == NULL)
    fatals("No member found in struct/union: ", Text);

  // 构建一个带有成员偏移量的A_INTLIT节点
  right = mkastleaf(A_INTLIT, P_INT, NULL, m->st_posn);

  // 将成员的偏移量加到struct/union的基址上
  // 并解引用。此时仍是左值
  left = mkastnode(A_ADD, pointer_to(m->type), left, NULL, right, NULL, 0);
  left = mkastunary(A_DEREF, m->type, left, NULL, 0);
  return (left);
}

// 解析后缀表达式并返回
// 表示它的AST节点。标识符已在Text中。
static struct ASTnode *postfix(void) {
  struct ASTnode *n;
  struct symtable *varptr;
  struct symtable *enumptr;

  // 如果标识符匹配枚举值，
  // 返回A_INTLIT节点
  if ((enumptr = findenumval(Text)) != NULL) {
    scan(&Token);
    return (mkastleaf(A_INTLIT, P_INT, NULL, enumptr->st_posn));
  }
  // 扫描下一个词素以查看是否有后缀表达式
  scan(&Token);

  // 函数调用
  if (Token.token == T_LPAREN)
    return (funccall());

  // 数组引用
  if (Token.token == T_LBRACKET)
    return (array_access());

  // 访问struct或union
  if (Token.token == T_DOT)
    return (member_access(0));
  if (Token.token == T_ARROW)
    return (member_access(1));

  // 变量。检查变量是否存在。
  if ((varptr = findsymbol(Text)) == NULL || varptr->stype != S_VARIABLE)
    fatals("Unknown variable", Text);

  switch (Token.token) {
    // 后递增：跳过词素
  case T_INC:
    scan(&Token);
    n = mkastleaf(A_POSTINC, varptr->type, varptr, 0);
    break;

    // 后递减：跳过词素
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

// 解析初等因子并返回
// 表示它的AST节点。
static struct ASTnode *primary(void) {
  struct ASTnode *n;
  int id;
  int type = 0;
  int size, class;
  struct symtable *ctype;

  switch (Token.token) {
  case T_STATIC:
  case T_EXTERN:
    fatal("Compiler doesn't support static or extern local declarations");
  case T_SIZEOF:
    // 跳过T_SIZEOF并确保有左括号
    scan(&Token);
    if (Token.token != T_LPAREN)
      fatal("Left parenthesis expected after sizeof");
    scan(&Token);

    // 获取括号内的类型
    type = parse_stars(parse_type(&ctype, &class));
    // 获取类型的大小
    size = typesize(type, ctype);
    rparen();
    // 返回带有大小的叶子节点整数常量
    return (mkastleaf(A_INTLIT, P_INT, NULL, size));

  case T_INTLIT:
    // 对于INTLIT词素，为其创建一个叶子AST节点。
    // 如果在P_CHAR范围内则使其为P_CHAR
    if (Token.intvalue >= 0 && Token.intvalue < 256)
      n = mkastleaf(A_INTLIT, P_CHAR, NULL, Token.intvalue);
    else
      n = mkastleaf(A_INTLIT, P_INT, NULL, Token.intvalue);
    break;

  case T_STRLIT:
    // 对于STRLIT词素，为其生成汇编代码。
    // 然后为其创建一个叶子AST节点。id是字符串的标签。
    id = genglobstr(Text);
    n = mkastleaf(A_STRLIT, pointer_to(P_CHAR), NULL, id);
    break;

  case T_IDENT:
    return (postfix());

  case T_LPAREN:
    // 括号表达式的开始，跳过'('。
    scan(&Token);


    // 如果后面的词素是类型标识符，这是强制转换表达式
    switch (Token.token) {
    case T_IDENT:
      // 我们需要查看标识符是否匹配typedef。
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
      type = parse_cast();
      // 跳过')'然后解析后续表达式
      rparen();

    default:
      n = binexpr(0);		// 扫描表达式
    }

    // 我们现在在n中至少有一个表达式，如果有强制转换则可能在type中有非零类型
    // 如果没有强制转换则跳过')'，如果有强制转换则不跳过。
    if (type == 0)
      rparen();
    else
      // 否则，为强制转换创建一个一元AST节点
      n = mkastunary(A_CAST, type, n, NULL, 0);
    return (n);

  default:
    fatals("Expecting a primary expression, got token", Token.tokstr);
  }

  // 扫描下一个词素并返回叶子节点
  scan(&Token);
  return (n);
}

// 将二元运算符词素转换为二元AST操作。
// 我们依赖从词素到AST操作的1:1映射
static int binastop(int tokentype) {
  if (tokentype > T_EOF && tokentype <= T_SLASH)
    return (tokentype);
  fatald("Syntax error, token", tokentype);
  return (0);			// 保持-Wall愉快
}

// 如果词素是右结合的则返回true，
// 否则返回false。
static int rightassoc(int tokentype) {
  if (tokentype >= T_ASSIGN && tokentype <= T_ASSLASH)
    return (1);
  return (0);
}

// 每个词素的运算符优先级。必须
// 与defs.h中的词素顺序相匹配
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

// 检查我们是否有二元运算符并
// 返回其优先级。
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
    // 获取下一个词素并递归
    // 解析为前缀表达式
    scan(&Token);
    tree = prefix();

    // 确保它是一个标识符
    if (tree->op != A_IDENT)
      fatal("& operator must be followed by an identifier");

    // 现在将操作符更改为A_ADDR并将类型更改为
    // 指向原始类型的指针
    tree->op = A_ADDR;
    tree->type = pointer_to(tree->type);
    break;
  case T_STAR:
    // 获取下一个词素并递归
    // 解析为前缀表达式
    scan(&Token);
    tree = prefix();

    // 目前，确保它是另一个解引用或
    // 标识符
    if (tree->op != A_IDENT && tree->op != A_DEREF)
      fatal("* operator must be followed by an identifier or *");

    // 将A_DEREF操作添加到树的开头
    tree = mkastunary(A_DEREF, value_at(tree->type), tree, NULL, 0);
    break;
  case T_MINUS:
    // 获取下一个词素并递归
    // 解析为前缀表达式
    scan(&Token);
    tree = prefix();

    // 将A_NEGATE操作添加到树的开头并
    // 使子节点成为右值。因为char是无符号的，
    // 还要将其扩展为int以使其为有符号
    tree->rvalue = 1;
    tree = modify_type(tree, P_INT, 0);
    tree = mkastunary(A_NEGATE, tree->type, tree, NULL, 0);
    break;
  case T_INVERT:
    // 获取下一个词素并递归
    // 解析为前缀表达式
    scan(&Token);
    tree = prefix();

    // 将A_INVERT操作添加到树的开头并
    // 使子节点成为右值。
    tree->rvalue = 1;
    tree = mkastunary(A_INVERT, tree->type, tree, NULL, 0);
    break;
  case T_LOGNOT:
    // 获取下一个词素并递归
    // 解析为前缀表达式
    scan(&Token);
    tree = prefix();

    // 将A_LOGNOT操作添加到树的开头并
    // 使子节点成为右值。
    tree->rvalue = 1;
    tree = mkastunary(A_LOGNOT, tree->type, tree, NULL, 0);
    break;
  case T_INC:
    // 获取下一个词素并递归
    // 解析为前缀表达式
    scan(&Token);
    tree = prefix();

    // 目前，确保它是一个标识符
    if (tree->op != A_IDENT)
      fatal("++ operator must be followed by an identifier");

    // 将A_PREINC操作添加到树的开头
    tree = mkastunary(A_PREINC, tree->type, tree, NULL, 0);
    break;
  case T_DEC:
    // 获取下一个词素并递归
    // 解析为前缀表达式
    scan(&Token);
    tree = prefix();

    // 目前，确保它是一个标识符
    if (tree->op != A_IDENT)
      fatal("-- operator must be followed by an identifier");

    // 将A_PREDEC操作添加到树的开头
    tree = mkastunary(A_PREDEC, tree->type, tree, NULL, 0);
    break;
  default:
    tree = primary();
  }
  return (tree);
}

// 返回以二元运算符为根的AST树。
// 参数ptp是先前词素的优先级。
struct ASTnode *binexpr(int ptp) {
  struct ASTnode *left, *right;
  struct ASTnode *ltemp, *rtemp;
  int ASTop;
  int tokentype;

  // 获取左边的树。
  // 同时获取下一个词素。
  left = prefix();

  // 如果遇到多个终止词素之一，则只返回左节点
  tokentype = Token.token;
  if (tokentype == T_SEMI || tokentype == T_RPAREN ||
      tokentype == T_RBRACKET || tokentype == T_COMMA ||
      tokentype == T_COLON || tokentype == T_RBRACE) {
    left->rvalue = 1;
    return (left);
  }
  // 当此词素的优先级大于先前词素的优先级时，
  // 或者它是右结合且等于先前词素的优先级时
  while ((op_precedence(tokentype) > ptp) ||
	 (rightassoc(tokentype) && op_precedence(tokentype) == ptp)) {
    // 获取下一个整数常量
    scan(&Token);

    // 递归调用binexpr()并使用
    // 我们词素的优先级来构建子树
    right = binexpr(OpPrec[tokentype]);

    // 确定要对子树执行的操作
    ASTop = binastop(tokentype);

    switch (ASTop) {
    case A_TERNARY:
      // 确保有':'词素，扫描其后的表达式
      match(T_COLON, ":");
      ltemp= binexpr(0);

      // 为此语句构建并返回AST。使用中间
      // 表达式的类型作为返回类型。XXX 我们还应该
      // 考虑第三个表达式的类型。
      return (mkastnode(A_TERNARY, right->type, left, right, ltemp, NULL, 0));

    case A_ASSIGN:
      // 赋值
      // 使右树成为右值
      right->rvalue = 1;

      // 确保右边的类型与左边匹配
      right = modify_type(right, left->type, 0);
      if (right == NULL)
	fatal("Incompatible expression in assignment");

      // 做一个赋值AST树。但是，交换
      // 左右，这样右表达式的 
      // 代码将在左表达式之前生成。
      ltemp = left;
      left = right;
      right = ltemp;
      break;

    default:
      // 我们不是在做三元或赋值，所以两个树都应该是
      // 右值。如果它们是左值树则转换为右值
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

    // 将该子树与我们的一起连接。同时
    // 将词素转换为AST操作。
    left =
      mkastnode(binastop(tokentype), left->type, left, NULL, right, NULL, 0);

    // 更新当前词素的详细信息。
    // 如果遇到终止词素，则只返回左节点
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