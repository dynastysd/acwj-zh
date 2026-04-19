#include "defs.h"
#include "data.h"
#include "decl.h"

// 类型和类型处理
// Copyright (c) 2019 Warren Toomey, GPL3

// 如果类型是任何大小的int类型则返回true，否则返回false
int inttype(int type) {
  return (((type & 0xf) == 0) && (type >= P_CHAR && type <= P_LONG));
}

// 如果类型是指针类型则返回true
int ptrtype(int type) {
  return ((type & 0xf) != 0);
}

// 给定一个原始类型，返回
// 指向它的指针类型
int pointer_to(int type) {
  if ((type & 0xf) == 0xf)
    fatald("Unrecognised in pointer_to: type", type);
  return (type + 1);
}

// 给定一个原始指针类型，返回
// 它指向的类型
int value_at(int type) {
  if ((type & 0xf) == 0x0)
    fatald("Unrecognised in value_at: type", type);
  return (type - 1);
}

// 给定一个类型和组合类型指针，返回
// 此类型的大小（以字节为单位）
int typesize(int type, struct symtable *ctype) {
  if (type == P_STRUCT || type == P_UNION)
    return (ctype->size);
  return (genprimsize(type));
}

// 给定一个AST树和我们希望它成为的类型，
// 可能通过加宽或缩放修改树以使其与此类型兼容。
// 如果没有发生更改则返回原始树，修改后的树，
// 如果树与给定类型不兼容则返回NULL。
// 如果这将是二元操作的一部分，则AST op不为零。
struct ASTnode *modify_type(struct ASTnode *tree, int rtype,
			    struct symtable *rctype, int op) {
  int ltype;
  int lsize, rsize;

  ltype = tree->type;

  // 对于A_LOGOR和A_LOGAND，两种类型都必须是int或指针类型
  if (op==A_LOGOR || op==A_LOGAND) {
    if (!inttype(ltype) && !ptrtype(ltype))
      return(NULL);
    if (!inttype(ltype) && !ptrtype(rtype))
      return(NULL);
    return (tree);
  }

  // XXX 这些我还不知道
  if (ltype == P_STRUCT || ltype == P_UNION)
    fatal("Don't know how to do this yet");
  if (rtype == P_STRUCT || rtype == P_UNION)
    fatal("Don't know how to do this yet");

  // 比较标量int类型
  if (inttype(ltype) && inttype(rtype)) {

    // 两种类型相同，什么都不做
    if (ltype == rtype)
      return (tree);

    // 获取每种类型的大小
    lsize = typesize(ltype, NULL);
    rsize = typesize(rtype, NULL);

    // 树的大小太大
    if (lsize > rsize)
      return (NULL);

    // 向右加宽
    if (rsize > lsize)
      return (mkastunary(A_WIDEN, rtype, NULL, tree, NULL, 0));
  }
  // 对于指针
  if (ptrtype(ltype) && ptrtype(rtype)) {
    // 我们可以比较它们
    if (op >= A_EQ && op <= A_GE)
      return (tree);

    // 对于非二元操作的相同类型比较是OK的，
    // 或者当左树是'void *'类型时。
    if (op == 0 && (ltype == rtype || ltype == pointer_to(P_VOID)))
      return (tree);
  }
  // 我们只能对A_ADD或A_SUBTRACT操作进行缩放
  if (op == A_ADD || op == A_SUBTRACT) {

    // 左边是int类型，右边是指针类型，
    // 原始类型的大小>1：缩放左边
    if (inttype(ltype) && ptrtype(rtype)) {
      rsize = genprimsize(value_at(rtype));
      if (rsize > 1)
	return (mkastunary(A_SCALE, rtype, rctype, tree, NULL, rsize));
      else
	return (tree);		// 大小为1，无需缩放
    }
  }
  // 如果到这里，类型不兼容
  return (NULL);
}