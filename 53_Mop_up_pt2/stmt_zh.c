#include "defs.h"
#include "data.h"
#include "decl.h"

// 语句解析
// Copyright (c) 2019 Warren Toomey, GPL3

// 原型
static struct ASTnode *single_statement(void);

// compound_statement:          // empty, i.e. no statement
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
//      |        if_head 'else' statement
//      ;
//
// if_head: 'if' '(' true_false_expression ')' statement  ;
//
// 解析IF语句包括任何
// 可选的ELSE子句并返回其AST
static struct ASTnode *if_statement(void) {
  struct ASTnode *condAST, *trueAST, *falseAST = NULL;

  // 确保我们有'if' '('
  match(T_IF, "if");
  lparen();

  // 解析后续表达式和后面的')'。
  // 强制非比较为布尔值。
  // 树的操作为比较。
  condAST = binexpr(0);
  if (condAST->op < A_EQ || condAST->op > A_GE)
    condAST =
      mkastunary(A_TOBOOL, condAST->type, condAST->ctype, condAST, NULL, 0);
  rparen();

  // 获取语句的AST
  trueAST = single_statement();

  // 如果我们有'else'，跳过它
  // 并获取语句的AST
  if (Token.token == T_ELSE) {
    scan(&Token);
    falseAST = single_statement();
  }
  // 为此语句构建并返回AST
  return (mkastnode(A_IF, P_NONE, NULL, condAST, trueAST, falseAST, NULL, 0));
}


// while_statement: 'while' '(' true_false_expression ')' statement  ;
//
// 解析WHILE语句并返回其AST
static struct ASTnode *while_statement(void) {
  struct ASTnode *condAST, *bodyAST;

  // 确保我们有'while' '('
  match(T_WHILE, "while");
  lparen();

  // 解析后续表达式和后面的')'。
  // 强制非比较为布尔值。
  // 树的操作为比较。
  condAST = binexpr(0);
  if (condAST->op < A_EQ || condAST->op > A_GE)
    condAST =
      mkastunary(A_TOBOOL, condAST->type, condAST->ctype, condAST, NULL, 0);
  rparen();

  // 获取语句的AST。
  // 在此过程中更新循环深度
  Looplevel++;
  bodyAST = single_statement();
  Looplevel--;

  // 为此语句构建并返回AST
  return (mkastnode(A_WHILE, P_NONE, NULL, condAST, NULL, bodyAST, NULL, 0));
}

// for_statement: 'for' '(' expression_list ';'
//                          true_false_expression ';'
//                          expression_list ')' statement  ;
//
// 解析FOR语句并返回其AST
static struct ASTnode *for_statement(void) {
  struct ASTnode *condAST, *bodyAST;
  struct ASTnode *preopAST, *postopAST;
  struct ASTnode *tree;

  // 确保我们有'for' '('
  match(T_FOR, "for");
  lparen();

  // 获取pre_op表达式和';'
  preopAST = expression_list(T_SEMI);
  semi();

  // 获取条件和';'。
  // 强制非比较为布尔值。
  // 树的操作为比较。
  condAST = binexpr(0);
  if (condAST->op < A_EQ || condAST->op > A_GE)
    condAST =
      mkastunary(A_TOBOOL, condAST->type, condAST->ctype, condAST, NULL, 0);
  semi();

  // 获取post_op表达式和')'
  postopAST = expression_list(T_RPAREN);
  rparen();

  // 获取作为循环体的语句
  // 在此过程中更新循环深度
  Looplevel++;
  bodyAST = single_statement();
  Looplevel--;

  // 将语句和postop树粘合在一起
  tree = mkastnode(A_GLUE, P_NONE, NULL, bodyAST, NULL, postopAST, NULL, 0);

  // 用这个新body制作WHILE循环
  tree = mkastnode(A_WHILE, P_NONE, NULL, condAST, NULL, tree, NULL, 0);

  // 将preop树粘合到A_WHILE树
  return (mkastnode(A_GLUE, P_NONE, NULL, preopAST, NULL, tree, NULL, 0));
}

// return_statement: 'return' '(' expression ')'  ;
//
// 解析return语句并返回其AST
static struct ASTnode *return_statement(void) {
  struct ASTnode *tree= NULL;

  // 确保我们有'return'
  match(T_RETURN, "return");

  // 查看我们是否有返回值
  if (Token.token == T_LPAREN) {
    // 如果函数返回P_VOID，不能返回值
    if (Functionid->type == P_VOID)
      fatal("Can't return from a void function");

    // 跳过左括号
    lparen();

    // 解析后续表达式
    tree = binexpr(0);

    // 确保这与函数的类型兼容
    tree = modify_type(tree, Functionid->type, Functionid->ctype, 0);
    if (tree == NULL)
      fatal("Incompatible type to return");

    // 获取')'
    rparen();
  } else {
    if (Functionid->type != P_VOID)
      fatal("Must return a value from a non-void function");
  }

  // 添加A_RETURN节点
  tree = mkastunary(A_RETURN, P_NONE, NULL, tree, NULL, 0);

  // 获取';'
  semi();
  return (tree);
}

// break_statement: 'break' ;
//
// 解析break语句并返回其AST
static struct ASTnode *break_statement(void) {

  if (Looplevel == 0 && Switchlevel == 0)
    fatal("no loop or switch to break out from");
  scan(&Token);
  semi();
  return (mkastleaf(A_BREAK, P_NONE, NULL, NULL, 0));
}

// continue_statement: 'continue' ;
//
// 解析continue语句并返回其AST
static struct ASTnode *continue_statement(void) {

  if (Looplevel == 0)
    fatal("no loop to continue to");
  scan(&Token);
  semi();
  return (mkastleaf(A_CONTINUE, P_NONE, NULL, NULL, 0));
}

// 解析switch语句并返回其AST
static struct ASTnode *switch_statement(void) {
  struct ASTnode *left, *body, *n, *c;
  struct ASTnode *casetree = NULL, *casetail;
  int inloop = 1, casecount = 0;
  int seendefault = 0;
  int ASTop, casevalue;

  // 跳过'switch'和'('
  scan(&Token);
  lparen();

  // 获取switch表达式、')'和'{'
  left = binexpr(0);
  rparen();
  lbrace();

  // 确保这是int类型
  if (!inttype(left->type))
    fatal("Switch expression is not of integer type");

  // 用表达式作为子节点构建A_SWITCH子树
  n = mkastunary(A_SWITCH, P_NONE, NULL, left, NULL, 0);

  // 现在解析cases
  Switchlevel++;
  while (inloop) {
    switch (Token.token) {
	// 遇到'}'时离开循环
      case T_RBRACE:
	if (casecount == 0)
	  fatal("No cases in switch");
	inloop = 0;
	break;
      case T_CASE:
      case T_DEFAULT:
	// 确保这不在之前的'default'之后
	if (seendefault)
	  fatal("case or default after existing default");

	// 设置AST操作。如需要，扫描case值
	if (Token.token == T_DEFAULT) {
	  ASTop = A_DEFAULT;
	  seendefault = 1;
	  scan(&Token);
	} else {
	  ASTop = A_CASE;
	  scan(&Token);
	  left = binexpr(0);
	  // 确保case值是整数字面量
	  if (left->op != A_INTLIT)
	    fatal("Expecting integer literal for case value");
	  casevalue = left->a_intvalue;

	  // 遍历现有case值列表以确保
	  // 没有重复的case值
	  for (c = casetree; c != NULL; c = c->right)
	    if (casevalue == c->a_intvalue)
	      fatal("Duplicate case value");
	}

	// 扫描':'并增加casecount
	match(T_COLON, ":");
	casecount++;

	// 如果下一个标记是T_CASE，现有case将落入
	// 下一个case。否则，解析case body。
	if (Token.token == T_CASE)
	  body = NULL;
	else
	  body = compound_statement(1);

	// 用任何复合语句作为左子节点构建子树，
	// 并将其链接到增长的A_CASE树
	if (casetree == NULL) {
	  casetree = casetail =
	    mkastunary(ASTop, P_NONE, NULL, body, NULL, casevalue);
	} else {
	  casetail->right =
	    mkastunary(ASTop, P_NONE, NULL, body, NULL, casevalue);
	  casetail = casetail->right;
	}
	break;
      default:
	fatals("Unexpected token in switch", Token.tokstr);
    }
  }
  Switchlevel--;

  // 我们有一个带有cases和任何default的子树。将
  // case数放入A_SWITCH节点并附加case树。
  n->a_intvalue = casecount;
  n->right = casetree;
  rbrace();

  return (n);
}

// 解析单个语句并返回其AST。
static struct ASTnode *single_statement(void) {
  struct ASTnode *stmt;
  struct symtable *ctype;

  switch (Token.token) {
    case T_SEMI:
      // 空语句
      semi();
      break;
    case T_LBRACE:
      // 我们有'{'，所以这是复合语句
      lbrace();
      stmt = compound_statement(0);
      rbrace();
      return (stmt);
    case T_IDENT:
      // 我们必须查看标识符是否匹配typedef。
      // 如果不是，则将其视为表达式。
      // 否则，下降到parse_type()调用。
      if (findtypedef(Text) == NULL) {
	stmt = binexpr(0);
	semi();
	return (stmt);
      }
    case T_CHAR:
    case T_INT:
    case T_LONG:
    case T_STRUCT:
    case T_UNION:
    case T_ENUM:
    case T_TYPEDEF:
      // 变量声明列表的开始。
      declaration_list(&ctype, C_LOCAL, T_SEMI, T_EOF, &stmt);
      semi();
      return (stmt);		// 来自声明的任何赋值
    case T_IF:
      return (if_statement());
    case T_WHILE:
      return (while_statement());
    case T_FOR:
      return (for_statement());
    case T_RETURN:
      return (return_statement());
    case T_BREAK:
      return (break_statement());
    case T_CONTINUE:
      return (continue_statement());
    case T_SWITCH:
      return (switch_statement());
    default:
      // 目前，看看这是不是表达式。
      // 这捕获赋值语句。
      stmt = binexpr(0);
      semi();
      return (stmt);
  }
  return (NULL);		// 保持-Wall满意
}

// 解析复合语句并返回其AST。
// 如果inswitch为true，
// 我们查找'}'、'case'或'default'标记
// 来结束解析。否则，只查找
// '}'来结束解析。
struct ASTnode *compound_statement(int inswitch) {
  struct ASTnode *left = NULL;
  struct ASTnode *tree;

  while (1) {
    // 遇到结束标记时离开。我们首先这样做以允许
    // 空复合语句
    if (Token.token == T_RBRACE)
      return (left);
    if (inswitch && (Token.token == T_CASE || Token.token == T_DEFAULT))
      return (left);

    // 解析单个语句
    tree = single_statement();

    // 对于每个新树，如果left为空则保存它，
    // 否则将left和新树粘合在一起
    if (tree != NULL) {
      if (left == NULL)
	left = tree;
      else
	left = mkastnode(A_GLUE, P_NONE, NULL, left, NULL, tree, NULL, 0);
    }
  }
  return (NULL);		// 保持-Wall满意
}