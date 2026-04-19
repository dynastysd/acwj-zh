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

// 生成IF语句和可选的ELSE子句的代码
static int genIF(struct ASTnode *n) {
  int Lfalse, Lend;

  // 生成两个标签：一个用于
  // 假的复合语句，一个用于
  // 整个IF语句的结束
  // 当没有ELSE子句时，Lfalse就是结束标签！
  Lfalse = genlabel();
  if (n->right)
    Lend = genlabel();

  // 生成条件代码，后跟
  // 跳转到假标签的指令
  genAST(n->left, Lfalse, n->op);
  genfreeregs();

  // 生成真复合语句
  genAST(n->mid, NOLABEL, n->op);
  genfreeregs();

  // 如果有可选的ELSE子句，
  // 生成跳转到结束的指令
  if (n->right)
    cgjump(Lend);

  // 现在是假标签
  cglabel(Lfalse);

  // 可选的ELSE子句：生成
  // 假复合语句和结束标签
  if (n->right) {
    genAST(n->right, NOLABEL, n->op);
    genfreeregs();
    cglabel(Lend);
  }

  return (NOREG);
}

// 生成WHILE语句的代码
static int genWHILE(struct ASTnode *n) {
  int Lstart, Lend;

  // 生成开始和结束标签
  // 并输出开始标签
  Lstart = genlabel();
  Lend = genlabel();
  cglabel(Lstart);

  // 生成条件代码，后跟
  // 跳转到结束标签的指令
  genAST(n->left, Lend, n->op);
  genfreeregs();

  // 生成作为循环体的复合语句
  genAST(n->right, NOLABEL, n->op);
  genfreeregs();

  // 最后输出跳回条件的指令
  // 和结束标签
  cgjump(Lstart);
  cglabel(Lend);
  return (NOREG);
}

// 生成将函数调用的参数复制到
// 其参数的代码，然后调用函数本身
// 返回包含函数返回值的寄存器
static int gen_funccall(struct ASTnode *n) {
  struct ASTnode *gluetree = n->left;
  int reg;
  int numargs = 0;

  // 如果有参数列表，从最后一个参数
  // （右子节点）到第一个参数遍历此列表
  while (gluetree) {
    // 计算表达式的值
    reg = genAST(gluetree->right, NOLABEL, gluetree->op);
    // 将其复制到第n个函数参数：大小为1, 2, 3, ...
    cgcopyarg(reg, gluetree->size);
    // 记录第一个（最高）数量的参数
    if (numargs == 0)
      numargs = gluetree->size;
    genfreeregs();
    gluetree = gluetree->left;
  }

  // 调用函数，清理栈（基于numargs），
  // 并返回其结果
  return (cgcall(n->sym, numargs));
}

// 给定一个AST、一个可选标签和父AST操作码，
// 递归生成汇编代码
// 返回包含树最终值的寄存器id
int genAST(struct ASTnode *n, int label, int parentASTop) {
  int leftreg, rightreg;

  // 在顶部我们对一些特定的AST节点进行处理
  // 这样我们不会立即求值子节点
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
      // 在子树的代码之前生成
      // 函数的前导码
      cgfuncpreamble(n->sym);
      genAST(n->left, NOLABEL, n->op);
      cgfuncpostamble(n->sym);
      return (NOREG);
  }

  // 以下是通用AST节点处理

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
      // 如果父AST节点是A_IF或A_WHILE，生成
      // 比较后跳转。否则，比较寄存器
      // 并根据比较结果将其设置为1或0
      if (parentASTop == A_IF || parentASTop == A_WHILE)
	return (cgcompare_and_jump(n->op, leftreg, rightreg, label));
      else
	return (cgcompare_and_set(n->op, leftreg, rightreg));
    case A_INTLIT:
      return (cgloadint(n->intvalue, n->type));
    case A_STRLIT:
      return (cgloadglobstr(n->intvalue));
    case A_IDENT:
      // 如果我们是rvalue或正在被解引用，
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
      // 将子节点的类型扩展到父节点的类型
      return (cgwiden(leftreg, n->left->type, n->type));
    case A_RETURN:
      cgreturn(leftreg, Functionid);
      return (NOREG);
    case A_ADDR:
      return (cgaddress(n->sym));
    case A_DEREF:
      // 如果我们是rvalue，解引用获取我们指向的值，
      // 否则为A_ASSIGN保留它以通过指针存储
      if (n->rvalue)
	return (cgderef(leftreg, n->left->type));
      else
	return (leftreg);
    case A_SCALE:
      // 小优化：如果比例值是2的幂，
      // 则使用移位
      switch (n->size) {
	case 2:
	  return (cgshlconst(leftreg, 1));
	case 4:
	  return (cgshlconst(leftreg, 2));
	case 8:
	  return (cgshlconst(leftreg, 3));
	default:
	  // 加载一个包含大小的寄存器
	  // 并将leftreg乘以这个大小
	  rightreg = cgloadint(n->size, P_INT);
	  return (cgmul(leftreg, rightreg));
      }
    case A_POSTINC:
    case A_POSTDEC:
      // 将变量的值加载到寄存器中并
      // 执行后递增/递减操作
      if (n->sym->class == C_GLOBAL)
	return (cgloadglob(n->sym, n->op));
      else
	return (cgloadlocal(n->sym, n->op));
    case A_PREINC:
    case A_PREDEC:
      // 将变量的值加载到寄存器中并
      // 执行前递增/递减操作
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
      // 如果父AST节点是A_IF或A_WHILE，生成
      // 比较后跳转。否则，根据其为零或非零
      // 将寄存器设置为0或1
      return (cgboolean(leftreg, parentASTop, label));
    default:
      fatald("Unknown AST operator", n->op);
  }
  return (NOREG);		// 保持-Wall愉快
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