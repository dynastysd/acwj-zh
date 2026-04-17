#include "defs.h"
#include "data.h"
#include "decl.h"

// 声明解析
// Copyright (c) 2019 Warren Toomey, GPL3


// 解析当前标记并返回
// 一个基本类型枚举值。同时
// 扫描下一个标记
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

  // 扫描一个或多个额外的 '*' 标记
  // 并确定正确的指针类型
  while (1) {
    scan(&Token);
    if (Token.token != T_STAR)
      break;
    type = pointer_to(type);
  }

  // 我们离开时下一个标记已经被扫描
  return (type);
}

// variable_declaration: type identifier ';'  ;
//
// 解析变量的声明。
// 标识符已经被扫描且我们有了类型
void var_declaration(int type) {
  int id;

  // Text 现在有标识符的名称。
  // 将其添加为已知标识符
  // 并在汇编中生成其空间
  id = addglob(Text, type, S_VARIABLE, 0);
  genglobsym(id);
  // 获取结尾的分号
  semi();
}

//
// function_declaration: type identifier '(' ')' compound_statement   ;
//
// 解析一个简单函数的声明。
// 标识符已经被扫描且我们有了类型
struct ASTnode *function_declaration(int type) {
  struct ASTnode *tree, *finalstmt;
  int nameslot, endlabel;

  // Text 现在有标识符的名称。
  // 获取结束标签的标签 ID，将函数
  // 添加到符号表，并将 Functionid 全局
  // 设置为函数的符号 ID
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

    // 检查复合语句中
    // 最后一个 AST 操作是否是 return 语句
    finalstmt = (tree->op == A_GLUE) ? tree->right : tree;
    if (finalstmt == NULL || finalstmt->op != A_RETURN)
      fatal("No return for function with non-void type");
  }
  // 返回 A_FUNCTION 节点，它有函数的 nameslot
  // 和复合语句子树
  return (mkastunary(A_FUNCTION, type, tree, nameslot));
}


// 解析一个或多个全局声明，
// 可以是变量或函数
void global_declarations(void) {
  struct ASTnode *tree;
  int type;

  while (1) {

    // 我们必须读取类型和标识符
    // 来查看是函数声明的 '('
    // 还是变量声明的 ',' 或 ';'。
    // Text 由 ident() 调用填充。
    type = parse_type();
    ident();
    if (Token.token == T_LPAREN) {

       // 解析函数声明并
       // 为其生成汇编代码
       tree = function_declaration(type);
       genAST(tree, NOREG, 0);
    } else {

       // 解析全局变量声明
       var_declaration(type);
    }

    // 当到达 EOF 时停止
    if (Token.token == T_EOF)
      break;
  }
}