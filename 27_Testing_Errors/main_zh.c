#include "defs.h"
#define extern_
#include "data.h"
#undef extern_
#include "decl.h"
#include <errno.h>

// 编译器设置和顶层执行
// Copyright (c) 2019 Warren Toomey, GPL3

// 初始化全局变量
static void init() {
  Line = 1;
  Putback = '\n';
  Globs = 0;
  Locls = NSYMBOLS - 1;
  O_dumpAST = 0;
}

// 如果启动方式不正确则打印用法信息
static void usage(char *prog) {
  fprintf(stderr, "Usage: %s [-T] infile\n", prog);
  exit(1);
}

// 主程序：检查参数，
// 如果没有参数则打印用法信息。
// 打开输入文件并调用 scanfile()
// 来扫描其中的词法单元。
int main(int argc, char *argv[]) {
  int i;

  // 初始化全局变量
  init();

  // 扫描命令行选项
  for (i = 1; i < argc; i++) {
    if (*argv[i] != '-')
      break;
    for (int j = 1; argv[i][j]; j++) {
      switch (argv[i][j]) {
      case 'T':
	O_dumpAST = 1;
	break;
      default:
	usage(argv[0]);
      }
    }
  }

  // 确保有输入文件参数
  if (i >= argc)
    usage(argv[0]);

  // 打开输入文件
  if ((Infile = fopen(argv[i], "r")) == NULL) {
    fprintf(stderr, "Unable to open %s: %s\n", argv[i], strerror(errno));
    exit(1);
  }
  // 创建输出文件
  if ((Outfile = fopen("out.s", "w")) == NULL) {
    fprintf(stderr, "Unable to create out.s: %s\n", strerror(errno));
    exit(1);
  }
  // 目前，确保 printint() 和 printchar() 已定义
  addglob("printint", P_INT, S_FUNCTION, C_GLOBAL, 0, 0);
  addglob("printchar", P_VOID, S_FUNCTION, C_GLOBAL, 0, 0);

  scan(&Token);			// 从输入获取第一个词法单元
  genpreamble();		// 输出前导代码
  global_declarations();	// 解析全局声明
  genpostamble();		// 输出后导代码
  fclose(Outfile);		// 关闭输出文件并退出
  return (0);
}