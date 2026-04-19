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
// 以及可选的 ELSE 子句
static int genIF(struct ASTnode *n, int looptoplabel, int loopendlabel) {
  int Lfalse, Lend;

  // 生成两个标签：一个用于
  // 假的复合语句，一个用于
  // 整个 IF 语句的结束。
  // 当没有 ELSE 子句时，Lfalse _就是_
  // 结束标签！
  Lfalse = genlabel();
  if (n->right)
    Lend = genlabel();

  // 生成条件代码，然后
  // 跳转到假标签。
  genAST(n->left, Lfalse, NOLABEL, NOLABEL, n->op);
  genfreeregs();

  // 生成真的复合语句
  genAST(n->mid, NOLABEL, looptoplabel, loopendlabel, n->op);
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
    genAST(n->right, NOLABEL, NOLABEL, NOLABEL, n->op);
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
  genAST(n->left, Lend, Lstart, Lend, n->op);
  genfreeregs();

  // 生成循环体的复合语句
  genAST(n->right, NOLABEL, Lstart, Lend, n->op);
  genfreeregs();

  // 最后输出跳回条件的跳转，
  // 和结束标签
  cgjump(Lstart);
  cglabel(Lend);
  return (NOREG);
}

// 生成 SWITCH 语句的代码
static int genSWITCH(struct ASTnode *n) {
  int *caseval, *caselabel;
  int Ljumptop, Lend;
  int i, reg, defaultlabel = 0, casecount = 0;
  struct ASTnode *c;

  // 为 case 值和相关标签创建数组。
  // 确保每个数组至少有一个位置。
  caseval = (int *) malloc((n->intvalue + 1) * sizeof(int));
  caselabel = (int *) malloc((n->intvalue + 1) * sizeof(int));

  // 为跳转表顶部和 switch 语句末尾生成标签。
  // 设置一个默认标签，以防我们没有 default。
  Ljumptop = genlabel();
  Lend = genlabel();
  defaultlabel = Lend;

  // 输出计算 switch 条件的代码
  reg = genAST(n->left, NOLABEL, NOLABEL, NOLABEL, 0);
  cgjump(Ljumptop);
  genfreeregs();

  // 遍历右子节点的链表来
  // 生成每个 case 的代码
  for (i = 0, c = n->right; c != NULL; i++, c = c->right) {

    // 为这个 case 获取一个标签。存储它
    // 和 case 值到数组中。
    // 记录是否是 default case。
    caselabel[i] = genlabel();
    caseval[i] = c->intvalue;
    cglabel(caselabel[i]);
    if (c->op == A_DEFAULT)
      defaultlabel = caselabel[i];
    else
      casecount++;

    // 生成 case 代码。传入结束的标签用于 break
    genAST(c->left, NOLABEL, NOLABEL, Lend, 0);
    genfreeregs();
  }

  // 确保最后一个 case 跳过 switch 表
  cgjump(Lend);

  // 现在输出 switch 表和结束标签。
  cgswitch(reg, casecount, Ljumptop, caselabel, caseval, defaultlabel);
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

  // 如果有参数列表，从
  // 最后一个参数（右子节点）到
  // 第一个参数遍历此列表
  while (gluetree) {
    // 计算表达式的值
    reg = genAST(gluetree->right, NOLABEL, NOLABEL, NOLABEL, gluetree->op);
    // 复制到第 n 个函数参数：大小为 1, 2, 3, ...
    cgcopyarg(reg, gluetree->size);
    // 记录第一个（最高）参数编号
    if (numargs == 0)
      numargs = gluetree->size;
    genfreeregs();
    gluetree = gluetree->left;
  }

  // 调用函数，清理栈（基于 numargs），
  // 并返回其结果
  return (cgcall(n->sym, numargs));
}

// 给定一个 AST、一个可选标签和
// 父节点的 AST op，递归生成汇编代码。
// 返回包含树最终值的寄存器 id。
int genAST(struct ASTnode *n, int iflabel, int looptoplabel,
	   int loopendlabel, int parentASTop) {
  int leftreg, rightreg;

  // 我们在顶部有一些特定的 AST 节点处理，
  // 这样我们就不会立即求值子子树
  switch (n->op) {
    case A_IF:
      return (genIF(n, looptoplabel, loopendlabel));
    case A_WHILE:
      return (genWHILE(n));
    case A_SWITCH:
      return (genSWITCH(n));
    case A_FUNCCALL:
      return (gen_funccall(n));
    case A_GLUE:
      // 执行每个子语句，并在每个子语句
      // 之后释放寄存器
      if (n->left != NULL) genAST(n->left, iflabel, looptoplabel, loopendlabel, n->op);
      genfreeregs();
      if (n->right != NULL) genAST(n->right, iflabel, looptoplabel, loopendlabel, n->op);
      genfreeregs();
      return (NOREG);
    case A_FUNCTION:
      // 在子树的代码之前生成
      // 函数的前导码
      cgfuncpreamble(n->sym);
      genAST(n->left, NOLABEL, NOLABEL, NOLABEL, n->op);
      cgfuncpostamble(n->sym);
      return (NOREG);
  }

  // 下面是通用的 AST 节点处理

  // 获取左右子树的值
  if (n->left)
    leftreg = genAST(n->left, NOLABEL, NOLABEL, NOLABEL, n->op);
  if (n->right)
    rightreg = genAST(n->right, NOLABEL, NOLABEL, NOLABEL, n->op);

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
	return (cgcompare_and_jump(n->op, leftreg, rightreg, iflabel));
      else
	return (cgcompare_and_set(n->op, leftreg, rightreg));
    case A_INTLIT:
      return (cgloadint(n->intvalue, n->type));
    case A_STRLIT:
      return (cgloadglobstr(n->intvalue));
    case A_IDENT:
      // 如果我们是 rvalue 或被解引用，
      // 则加载我们的值
      if (n->rvalue || parentASTop == A_DEREF) {
	if (n->sym->class == C_GLOBAL) {
	  return (cgloadglob(n->sym, n->op));
	} else {
	  return (cgloadlocal(n->sym, n->op));
	}
      } else
	return (NOREG);
    case A_ASSIGN:
      // 我们是赋值给标识符还是通过指针赋值？
      switch (n->right->op) {
	case A_IDENT:
	  if (n->right->sym->class == C_GLOBAL)
	    return (cgstorglob(leftreg, n->right->sym));
	  else
	    return (cgstorlocal(leftreg, n->right->sym));
	case A_DEREF:
	  return (cgstorderef(leftreg, rightreg, n->right->type));
	default:
	  fatald("Can't A_ASSIGN in genAST(), op", n->op);
      }
    case A_WIDEN:
      // 将子节点的类型扩展为父节点的类型
      return (cgwiden(leftreg, n->left->type, n->type));
    case A_RETURN:
      cgreturn(leftreg, Functionid);
      return (NOREG);
    case A_ADDR:
      return (cgaddress(n->sym));
    case A_DEREF:
      // 如果我们是 rvalue，解引用获取我们指向的值，
      // 否则保留它让 A_ASSIGN 存储通过指针
      if (n->rvalue)
	return (cgderef(leftreg, n->left->type));
      else
	return (leftreg);
    case A_SCALE:
      // 小优化：如果
      // 缩放值是 2 的已知幂次，则使用移位
      switch (n->size) {
	case 2:
	  return (cgshlconst(leftreg, 1));
	case 4:
	  return (cgshlconst(leftreg, 2));
	case 8:
	  return (cgshlconst(leftreg, 3));
	default:
	  // 用大小加载一个寄存器并
	  // 将 leftreg 乘以这个大小
	  rightreg = cgloadint(n->size, P_INT);
	  return (cgmul(leftreg, rightreg));
      }
    case A_POSTINC:
    case A_POSTDEC:
      // 将变量的值加载到寄存器中
      // 然后后递增/递减它
      if (n->sym->class == C_GLOBAL)
	return (cgloadglob(n->sym, n->op));
      else
	return (cgloadlocal(n->sym, n->op));
    case A_PREINC:
    case A_PREDEC:
      // 将变量的值加载到寄存器中
      // 然后前递增/递减它
      if (n->left->sym->class == C_GLOBAL)
	return (cgloadglob(n->left->sym, n->op));
      else
	return (cgloadlocal(n->left->sym, n->op));
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
      return (cgboolean(leftreg, parentASTop, iflabel));
    case A_BREAK:
      cgjump(loopendlabel);
      return (NOREG);
    case A_CONTINUE:
      cgjump(looptoplabel);
      return (NOREG);
    default:
      fatald("Unknown AST operator", n->op);
  }
  return (NOREG);		// 保持 -Wall 开心
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
void genglobsym(struct symtable *node) {
  cgglobsym(node);
}
int genglobstr(char *strvalue) {
  int l = genlabel();
  cgglobstr(l, strvalue);
  return (l);
}
int genprimsize(int type) {
  return (cgprimsize(type));
}
int genalign(int type, int offset, int direction) {
  return (cgalign(type, offset, direction));
}