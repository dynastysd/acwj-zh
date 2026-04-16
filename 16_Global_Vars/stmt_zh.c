#include "defs.h"
#include "data.h"
#include "decl.h"

// 语句解析
// Copyright (c) 2019 Warren Toomey, GPL3

// 原型
static struct ASTnode *single_statement(void);

// compound_statement:          // 空，即无语句
//      |      statement
//      |      statement statements
//      ;
//
// statement: print_statement
//      |     declaration
//      |     assignment_statement
//      |     function_call
//      |     if_statement
//      |     while_statement
//      |     for_statement
//      |     return_statement
//      ;

// print_statement: 'print' expression ';'  ;
//
static struct ASTnode *print_statement(void) {
  struct ASTnode *tree;
  int lefttype, righttype;

  // 匹配 'print' 作为第一个标记
  match(T_PRINT, "print");

  // 解析后续表达式
  tree = binexpr(0);

  // 确保两种类型兼容。
  lefttype = P_INT;
  righttype = tree->type;
  if (!type_compatible(&lefttype, &righttype, 0))
    fatal("Incompatible types");

  // 如有需要，加宽树。
  if (righttype)
    tree = mkastunary(righttype, P_INT, tree, 0);

  // 创建一个 print AST 树
  tree = mkastunary(A_PRINT, P_NONE, tree, 0);

  // 返回 AST
  return (tree);
}

// assignment_statement: identifier '=' expression ';'   ;
//
// 解析赋值语句并返回其 AST
static struct ASTnode *assignment_statement(void) {
  struct ASTnode *left, *right, *tree;
  int lefttype, righttype;
  int id;

  // 确保我们有一个标识符
  ident();

  // 这可能是变量或函数调用。
  // 如果下一个标记是 '('，则是函数调用
  if (Token.token == T_LPAREN)
    return (funccall());

  // 不是函数调用，那就继续进行赋值！
  // 检查标识符是否已定义，然后为其创建一个叶子节点
  // XXX 添加结构类型测试
  if ((id = findglob(Text)) == -1) {
    fatals("Undeclared variable", Text);
  }
  right = mkastleaf(A_LVIDENT, Gsym[id].type, id);

  // 确保我们有一个等号
  match(T_ASSIGN, "=");

  // 解析后续表达式
  left = binexpr(0);

  // 确保两种类型兼容。
  lefttype = left->type;
  righttype = right->type;
  if (!type_compatible(&lefttype, &righttype, 1))
    fatal("Incompatible types");

  // 如有需要，加宽左侧。
  if (lefttype)
    left = mkastunary(lefttype, right->type, left, 0);

  // 创建一个赋值 AST 树
  tree = mkastnode(A_ASSIGN, P_INT, left, NULL, right, 0);

  // 返回 AST
  return (tree);
}

// if_statement: if_head
//      |        if_head 'else' compound_statement
//      ;
//
// if_head: 'if' '(' true_false_expression ')' compound_statement  ;
//
// 解析 IF 语句包括任何
// 可选的 ELSE 子句并返回其 AST
static struct ASTnode *if_statement(void) {
  struct ASTnode *condAST, *trueAST, *falseAST = NULL;

  // 确保我们有 'if' '('
  match(T_IF, "if");
  lparen();

  // 解析后续表达式
  // 和后面的 ')'。确保
  // 树的操是比较操作。
  condAST = binexpr(0);
  if (condAST->op < A_EQ || condAST->op > A_GE)
    fatal("Bad comparison operator");
  rparen();

  // 获取复合语句的 AST
  trueAST = compound_statement();

  // 如果我们有 'else'，跳过它
  // 并获取复合语句的 AST
  if (Token.token == T_ELSE) {
    scan(&Token);
    falseAST = compound_statement();
  }
  // 为此语句构建并返回 AST
  return (mkastnode(A_IF, P_NONE, condAST, trueAST, falseAST, 0));
}


// while_statement: 'while' '(' true_false_expression ')' compound_statement  ;
//
// 解析 WHILE 语句并返回其 AST
static struct ASTnode *while_statement(void) {
  struct ASTnode *condAST, *bodyAST;

  // 确保我们有 'while' '('
  match(T_WHILE, "while");
  lparen();

  // 解析后续表达式
  // 和后面的 ')'。确保
  // 树的操是比较操作。
  condAST = binexpr(0);
  if (condAST->op < A_EQ || condAST->op > A_GE)
    fatal("Bad comparison operator");
  rparen();

  // 获取复合语句的 AST
  bodyAST = compound_statement();

  // 构建并返回此语句的 AST
  return (mkastnode(A_WHILE, P_NONE, condAST, NULL, bodyAST, 0));
}

// for_statement: 'for' '(' preop_statement ';'
//                          true_false_expression ';'
//                          postop_statement ')' compound_statement  ;
//
// preop_statement:  statement          (目前)
// postop_statement: statement          (目前)
//
// 解析 FOR 语句并返回其 AST
static struct ASTnode *for_statement(void) {
  struct ASTnode *condAST, *bodyAST;
  struct ASTnode *preopAST, *postopAST;
  struct ASTnode *tree;

  // 确保我们有 'for' '('
  match(T_FOR, "for");
  lparen();

  // 获取 pre_op 语句和 ';'
  preopAST = single_statement();
  semi();

  // 获取条件和 ';'
  condAST = binexpr(0);
  if (condAST->op < A_EQ || condAST->op > A_GE)
    fatal("Bad comparison operator");
  semi();

  // 获取 post_op 语句和 ')'
  postopAST = single_statement();
  rparen();

  // 获取作为主体的复合语句
  bodyAST = compound_statement();

  // 目前，所有四个子树都必须非空。
  // 稍后，当我们缺少一些时，我们将更改语义

  // 将复合语句和 postop 树粘合在一起
  tree = mkastnode(A_GLUE, P_NONE, bodyAST, NULL, postopAST, 0);

  // 使用条件和新主体创建一个 WHILE 循环
  tree = mkastnode(A_WHILE, P_NONE, condAST, NULL, tree, 0);

  // 并将 preop 树粘合到 A_WHILE 树
  return (mkastnode(A_GLUE, P_NONE, preopAST, NULL, tree, 0));
}

// return_statement: 'return' '(' expression ')'  ;
//
// 解析 return 语句并返回其 AST
static struct ASTnode *return_statement(void) {
  struct ASTnode *tree;
  int returntype, functype;

  // 如果函数返回 P_VOID，则不能返回值
  if (Gsym[Functionid].type == P_VOID)
    fatal("Can't return from a void function");

  // 确保我们有 'return' '('
  match(T_RETURN, "return");
  lparen();

  // 解析后续表达式
  tree = binexpr(0);

  // 确保这与函数的类型兼容
  returntype = tree->type;
  functype = Gsym[Functionid].type;
  if (!type_compatible(&returntype, &functype, 1))
    fatal("Incompatible types");

  // 如有需要，加宽左侧。
  if (returntype)
    tree = mkastunary(returntype, functype, tree, 0);

  // 添加 A_RETURN 节点
  tree = mkastunary(A_RETURN, P_NONE, tree, 0);

  // 获取 ')'
  rparen();
  return (tree);
}

// 解析单个语句并返回其 AST
static struct ASTnode *single_statement(void) {
  int type;

  switch (Token.token) {
    case T_PRINT:
      return (print_statement());
    case T_CHAR:
    case T_INT:
    case T_LONG:

      // 变量声明的开始。
      // 解析类型并获取标识符。
      // 然后解析声明的其余部分。
      // XXX：这些目前是全局的。
      type = parse_type();
      ident();
      var_declaration(type);
      return (NULL);		// 这里没有生成 AST
    case T_IDENT:
      return (assignment_statement());
    case T_IF:
      return (if_statement());
    case T_WHILE:
      return (while_statement());
    case T_FOR:
      return (for_statement());
    case T_RETURN:
      return (return_statement());
    default:
      fatald("Syntax error, token", Token.token);
  }
  return (NULL);		// 保持 -Wall 高兴
}

// 解析复合语句
// 并返回其 AST
struct ASTnode *compound_statement(void) {
  struct ASTnode *left = NULL;
  struct ASTnode *tree;

  // 需要左花括号
  lbrace();

  while (1) {
    // 解析单个语句
    tree = single_statement();

    // 有些语句后面必须跟分号
    if (tree != NULL && (tree->op == A_PRINT || tree->op == A_ASSIGN ||
			 tree->op == A_RETURN || tree->op == A_FUNCCALL))
      semi();

    // 对于每个新树，如果左树为空，
    // 则保存在左树中，否则将左树
    // 和新树粘合在一起
    if (tree != NULL) {
      if (left == NULL)
	left = tree;
      else
	left = mkastnode(A_GLUE, P_NONE, left, NULL, tree, 0);
    }
    // 当遇到右花括号时，
    // 跳过它并返回 AST
    if (Token.token == T_RBRACE) {
      rbrace();
      return (left);
    }
  }
}