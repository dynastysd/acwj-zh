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

// 为 IF 语句生成代码
// 以及可选的 ELSE 子句
static int genIF(struct ASTnode *n) {
  int Lfalse, Lend;

  // 生成两个标签：一个用于
  // 假的复合语句，一个用于
  // 整个 IF 语句的结束。
  // 当没有 ELSE 子句时，Lfalse 就是
  // 结束标签！
  Lfalse = genlabel();
  if (n->right)
    Lend = genlabel();

  // 生成条件代码后跟
  // 跳转到假标签的指令。
  // 我们通过将 Lfalse 标签作为寄存器来欺骗。
  genAST(n->left, Lfalse, n->op);
  genfreeregs();

  // 生成真复合语句
  genAST(n->mid, NOREG, n->op);
  genfreeregs();

  // 如果有可选的 ELSE 子句，
  // 生成跳转到结束的指令
  if (n->right)
    cgjump(Lend);

  // 现在是假标签
  cglabel(Lfalse);

  // 可选 ELSE 子句：生成
  // 假复合语句和结束标签
  if (n->right) {
    genAST(n->right, NOREG, n->op);
    genfreeregs();
    cglabel(Lend);
  }

  return (NOREG);
}

// 为 WHILE 语句生成代码
// 以及可选的 ELSE 子句
static int genWHILE(struct ASTnode *n) {
  int Lstart, Lend;

  // 生成开始和结束标签
  // 并输出开始标签
  Lstart = genlabel();
  Lend = genlabel();
  cglabel(Lstart);

  // 生成条件代码后跟
  // 跳转到结束标签的指令。
  // 我们通过将 Lfalse 标签作为寄存器来欺骗。
  genAST(n->left, Lend, n->op);
  genfreeregs();

  // 为循环体生成复合语句
  genAST(n->right, NOREG, n->op);
  genfreeregs();

  // 最后输出跳回条件的指令，
  // 和结束标签
  cgjump(Lstart);
  cglabel(Lend);
  return (NOREG);
}

// 给定一个 AST，之前保存
// 右值的寄存器（如果有），以及
// 父级的 AST 操作，
// 递归生成汇编代码。
// 返回持有树最终值的寄存器 id
int genAST(struct ASTnode *n, int reg, int parentASTop) {
  int leftreg, rightreg;

  // 我们在顶部有特定的 AST 节点处理
  switch (n->op) {
    case A_IF:
      return (genIF(n));
    case A_WHILE:
      return (genWHILE(n));
    case A_GLUE:
      // 执行每个子语句，并在每个子语句
      // 之后释放寄存器
      genAST(n->left, NOREG, n->op);
      genfreeregs();
      genAST(n->right, NOREG, n->op);
      genfreeregs();
      return (NOREG);
    case A_FUNCTION:
      // 在代码之前生成函数的前导码
      cgfuncpreamble(n->v.id);
      genAST(n->left, NOREG, n->op);
      cgfuncpostamble(n->v.id);
      return (NOREG);
  }

  // 下面是通用 AST 节点处理

  // 获取左右子树的值
  if (n->left)
    leftreg = genAST(n->left, NOREG, n->op);
  if (n->right)
    rightreg = genAST(n->right, leftreg, n->op);

  switch (n->op) {
    case A_ADD:
      return (cgadd(leftreg, rightreg));
    case A_SUBTRACT:
      return (cgsub(leftreg, rightreg));
    case A_MULTIPLY:
      return (cgmul(leftreg, rightreg));
    case A_DIVIDE:
      return (cgdiv(leftreg, rightreg));
    case A_EQ:
    case A_NE:
    case A_LT:
    case A_GT:
    case A_LE:
    case A_GE:
      // 如果父 AST 节点是 A_IF 或 A_WHILE，生成
      // 比较后跳转。否则，比较寄存器
      // 并根据比较结果将其中一个设为 1 或 0
      if (parentASTop == A_IF || parentASTop == A_WHILE)
	return (cgcompare_and_jump(n->op, leftreg, rightreg, reg));
      else
	return (cgcompare_and_set(n->op, leftreg, rightreg));
    case A_INTLIT:
      return (cgloadint(n->v.intvalue, n->type));
    case A_IDENT:
      return (cgloadglob(n->v.id));
    case A_LVIDENT:
      return (cgstorglob(reg, n->v.id));
    case A_ASSIGN:
      // 工作已经完成，返回结果
      return (rightreg);
    case A_PRINT:
      // 打印左子节点的值
      // 并返回无寄存器
      genprintint(leftreg);
      genfreeregs();
      return (NOREG);
    case A_WIDEN:
      // 将子节点的类型扩展到父节点的类型
      return (cgwiden(leftreg, n->left->type, n->type));
    case A_RETURN:
      cgreturn(leftreg, Functionid);
      return (NOREG);
    case A_FUNCCALL:
      return (cgcall(leftreg, n->v.id));
    default:
      fatald("Unknown AST operator", n->op);
  }
  return (NOREG);	// 保持 -Wall 愉快
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
void genprintint(int reg) {
  cgprintint(reg);
}
void genglobsym(int id) {
  cgglobsym(id);
}
int genprimsize(int type) {
  return (cgprimsize(type));
}