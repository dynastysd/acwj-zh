#include "defs.h"
#include "data.h"
#include "decl.h"

// 声明解析
// Copyright (c) 2019 Warren Toomey, GPL3


// 解析当前标记并
// 返回一个基本类型枚举值
int parse_type(int t) {
  if (t == T_CHAR)
    return (P_CHAR);
  if (t == T_INT)
    return (P_INT);
  if (t == T_LONG)
    return (P_LONG);
  if (t == T_VOID)
    return (P_VOID);
  fatald("Illegal type, token", t);
  return(0);	// 保持 -Wall 愉快
}

// variable_declaration: type identifier ';'  ;
//
// 解析变量声明
void var_declaration(void) {
  int id, type;

  // 获取变量类型，然后是标识符
  type = parse_type(Token.token);
  scan(&Token);
  ident();
  // Text 现在有标识符的名称。
  // 将其添加为已知标识符
  // 并在汇编中生成其空间
  id = addglob(Text, type, S_VARIABLE, 0);
  genglobsym(id);
  // 获取尾随的分号
  semi();
}

//
// function_declaration: type identifier '(' ')' compound_statement   ;
//
// 解析一个简单函数的声明
struct ASTnode *function_declaration(void) {
  struct ASTnode *tree, *finalstmt;
  int nameslot, type, endlabel;

  // 获取变量类型，然后是标识符
  type = parse_type(Token.token);
  scan(&Token);
  ident();

  // 获取结束标签的标签 id，将函数
  // 添加到符号表，并设置 Functionid 全局
  // 变量为函数的符号 id
  endlabel = genlabel();
  nameslot = addglob(Text, type, S_FUNCTION, endlabel);
  Functionid = nameslot;

  // 扫描括号
  lparen();
  rparen();

  // 获取复合语句的 AST 树
  tree = compound_statement();

  // 如果函数类型不是 P_VOID，检查
  // 复合语句中最后一个 AST 操作
  // 是 return 语句
  if (type != P_VOID) {
    finalstmt = (tree->op == A_GLUE) ? tree->right : tree;
    if (finalstmt == NULL || finalstmt->op != A_RETURN)
      fatal("No return for function with non-void type");
  }
  // 返回一个 A_FUNCTION 节点，它有函数的 nameslot
  // 和复合语句子树
  return (mkastunary(A_FUNCTION, type, tree, nameslot));
}