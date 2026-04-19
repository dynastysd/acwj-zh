#include "defs.h"
#include "data.h"
#include "decl.h"

// 通用代码生成器
// Copyright (c) 2019 Warren Toomey, GPL3

// 生成并返回一个新的标签号
int genlabel(void) {
  static int id = 1;
  return (id++);
}

// 生成 IF 语句的代码
// 及其可选的 ELSE 子句
static int genIF(struct ASTnode *n) {
  int Lfalse, Lend;

  // 生成两个标签：一个用于
  // 假的复合语句，一个用于
  // 整个 IF 语句的结束。
  // 如果没有 ELSE 子句，Lfalse 就是
  // 结束标签！
  Lfalse = genlabel();
  if (n->right)
    Lend = genlabel();

  // 生成条件代码，然后
  // 跳转到假标签。
  genAST(n->left, Lfalse, n->op);
  genfreeregs();

  // 生成真的复合语句
  genAST(n->mid, NOLABEL, n->op);
  genfreeregs();

  // 如果有可选的 ELSE 子句，
  // 生成跳转到结束的跳转
  if (n->right)
    cgjump(Lend);

  // 现在是假标签
  cglabel(Lfalse);

  // 可选的 ELSE 子句：生成
  // 假的复合语句和
  // 结束标签
  if (n->right) {
    genAST(n->right, NOLABEL, n->op);
    genfreeregs();
    cglabel(Lend);
  }

  return (NOREG);
}

// 生成 WHILE 语句的代码
static int genWHILE(struct ASTnode *n) {
  int Lstart, Lend;

  // 生成开始和结束标签
  // 并输出开始标签
  Lstart = genlabel();
  Lend = genlabel();
  cglabel(Lstart);

  // 生成条件代码，然后
  // 跳转到结束标签。
  genAST(n->left, Lend, n->op);
  genfreeregs();

  // 生成复合语句作为循环体
  genAST(n->right, NOLABEL, n->op);
  genfreeregs();

  // 最后输出跳回条件的跳转，
  // 和结束标签
  cgjump(Lstart);
  cglabel(Lend);
  return (NOREG);
}

// 生成将函数调用的参数复制到
// 其参数的代码，然后调用
// 函数本身。返回保存
// 函数返回值的寄存器。
static int gen_funccall(struct ASTnode *n) {
  struct ASTnode *gluetree = n->left;
  int reg;
  int numargs = 0;

  // 如果有参数列表，从最后一个
  // 参数（右手子节点）到第一个
  // 遍历此列表
  while (gluetree) {
    // 计算表达式的值
    reg = genAST(gluetree->right, NOLABEL, gluetree->op);
    // 将其复制到第 n 个函数参数：大小为 1、2、3、...
    cgcopyarg(reg, gluetree->v.size);
    // 保留第一个（最高）参数编号
    if (numargs == 0)
      numargs = gluetree->v.size;
    genfreeregs();
    gluetree = gluetree->left;
  }

  // 调用函数，清理栈（基于 numargs），
  // 并返回其结果
  return (cgcall(n->v.id, numargs));
}

// 给定一个 AST、一个可选标签和
// 父节点的 AST op，递归生成
// 汇编代码。返回包含树最终值的
// 寄存器 id。
int genAST(struct ASTnode *n, int label, int parentASTop) {
  int leftreg, rightreg;

  // 我们在顶部对某些特定 AST 节点进行处理，
  // 以便立即不求值子子树
  switch (n->op) {
  case A_IF:
    return (genIF(n));
  case A_WHILE:
    return (genWHILE(n));
  case A_FUNCCALL:
    return (gen_funccall(n));
  case A_GLUE:
    // 执行每个子语句，并在
    // 每个子语句后释放寄存器
    genAST(n->left, NOLABEL, n->op);
    genfreeregs();
    genAST(n->right, NOLABEL, n->op);
    genfreeregs();
    return (NOREG);
  case A_FUNCTION:
    // 在子树中的代码之前
    // 生成函数的前导码
    cgfuncpreamble(n->v.id);
    genAST(n->left, NOLABEL, n->op);
    cgfuncpostamble(n->v.id);
    return (NOREG);
  }

  // 以下是通用 AST 节点处理

  // 获取左右子树的值
  if (n->left)
    leftreg = genAST(n->left, NOLABEL, n->op);
  if (n->right)
    rightreg = genAST(n->right, NOLABEL, n->op);

  switch (n->op) {
  case A_ADD:
    return (cgadd(leftreg, rightreg));
  case A_SUBTRACT:
    return (cgsub(leftreg, rightreg));
  case A_MULTIPLY:
    return (cgmul(leftreg, rightreg));
  case A_DIVIDE:
    return (cgdiv(leftreg, rightreg));
  case A_AND:
    return (cgand(leftreg, rightreg));
  case A_OR:
    return (cgor(leftreg, rightreg));
  case A_XOR:
    return (cgxor(leftreg, rightreg));
  case A_LSHIFT:
    return (cgshl(leftreg, rightreg));
  case A_RSHIFT:
    return (cgshr(leftreg, rightreg));
  case A_EQ:
  case A_NE:
  case A_LT:
  case A_GT:
  case A_LE:
  case A_GE:
    // 如果父 AST 节点是 A_IF 或 A_WHILE，生成
    // 比较后跳转。否则，比较寄存器
    // 并根据比较结果将其设置为 1 或 0。
    if (parentASTop == A_IF || parentASTop == A_WHILE)
      return (cgcompare_and_jump(n->op, leftreg, rightreg, label));
    else
      return (cgcompare_and_set(n->op, leftreg, rightreg));
  case A_INTLIT:
    return (cgloadint(n->v.intvalue, n->type));
  case A_STRLIT:
    return (cgloadglobstr(n->v.id));
  case A_IDENT:
    // 如果我们是右值或
    // 被解引用，则加载我们的值
    if (n->rvalue || parentASTop == A_DEREF) {
      if (Symtable[n->v.id].class == C_GLOBAL) {
	return (cgloadglob(n->v.id, n->op));
      } else {
	return (cgloadlocal(n->v.id, n->op));
      }
    } else
      return (NOREG);
  case A_ASSIGN:
    // 我们是赋值给标识符还是通过指针赋值？
    switch (n->right->op) {
    case A_IDENT:
      if (Symtable[n->right->v.id].class == C_GLOBAL)
	return (cgstorglob(leftreg, n->right->v.id));
      else
	return (cgstorlocal(leftreg, n->right->v.id));
    case A_DEREF:
      return (cgstorderef(leftreg, rightreg, n->right->type));
    default:
      fatald("Can't A_ASSIGN in genAST(), op", n->op);
    }
  case A_WIDEN:
    // 将子节点的类型扩展到父节点的类型
    return (cgwiden(leftreg, n->left->type, n->type));
  case A_RETURN:
    cgreturn(leftreg, Functionid);
    return (NOREG);
  case A_ADDR:
    return (cgaddress(n->v.id));
  case A_DEREF:
    // 如果我们是右值，解引用获取我们指向的值，
    // 否则将其留给 A_ASSIGN 通过指针存储
    if (n->rvalue)
      return (cgderef(leftreg, n->left->type));
    else
      return (leftreg);
  case A_SCALE:
    // 小优化：如果
    // 缩放值是 2 的幂次方，则使用移位
    switch (n->v.size) {
    case 2:
      return (cgshlconst(leftreg, 1));
    case 4:
      return (cgshlconst(leftreg, 2));
    case 8:
      return (cgshlconst(leftreg, 3));
    default:
      // 将寄存器加载大小，然后
      // 用此大小乘以 leftreg
      rightreg = cgloadint(n->v.size, P_INT);
      return (cgmul(leftreg, rightreg));
    }
  case A_POSTINC:
  case A_POSTDEC:
    // 将变量的值加载到寄存器中
    // 然后后增/减它
    if (Symtable[n->v.id].class == C_GLOBAL)
      return (cgloadglob(n->v.id, n->op));
    else
      return (cgloadlocal(n->v.id, n->op));
  case A_PREINC:
  case A_PREDEC:
    // 将变量的值加载到寄存器中
    // 然后前增/减它
    if (Symtable[n->left->v.id].class == C_GLOBAL)
      return (cgloadglob(n->left->v.id, n->op));
    else
      return (cgloadlocal(n->left->v.id, n->op));
  case A_NEGATE:
    return (cgnegate(leftreg));
  case A_INVERT:
    return (cginvert(leftreg));
  case A_LOGNOT:
    return (cglognot(leftreg));
  case A_TOBOOL:
    // 如果父 AST 节点是 A_IF 或 A_WHILE，生成
    // 比较后跳转。否则，根据其为零或非零
    // 将寄存器设置为 0 或 1
    return (cgboolean(leftreg, parentASTop, label));
  default:
    fatald("Unknown AST operator", n->op);
  }
  return (NOREG);		// 保持 -Wall 高兴
}

void genpreamble() {
  cgpreamble();
}
void genpostamble() {
  cgpostamble();
}
void genfreeregs() {
  freeall_registers();
}
void genglobsym(int id) {
  cgglobsym(id);
}
int genglobstr(char *strvalue) {
  int l = genlabel();
  cgglobstr(l, strvalue);
  return (l);
}
int genprimsize(int type) {
  return (cgprimsize(type));
}