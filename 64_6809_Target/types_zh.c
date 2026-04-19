#include "defs.h"
#include "data.h"
#include "gen.h"
#include "misc.h"
#include "target.h"
#include "tree.h"

// Types and type handling
// Copyright (c) 2019 Warren Toomey, GPL3

// 如果类型是任何大小的 int 类型则返回真，否则返回假
int inttype(int type) {
  return (((type & 0xf) == 0) && (type >= P_CHAR && type <= P_LONG));
}

// 如果类型是指针类型则返回真
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

// 给定一个指针类型，返回
// 它所指向的类型
int value_at(int type) {
  if ((type & 0xf) == 0x0)
    fatald("Unrecognised in value_at: type", type);
  return (type - 1);
}

// 给定一个类型和一个组合类型指针，返回
// 该类型的大小（字节）
int typesize(int type, struct symtable *ctype) {
  if (type == P_STRUCT || type == P_UNION)
    return (ctype->size);
  return (genprimsize(type));
}

// 给定一个 AST 树和我们希望它成为的类型，
// 可能通过加宽或缩放来修改树，使其
// 与该类型兼容。如果未发生更改则返回原始树，
// 如果树与给定类型不兼容则返回修改后的树或 NULL。
// 如果这将是二元操作的一部分，则 AST op 不为零。
struct ASTnode *modify_type(struct ASTnode *tree, int rtype,
 			    struct symtable *rctype, int op) {
  int ltype;
  int lsize, rsize;

  ltype = tree->type;

  // 对于 A_LOGOR 和 A_LOGAND，两种类型都必须是 int 或指针类型
  if (op==A_LOGOR || op==A_LOGAND) {
    if (!inttype(ltype) && !ptrtype(ltype))
      return(NULL);
    if (!inttype(ltype) && !ptrtype(rtype))
      return(NULL);
    return (tree);
  }

  // 目前对这些类型还不清楚
  if (ltype == P_STRUCT || ltype == P_UNION)
    fatal("Don't know how to do this yet");
  if (rtype == P_STRUCT || rtype == P_UNION)
    fatal("Don't know how to do this yet");

  // 比较标量 int 类型
  if (inttype(ltype) && inttype(rtype)) {

    // 两种类型相同，无需操作
    if (ltype == rtype)
      return (tree);

    // 获取每种类型的大小
    lsize = typesize(ltype, NULL);
    rsize = typesize(rtype, NULL);

    // 树类型大小太大且无法缩窄
    if (lsize > rsize)
      return (NULL);

    // 加宽到右侧
    if (rsize > lsize)
      return (mkastunary(A_WIDEN, rtype, NULL, tree, NULL, 0));
  }

  // 对于指针
  if (ptrtype(ltype) && ptrtype(rtype)) {
    // 我们可以比较它们
    if (op >= A_EQ && op <= A_GE)
      return (tree);

    // 注意：我们可以做减法，但应该按指针所指向事物的大小来缩放。
    // 目前，我们只处理 char 指针。
    if (op== A_SUBTRACT && ltype== pointer_to(P_CHAR) && ltype==rtype) {
      tree->type= P_INT;
      return (tree);
    }

    // 对于非二元操作的相同类型比较是 OK 的，
    // 或者当任一树是 'void *' 类型时也是 OK 的。
    if (op == 0 &&
      (ltype == rtype || ltype == pointer_to(P_VOID) || rtype == pointer_to(P_VOID)) )
      return (tree);
  }

  // 我们只能对加法和减法操作进行缩放
  if (op == A_ADD || op == A_SUBTRACT ||
      op == A_ASPLUS || op == A_ASMINUS) {

    // 左侧是 int 类型，右侧是指针类型，且原始类型的大小 >1：缩放左侧
    if (inttype(ltype) && ptrtype(rtype)) {
      rsize = typesize(value_at(rtype), rctype);
      if (rsize > 1)
	return (mkastunary(A_SCALE, rtype, rctype, tree, NULL, rsize));
      else
        // 不需要缩放，但我们需要加宽到指针大小
        return (mkastunary(A_WIDEN, rtype, NULL, tree, NULL, 0));
    }
  }

  // 如果到达这里，类型不兼容
  return (NULL);
}