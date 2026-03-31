#include "defs.h"
#include "data.h"
#include "decl.h"

// 语句解析
// Copyright (c) 2019 Warren Toomey, GPL3

// statements: statement
//      | statement statements
//      ;
//
// statement: 'print' expression ';'
//      ;


// 解析一个或多个语句
void statements(void) {
  struct ASTnode *tree;
  int reg;

  while (1) {
    // 匹配 'print' 作为第一个 token
    match(T_PRINT, "print");

    // 解析后续表达式并
    // 生成汇编代码
    tree = binexpr(0);
    reg = genAST(tree);
    genprintint(reg);
    genfreeregs();

    // 匹配后续分号
    // 如果到达 EOF 则停止
    semi();
    if (Token.token == T_EOF)
      return;
  }
}
