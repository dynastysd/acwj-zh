#ifndef extern_
#define extern_ extern
#endif

// 全局变量
// Copyright (c) 2019 Warren Toomey, GPL3

extern_ int Line;		// 当前行号
extern_ int Putback;		// 扫描器放回的字符
extern_ int Functionid;		// 当前函数的符号 ID
extern_ int Globs;		// 下一个空闲全局符号槽的位置
extern_ int Locls;		// 下一个空闲局部符号槽的位置
extern_ FILE *Infile;		// 输入和输出文件
extern_ FILE *Outfile;
extern_ struct token Token;	// 最后扫描到的词法单元
extern_ char Text[TEXTLEN + 1];	// 最后扫描到的标识符
extern_ struct symtable Symtable[NSYMBOLS];	// 全局符号表

extern_ int O_dumpAST;