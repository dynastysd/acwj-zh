#include "defs.h"
#include "data.h"
#include "decl.h"

// 类型和类型处理
// Copyright (c) 2019 Warren Toomey, GPL3

// 如果类型是任何大小的 int 类型则返回 true，
// 否则返回 false
int inttype(int type) {
  return ((type & 0xf) == 0);
}

// 如果类型是指针类型则返回 true
int ptrtype(int type) {
  return ((type & 0xf) != 0);
}

// 给定一个基本类型，返回
// 指向它的指针类型
int pointer_to(int type) {
  if ((type & 0xf) == 0xf)
    fatald("Unrecognised in pointer_to: type", type);
  return (type + 1);
}

// 给定一个基本指针类型，返回
// 它所指向的类型
int value_at(int type) {
  if ((type & 0xf) == 0x0)
    fatald("Unrecognised in value_at: type", type);
  return (type - 1);
}

// 给定一个 AST 树和我们想要的类型，
// 可能通过加宽或缩放修改树，
// 使其与此类型兼容。如果不兼容，
// 则返回原始树、修改后的树，或 NULL。
// 如果这将是二元操作的一部分，则 AST op 不为零。
struct ASTnode *modify_type(struct ASTnode *tree, int rtype, int op) {
  int ltype;
  int lsize, rsize;

  ltype = tree->type;

  // 比较标量 int 类型
  if (inttype(ltype) && inttype(rtype)) {

    // 两种类型相同，无需操作
    if (ltype == rtype)
      return (tree);

    // 获取每种类型的大小
    lsize = genprimsize(ltype);
    rsize = genprimsize(rtype);

    // 树的大小太大
    if (lsize > rsize)
      return (NULL);

    // 加宽到右侧
    if (rsize > lsize)
      return (mkastunary(A_WIDEN, rtype, tree, NULL, 0));
  }
  // 左侧为指针
  if (ptrtype(ltype)) {
    // 右侧类型相同且不是二元操作是可以的
    if (op == 0 && ltype == rtype)
      return (tree);
  }
  // 我们只能在 A_ADD 或 A_SUBTRACT 操作上缩放
  if (op == A_ADD || op == A_SUBTRACT) {

    // 左侧为 int 类型，右侧为指针类型，
    // 原始类型的大小 >1：缩放左侧
    if (inttype(ltype) && ptrtype(rtype)) {
      rsize = genprimsize(value_at(rtype));
      if (rsize > 1)
	return (mkastunary(A_SCALE, rtype, tree, NULL, rsize));
      else
	return (tree);		// 大小为 1，无需缩放
    }
  }
  // 如果我们到这里，类型不兼容
  return (NULL);
}