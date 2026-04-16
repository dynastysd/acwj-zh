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
}

// 如果启动方式不正确则输出用法
static void usage(char *prog) {
  fprintf(stderr, "Usage: %s infile\n", prog);
  exit(1);
}

// 主程序：检查参数并在没有参数时输出用法。
// 打开输入文件并调用 scanfile() 来扫描其中的标记。
int main(int argc, char *argv[]) {
  struct ASTnode *tree;

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

  // 目前，确保 void printint() 已定义
  addglob("printint", P_CHAR, S_FUNCTION, 0);

  scan(&Token);			// 从输入获取第一个标记
  genpreamble();		// 输出前导码
  while (1) {			// 解析一个函数
    tree = function_declaration();
    genAST(tree, NOREG, 0);	// 为其生成汇编代码
    if (Token.token == T_EOF)	// 到达 EOF 时停止
      break;
  }
  genpostamble();
  fclose(Outfile);		// 关闭输出文件并退出
  return(0);
}