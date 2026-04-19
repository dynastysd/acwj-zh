#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

// 结构体和枚举定义
// Copyright (c) 2019 Warren Toomey, GPL3

enum {
  TEXTLEN=512,			// 输入中符号的长度
  NSYMBOLS=1024			// 符号表条目数量
};

// 命令和默认文件名
#define AOUT "a.out"
#ifdef __NASM__
#define ASCMD "nasm -f elf64 -o "
#define LDCMD "cc -no-pie -fno-plt -Wall -o "
#else
#define ASCMD "as -o "
#define LDCMD "cc -o "
#endif

// Token类型
enum {
  T_EOF,

  // 二元操作符
  T_ASSIGN, T_LOGOR, T_LOGAND, 
  T_OR, T_XOR, T_AMPER, 
  T_EQ, T_NE,
  T_LT, T_GT, T_LE, T_GE,
  T_LSHIFT, T_RSHIFT,
  T_PLUS, T_MINUS, T_STAR, T_SLASH,

  // 其他操作符
  T_INC, T_DEC, T_INVERT, T_LOGNOT,

  // 类型关键字
  T_VOID, T_CHAR, T_INT, T_LONG,

  // 其他关键字
  T_IF, T_ELSE, T_WHILE, T_FOR, T_RETURN,

  // 结构token
  T_INTLIT, T_STRLIT, T_SEMI, T_IDENT,
  T_LBRACE, T_RBRACE, T_LPAREN, T_RPAREN,
  T_LBRACKET, T_RBRACKET, T_COMMA
};

// Token结构体
struct token {
  int token;			// Token类型,来自上面的枚举列表
  int intvalue;			// 对于T_INTLIT,整数值
};

// AST节点类型。前几个与相关的token对应
enum {
  A_ASSIGN= 1, A_LOGOR, A_LOGAND, A_OR, A_XOR, A_AND,
  A_EQ, A_NE, A_LT, A_GT, A_LE, A_GE, A_LSHIFT, A_RSHIFT,
  A_ADD, A_SUBTRACT, A_MULTIPLY, A_DIVIDE,
  A_INTLIT, A_STRLIT, A_IDENT, A_GLUE,
  A_IF, A_WHILE, A_FUNCTION, A_WIDEN, A_RETURN,
  A_FUNCCALL, A_DEREF, A_ADDR, A_SCALE,
  A_PREINC, A_PREDEC, A_POSTINC, A_POSTDEC,
  A_NEGATE, A_INVERT, A_LOGNOT, A_TOBOOL
};

// 基本类型。低4位是表示间接级别的整数值,
// 例如0=无指针,1=指针,2=指针的指针等
enum {
  P_NONE, P_VOID=16, P_CHAR=32, P_INT=48, P_LONG=64
};

// 抽象语法树结构
struct ASTnode {
  int op;			// 要在此树上执行的"操作"
  int type;			// 此树生成的任何表达式的类型
  int rvalue;			// 如果节点是rvalue则为真
  struct ASTnode *left;		// 左、中、右子树
  struct ASTnode *mid;
  struct ASTnode *right;
  union {			// 对于A_INTLIT,整数值
    int intvalue;		// 对于A_IDENT,符号槽号
    int id;			// 对于A_FUNCTION,符号槽号
    int size;			// 对于A_SCALE,要缩放的大小
  };				// 对于A_FUNCCALL,符号槽号
};

enum {
  NOREG= -1,			// 当AST生成函数没有寄存器返回时使用NOREG
  NOLABEL=  0			// 当我们没有标签传递给genAST()时使用NOLABEL
};

// 结构类型
enum {
  S_VARIABLE, S_FUNCTION, S_ARRAY
};

// 存储类
enum {
        C_GLOBAL = 1,		// 全局可见符号
        C_LOCAL,		// 局部可见符号
        C_PARAM			// 局部可见函数参数
};

// 符号表结构
struct symtable {
  char *name;			// 符号名称
  int type;			// 符号的基本类型
  int stype;			// 符号的结构类型
  int class;			// 符号的存储类
  union {
    int size;			// 符号中的元素数量
    int endlabel;		// 对于函数,结束标签
  };
  union {
    int nelems;			// 对于函数,参数数量
    int posn;			// 对于局部变量,相对于栈基指针的负偏移
  };
};