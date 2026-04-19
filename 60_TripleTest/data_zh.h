#ifndef extern_
#define extern_ extern
#endif

// 全局变量
// Copyright (c) 2019 Warren Toomey, GPL3

extern_ int Line;		     	// 当前行号
extern_ int Linestart;		     	// 如果在行首则为真
extern_ int Putback;		     	// 由扫描器放回的字符
extern_ struct symtable *Functionid; 	// 当前函数的符号指针
extern_ FILE *Infile;		     	// 输入和输出文件
extern_ FILE *Outfile;
extern_ char *Infilename;		// 我们正在解析的文件名
extern_ char *Outfilename;		// 作为 Outfile 打开的文件名
extern_ struct token Token;		// 最后扫描的词法标记
extern_ struct token Peektoken;		// 向前查看的词法标记
extern_ char Text[TEXTLEN + 1];		// 最后扫描的标识符
extern_ int Looplevel;			// 嵌套循环的深度
extern_ int Switchlevel;		// 嵌套 switch 的深度
extern char *Tstring[];			// 词法标记字符串列表

// 符号表列表
extern_ struct symtable *Globhead, *Globtail;	  // 全局变量和函数
extern_ struct symtable *Loclhead, *Locltail;	  // 局部变量
extern_ struct symtable *Parmhead, *Parmtail;	  // 局部参数
extern_ struct symtable *Membhead, *Membtail;	  // 结构体/联合体成员的临时列表
extern_ struct symtable *Structhead, *Structtail; // 结构体类型列表
extern_ struct symtable *Unionhead, *Uniontail;   // 联合体类型列表
extern_ struct symtable *Enumhead,  *Enumtail;    // 枚举类型和值列表
extern_ struct symtable *Typehead,  *Typetail;    // typedef 列表

// 命令行标志
extern_ int O_dumpAST;		// 如果为真，转储 AST 树
extern_ int O_dumpsym;		// 如果为真，转储符号表
extern_ int O_keepasm;		// 如果为真，保留任何汇编文件
extern_ int O_assemble;		// 如果为真，汇编汇编文件
extern_ int O_dolink;		// 如果为真，链接目标文件
extern_ int O_verbose;		// 如果为真，打印编译阶段的信息
