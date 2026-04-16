#include "defs.h"
#include "data.h"
#include "decl.h"

// 声明的解析
// Copyright (c) 2019 Warren Toomey, GPL3


// 解析当前词法单元并返回
// 一个原始类型的枚举值。同时
// 扫描下一个词法单元
int parse_type(void) {
  int type;
  switch (Token.token) {
    case T_VOID:
      type = P_VOID;
      break;
    case T_CHAR:
      type = P_CHAR;
      break;
    case T_INT:
      type = P_INT;
      break;
    case T_LONG:
      type = P_LONG;
      break;
    default:
      fatald("Illegal type, token", Token.token);
  }

  // 扫描一个或多个后续的 '*' 词法单元
  // 并确定正确的指针类型
  while (1) {
    scan(&Token);
    if (Token.token != T_STAR)
      break;
    type = pointer_to(type);
  }

  // 我们离开时下一个词法单元已经被扫描
  return (type);
}

// variable_declaration: type identifier ';'  ;
//
// 解析变量的声明
void var_declaration(void) {
  int id, type;

  // 获取变量的类型
  // 同时扫描标识符
  type = parse_type();
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
// 解析一个简化函数的声明
struct ASTnode *function_declaration(void) {
  struct ASTnode *tree, *finalstmt;
  int nameslot, type, endlabel;

  // 获取变量的类型
  // 同时扫描标识符
  type = parse_type();
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

  // 如果函数类型不是 P_VOID ..
  if (type != P_VOID) {

    // 如果函数中没有语句则报错
    if (tree == NULL)
      fatal("No statements in function with non-void type");

    // 检查复合语句中最后一个 AST 操作
    // 是 return 语句
    finalstmt = (tree->op == A_GLUE) ? tree->right : tree;
    if (finalstmt == NULL || finalstmt->op != A_RETURN)
      fatal("No return for function with non-void type");
  }
  // 返回 A_FUNCTION 节点，它包含函数的 nameslot
  // 和复合语句子树
  return (mkastunary(A_FUNCTION, type, tree, nameslot));
}