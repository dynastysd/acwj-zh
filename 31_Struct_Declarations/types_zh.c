#include "defs.h"
#include "data.h"
#include "decl.h"

// 类型和类型处理
// Copyright (c) 2019 Warren Toomey, GPL3

// 如果类型是任何大小的int类型则返回true，
// 否则返回false
int inttype(int type) {
  return ((type & 0xf) == 0);
}

// 如果类型是指针类型则返回true
int ptrtype(int type) {
  return ((type & 0xf) != 0);
}

// 给定原始类型，返回
// 指向它的指针类型
int pointer_to(int type) {
  if ((type & 0xf) == 0xf)
    fatald("pointer_to中无法识别的类型", type);
  return (type + 1);
}

// 给定原始指针类型，返回
// 它指向的类型
int value_at(int type) {
  if ((type & 0xf) == 0x0)
    fatald("value_at中无法识别的类型", type);
  return (type - 1);
}

// 给定类型和复合类型指针，返回
// 此类型的大小(以字节为单位)
int typesize(int type, struct symtable *ctype) {
  if (type == P_STRUCT)
    return(ctype->size);
  return(genprimsize(type));
}

// 给定AST树和我们想要它成为的类型，
// 可能通过加宽或缩放修改树使其
// 与此类型兼容。如果未发生更改则返回原始树，
// 如果类型不兼容则返回修改后的树或NULL
// 如果这将是二元操作的一部分，则AST op不为零
struct ASTnode *modify_type(struct ASTnode *tree, int rtype, int op) {
  int ltype;
  int lsize, rsize;

  ltype = tree->type;

  // XXX 目前对这些一无所知
  if (ltype == P_STRUCT)
    fatal("还不知道如何处理这个");
  if (rtype == P_STRUCT)
    fatal("还不知道如何处理这个");

  // 比较标量int类型
  if (inttype(ltype) && inttype(rtype)) {

    // 两种类型相同，无需操作
    if (ltype == rtype)
      return (tree);

    // 获取每种类型的大小
    lsize = typesize(ltype, NULL); // XXX 很快修复
    rsize = typesize(rtype, NULL); // XXX 很快修复

    // 树的大小太大
    if (lsize > rsize)
      return (NULL);

    // 加宽到右边
    if (rsize > lsize)
      return (mkastunary(A_WIDEN, rtype, tree, NULL, 0));
  }
  // 左边的是指针
  if (ptrtype(ltype)) {
    // 右边类型相同且不是二元操作是可以的
    if (op == 0 && ltype == rtype)
      return (tree);
  }
  // 我们只能在A_ADD或A_SUBTRACT操作上缩放
  if (op == A_ADD || op == A_SUBTRACT) {

    // 左边是int类型，右边是指针类型，
    // 如果原始类型的大小>1:缩放左边
    if (inttype(ltype) && ptrtype(rtype)) {
      rsize = genprimsize(value_at(rtype));
      if (rsize > 1)
	return (mkastunary(A_SCALE, rtype, tree, NULL, rsize));
      else
	return (tree);		// 大小为1，无需缩放
    }
  }
  // 如果到达这里，类型不兼容
  return (NULL);
}
