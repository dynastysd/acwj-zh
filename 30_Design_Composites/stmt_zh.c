#include "defs.h"
#include "data.h"
#include "decl.h"

// 语句解析
// Copyright (c) 2019 Warren Toomey, GPL3

// 原型
static struct ASTnode *single_statement(void);

// compound_statement:          // 空，即没有语句
//      |      statement
//      |      statement statements
//      ;
//
// statement: declaration
//      |     expression_statement
//      |     function_call
//      |     if_statement
//      |     while_statement
//      |     for_statement
//      |     return_statement
//      ;


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
  // 和后面的 ')'。强制
  // 非比较为布尔值
  // 树的操作为比较。
  condAST = binexpr(0);
  if (condAST->op < A_EQ || condAST->op > A_GE)
    condAST = mkastunary(A_TOBOOL, condAST->type, condAST, NULL, 0);
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
  return (mkastnode(A_IF, P_NONE, condAST, trueAST, falseAST, NULL, 0));
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
  // 和后面的 ')'。强制
  // 非比较为布尔值
  // 树的操作为比较。
  condAST = binexpr(0);
  if (condAST->op < A_EQ || condAST->op > A_GE)
    condAST = mkastunary(A_TOBOOL, condAST->type, condAST, NULL, 0);
  rparen();

  // 获取复合语句的 AST
  bodyAST = compound_statement();

  // 为此语句构建并返回 AST
  return (mkastnode(A_WHILE, P_NONE, condAST, NULL, bodyAST, NULL, 0));
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

  // 获取条件和 ';'。
  // 强制非比较为布尔值
  // 树的操作为比较。
  condAST = binexpr(0);
  if (condAST->op < A_EQ || condAST->op > A_GE)
    condAST = mkastunary(A_TOBOOL, condAST->type, condAST, NULL, 0);
  semi();

  // 获取 post_op 语句和 ')'
  postopAST = single_statement();
  rparen();

  // 获取作为循环体的复合语句
  bodyAST = compound_statement();

  // 目前，所有四个子树都不能为 NULL。
  // 稍后，我们将更改某些缺失时的语义

  // 将复合语句和 postop 树粘合在一起
  tree = mkastnode(A_GLUE, P_NONE, bodyAST, NULL, postopAST, NULL, 0);

  // 用这个新 body 创建一个 WHILE 循环
  tree = mkastnode(A_WHILE, P_NONE, condAST, NULL, tree, NULL, 0);

  // 将 preop 树粘合到 A_WHILE 树
  return (mkastnode(A_GLUE, P_NONE, preopAST, NULL, tree, NULL, 0));
}

// return_statement: 'return' '(' expression ')'  ;
//
// 解析 return 语句并返回其 AST
static struct ASTnode *return_statement(void) {
  struct ASTnode *tree;

  // 如果函数返回 P_VOID，则不能返回值
  if (Functionid->type == P_VOID)
    fatal("Can't return from a void function");

  // 确保我们有 'return' '('
  match(T_RETURN, "return");
  lparen();

  // 解析后续表达式
  tree = binexpr(0);

  // 确保这与函数的类型兼容
  tree = modify_type(tree, Functionid->type, 0);
  if (tree == NULL)
    fatal("Incompatible type to return");

  // 添加 A_RETURN 节点
  tree = mkastunary(A_RETURN, P_NONE, tree, NULL, 0);

  // 获取 ')'
  rparen();
  return (tree);
}

// 解析单个语句并返回其 AST
static struct ASTnode *single_statement(void) {
  int type;

  switch (Token.token) {
    case T_CHAR:
    case T_INT:
    case T_LONG:

      // 变量声明的开始。
      // 解析类型并获取标识符。
      // 然后解析声明的其余部分
      // 并跳过冒号
      type = parse_type();
      ident();
      var_declaration(type, C_LOCAL);
      semi();
      return (NULL);		// 这里没有生成 AST
    case T_IF:
      return (if_statement());
    case T_WHILE:
      return (while_statement());
    case T_FOR:
      return (for_statement());
    case T_RETURN:
      return (return_statement());
    default:
      // 目前，看看这是否是表达式。
      // 这捕获赋值语句。
      return (binexpr(0));
  }
  return (NULL);		// 保持 -Wall 愉快
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

    // 某些语句后面必须跟分号
    if (tree != NULL && (tree->op == A_ASSIGN ||
			 tree->op == A_RETURN || tree->op == A_FUNCCALL))
      semi();

    // 对于每个新树，如果 left 为空则保存它，
    // 否则将 left 和新树粘合在一起
    if (tree != NULL) {
      if (left == NULL)
	left = tree;
      else
	left = mkastnode(A_GLUE, P_NONE, left, NULL, tree, NULL, 0);
    }
    // 当遇到右花括号时，
    // 跳过它并返回 AST
    if (Token.token == T_RBRACE) {
      rbrace();
      return (left);
    }
  }
}