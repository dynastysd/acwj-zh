#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

// 结构和枚举定义
// Copyright (c) 2019 Warren Toomey, GPL3

#define TEXTLEN		512	// 输入中符号的长度
#define NSYMBOLS        1024	// 符号表条目数量

// 标记类型
enum {
  T_EOF,
  // 运算符
  T_PLUS, T_MINUS,
  T_STAR, T_SLASH,
  T_EQ, T_NE,
  T_LT, T_GT, T_LE, T_GE,
  // 类型关键字
  T_VOID, T_CHAR, T_INT, T_LONG,
  // 结构标记
  T_INTLIT, T_SEMI, T_ASSIGN, T_IDENT,
  T_LBRACE, T_RBRACE, T_LPAREN, T_RPAREN,
  // 其他关键字
  T_PRINT, T_IF, T_ELSE, T_WHILE, T_FOR, T_RETURN
};

// 标记结构
struct token {
  int token;			// 标记类型，来自上面的枚举列表
  int intvalue;			// 对于 T_INTLIT，整数值
};

// AST 节点类型。前几个与相关标记
// 一一对应
enum {
  A_ADD = 1, A_SUBTRACT, A_MULTIPLY, A_DIVIDE,
  A_EQ, A_NE, A_LT, A_GT, A_LE, A_GE,
  A_INTLIT,
  A_IDENT, A_LVIDENT, A_ASSIGN, A_PRINT, A_GLUE,
  A_IF, A_WHILE, A_FUNCTION, A_WIDEN, A_RETURN,
  A_FUNCCALL
};

// 基本类型
enum {
  P_NONE, P_VOID, P_CHAR, P_INT, P_LONG
};

// 抽象语法树结构
struct ASTnode {
  int op;			// 要在该树上执行的"操作"
  int type;			// 该树生成的任何表达式的类型
  struct ASTnode *left;		// 左、中、右子树
  struct ASTnode *mid;
  struct ASTnode *right;
  union {			// 对于 A_INTLIT，整数值
    int intvalue;		// 对于 A_IDENT，符号槽号
    int id;			// 对于 A_FUNCTION，符号槽号
  } v;				// 对于 A_FUNCCALL，符号槽号
};

#define NOREG	-1		// 当 AST 生成函数没有寄存器
				// 可返回时使用 NOREG

// 结构类型
enum {
  S_VARIABLE, S_FUNCTION
};

// 符号表结构
struct symtable {
  char *name;			// 符号名称
  int type;			// 符号的基本类型
  int stype;			// 符号的结构类型
  int endlabel;			// 对于 S_FUNCTION，结束标签
};