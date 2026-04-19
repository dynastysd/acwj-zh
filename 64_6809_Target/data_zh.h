#ifndef extern_
#define extern_ extern
#endif

// 全局变量
// 版权所有 (c) 2019 Warren Toomey, GPL3

extern_ int Line;		     	// 当前行号
extern_ int Linestart;		     	// 是否在行开头
extern_ int Putback;		     	// 被扫描器放回的字符
extern_ struct symtable *Functionid; 	// 当前函数的符号指针
extern_ FILE *Infile;		     	// 输入和输出文件
extern_ FILE *Outfile;
extern_ FILE *Symfile;			// 符号表文件
extern_ FILE *Idxfile;			// AST偏移索引文件
extern_ char *Infilename;		// 我们正在解析的文件名
extern_ struct token Token;		// 上一个扫描的令牌
extern_ struct token Peektoken;		// 向前看的令牌
extern_ char Text[TEXTLEN + 1];		// 上一个扫描的标识符
extern_ int Looplevel;			// 嵌套循环的深度
extern_ int Switchlevel;		// 嵌套switch的深度
extern char *Tstring[];			// 令牌字符串列表