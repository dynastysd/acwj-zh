#include "defs.h"
#include "data.h"
#include "decl.h"

// 通用代码生成器
// Copyright (c) 2019 Warren Toomey, GPL3

// 生成并返回一个新的标签号
static int labelid = 1;
int genlabel(void) {
  return (labelid++);
}

// 生成 IF 语句及可选的 ELSE 子句的代码
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

  // 生成条件代码，后跟
  // 跳转到假标签的跳转。
  genAST(n->left, Lfalse, NOLABEL, NOLABEL, n->op);
  genfreeregs(NOREG);

  // 生成真的复合语句
  genAST(n->mid, NOLABEL, looptoplabel, loopendlabel, n->op);
  genfreeregs(NOREG);

  // 如果有可选的 ELSE 子句，
  // 生成跳转到结束的跳转
  if (n->right)
    cgjump(Lend);

  // 现在的假标签
  cglabel(Lfalse);

  // 可选的 ELSE 子句：生成
  // 假的复合语句和结束标签
  if (n->right) {
    genAST(n->right, NOLABEL, NOLABEL, NOLABEL, n->op);
    genfreeregs(NOREG);
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

  // 生成条件代码，后跟
  // 跳转到结束标签的跳转。
  genAST(n->left, Lend, Lstart, Lend, n->op);
  genfreeregs(NOREG);

  // 为循环体生成复合语句
  genAST(n->right, NOLABEL, Lstart, Lend, n->op);
  genfreeregs(NOREG);

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

  // 为case值和关联的标签创建数组。
  // 确保每个数组至少有一个位置。
  caseval = (int *) malloc((n->a_intvalue + 1) * sizeof(int));
  caselabel = (int *) malloc((n->a_intvalue + 1) * sizeof(int));

  // 为跳转表顶部和switch语句末尾生成标签。
  // 设置一个默认标签以防我们没有default。
  Ljumptop = genlabel();
  Lend = genlabel();
  defaultlabel = Lend;

  // 输出计算switch条件的代码
  reg = genAST(n->left, NOLABEL, NOLABEL, NOLABEL, 0);
  cgjump(Ljumptop);
  genfreeregs(reg);

  // 遍历右子链表以
  // 为每个case生成代码
  for (i = 0, c = n->right; c != NULL; i++, c = c->right) {

    // 为这个case获取一个标签。存储它
    // 和case值到数组中。
    // 记录是否是default case。
    caselabel[i] = genlabel();
    caseval[i] = c->a_intvalue;
    cglabel(caselabel[i]);
    if (c->op == A_DEFAULT)
      defaultlabel = caselabel[i];
    else
      casecount++;

    // 生成case代码。传入结束标签用于break。
    // 如果case没有body，我们将进入下一个body。
    if (c->left)
      genAST(c->left, NOLABEL, NOLABEL, Lend, 0);
    genfreeregs(NOREG);
  }

  // 确保最后一个case跳过switch表
  cgjump(Lend);

  // 现在输出switch表和结束标签。
  cgswitch(reg, casecount, Ljumptop, caselabel, caseval, defaultlabel);
  cglabel(Lend);
  return (NOREG);
}

// 为 A_LOGAND 或 A_LOGOR 操作生成代码
static int gen_logandor(struct ASTnode *n) {
  // 生成两个标签
  int Lfalse = genlabel();
  int Lend = genlabel();
  int reg;

  // 生成左表达式的代码
  // 后跟跳转到假标签的跳转
  reg= genAST(n->left, NOLABEL, NOLABEL, NOLABEL, 0);
  cgboolean(reg, n->op, Lfalse);
  genfreeregs(NOREG);

  // 生成右表达式的代码
  // 后跟跳转到假标签的跳转
  reg= genAST(n->right, NOLABEL, NOLABEL, NOLABEL, 0);
  cgboolean(reg, n->op, Lfalse);
  genfreeregs(reg);

  // 我们没有跳转，所以设置正确的布尔值
  if (n->op== A_LOGAND) {
    cgloadboolean(reg, 1);
    cgjump(Lend);
    cglabel(Lfalse);
    cgloadboolean(reg, 0);
  } else {
    cgloadboolean(reg, 0);
    cgjump(Lend);
    cglabel(Lfalse);
    cgloadboolean(reg, 1);
  }
  cglabel(Lend);
  return(reg);
}

// 生成将函数调用的参数复制到
// 其参数的代码，然后调用函数本身。
// 返回持有函数返回值的寄存器。
static int gen_funccall(struct ASTnode *n) {
  struct ASTnode *gluetree = n->left;
  int reg;
  int numargs = 0;

  // 在复制参数之前保存寄存器
  spill_all_regs();

  // 如果有参数列表，从
  // 最后一个参数（右子）到第一个
  // 遍历此列表
  while (gluetree) {
    // 计算表达式的值
    reg = genAST(gluetree->right, NOLABEL, NOLABEL, NOLABEL, gluetree->op);
    // 将其复制到第n个函数参数：大小为1, 2, 3, ...
    cgcopyarg(reg, gluetree->a_size);
    // 保留第一个（最高）数量的参数
    if (numargs == 0)
      numargs = gluetree->a_size;
    gluetree = gluetree->left;
  }

  // 调用函数，清理栈（基于numargs），
  // 并返回其结果
  return (cgcall(n->sym, numargs));
}

// 为三元表达式生成代码
static int gen_ternary(struct ASTnode *n) {
  int Lfalse, Lend;
  int reg, expreg;

  // 生成两个标签：一个用于
  // 假表达式，一个用于
  // 整个表达式的结束
  Lfalse = genlabel();
  Lend = genlabel();

  // 生成条件代码，后跟
  // 跳转到假标签的跳转。
  genAST(n->left, Lfalse, NOLABEL, NOLABEL, n->op);
  genfreeregs(NOREG);

  // 获取一个寄存器来保存两个表达式的结果
  reg = alloc_register();

  // 生成真表达式和假标签。
  // 将表达式结果移动到已知寄存器中。
  expreg = genAST(n->mid, NOLABEL, NOLABEL, NOLABEL, n->op);
  cgmove(expreg, reg);
  // 但不要释放持有结果的寄存器！
  genfreeregs(reg);
  cgjump(Lend);
  cglabel(Lfalse);

  // 生成假表达式和结束标签。
  // 将表达式结果移动到已知寄存器中。
  expreg = genAST(n->right, NOLABEL, NOLABEL, NOLABEL, n->op);
  cgmove(expreg, reg);
  // 但不要释放持有结果的寄存器！
  genfreeregs(reg);
  cglabel(Lend);
  return (reg);
}

// 给定一个AST、一个可选标签和
// 父节点的AST操作符，递归生成
// 汇编代码。
// 返回持有树最终值的寄存器id。
int genAST(struct ASTnode *n, int iflabel, int looptoplabel,
	   int loopendlabel, int parentASTop) {
  int leftreg= NOREG, rightreg= NOREG;

  // 空树，什么都不做
  if (n==NULL) return(NOREG);

  // 我们在顶部有一些特定的AST节点处理，
  // 这样我们就不会立即计算子子树
  switch (n->op) {
    case A_IF:
      return (genIF(n, looptoplabel, loopendlabel));
    case A_WHILE:
      return (genWHILE(n));
    case A_SWITCH:
      return (genSWITCH(n));
    case A_FUNCCALL:
      return (gen_funccall(n));
    case A_TERNARY:
      return (gen_ternary(n));
    case A_LOGOR:
      return (gen_logandor(n));
    case A_LOGAND:
      return (gen_logandor(n));
    case A_GLUE:
      // 执行每个子语句，并在每个
      // 子语句后释放寄存器
      if (n->left != NULL)
	genAST(n->left, iflabel, looptoplabel, loopendlabel, n->op);
      genfreeregs(NOREG);
      if (n->right != NULL)
	genAST(n->right, iflabel, looptoplabel, loopendlabel, n->op);
      genfreeregs(NOREG);
      return (NOREG);
    case A_FUNCTION:
      // 在子树的代码之前生成
      // 函数的前导码
      cgfuncpreamble(n->sym);
      genAST(n->left, NOLABEL, NOLABEL, NOLABEL, n->op);
      cgfuncpostamble(n->sym);
      return (NOREG);
  }

  // 通用的AST节点处理在下面

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
      // 如果父AST节点是 A_IF、A_WHILE 或 A_TERNARY，
      // 生成比较后跟跳转。否则，
      // 比较寄存器并根据比较将其设置为1或0。
      if (parentASTop == A_IF || parentASTop == A_WHILE ||
	  parentASTop == A_TERNARY)
	return (cgcompare_and_jump(n->op, leftreg, rightreg, iflabel));
      else
	return (cgcompare_and_set(n->op, leftreg, rightreg));
    case A_INTLIT:
      return (cgloadint(n->a_intvalue, n->type));
    case A_STRLIT:
      return (cgloadglobstr(n->a_intvalue));
    case A_IDENT:
      // 如果我们是rvalue或正在被解引用，则加载我们的值
      if (n->rvalue || parentASTop == A_DEREF) {
	if (n->sym->class == C_GLOBAL || n->sym->class == C_STATIC
	    || n->sym->class == C_EXTERN) {
	  return (cgloadglob(n->sym, n->op));
	} else {
	  return (cgloadlocal(n->sym, n->op));
	}
      } else
	return (NOREG);
    case A_ASPLUS:
    case A_ASMINUS:
    case A_ASSTAR:
    case A_ASSLASH:
    case A_ASSIGN:

      // 对于 '+=' 及相关运算符，生成合适的代码
      // 并获取结果的寄存器。然后取左子节点，
      // 使其成为右子节点，这样我们就可以进入赋值代码。
      switch (n->op) {
	case A_ASPLUS:
	  leftreg = cgadd(leftreg, rightreg);
	  n->right = n->left;
	  break;
	case A_ASMINUS:
	  leftreg = cgsub(leftreg, rightreg);
	  n->right = n->left;
	  break;
	case A_ASSTAR:
	  leftreg = cgmul(leftreg, rightreg);
	  n->right = n->left;
	  break;
	case A_ASSLASH:
	  leftreg = cgdiv(leftreg, rightreg);
	  n->right = n->left;
	  break;
      }

      // 现在进入赋值代码
      // 我们是赋值给标识符还是通过指针赋值？
      switch (n->right->op) {
	case A_IDENT:
	  if (n->right->sym->class == C_GLOBAL ||
	      n->right->sym->class == C_STATIC)
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
      // 如果我们是rvalue，解引用获取我们指向的值，
      // 否则留给 A_ASSIGN 通过指针存储
      if (n->rvalue)
	return (cgderef(leftreg, n->left->type));
      else
	return (leftreg);
    case A_SCALE:
      // 小优化：如果
      // 缩放值是2的已知幂，则使用移位
      switch (n->a_size) {
	case 2:
	  return (cgshlconst(leftreg, 1));
	case 4:
	  return (cgshlconst(leftreg, 2));
	case 8:
	  return (cgshlconst(leftreg, 3));
	default:
	  // 用大小加载一个寄存器并
	  // 将leftreg乘以这个大小
	  rightreg = cgloadint(n->a_size, P_INT);
	  return (cgmul(leftreg, rightreg));
      }
    case A_POSTINC:
    case A_POSTDEC:
      // 将变量的值加载到寄存器中并
      // 后递增/递减它
      if (n->sym->class == C_GLOBAL || n->sym->class == C_STATIC)
	return (cgloadglob(n->sym, n->op));
      else
	return (cgloadlocal(n->sym, n->op));
    case A_PREINC:
    case A_PREDEC:
      // 将变量的值加载到寄存器中并
      // 前递增/递减它
      if (n->left->sym->class == C_GLOBAL || n->left->sym->class == C_STATIC)
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
      // 如果父AST节点是 A_IF 或 A_WHILE，生成
      // 比较后跟跳转。否则，根据其为零或非零
      // 将寄存器设置为0或1
      return (cgboolean(leftreg, parentASTop, iflabel));
    case A_BREAK:
      cgjump(loopendlabel);
      return (NOREG);
    case A_CONTINUE:
      cgjump(looptoplabel);
      return (NOREG);
    case A_CAST:
      return (leftreg);		// 没什么可做的
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
void genfreeregs(int keepreg) {
  freeall_registers(keepreg);
}
void genglobsym(struct symtable *node) {
  cgglobsym(node);
}

// 生成一个全局字符串。
// 如果append为true，追加到
// 之前的genglobstr()调用。
int genglobstr(char *strvalue, int append) {
  int l = genlabel();
  cgglobstr(l, strvalue, append);
  return (l);
}
void genglobstrend(void) {
  cgglobstrend();
}
int genprimsize(int type) {
  return (cgprimsize(type));
}
int genalign(int type, int offset, int direction) {
  return (cgalign(type, offset, direction));
}