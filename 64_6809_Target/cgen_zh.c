#include "defs.h"
#define extern_
#include "data.h"
#undef extern_
#include "gen.h"
#include "misc.h"
#include "sym.h"
#include "tree.h"
#include "types.h"

// Assembly code generator.
// Copyright (c) 2023,2024 Warren Toomey, GPL3

// 为变量分配空间
// 然后释放符号表。
void allocateGlobals(void) {
  struct symtable *sym, *litsym;
  int i;

  // 加载所有类型和所有全局变量
  loadGlobals();

  // 现在所有类型和全局变量都在内存中了
  // 首先生成字符串字面量
  for (sym=Symhead; sym!=NULL; sym=sym->next) {
      if (sym->stype== S_STRLIT)
  	sym->st_label= genglobstr(sym->name);
  }

  // 现在处理非字符串字面量
  // XXX 待修复：sym=sym->next
  for (sym=Symhead; sym!=NULL; ) {
      if (sym->stype== S_STRLIT) { sym=sym->next; continue; }

      // 如果这是一个 char 指针或 char 指针数组，
      // 用关联的字符串字面量标签替换 initlist 中的值（这些值是符号 id）。
      // 是的，P_CHAR+2 表示 char 指针数组。
      if (sym->initlist!=NULL &&
        (sym->type== pointer_to(P_CHAR) || sym->type == P_CHAR+2)) {
        for (i=0; i<sym->nelems; i++)
          if (sym->initlist[i]!=0) {
            litsym= findSymbol(NULL, 0, sym->initlist[i]);
            sym->initlist[i]= litsym->st_label;
          }
      }
      genglobsym(sym);
      sym=sym->next;
  }
  freeSymtable();		// 清除符号表
}


// 打开符号表文件和 AST 文件
// 循环：
//  从文件读取下一个 AST 树
//  生成汇编代码
//  释放内存中的符号表
int main(int argc, char **argv) {
  struct ASTnode *node;

  if (argc !=4) {
    fprintf(stderr, "Usage: %s symfile astfile idxfile\n", argv[0]);
    exit(1);
  }

  // 打开符号表文件
  Symfile= fopen(argv[1], "r");
  if (Symfile == NULL) {
    fprintf(stderr, "Can't open %s\n", argv[1]); exit(1);
  }

  // 打开 AST 文件
  Infile= fopen(argv[2], "r");
  if (Infile == NULL) {
    fprintf(stderr, "Can't open %s\n", argv[2]); exit(1);
  }

  // 打开 AST 索引偏移文件以进行读写
  Idxfile= fopen(argv[3], "w+");
  if (Idxfile == NULL) {
    fprintf(stderr, "Can't open %s\n", argv[3]); exit(1);
  }

  // 我们向 stdout 输出汇编代码
  Outfile=stdout;

  mkASTidxfile();		// 构建 AST 索引偏移文件
  freeSymtable();		// 清除符号表
  genpreamble();		// 输出前导码
  allocateGlobals();		// 分配全局变量

  while (1) {
    // 从文件读取下一个函数的顶级节点
    node= loadASTnode(0, 1);
    if (node==NULL) break;

    // 为该树生成汇编代码
    genAST(node, NOLABEL, NOLABEL, NOLABEL, 0);

    // 释放内存中符号表中的符号。
    // 也释放我们加载的 AST 节点
    freeSymtable();
    freeASTnode(node);
  }

  genpostamble();               // 输出后导码
  freeSymtable();
  fclose(Infile);
  fclose(Symfile);
  exit(0);
  return(0);
}