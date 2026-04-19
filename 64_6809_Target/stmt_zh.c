#include "defs.h"
#include "data.h"
#include "decl.h"
#include "expr.h"
#include "misc.h"
#include "opt.h"
#include "parse.h"
#include "stmt.h"
#include "sym.h"
#include "tree.h"
#include "types.h"

// 语句解析
// Copyright (c) 2019 Warren Toomey, GPL3

// 原型
static struct ASTnode *single_statement(void);

// compound_statement:          // 空，即无语句
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
// 解析 IF 语句包括任何可选的 ELSE 子句并返回其 AST
static struct ASTnode *if_statement(void) {
  struct ASTnode *condAST, *trueAST, *falseAST = NULL;

  // 确保我们有 'if' '('
  match(T_IF, "if");
  lparen();

  // 解析随后的表达式和后面的 ')'。
  // 强制非布尔操作变为布尔。
  condAST = binexpr(0);
  if (condAST->op != A_LOGOR && condAST->op != A_LOGAND &&
		(condAST->op < A_EQ || condAST->op > A_GE))
    condAST =
      mkastunary(A_TOBOOL, condAST->type, condAST->ctype, condAST, NULL, 0);
  rparen();

  // 获取语句的 AST
  trueAST = single_statement();

  // 如果我们有 'else'，跳过它
  // 并获取语句的 AST
  if (Token.token == T_ELSE) {
    scan(&Token);
    falseAST = single_statement();
  }

  // 为这个语句构建并返回 AST
  return (mkastnode(A_IF, P_NONE, NULL, condAST, trueAST, falseAST, NULL, 0));
}


// while_statement: 'while' '(' true_false_expression ')' statement  ;
//
// 解析 WHILE 语句并返回其 AST
static struct ASTnode *while_statement(void) {
  struct ASTnode *condAST, *bodyAST;

  // 确保我们有 'while' '('
  match(T_WHILE, "while");
  lparen();

  // 解析随后的表达式和后面的 ')'。
  // 强制非布尔操作变为布尔。
  condAST = binexpr(0);
  if (condAST->op != A_LOGOR && condAST->op != A_LOGAND &&
		(condAST->op < A_EQ || condAST->op > A_GE))
    condAST =
      mkastunary(A_TOBOOL, condAST->type, condAST->ctype, condAST, NULL, 0);
  rparen();

  // 获取语句的 AST。
  // 在此过程中更新循环深度
  Looplevel++;
  bodyAST = single_statement();
  Looplevel--;

  // 为这个语句构建并返回 AST
  return (mkastnode(A_WHILE, P_NONE, NULL, condAST, NULL, bodyAST, NULL, 0));
}

// for_statement: 'for' '(' expression_list ';'
//                          true_false_expression ';'
//                          expression_list ')' statement  ;
//
// 解析 FOR 语句并返回其 AST
static struct ASTnode *for_statement(void) {
  struct ASTnode *condAST, *bodyAST;
  struct ASTnode *preopAST, *postopAST;
  struct ASTnode *tree;

  // 确保我们有 'for' '('
  match(T_FOR, "for");
  lparen();

  // 获取 pre_op 表达式和 ';'
  preopAST = expression_list(T_SEMI);
  semi();

  // 获取条件表达式和 ';'。强制非布尔操作变为布尔。
  condAST = binexpr(0);
  if (condAST->op != A_LOGOR && condAST->op != A_LOGAND &&
		(condAST->op < A_EQ || condAST->op > A_GE))
    condAST =
      mkastunary(A_TOBOOL, condAST->type, condAST->ctype, condAST, NULL, 0);
  semi();

  // 获取 post_op 表达式和 ')'
  postopAST = expression_list(T_RPAREN);
  rparen();

  // 获取作为循环体的语句
  // 在此过程中更新循环深度
  Looplevel++;
  bodyAST = single_statement();
  Looplevel--;

  // 将语句和 postop 树粘合在一起
  tree = mkastnode(A_GLUE, P_NONE, NULL, bodyAST, NULL, postopAST, NULL, 0);

  // 用这个新 body 创建一个 WHILE 循环
  tree = mkastnode(A_WHILE, P_NONE, NULL, condAST, NULL, tree, NULL, 0);

  // 将 preop 树粘合到 A_WHILE 树
  return (mkastnode(A_GLUE, P_NONE, NULL, preopAST, NULL, tree, NULL, 0));
}

// return_statement: 'return' '(' expression ')'  ;
//
// 解析 return 语句并返回其 AST
static struct ASTnode *return_statement(void) {
  struct ASTnode *tree= NULL;

  // 确保我们有 'return'
  match(T_RETURN, "return");

  // 查看我们是否有返回值
  if (Token.token == T_LPAREN) {
    // 如果函数返回 P_VOID，不能返回值
    if (Functionid->type == P_VOID)
      fatal("Can't return from a void function");

    // 跳过左括号
    lparen();

    // 解析随后的表达式
    tree = binexpr(0);

    // 确保这与函数类型兼容
    tree = modify_type(tree, Functionid->type, Functionid->ctype, 0);
    if (tree == NULL)
      fatal("Incompatible type to return");

    // 获取 ')'
    rparen();
  }

  // 添加 A_RETURN 节点
  tree = mkastunary(A_RETURN, P_NONE, NULL, tree, NULL, 0);

  // 获取 ';'
  semi();
  return (tree);
}

// break_statement: 'break' ;
//
// 解析 break 语句并返回其 AST
static struct ASTnode *break_statement(void) {

  if (Looplevel == 0 && Switchlevel == 0)
    fatal("no loop or switch to break out from");
  scan(&Token);
  semi();
  return (mkastleaf(A_BREAK, P_NONE, NULL, NULL, 0));
}

// continue_statement: 'continue' ;
//
// 解析 continue 语句并返回其 AST
static struct ASTnode *continue_statement(void) {

  if (Looplevel == 0)
    fatal("no loop to continue to");
  scan(&Token);
  semi();
  return (mkastleaf(A_CONTINUE, P_NONE, NULL, NULL, 0));
}

// 解析 switch 语句并返回其 AST
static struct ASTnode *switch_statement(void) {
  struct ASTnode *left, *body, *n, *c;
  struct ASTnode *casetree = NULL, *casetail;
  int inloop = 1, casecount = 0;
  int seendefault = 0;
  int ASTop, casevalue;

  // 跳过 'switch' 和 '('
  scan(&Token);
  lparen();

  // 获取 switch 表达式、')' 和 '{'
  left = binexpr(0);
  rparen();
  lbrace();

  // 确保这是 int 类型
  if (!inttype(left->type))
    fatal("Switch expression is not of integer type");

  // 如果其类型是 P_CHAR，将其加宽到 P_INT
  if (left->type == P_CHAR)
    left= mkastunary(A_WIDEN, P_INT, NULL, left, NULL, 0);

  // 构建一个以表达式为子节点的 A_SWITCH 子树
  n = mkastunary(A_SWITCH, P_NONE, NULL, left, NULL, 0);

  // 现在解析 case
  Switchlevel++;
  while (inloop) {
    switch (Token.token) {
	// 当我们遇到 '}' 时离开循环
      case T_RBRACE:
	if (casecount == 0)
	  fatal("No cases in switch");
	inloop = 0;
	break;
      case T_CASE:
      case T_DEFAULT:
	// 确保这不是在之前的 'default' 之后
	if (seendefault)
	  fatal("case or default after existing default");

	// 设置 AST 操作。如果需要，扫描 case 值
	if (Token.token == T_DEFAULT) {
	  ASTop = A_DEFAULT;
	  seendefault = 1;
	  scan(&Token);
	} else {
	  ASTop = A_CASE;
	  scan(&Token);
	  left = binexpr(0);

	  // 确保 case 值是整数字面量
	  if (left->op != A_INTLIT)
	    fatal("Expecting integer literal for case value");
	  casevalue = left->a_intvalue;

	  // 遍历现有 case 值列表以确保没有重复的 case 值
	  for (c = casetree; c != NULL; c = c->right)
	    if (casevalue == c->a_intvalue)
	      fatal("Duplicate case value");
	}

	// 扫描 ':' 并增加 casecount
	match(T_COLON, ":");
	casecount++;

	// 如果下一个 token 是 T_CASE，现有的 case 将落入下一个 case。
	// 否则，解析 case body。
	if (Token.token == T_CASE)
	  body = NULL;
	else
	  body = compound_statement(1);

	// 使用任何复合语句作为左子节点构建一个子树，
	// 并将其链接到正在增长的 A_CASE 树
	if (casetree == NULL) {
	  casetree = casetail =
	    mkastunary(ASTop, P_NONE, NULL, body, NULL, casevalue);
	} else {
	  casetail->right =
	    mkastunary(ASTop, P_NONE, NULL, body, NULL, casevalue);
	  casetail->rightid= casetail->right->nodeid;
	  casetail = casetail->right;
	}
	break;
      default:
	fatals("Unexpected token in switch", Tstring[Token.token]);
    }
  }
  Switchlevel--;

  // 我们有一个包含 case 和任何 default 的子树。
  // 将 case 计数放入 A_SWITCH 节点并附加 case 树。
  n->a_intvalue = casecount;
  n->right = casetree;
  n->rightid = casetree->nodeid;
  rbrace();

  return (n);
}

// 解析单个语句并返回其 AST。
static struct ASTnode *single_statement(void) {
  struct ASTnode *stmt;
  struct symtable *ctype;
  int linenum= Line;

  switch (Token.token) {
    case T_SEMI:
      // 一个空语句
      semi();
      break;
    case T_LBRACE:
      // 我们有一个 '{'，所以这是一个复合语句
      lbrace();
      stmt = compound_statement(0);
      stmt->linenum= linenum;
      rbrace();
      return (stmt);
    case T_IDENT:
      // 我们必须查看标识符是否匹配 typedef。
      // 如果不是，则将其视为表达式。
      // 否则，下降到 parse_type() 调用。
      if (findtypedef(Text) == NULL) {
	stmt = binexpr(0);
        stmt->linenum= linenum;
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
      declaration_list(&ctype, V_LOCAL, T_SEMI, T_EOF, &stmt);
      semi();
      return (stmt);		// 声明中的任何赋值
    case T_IF:
      stmt= if_statement(); stmt->linenum= linenum; return(stmt);
    case T_WHILE:
      stmt= while_statement(); stmt->linenum= linenum; return(stmt);
    case T_FOR:
      stmt= for_statement(); stmt->linenum= linenum; return(stmt);
    case T_RETURN:
      stmt= return_statement(); stmt->linenum= linenum; return(stmt);
    case T_BREAK:
      stmt= break_statement(); stmt->linenum= linenum; return(stmt);
    case T_CONTINUE:
      stmt= continue_statement(); stmt->linenum= linenum; return(stmt);
    case T_SWITCH:
      stmt= switch_statement(); stmt->linenum= linenum; return(stmt);
    default:
      // 目前，看看这是否是表达式。
      // 这处理赋值语句。
      stmt = binexpr(0);
      stmt->linenum= linenum;
      semi();
      return (stmt);
  }
  return (NULL);		// 保持 -Wall 高兴
}

// 解析复合语句并返回其 AST。如果 inswitch 为真，
// 我们查找 '}'、'case' 或 'default' token 来结束解析。
// 否则，只查找 '}' 来结束解析。
struct ASTnode *compound_statement(int inswitch) {
  struct ASTnode *left = NULL;
  struct ASTnode *tree;

  while (1) {
    // 如果我们遇到了结束 token，则离开。首先这样做允许空复合语句
    if (Token.token == T_RBRACE)
      return (left);
    if (inswitch && (Token.token == T_CASE || Token.token == T_DEFAULT))
      return (left);

    // 解析单个语句
    tree = single_statement();

    // 对于每个新树，如果 left 为空则保存它，
    // 否则将 left 和新树粘合在一起
    if (tree != NULL) {
      if (left == NULL)
	left = tree;
      else {
	left = mkastnode(A_GLUE, P_NONE, NULL, left, NULL, tree, NULL, 0);

	// 为了节省内存，我们尝试优化单个语句树。
	// 然后我们序列化树并释放它。我们将 left 中的右指针设置为 NULL；
	// 这将阻止序列化器下降到我们已经序列化的树中。
	tree = optimise(tree);
	serialiseAST(tree);
	freetree(tree, 0);
	left->right=NULL;
      }
    }
  }
  return (NULL);		// 保持 -Wall 高兴
}