#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

// Structure and enum definitions
// Copyright (c) 2019 Warren Toomey, GPL3

enum {
  TEXTLEN = 512			// 输入中标识符的长度
};

// 命令和默认文件名
#define AOUT "a.out"
#define ASCMD "as6809 -o "
#define LDCMD "ld6809 -o %s /tmp/crt0.o %s /opt/fcc/lib/6809/libc.a /opt/fcc/lib/6809/lib6809.a -m %s.map"
#define CPPCMD "cpp -nostdinc -isystem "

// 词元类型
enum {
  T_EOF,

  // 二元运算符
  T_ASSIGN, T_ASPLUS, T_ASMINUS,                // 1
  T_ASSTAR, T_ASSLASH, T_ASMOD,                 // 4
  T_QUESTION, T_LOGOR, T_LOGAND,                // 7
  T_OR, T_XOR, T_AMPER,                         // 10
  T_EQ, T_NE,                                   // 13
  T_LT, T_GT, T_LE, T_GE,                       // 15
  T_LSHIFT, T_RSHIFT,                           // 19
  T_PLUS, T_MINUS, T_STAR, T_SLASH, T_MOD,      // 21

  // 其他运算符
  T_INC, T_DEC, T_INVERT, T_LOGNOT,             // 26

  // 类型关键字
  T_VOID, T_CHAR, T_INT, T_LONG,                // 30

  // 其他关键字
  T_IF, T_ELSE, T_WHILE, T_FOR, T_RETURN,       // 34
  T_STRUCT, T_UNION, T_ENUM, T_TYPEDEF,         // 39
  T_EXTERN, T_BREAK, T_CONTINUE, T_SWITCH,      // 43
  T_CASE, T_DEFAULT, T_SIZEOF, T_STATIC,        // 47

  // 结构词元
  T_INTLIT, T_STRLIT, T_SEMI, T_IDENT,          // 51
  T_LBRACE, T_RBRACE, T_LPAREN, T_RPAREN,       // 55
  T_LBRACKET, T_RBRACKET, T_COMMA, T_DOT,       // 59
  T_ARROW, T_COLON, T_ELLIPSIS, T_CHARLIT,      // 63

  // 杂项
  T_FILENAME, T_LINENUM				// 67
};

// 词元结构
struct token {
  int token;			// 词元类型，来自上面的枚举列表
  char *tokstr;			// 词元的字符串版本
  int intvalue;			// 对于 T_INTLIT，整数值
};

// AST 节点类型。前几个与相关的词元对应
enum {
  A_ASSIGN = 1, A_ASPLUS, A_ASMINUS, A_ASSTAR,			//  1
  A_ASSLASH, A_ASMOD, A_TERNARY, A_LOGOR,			//  5
  A_LOGAND, A_OR, A_XOR, A_AND, A_EQ, A_NE, A_LT,		//  9
  A_GT, A_LE, A_GE, A_LSHIFT, A_RSHIFT,				// 16
  A_ADD, A_SUBTRACT, A_MULTIPLY, A_DIVIDE, A_MOD,		// 21
  A_INTLIT, A_STRLIT, A_IDENT, A_GLUE,				// 26
  A_IF, A_WHILE, A_FUNCTION, A_WIDEN, A_RETURN,			// 30
  A_FUNCCALL, A_DEREF, A_ADDR, A_SCALE,				// 35
  A_PREINC, A_PREDEC, A_POSTINC, A_POSTDEC,			// 39
  A_NEGATE, A_INVERT, A_LOGNOT, A_TOBOOL, A_BREAK,		// 43
  A_CONTINUE, A_SWITCH, A_CASE, A_DEFAULT, A_CAST		// 48
};

// 基本类型。低4位是一个整数值，表示
// 间接级别，例如：0=无指针，1=指针，2=指针的指针等。
enum {
  P_NONE, P_VOID = 16, P_CHAR = 32, P_INT = 48, P_LONG = 64,
  P_STRUCT=80, P_UNION=96
};

// 符号表中的符号是以下结构类型之一
enum {
  S_VARIABLE, S_FUNCTION, S_ARRAY, S_ENUMVAL, S_STRLIT,
  S_STRUCT, S_UNION, S_ENUMTYPE, S_TYPEDEF, S_NOTATYPE
};

// 符号的可见性类
enum {
  V_GLOBAL,			// 全局可见的符号
  V_EXTERN,			// 外部全局可见的符号
  V_STATIC,			// 静态符号，只在一个文件中可见
  V_LOCAL,			// 局部可见的符号
  V_PARAM,			// 局部可见的函数参数
  V_MEMBER			// 结构体或联合体的成员
};

// 符号表结构
struct symtable {
  char *name;			// 符号的名称
  int id;			// 符号的数值ID
  int type;			// 符号的基本类型
  struct symtable *ctype;	// 如果是结构体/联合体，指向该类型的指针
  int ctypeid;			// 结构体/联合体类型的数值ID
  int stype;			// 符号的结构类型
  int class;			// 符号的可见性类
  int size;			// 该符号的总大小（字节）
                                // 对于函数：大小为1表示...（省略号）
#define has_ellipsis size
  int nelems;			// 函数：参数数量。数组：元素数量。
  int st_hasaddr;		// 对于局部变量，如果有任何 A_ADDR 操作则为1
#define st_endlabel st_posn	// 对于函数，结束标签
#define st_label st_posn	// 对于字符串字面量，关联的标签
  int st_posn;			// 对于局部变量，相对于栈基指针的负偏移量。
     				// 对于结构体成员，成员相对于结构体基址的偏移量
  int *initlist;		// 初始值列表
  struct symtable *next;	// 符号表中的下一个符号
  struct symtable *member;	// 结构体、联合体或枚举的成员列表。
};				// 对于函数，是参数和局部变量的列表。

// 抽象语法树结构
struct ASTnode {
  int op;			// 在该树上执行的"操作"
  int type;			// 该树生成的任何表达式的类型
  struct symtable *ctype;	// 如果是结构体/联合体，指向该类型的指针
  int rvalue;			// 如果节点是右值则为真
  struct ASTnode *left;		// 左、中、右子树
  struct ASTnode *mid;
  struct ASTnode *right;
  int nodeid;                   // 树序列化时的节点ID
  int leftid;                   // 序列化时的数值ID
  int midid;
  int rightid;
  struct symtable *sym;		// 对于许多AST节点，指向符号表中符号的指针
  char *name;			// 符号的名称（供序列化器使用）
  int symid;			// 符号的唯一ID（供序列化器使用）
#define a_intvalue a_size	// 对于 A_INTLIT，整数值
  int a_size;			// 对于 A_SCALE，要缩放的大小
  int linenum;			// 该节点来自的行号
};

enum {
  NOREG = -1,			// 当 AST 生成函数没有寄存器返回时使用 NOREG
  				// 函数没有寄存器要返回时使用
  NOLABEL = 0			// 当我们没有标签要传递给 genAST() 时使用 NOLABEL
     				// 传递给 genAST()
};