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

static void update_line(struct ASTnode *n) {
  // 如果我们在 AST 节点中更改了行号,
  // 则将行号输出到汇编代码中
  if (n->linenum != 0 && Line != n->linenum) {
    Line = n->linenum;
    cglinenum(Line);
  }
}

// 生成 IF 语句的代码
// 以及可选的 ELSE 子句。
static int genIF(struct ASTnode *n, int looptoplabel, int loopendlabel) {
  int Lfalse, Lend;

  // 生成两个标签:一个用于
  // 假的复合语句,和一个
  // 用于整个 IF 语句的结束。
  // 如果没有 ELSE 子句,Lfalse _就是_
  // 结束标签!
  Lfalse = genlabel();
  if (n->right)
    Lend = genlabel();

  // 生成条件代码,后跟
  // 跳转到假标签。
  genAST(n->left, Lfalse, NOLABEL, NOLABEL, n->op);
  genfreeregs(NOREG);

  // 生成真的复合语句
  genAST(n->mid, NOLABEL, looptoplabel, loopendlabel, n->op);
  genfreeregs(NOREG);

  // 如果有可选的 ELSE 子句,
  // 生成跳转到结束的跳转
  if (n->right)
    cgjump(Lend);

  // 现在是假标签
  cglabel(Lfalse);

  // 可选的 ELSE 子句:生成
  // 假的复合语句和结束标签
  if (n->right) {
    genAST(n->right, NOLABEL, NOLABEL, loopendlabel, n->op);
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

  // 生成条件代码,后跟
  // 跳转到结束标签。
  genAST(n->left, Lend, Lstart, Lend, n->op);
  genfreeregs(NOREG);

  // 生成复合语句作为循环体
  genAST(n->right, NOLABEL, Lstart, Lend, n->op);
  genfreeregs(NOREG);

  // 最后输出跳回条件的跳转,
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

  // 为 case 值和关联的标签创建数组。
  // 确保每个数组至少有一个位置。
  caseval = (int *) malloc((n->a_intvalue + 1) * sizeof(int));
  caselabel = (int *) malloc((n->a_intvalue + 1) * sizeof(int));

  // 为跳转表顶部和
  // switch 语句末尾生成标签。
  // 设置一个默认标签用于 switch 末尾,
  // 以防我们没有 default。
  Ljumptop = genlabel();
  Lend = genlabel();
  defaultlabel = Lend;

  // 输出计算 switch 条件的代码
  reg = genAST(n->left, NOLABEL, NOLABEL, NOLABEL, 0);
  cgjump(Ljumptop);
  genfreeregs(reg);

  // 遍历右子链接列表以
  // 为每个 case 生成代码
  for (i = 0, c = n->right; c != NULL; i++, c = c->right) {

    // 为这个 case 获取一个标签。存储它
    // 和 case 值在数组中。
    // 记录它是否是 default case。
    caselabel[i] = genlabel();
    caseval[i] = c->a_intvalue;
    cglabel(caselabel[i]);
    if (c->op == A_DEFAULT)
      defaultlabel = caselabel[i];
    else
      casecount++;

    // 生成 case 代码。传入结束标签用于 break。
    // 如果 case 没有 body,我们将落入下一个 body。
    if (c->left)
      genAST(c->left, NOLABEL, NOLABEL, Lend, 0);
    genfreeregs(NOREG);
  }

  // 确保最后一个 case 跳过 switch 表
  cgjump(Lend);

  // 现在输出 switch 表和结束标签。
  cgswitch(reg, casecount, Ljumptop, caselabel, caseval, defaultlabel);
  cglabel(Lend);
  return (NOREG);
}

// 生成 A_LOGAND 或 A_LOGOR 操作的代码
static int gen_logandor(struct ASTnode *n) {
  // 生成两个标签
  int Lfalse = genlabel();
  int Lend = genlabel();
  int reg;

  // 生成左表达式的代码,
  // 后跟跳转到假标签
  reg = genAST(n->left, NOLABEL, NOLABEL, NOLABEL, 0);
  cgboolean(reg, n->op, Lfalse);
  genfreeregs(NOREG);

  // 生成右表达式的代码,
  // 后跟跳转到假标签
  reg = genAST(n->right, NOLABEL, NOLABEL, NOLABEL, 0);
  cgboolean(reg, n->op, Lfalse);
  genfreeregs(reg);

  // 我们没有跳转,所以设置正确的布尔值
  if (n->op == A_LOGAND) {
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
  return (reg);
}

// 生成将函数调用的参数复制到
// 其参数的代码,然后调用函数本身。
// 返回保存函数返回值的寄存器。
static int gen_funccall(struct ASTnode *n) {
  struct ASTnode *gluetree = n->left;
  int reg;
  int numargs = 0;

  // 复制参数之前保存寄存器
  spill_all_regs();

  // 如果有参数列表,从最后一个参数(右子)
  // 到第一个遍历此列表
  while (gluetree) {
    // 计算表达式的值
    reg = genAST(gluetree->right, NOLABEL, NOLABEL, NOLABEL, gluetree->op);
    // 将其复制到第 n 个函数参数:大小为 1, 2, 3, ...
    cgcopyarg(reg, gluetree->a_size);
    // 保留第一个(最大)参数编号
    if (numargs == 0)
      numargs = gluetree->a_size;
    gluetree = gluetree->left;
  }

  // 调用函数,清理堆栈(基于 numargs),
  // 并返回其结果
  return (cgcall(n->sym, numargs));
}

// 生成三元表达式的代码
static int gen_ternary(struct ASTnode *n) {
  int Lfalse, Lend;
  int reg, expreg;

  // 生成两个标签:一个用于
  // 假表达式,一个用于
  // 整个表达式的结束
  Lfalse = genlabel();
  Lend = genlabel();

  // 生成条件代码,后跟
  // 跳转到假标签。
  genAST(n->left, Lfalse, NOLABEL, NOLABEL, n->op);
  genfreeregs(NOREG);

  // 获取一个寄存器来保存两个表达式的结果
  reg = alloc_register();

  // 生成真表达式和假标签。
  // 将表达式结果移动到已知寄存器。
  expreg = genAST(n->mid, NOLABEL, NOLABEL, NOLABEL, n->op);
  cgmove(expreg, reg);
  // 但是不要释放保存结果的寄存器!
  genfreeregs(reg);
  cgjump(Lend);
  cglabel(Lfalse);

  // 生成假表达式和结束标签。
  // 将表达式结果移动到已知寄存器。
  expreg = genAST(n->right, NOLABEL, NOLABEL, NOLABEL, n->op);
  cgmove(expreg, reg);
  // 但是不要释放保存结果的寄存器!
  genfreeregs(reg);
  cglabel(Lend);
  return (reg);
}

// 给定一个 AST、一个可选标签和父级的 AST op,
// 递归生成汇编代码。
// 返回具有树最终值的寄存器 id。
int genAST(struct ASTnode *n, int iflabel, int looptoplabel,
	   int loopendlabel, int parentASTop) {
  int leftreg = NOREG, rightreg = NOREG;

  // 空树,什么都不做
  if (n == NULL)
    return (NOREG);

  // 更新输出中的行号
  update_line(n);

  // 我们在顶部有一些特定的 AST 节点处理,
  // 这样我们就不会立即计算子树
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
    // 执行每个子语句,并在每个子语句之后
    // 释放寄存器
    if (n->left != NULL)
      genAST(n->left, iflabel, looptoplabel, loopendlabel, n->op);
    genfreeregs(NOREG);
    if (n->right != NULL)
      genAST(n->right, iflabel, looptoplabel, loopendlabel, n->op);
    genfreeregs(NOREG);
    return (NOREG);
  case A_FUNCTION:
    // 在子树代码之前生成函数前导码
    cgfuncpreamble(n->sym);
    genAST(n->left, NOLABEL, NOLABEL, NOLABEL, n->op);
    cgfuncpostamble(n->sym);
    return (NOREG);
  }

  // 下面是通用 AST 节点处理

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
    return (cgdivmod(leftreg, rightreg, A_DIVIDE));
  case A_MOD:
    return (cgdivmod(leftreg, rightreg, A_MOD));
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
    // 如果父 AST 节点是 A_IF、A_WHILE 或 A_TERNARY,
    // 生成比较后跟跳转。否则,比较寄存器并根据比较结果
    // 将一个设置为 1 或 0。
    if (parentASTop == A_IF || parentASTop == A_WHILE ||
	parentASTop == A_TERNARY)
      return (cgcompare_and_jump
	      (n->op, leftreg, rightreg, iflabel, n->left->type));
    else
      return (cgcompare_and_set(n->op, leftreg, rightreg, n->left->type));
  case A_INTLIT:
    return (cgloadint(n->a_intvalue, n->type));
  case A_STRLIT:
    return (cgloadglobstr(n->a_intvalue));
  case A_IDENT:
    // 如果我们是右值或者被解引用,则加载我们的值
    if (n->rvalue || parentASTop == A_DEREF) {
      return (cgloadvar(n->sym, n->op));
    } else
      return (NOREG);
  case A_ASPLUS:
  case A_ASMINUS:
  case A_ASSTAR:
  case A_ASSLASH:
  case A_ASMOD:
  case A_ASSIGN:

    // 对于 '+=' 及相关运算符,生成合适的代码
    // 并获取结果的寄存器。然后取左子节点,
    // 使其成为右子节点,以便我们可以进入赋值代码。
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
      leftreg = cgdivmod(leftreg, rightreg, A_DIVIDE);
      n->right = n->left;
      break;
    case A_ASMOD:
      leftreg = cgdivmod(leftreg, rightreg, A_MOD);
      n->right = n->left;
      break;
    }

    // 现在进入赋值代码
    // 我们是赋值给标识符还是通过指针赋值?
    switch (n->right->op) {
    case A_IDENT:
      if (n->right->sym->class == C_GLOBAL ||
	  n->right->sym->class == C_EXTERN ||
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
    // 将子节点类型扩展为父节点的类型
    return (cgwiden(leftreg, n->left->type, n->type));
  case A_RETURN:
    cgreturn(leftreg, Functionid);
    return (NOREG);
  case A_ADDR:
    return (cgaddress(n->sym));
  case A_DEREF:
    // 如果我们是右值,解引用以获取我们指向的值,
    // 否则将其保留给 A_ASSIGN 以通过指针存储
    if (n->rvalue)
      return (cgderef(leftreg, n->left->type));
    else
      return (leftreg);
  case A_SCALE:
    // 小优化:如果缩放值是已知的 2 的幂,则使用移位
    switch (n->a_size) {
    case 2:
      return (cgshlconst(leftreg, 1));
    case 4:
      return (cgshlconst(leftreg, 2));
    case 8:
      return (cgshlconst(leftreg, 3));
    default:
      // 将寄存器加载为大小,并
      // 将 leftreg 乘以这个大小
      rightreg = cgloadint(n->a_size, P_INT);
      return (cgmul(leftreg, rightreg));
    }
  case A_POSTINC:
  case A_POSTDEC:
    // 将变量的值加载到寄存器中并后递增/递减
    return (cgloadvar(n->sym, n->op));
  case A_PREINC:
  case A_PREDEC:
    // 将变量的值加载到寄存器中并前递增/递减
    return (cgloadvar(n->left->sym, n->op));
  case A_NEGATE:
    return (cgnegate(leftreg));
  case A_INVERT:
    return (cginvert(leftreg));
  case A_LOGNOT:
    return (cglognot(leftreg));
  case A_TOBOOL:
    // 如果父 AST 节点是 A_IF 或 A_WHILE,生成
    // 比较后跟跳转。否则,根据其为零性或非零性
    // 将寄存器设置为 0 或 1
    return (cgboolean(leftreg, parentASTop, iflabel));
  case A_BREAK:
    cgjump(loopendlabel);
    return (NOREG);
  case A_CONTINUE:
    cgjump(looptoplabel);
    return (NOREG);
  case A_CAST:
    return (leftreg);		// 没太多要做的
  default:
    fatald("Unknown AST operator", n->op);
  }
  return (NOREG);		// 保持 -Wall 高兴
}

void genpreamble(char *filename) {
  cgpreamble(filename);
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
// 如果 append 为 true,追加到
// 先前的 genglobstr() 调用。
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