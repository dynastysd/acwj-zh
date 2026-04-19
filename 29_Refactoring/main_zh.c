#include "defs.h"
#define extern_
#include "data.h"
#undef extern_
#include "decl.h"
#include <errno.h>
#include <unistd.h>

// 编译器设置和顶层执行
// Copyright (c) 2019 Warren Toomey, GPL3

// 给定一个包含'.'且'.'后至少有1个字符后缀的字符串,
// 将后缀改为给定的字符
// 如果原字符串无法修改则返回NULL
char *alter_suffix(char *str, char suffix) {
  char *posn;
  char *newstr;

  // 复制字符串
  if ((newstr = strdup(str)) == NULL)
    return (NULL);

  // 查找'.'
  if ((posn = strrchr(newstr, '.')) == NULL)
    return (NULL);

  // 确保有后缀
  posn++;
  if (*posn == '\0')
    return (NULL);

  // 修改后缀并NUL终止字符串
  *posn++ = suffix;
  *posn = '\0';
  return (newstr);
}

// 给定输入文件名,将该文件编译
// 为汇编代码。返回新文件名
static char *do_compile(char *filename) {
  Outfilename = alter_suffix(filename, 's');
  if (Outfilename == NULL) {
    fprintf(stderr, "错误: %s 没有后缀,请在末尾加上 .c\n", filename);
    exit(1);
  }
  // 打开输入文件
  if ((Infile = fopen(filename, "r")) == NULL) {
    fprintf(stderr, "无法打开 %s: %s\n", filename, strerror(errno));
    exit(1);
  }
  // 创建输出文件
  if ((Outfile = fopen(Outfilename, "w")) == NULL) {
    fprintf(stderr, "无法创建 %s: %s\n", Outfilename,
	    strerror(errno));
    exit(1);
  }

  Line = 1;			// 重置扫描器
  Putback = '\n';
  clear_symtable();		// 清空符号表
  if (O_verbose)
    printf("正在编译 %s\n", filename);
  scan(&Token);			// 从输入获取第一个token
  genpreamble();		// 输出序言
  global_declarations();	// 解析全局声明
  genpostamble();		// 输出尾声
  fclose(Outfile);		// 关闭输出文件
  return (Outfilename);
}

// 给定输入文件名,将该文件汇编
// 为目标代码。返回目标文件名
char *do_assemble(char *filename) {
  char cmd[TEXTLEN];
  int err;

  char *outfilename = alter_suffix(filename, 'o');
  if (outfilename == NULL) {
    fprintf(stderr, "错误: %s 没有后缀,请在末尾加上 .s\n", filename);
    exit(1);
  }
  // 构建汇编命令并执行
  snprintf(cmd, TEXTLEN, "%s %s %s", ASCMD, outfilename, filename);
  if (O_verbose)
    printf("%s\n", cmd);
  err = system(cmd);
  if (err != 0) {
    fprintf(stderr, "%s 的汇编失败\n", filename);
    exit(1);
  }
  return (outfilename);
}

// 给定目标文件列表和输出文件名,
// 将所有目标文件链接在一起
void do_link(char *outfilename, char *objlist[]) {
  int cnt, size = TEXTLEN;
  char cmd[TEXTLEN], *cptr;
  int err;

  // 从链接器命令和输出文件开始
  cptr = cmd;
  cnt = snprintf(cptr, size, "%s %s ", LDCMD, outfilename);
  cptr += cnt;
  size -= cnt;

  // 现在追加每个目标文件
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
    fprintf(stderr, "链接失败\n");
    exit(1);
  }
}

// 如果启动方式不正确则打印用法说明
static void usage(char *prog) {
  fprintf(stderr, "用法: %s [-vcST] [-o outfile] file [file ...]\n", prog);
  fprintf(stderr,
	  "       -v 显示编译各阶段的详细信息\n");
  fprintf(stderr, "       -c 生成目标文件但不链接\n");
  fprintf(stderr, "       -S 生成汇编文件但不链接\n");
  fprintf(stderr, "       -T 转储每个输入文件的AST树\n");
  fprintf(stderr, "       -o outfile, 生成名为outfile的可执行文件\n");
  exit(1);
}

// 主程序:检查参数,如果没有参数则打印用法
// 打开输入文件并调用scanfile()扫描其中的token
enum { MAXOBJ= 100 };
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
    // 没有前导'-',停止扫描选项
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
      objfile = do_assemble(asmfile);	// 汇编为目标文件
      if (objcnt == (MAXOBJ - 2)) {
	fprintf(stderr, "目标文件太多,编译器无法处理\n");
	exit(1);
      }
      objlist[objcnt++] = objfile;	// 将目标文件名
      objlist[objcnt] = NULL;	// 添加到目标文件列表
    }

    if (!O_keepasm)		// 如果不需要保留汇编文件
      unlink(asmfile);		// 则删除它
    i++;
  }

  // 现在链接所有目标文件
  if (O_dolink) {
    do_link(outfilename, objlist);

    // 如果不需要保留目标文件,则删除它们
    if (!O_assemble) {
      for (i = 0; objlist[i] != NULL; i++)
	unlink(objlist[i]);
    }
  }

  return (0);
}