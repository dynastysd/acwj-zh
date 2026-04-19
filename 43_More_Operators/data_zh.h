#ifndef extern_
#define extern_ extern
#endif

// 全局变量
// Copyright (c) 2019 Warren Toomey, GPL3

extern_ int Line;		     	// 当前行号
extern_ int Putback;		     	// 由扫描器放回的字符
extern_ struct symtable *Functionid; 	// 当前函数的符号指针
extern_ FILE *Infile;		     	// 输入和输出文件
extern_ FILE *Outfile;
extern_ char *Infilename;		// 我们正在解析的文件名
extern_ char *Outfilename;		// 我们打开作为 Outfile 的文件名
extern_ struct token Token;		// 上次扫描的标记
extern_ char Text[TEXTLEN + 1];		// 上次扫描的标识符
extern_ int Looplevel;			// 嵌套循环的深度
extern_ int Switchlevel;		// 嵌套 switch 的深度

// 符号表列表
extern_ struct symtable *Globhead, *Globtail;	  // 全局变量和函数
extern_ struct symtable *Loclhead, *Locltail;	  // 局部变量
extern_ struct symtable *Parmhead, *Parmtail;	  // 局部参数
extern_ struct symtable *Membhead, *Membtail;	  // struct/union 成员的临时列表
extern_ struct symtable *Structhead, *Structtail; // struct 类型的列表
extern_ struct symtable *Unionhead, *Uniontail;   // union 类型的列表
extern_ struct symtable *Enumhead,  *Enumtail;    // 枚举类型和值的列表
extern_ struct symtable *Typehead,  *Typetail;    // typedef 的列表

// 命令行标志
extern_ int O_dumpAST;		// 如果为真，转储 AST 树
extern_ int O_keepasm;		// 如果为真，保留任何汇编文件
extern_ int O_assemble;		// 如果为真，汇编汇编文件
extern_ int O_dolink;		// 如果为真，链接目标文件
extern_ int O_verbose;		// 如果为真，打印编译阶段的信息