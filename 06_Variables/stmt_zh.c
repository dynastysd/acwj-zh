#include "defs.h"
#include "data.h"
#include "decl.h"

// 语句的解析
// Copyright (c) 2019 Warren Toomey, GPL3

// statements: statement
//      |      statement statements
//      ;
//
// statement: 'print' expression ';'
//      |     'int'   identifier ';'
//      |     identifier '=' expression ';'
//      ;
//
// identifier: T_IDENT
//      ;

void print_statement(void) {
  struct ASTnode *tree;
  int reg;

  // 匹配 'print' 作为第一个词法单元
  match(T_PRINT, "print");

  // 解析随后的表达式并
  // 生成汇编代码
  tree = binexpr(0);
  reg = genAST(tree, -1);
  genprintint(reg);
  genfreeregs();

  // 匹配随后的分号
  semi();
}

void assignment_statement(void) {
  struct ASTnode *left, *right, *tree;
  int id;

  // 确保有一个标识符
  ident();

  // 检查它是否已被定义，然后为其创建一个叶子 AST 节点
  if ((id = findglob(Text)) == -1) {
    fatals("Undeclared variable", Text);
  }
  right = mkastleaf(A_LVIDENT, id);

  // 确保有一个等号
  match(T_EQUALS, "=");

  // 解析随后的表达式
  left = binexpr(0);

  // 创建一个赋值 AST 树
  tree = mkastnode(A_ASSIGN, left, right, 0);

  // 生成赋值的汇编代码
  genAST(tree, -1);
  genfreeregs();

  // 匹配随后的分号
  semi();
}


// 解析一个或多个语句
void statements(void) {

  while (1) {
    switch (Token.token) {
    case T_PRINT:
      print_statement();
      break;
    case T_INT:
      var_declaration();
      break;
    case T_IDENT:
      assignment_statement();
      break;
    case T_EOF:
      return;
    default:
      fatald("Syntax error, token", Token.token);
    }
  }
}