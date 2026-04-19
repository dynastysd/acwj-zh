#include "defs.h"
#include "data.h"
#include "tree.h"

// AST Tree Optimisation Code
// Copyright (c) 2019 Warren Toomey, GPL3

// 折叠具有一元运算符和一个 INTLIT 子节点的 AST 树。
// 返回原始树或新的叶子节点。
static struct ASTnode *fold1(struct ASTnode *n) {
  int val;

  // 获取子节点的值。如果识别到操作则执行。
  // 返回新的叶子节点。
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
    case A_SCALE:
      val = val * n->a_intvalue;
      break;
    default:
      return (n);
  }

  // 返回具有新值的叶子节点
  return (mkastleaf(A_INTLIT, n->type, NULL, NULL, val));
}

// 折叠具有二元运算符和两个 A_INTLIT 子节点的 AST 树。
// 返回原始树或新的叶子节点。
static struct ASTnode *fold2(struct ASTnode *n) {
  int val, leftval, rightval;

  // 从每个子节点获取值
  leftval = n->left->a_intvalue;
  rightval = n->right->a_intvalue;

  // 执行一些二元操作。
  // 对于任何我们无法处理的 AST op，返回原始树。
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
    case A_AND:
      val = leftval & rightval;
      break;
    case A_OR:
      val = leftval | rightval;
      break;
    case A_XOR:
      val = leftval ^ rightval;
      break;
    case A_LSHIFT:
      val = leftval << rightval;
      break;
    case A_RSHIFT:
      val = leftval >> rightval;
      break;
    default:
      return (n);
  }

  // 返回具有新值的叶子节点
  return (mkastleaf(A_INTLIT, n->type, NULL, NULL, val));
}

// 通过深度优先节点遍历来优化 AST 树
struct ASTnode *optimise(struct ASTnode *n) {

  if (n == NULL) return (NULL);

  // 先优化左子节点再优化右子节点
  n->left = optimise(n->left);
  if (n->left!=NULL) n->leftid= n->left->nodeid;
  n->right = optimise(n->right);
  if (n->right!=NULL) n->rightid= n->right->nodeid;

  // 折叠字面量常量：
  // 如果两个子节点都是 A_INTLIT，执行 fold2()
  if (n->left && n->left->op == A_INTLIT) {
    if (n->right && n->right->op == A_INTLIT)
      n = fold2(n);
    else
      // 如果只有左侧是 A_INTLIT，执行 fold1()
      n = fold1(n);
  }

  // 返回可能修改过的树
  return (n);
}