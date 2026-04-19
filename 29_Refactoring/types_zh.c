#include "defs.h"
#include "data.h"
#include "decl.h"

// 类型和类型处理
// Copyright (c) 2019 Warren Toomey, GPL3

// 如果类型是任何大小的int类型则返回true,否则返回false
int inttype(int type) {
  return ((type & 0xf) == 0);
}

// 如果类型是指针类型则返回true
int ptrtype(int type) {
  return ((type & 0xf) != 0);
}

// 给定一个基本类型,返回
// 指向它的指针类型
int pointer_to(int type) {
  if ((type & 0xf) == 0xf)
    fatald("pointer_to中无法识别类型:", type);
  return (type + 1);
}

// 给定一个基本指针类型,返回
// 它指向的类型
int value_at(int type) {
  if ((type & 0xf) == 0x0)
    fatald("value_at中无法识别类型:", type);
  return (type - 1);
}

// 给定一棵AST树和我们想要的类型,
// 可能通过加宽或缩放修改树,以使其
// 与此类型兼容。如果未发生更改则返回原始树,
// 如果更改了返回修改后的树,如果树与给定类型不兼容则返回NULL
// 如果这将是二元操作的一部分,则AST op不为零
struct ASTnode *modify_type(struct ASTnode *tree, int rtype, int op) {
  int ltype;
  int lsize, rsize;

  ltype = tree->type;

  // 比较标量int类型
  if (inttype(ltype) && inttype(rtype)) {

    // 两种类型相同,无需操作
    if (ltype == rtype)
      return (tree);

    // 获取每种类型的大小
    lsize = genprimsize(ltype);
    rsize = genprimsize(rtype);

    // 树的大小太大
    if (lsize > rsize)
      return (NULL);

    // 向右加宽
    if (rsize > lsize)
      return (mkastunary(A_WIDEN, rtype, tree, 0));
  }
  // 左边为指针
  if (ptrtype(ltype)) {
    // 右边类型相同且不是二元操作则OK
    if (op == 0 && ltype == rtype)
      return (tree);
  }
  // 我们只能在A_ADD或A_SUBTRACT操作上进行缩放
  if (op == A_ADD || op == A_SUBTRACT) {

    // 左边是int类型,右边是指针类型,
    // 且原始类型的大小>1:缩放左边
    if (inttype(ltype) && ptrtype(rtype)) {
      rsize = genprimsize(value_at(rtype));
      if (rsize > 1)
	return (mkastunary(A_SCALE, rtype, tree, rsize));
      else
	return (tree);		// 大小为1,无需缩放
    }
  }
  // 如果到达此处,则类型不兼容
  return (NULL);
}