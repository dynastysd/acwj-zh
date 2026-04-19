// 编译器前端主程序
// Copyright (c) 2024 Warren Toomey, GPL3

#define AOUT	"a.out"
#define TMPDIR 	"/tmp"

///////////////
// QBE部分 //
///////////////

// 阶段命令字符串列表
char *qbephasecmd[]= {
  "cpp",				// C预处理器
  BINDIR "/cscan",			// 词法分析器
  BINDIR "/cparseqbe",			// 解析器
  BINDIR "/cgenqbe",			// 代码生成器
  "qbe",				// QBE到汇编器
  "as",					// 汇编器
  "cc"					// 链接器
};

// C预处理器标志列表
char *qbecppflags[]= {
  "-nostdinc",
  "-isystem",
  INCQBEDIR,
  NULL
};

// 必须在任何编译对象之前的对象文件列表
// 例如crt0.o文件
char *qbepreobjs[]= {
  NULL
};

// 必须在任何编译对象之后的对象文件和/或库列表
char *qbepostobjs[]= {
  NULL
};

////////////////
// 6809部分 //
////////////////

// 阶段命令字符串列表
char *phasecmd6809[]= {
  "cpp",				// C预处理器
  BINDIR "/cscan",			// 词法分析器
  BINDIR "/cparse6809",			// 解析器
  BINDIR "/cgen6809",			// 代码生成器
  BINDIR "/cpeep",			// 窥孔优化器
  "as6809",				// 汇编器
  "ld6809"				// 链接器
};

// C预处理器标志列表
char *cppflags6809[]= {
  "-nostdinc",
  "-isystem",
  INC6809DIR,
  NULL
};

// 必须在任何编译对象之前的对象文件列表
// 例如crt0.o文件
char *preobjs6809[]= {
  LIB6809DIR "/crt0.o",
  NULL
};

// 必须在任何编译对象之后的对象文件和/或库列表
char *postobjs6809[]= {
  LIB6809DIR "/libc.a",
  LIB6809DIR "/lib6809.a",
  NULL
};