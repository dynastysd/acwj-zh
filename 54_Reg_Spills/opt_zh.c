#include "defs.h"
#include "data.h"
#include "decl.h"

// AST 树优化代码
// Copyright (c) 2019 Warren Toomey, GPL3

// 对具有二元运算符和两个 A_INTLIT 子节点的 AST 树进行折叠。
// 返回原始树或新的叶子节点。
static struct ASTnode *fold2(struct ASTnode *n) {
  int val, leftval, rightval;

  // 从每个子节点获取值
  leftval = n->left->a_intvalue;
  rightval = n->right->a_intvalue;

  // 执行一些二元操作。
  // 对于任何我们不能做的 AST op，返回
  // 原始树。
  switch (n->op) {
    case A_ADD:
      val = leftval + rightval;
      break;
    case A_SUBTRACT:
      val = leftval - rightval;
      break;
    case A_MULTIPLY:
      val = leftval * rightval;
      break;
    case A_DIVIDE:
      // 不要尝试除以零。
      if (rightval == 0)
	return (n);
      val = leftval / rightval;
      break;
    default:
      return (n);
  }

  // 返回具有新值的叶子节点
  return (mkastleaf(A_INTLIT, n->type, NULL, NULL, val));
}

// 对具有一元运算符和一个 INTLIT 子节点的 AST 树进行折叠。
// 返回原始树或新的叶子节点。
static struct ASTnode *fold1(struct ASTnode *n) {
  int val;

  // 获取子节点的值。如果识别则执行
  // 操作。返回新的叶子节点。
  val = n->left->a_intvalue;
  switch (n->op) {
    case A_WIDEN:
      break;
    case A_INVERT:
      val = ~val;
      break;
    case A_LOGNOT:
      val = !val;
      break;
    default:
      return (n);
  }

  // 返回具有新值的叶子节点
  return (mkastleaf(A_INTLIT, n->type, NULL, NULL, val));
}

// 尝试对以根节点 n
// 为根的 AST 树进行常量折叠
static struct ASTnode *fold(struct ASTnode *n) {

  if (n == NULL)
    return (NULL);

  // 在左子节点上折叠，然后
  // 在右子节点上做同样的事情
  n->left = fold(n->left);
  n->right = fold(n->right);

  // 如果两个子节点都是 A_INTLIT，执行 fold2()
  if (n->left && n->left->op == A_INTLIT) {
    if (n->right && n->right->op == A_INTLIT)
      n = fold2(n);
    else
      // 如果只有左边是 A_INTLIT，执行 fold1()
      n = fold1(n);
  }
  // 返回可能修改的树
  return (n);
}

// 通过在所有子树上进行常量折叠来优化 AST 树
struct ASTnode *optimise(struct ASTnode *n) {
  n = fold(n);
  return (n);
}