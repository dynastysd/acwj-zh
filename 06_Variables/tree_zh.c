#include "defs.h"
#include "data.h"
#include "decl.h"

// AST 树函数
// Copyright (c) 2019 Warren Toomey, GPL3

// 构建并返回一个通用的抽象语法树节点
struct ASTnode *mkastnode(int op, struct ASTnode *left,
			  struct ASTnode *right, int intvalue) {
  struct ASTnode *n;

  // 为新的 ASTnode 分配内存
  n = (struct ASTnode *) malloc(sizeof(struct ASTnode));
  if (n == NULL)
    fatal("Unable to malloc in mkastnode()");

  // 复制字段值并返回
  n->op = op;
  n->left = left;
  n->right = right;
  n->v.intvalue = intvalue;
  return (n);
}


// 创建一个抽象语法树叶子节点
struct ASTnode *mkastleaf(int op, int intvalue) {
  return (mkastnode(op, NULL, NULL, intvalue));
}

// 创建一个一元抽象语法树节点：只有一个子节点
struct ASTnode *mkastunary(int op, struct ASTnode *left, int intvalue) {
  return (mkastnode(op, left, NULL, intvalue));
}