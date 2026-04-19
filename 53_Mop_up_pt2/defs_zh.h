#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

// 结构和枚举定义
// Copyright (c) 2019 Warren Toomey, GPL3

enum {
  TEXTLEN = 512			// 输入中标识符的长度
};

// 命令和默认文件名
#define AOUT "a.out"
#ifdef __NASM__
#define ASCMD "nasm -f elf64 -w-ptr -o "
#define LDCMD "cc -no-pie -fno-plt -Wall -o "
#else
#define ASCMD "as -o "
#define LDCMD "cc -o "
#endif
#define CPPCMD "cpp -nostdinc -isystem "

// 标记类型
enum {
  T_EOF,

  // 二元操作符
  T_ASSIGN, T_ASPLUS, T_ASMINUS,
  T_ASSTAR, T_ASSLASH,
  T_QUESTION, T_LOGOR, T_LOGAND,
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
  T_STRUCT, T_UNION, T_ENUM, T_TYPEDEF,
  T_EXTERN, T_BREAK, T_CONTINUE, T_SWITCH,
  T_CASE, T_DEFAULT, T_SIZEOF, T_STATIC,

  // 结构标记
  T_INTLIT, T_STRLIT, T_SEMI, T_IDENT,
  T_LBRACE, T_RBRACE, T_LPAREN, T_RPAREN,
  T_LBRACKET, T_RBRACKET, T_COMMA, T_DOT,
  T_ARROW, T_COLON
};

// 标记结构
struct token {
  int token;			// 标记类型，来自上面的枚举列表
  char *tokstr;			// 标记的字符串版本
  int intvalue;			// 对于T_INTLIT，整数值
};

// AST节点类型。前几个与相关标记一致
enum {
  A_ASSIGN = 1, A_ASPLUS, A_ASMINUS, A_ASSTAR, A_ASSLASH,	// 1
  A_TERNARY, A_LOGOR, A_LOGAND, A_OR, A_XOR, A_AND,		// 6
  A_EQ, A_NE, A_LT, A_GT, A_LE, A_GE, A_LSHIFT, A_RSHIFT,	// 12
  A_ADD, A_SUBTRACT, A_MULTIPLY, A_DIVIDE,			// 20
  A_INTLIT, A_STRLIT, A_IDENT, A_GLUE,				// 24
  A_IF, A_WHILE, A_FUNCTION, A_WIDEN, A_RETURN,			// 28
  A_FUNCCALL, A_DEREF, A_ADDR, A_SCALE,				// 33
  A_PREINC, A_PREDEC, A_POSTINC, A_POSTDEC,			// 37
  A_NEGATE, A_INVERT, A_LOGNOT, A_TOBOOL, A_BREAK,		// 41
  A_CONTINUE, A_SWITCH, A_CASE, A_DEFAULT, A_CAST		// 46
};

// 原始类型。低4位是一个整数
// 值，表示间接级别，
// 例如 0=无指针，1=指针，2=指针的指针等。
enum {
  P_NONE, P_VOID = 16, P_CHAR = 32, P_INT = 48, P_LONG = 64,
  P_STRUCT=80, P_UNION=96
};

// 结构类型
enum {
  S_VARIABLE, S_FUNCTION, S_ARRAY
};

// 存储类别
enum {
  C_GLOBAL = 1,			// 全局可见符号
  C_LOCAL,			// 局部可见符号
  C_PARAM,			// 局部可见函数参数
  C_EXTERN,			// 外部全局可见符号
  C_STATIC,			// 静态符号，在一个文件中可见
  C_STRUCT,			// 一个struct
  C_UNION,			// 一个union
  C_MEMBER,			// struct或union的成员
  C_ENUMTYPE,			// 命名枚举类型
  C_ENUMVAL,			// 命名枚举值
  C_TYPEDEF			// 命名typedef
};

// 符号表结构
struct symtable {
  char *name;			// 符号名称
  int type;			// 符号的原始类型
  struct symtable *ctype;	// 如果是struct/union，指向该类型的指针
  int stype;			// 符号的结构类型
  int class;			// 符号的存储类别
  int size;			// 此符号的总大小（字节）
  int nelems;			// 函数：#参数。数组：#元素
#define st_endlabel st_posn	// 对于函数，结束标签
  int st_posn;			// 对于局部变量，相对于栈基址指针的负偏移量
    				// 
  int *initlist;		// 初始值列表
  struct symtable *next;	// 下一个符号在一列表中
  struct symtable *member;	// 函数、struct、union或枚举的第一个成员
};				// 

// 抽象语法树结构
struct ASTnode {
  int op;			// 要在此树上执行的"操作"
  int type;			// 此树生成的任何表达式的类型
  struct symtable *ctype;	// 如果是struct/union，指向该类型的指针
  int rvalue;			// 如果节点是rvalue，则为真
  struct ASTnode *left;		// 左、中、右子树
  struct ASTnode *mid;
  struct ASTnode *right;
  struct symtable *sym;		// 对于许多AST节点，指向符号表中符号的指针
  				// 
#define a_intvalue a_size	// 对于A_INTLIT，整数值
  int a_size;			// 对于A_SCALE，要缩放的大小
};

enum {
  NOREG = -1,			// 当AST生成函数没有寄存器返回时使用NOREG
  				// 
  NOLABEL = 0			// 当我们没有标签传递给genAST()时使用NOLABEL
    				// 
};