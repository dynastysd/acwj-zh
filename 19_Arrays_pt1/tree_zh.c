#include "defs.h"
#include "data.h"
#include "decl.h"

// AST 树函数
// Copyright (c) 2019 Warren Toomey, GPL3

// 构建并返回一个通用的 AST 节点
struct ASTnode *mkastnode(int op, int type,
			  struct ASTnode *left,
			  struct ASTnode *mid,
			  struct ASTnode *right, int intvalue) {
  struct ASTnode *n;

  // Malloc 一个新的 ASTnode
  n = (struct ASTnode *) malloc(sizeof(struct ASTnode));
  if (n == NULL)
    fatal("Unable to malloc in mkastnode()");

  // 复制字段值并返回它
  n->op = op;
  n->type = type;
  n->left = left;
  n->mid = mid;
  n->right = right;
  n->v.intvalue = intvalue;
  return (n);
}


// 创建一个 AST 叶子节点
struct ASTnode *mkastleaf(int op, int type, int intvalue) {
  return (mkastnode(op, type, NULL, NULL, NULL, intvalue));
}

// 创建一个一元 AST 节点：只有一个子节点
struct ASTnode *mkastunary(int op, int type, struct ASTnode *left,
			    int intvalue) {
  return (mkastnode(op, type, left, NULL, NULL, intvalue));
}

// 生成并返回一个新的标签号，
// 仅用于 AST 转储目的
static int gendumplabel(void) {
  static int id = 1;
  return (id++);
}

// 给定一个 AST 树，打印它并按照
// genAST() 遵循的遍历方式进行遍历
void dumpAST(struct ASTnode *n, int label, int level) {
  int Lfalse, Lstart, Lend;


  switch (n->op) {
    case A_IF:
      Lfalse = gendumplabel();
      for (int i=0; i < level; i++) fprintf(stdout, " ");
      fprintf(stdout, "A_IF");
      if (n->right) { Lend = gendumplabel();
        fprintf(stdout, ", end L%d", Lend);
      }
      fprintf(stdout, "\n");
      dumpAST(n->left, Lfalse, level+2);
      dumpAST(n->mid, NOLABEL, level+2);
      if (n->right) dumpAST(n->right, NOLABEL, level+2);
      return;
    case A_WHILE:
      Lstart = gendumplabel();
      for (int i=0; i < level; i++) fprintf(stdout, " ");
      fprintf(stdout, "A_WHILE, start L%d\n", Lstart);
      Lend = gendumplabel();
      dumpAST(n->left, Lend, level+2);
      dumpAST(n->right, NOLABEL, level+2);
      return;
  }

  // 对于 A_GLUE 将级别重置为 -2
  if (n->op==A_GLUE) level= -2;

  // 通用 AST 节点处理
  if (n->left) dumpAST(n->left, NOLABEL, level+2);
  if (n->right) dumpAST(n->right, NOLABEL, level+2);


  for (int i=0; i < level; i++) fprintf(stdout, " ");
  switch (n->op) {
    case A_GLUE:
      fprintf(stdout, "\n\n"); return;
    case A_FUNCTION:
      fprintf(stdout, "A_FUNCTION %s\n", Gsym[n->v.id].name); return;
    case A_ADD:
      fprintf(stdout, "A_ADD\n"); return;
    case A_SUBTRACT:
      fprintf(stdout, "A_SUBTRACT\n"); return;
    case A_MULTIPLY:
      fprintf(stdout, "A_MULTIPLY\n"); return;
    case A_DIVIDE:
      fprintf(stdout, "A_DIVIDE\n"); return;
    case A_EQ:
      fprintf(stdout, "A_EQ\n"); return;
    case A_NE:
      fprintf(stdout, "A_NE\n"); return;
    case A_LT:
      fprintf(stdout, "A_LE\n"); return;
    case A_GT:
      fprintf(stdout, "A_GT\n"); return;
    case A_LE:
      fprintf(stdout, "A_LE\n"); return;
    case A_GE:
      fprintf(stdout, "A_GE\n"); return;
    case A_INTLIT:
      fprintf(stdout, "A_INTLIT %d\n", n->v.intvalue); return;
    case A_IDENT:
      if (n->rvalue)
        fprintf(stdout, "A_IDENT rval %s\n", Gsym[n->v.id].name);
      else
        fprintf(stdout, "A_IDENT %s\n", Gsym[n->v.id].name);
      return;
    case A_ASSIGN:
      fprintf(stdout, "A_ASSIGN\n"); return;
    case A_WIDEN:
      fprintf(stdout, "A_WIDEN\n"); return;
    case A_RETURN:
      fprintf(stdout, "A_RETURN\n"); return;
    case A_FUNCCALL:
      fprintf(stdout, "A_FUNCCALL %s\n", Gsym[n->v.id].name); return;
    case A_ADDR:
      fprintf(stdout, "A_ADDR %s\n", Gsym[n->v.id].name); return;
    case A_DEREF:
      if (n->rvalue)
        fprintf(stdout, "A_DEREF rval\n");
      else
        fprintf(stdout, "A_DEREF\n");
      return;
    case A_SCALE:
      fprintf(stdout, "A_SCALE %d\n", n->v.size); return;
    default:
      fatald("Unknown dumpAST operator", n->op);
  }
}