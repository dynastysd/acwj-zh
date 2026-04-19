#ifndef extern_
#define extern_ extern
#endif

// 全局变量
// Copyright (c) 2019 Warren Toomey, GPL3

extern_ int Line;		// 当前行号
extern_ int Putback;		// 被扫描器放回的字符
extern_ int Functionid;		// 当前函数的符号 ID
extern_ int Globs;		// 下一个空闲全局符号槽的位置
extern_ int Locls;		// 下一个空闲局部符号槽的位置
extern_ FILE *Infile;		// 输入和输出文件
extern_ FILE *Outfile;
extern_ char *Outfilename;	// 作为 Outfile 打开的文件名
extern_ struct token Token;	// 最后扫描的词法单元
extern_ char Text[TEXTLEN + 1];	// 最后扫描的标识符
extern_ struct symtable Symtable[NSYMBOLS];	// 全局符号表

extern_ int O_dumpAST;		// 如果为真，转储抽象语法树
extern_ int O_keepasm;		// 如果为真，保留任何汇编文件
extern_ int O_assemble;		// 如果为真，汇编汇编文件
extern_ int O_dolink;		// 如果为真，链接目标文件
extern_ int O_verbose;		// 如果为真，打印编译阶段信息
