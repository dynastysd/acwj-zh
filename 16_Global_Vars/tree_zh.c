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