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
}

// 如果启动方式不正确，打印用法说明
static void usage(char *prog) {
  fprintf(stderr, "Usage: %s infile\n", prog);
  exit(1);
}

// 主程序：检查参数，如果我们没有参数则打印用法说明。
// 打开输入文件并调用 scanfile() 来扫描其中的词法单元。
void main(int argc, char *argv[]) {

  if (argc != 2)
    usage(argv[0]);

  init();

  // 打开输入文件
  if ((Infile = fopen(argv[1], "r")) == NULL) {
    fprintf(stderr, "Unable to open %s: %s\n", argv[1], strerror(errno));
    exit(1);
  }
  // 创建输出文件
  if ((Outfile = fopen("out.s", "w")) == NULL) {
    fprintf(stderr, "Unable to create out.s: %s\n", strerror(errno));
    exit(1);
  }

  scan(&Token);			// 从输入获取第一个词法单元
  genpreamble();		// 输出前导代码
  statements();			// 解析输入中的语句
  genpostamble();		// 输出后置代码
  fclose(Outfile);		// 关闭输出文件并退出
  exit(0);
}