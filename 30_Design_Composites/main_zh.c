#include "defs.h"
#define extern_
#include "data.h"
#undef extern_
#include "decl.h"
#include <errno.h>
#include <unistd.h>

// 编译器设置和顶层执行
// Copyright (c) 2019 Warren Toomey, GPL3

// 给定一个包含 '.' 且后面至少有 1 个字符后缀的字符串，
// 将后缀更改为给定的字符。
// 返回新字符串，如果原字符串无法修改则返回 NULL
char *alter_suffix(char *str, char suffix) {
  char *posn;
  char *newstr;

  // 克隆字符串
  if ((newstr = strdup(str)) == NULL)
    return (NULL);

  // 查找 '.'
  if ((posn = strrchr(newstr, '.')) == NULL)
    return (NULL);

  // 确保有后缀
  posn++;
  if (*posn == '\0')
    return (NULL);

  // 更改后缀并 NUL 终止字符串
  *posn++ = suffix;
  *posn = '\0';
  return (newstr);
}

// 给定输入文件名，编译该文件
// 生成汇编代码。返回新文件的名称
static char *do_compile(char *filename) {
  Outfilename = alter_suffix(filename, 's');
  if (Outfilename == NULL) {
    fprintf(stderr, "Error: %s has no suffix, try .c on the end\n", filename);
    exit(1);
  }
  // 打开输入文件
  if ((Infile = fopen(filename, "r")) == NULL) {
    fprintf(stderr, "Unable to open %s: %s\n", filename, strerror(errno));
    exit(1);
  }
  // 创建输出文件
  if ((Outfile = fopen(Outfilename, "w")) == NULL) {
    fprintf(stderr, "Unable to create %s: %s\n", Outfilename,
	    strerror(errno));
    exit(1);
  }

  Line = 1;			// 重置扫描器
  Putback = '\n';
  clear_symtable();		// 清空符号表
  if (O_verbose)
    printf("compiling %s\n", filename);
  scan(&Token);			// 从输入获取第一个标记
  genpreamble();		// 输出前导码
  global_declarations();	// 解析全局声明
  genpostamble();		// 输出后置码
  fclose(Outfile);		// 关闭输出文件
  return (Outfilename);
}

// 给定输入文件名，汇编该文件
// 生成目标代码。返回目标文件名
char *do_assemble(char *filename) {
  char cmd[TEXTLEN];
  int err;

  char *outfilename = alter_suffix(filename, 'o');
  if (outfilename == NULL) {
    fprintf(stderr, "Error: %s has no suffix, try .s on the end\n", filename);
    exit(1);
  }
  // 构建汇编命令并运行它
  snprintf(cmd, TEXTLEN, "%s %s %s", ASCMD, outfilename, filename);
  if (O_verbose)
    printf("%s\n", cmd);
  err = system(cmd);
  if (err != 0) {
    fprintf(stderr, "Assembly of %s failed\n", filename);
    exit(1);
  }
  return (outfilename);
}

// 给定目标文件名和对象文件列表，
// 将所有对象文件名链接在一起。
void do_link(char *outfilename, char *objlist[]) {
  int cnt, size = TEXTLEN;
  char cmd[TEXTLEN], *cptr;
  int err;

  // 从链接器命令和输出文件开始
  cptr = cmd;
  cnt = snprintf(cptr, size, "%s %s ", LDCMD, outfilename);
  cptr += cnt;
  size -= cnt;

  // 现在追加每个对象文件
  while (*objlist != NULL) {
    cnt = snprintf(cptr, size, "%s ", *objlist);
    cptr += cnt;
    size -= cnt;
    objlist++;
  }

  if (O_verbose)
    printf("%s\n", cmd);
  err = system(cmd);
  if (err != 0) {
    fprintf(stderr, "Linking failed\n");
    exit(1);
  }
}

// 打印使用说明（如果启动不正确）
static void usage(char *prog) {
  fprintf(stderr, "Usage: %s [-vcST] [-o outfile] file [file ...]\n", prog);
  fprintf(stderr,
	  "       -v give verbose output of the compilation stages\n");
  fprintf(stderr, "       -c generate object files but don't link them\n");
  fprintf(stderr, "       -S generate assembly files but don't link them\n");
  fprintf(stderr, "       -T dump the AST trees for each input file\n");
  fprintf(stderr, "       -o outfile, produce the outfile executable file\n");
  exit(1);
}

// 主程序：检查参数，
// 如果没有参数则打印使用说明。
// 打开输入文件并调用 scanfile() 来扫描其中的标记。
enum { MAXOBJ = 100 };
int main(int argc, char *argv[]) {
  char *outfilename = AOUT;
  char *asmfile, *objfile;
  char *objlist[MAXOBJ];
  int i, objcnt = 0;

  // 初始化变量
  O_dumpAST = 0;
  O_keepasm = 0;
  O_assemble = 0;
  O_verbose = 0;
  O_dolink = 1;

  // 扫描命令行选项
  for (i = 1; i < argc; i++) {
    // 没有前导 '-'，停止扫描选项
    if (*argv[i] != '-')
      break;

    // 对于此参数中的每个选项
    for (int j = 1; (*argv[i] == '-') && argv[i][j]; j++) {
      switch (argv[i][j]) {
	case 'o':
	  outfilename = argv[++i];	// 保存并跳到下一个参数
	  break;
	case 'T':
	  O_dumpAST = 1;
	  break;
	case 'c':
	  O_assemble = 1;
	  O_keepasm = 0;
	  O_dolink = 0;
	  break;
	case 'S':
	  O_keepasm = 1;
	  O_assemble = 0;
	  O_dolink = 0;
	  break;
	case 'v':
	  O_verbose = 1;
	  break;
	default:
	  usage(argv[0]);
      }
    }
  }

  // 确保至少有一个输入文件参数
  if (i >= argc)
    usage(argv[0]);

  // 依次处理每个输入文件
  while (i < argc) {
    asmfile = do_compile(argv[i]);	// 编译源文件

    if (O_dolink || O_assemble) {
      objfile = do_assemble(asmfile);	// 汇编到目标格式
      if (objcnt == (MAXOBJ - 2)) {
	fprintf(stderr, "Too many object files for the compiler to handle\n");
	exit(1);
      }
      objlist[objcnt++] = objfile;	// 将对象文件名添加到列表
      objlist[objcnt] = NULL;
    }

    if (!O_keepasm)		// 如果不需要保留汇编文件
      unlink(asmfile);		// 则删除它
    i++;
  }

  // 现在链接所有对象文件
  if (O_dolink) {
    do_link(outfilename, objlist);

    // 如果不需要保留目标文件，则删除它们
    if (!O_assemble) {
      for (i = 0; objlist[i] != NULL; i++)
	unlink(objlist[i]);
    }
  }

  return (0);
}