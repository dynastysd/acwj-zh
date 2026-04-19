#include "defs.h"
#include "data.h"
#include "decl.h"

// AST树优化代码
// Copyright (c) 2019 Warren Toomey, GPL3

// 折叠具有二元运算符
// 和两个A_INTLIT子节点的AST树。返回
// 原始树或新的叶节点。
static struct ASTnode *fold2(struct ASTnode *n) {
  int val, leftval, rightval;

  // 从每个子节点获取值
  leftval = n->left->a_intvalue;
  rightval = n->right->a_intvalue;

  // 执行一些二元操作。
  // 对于任何我们不能做的AST操作，
  // 返回原始树。
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

  // 用新值返回一个叶节点
  return (mkastleaf(A_INTLIT, n->type, NULL, NULL, val));
}

// 折叠具有一元运算符
// 和一个INTLIT子节点的AST树。返回
// 原始树或新的叶节点。
static struct ASTnode *fold1(struct ASTnode *n) {
  int val;

  // 获取子节点值。如果识别则执行操作。
  // 返回新的叶节点。
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

  // 用新值返回一个叶节点
  return (mkastleaf(A_INTLIT, n->type, NULL, NULL, val));
}

// 尝试对以节点n为根的AST树
// 进行常量折叠
static struct ASTnode *fold(struct ASTnode *n) {

  if (n == NULL)
    return (NULL);

  // 在左子树上折叠，然后
  // 在右子树上做同样的操作
  n->left = fold(n->left);
  n->right = fold(n->right);

  // 如果两个子节点都是A_INTLIT，执行fold2()
  if (n->left && n->left->op == A_INTLIT) {
    if (n->right && n->right->op == A_INTLIT)
      n = fold2(n);
    else
      // 如果只有左边是A_INTLIT，执行fold1()
      n = fold1(n);
  }
  // 返回可能修改后的树
  return (n);
}

// 通过在所有子树中
// 进行常量折叠来优化AST树
struct ASTnode *optimise(struct ASTnode *n) {
  n = fold(n);
  return (n);
}