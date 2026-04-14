#include "defs.h"
#include "data.h"
#include "decl.h"

// 通用代码生成器
// Copyright (c) 2019 Warren Toomey, GPL3

// 生成并返回一个新的标签号
static int label(void) {
  static int id = 1;
  return (id++);
}

// 生成 IF 语句的代码
// 以及可选的 ELSE 子句
static int genIFAST(struct ASTnode *n) {
  int Lfalse, Lend;

  // 生成两个标签：一个用于
  // 假的复合语句，另一个用于
  // 整个 IF 语句的结尾。
  // 当没有 ELSE 子句时，Lfalse _就是_
  // 结尾标签！
  Lfalse = label();
  if (n->right)
    Lend = label();

  // 生成条件代码，后跟
  // 一个零跳转（到假标签）。
  // 我们通过将 Lfalse 标签作为寄存器来欺骗。
  genAST(n->left, Lfalse, n->op);
  genfreeregs();

  // 生成真分支的复合语句
  genAST(n->mid, NOREG, n->op);
  genfreeregs();

  // 如果存在可选的 ELSE 子句，
  // 生成跳转到末尾的跳转
  if (n->right)
    cgjump(Lend);

  // 现在是假分支标签
  cglabel(Lfalse);

  // 可选 ELSE 子句：生成
  // 假分支的复合语句和
  // 末尾标签
  if (n->right) {
    genAST(n->right, NOREG, n->op);
    genfreeregs();
    cglabel(Lend);
  }

  return (NOREG);
}

// 给定一个抽象语法树、保存先前右值的寄存器（如果有）以及父节点的 AST 操作码，
// 递归生成汇编代码。
// 返回包含树最终值的寄存器 id
int genAST(struct ASTnode *n, int reg, int parentASTop) {
  int leftreg, rightreg;

  // 现在在顶部有特定的 AST 节点处理
  switch (n->op) {
    case A_IF:
      return (genIFAST(n));
    case A_GLUE:
      // 执行每个子语句，并在每个子语句后释放寄存器
      genAST(n->left, NOREG, n->op);
      genfreeregs();
      genAST(n->right, NOREG, n->op);
      genfreeregs();
      return (NOREG);
  }

  // 以下是常规 AST 节点处理

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
      // 如果父 AST 节点是 A_IF，则生成比较后跳转。
      // 否则，比较寄存器并根据比较结果将其设为 1 或 0。
      if (parentASTop == A_IF)
	return (cgcompare_and_jump(n->op, leftreg, rightreg, reg));
      else
	return (cgcompare_and_set(n->op, leftreg, rightreg));
    case A_INTLIT:
      return (cgloadint(n->v.intvalue));
    case A_IDENT:
      return (cgloadglob(Gsym[n->v.id].name));
    case A_LVIDENT:
      return (cgstorglob(reg, Gsym[n->v.id].name));
    case A_ASSIGN:
      // 工作已经完成，返回结果
      return (rightreg);
    case A_PRINT:
      // 打印左子节点的值
      // 并返回无寄存器
      genprintint(leftreg);
      genfreeregs();
      return (NOREG);
    default:
      fatald("Unknown AST operator", n->op);
  }
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

void genglobsym(char *s) {
  cgglobsym(s);
}