#include "defs.h"
#include "data.h"
#include "cg.h"
#include "decl.h"
#include "gen.h"
#include "misc.h"
#include "target.h"
#include "tree.h"
#include "types.h"

// 通用代码生成器
// Copyright (c) 2019 Warren Toomey, GPL3

int genAST(struct ASTnode *n, int iflabel, int looptoplabel, int loopendlabel,
	   int parentASTop);

// 生成并返回一个新的标签号
static int labelid = 1;
int genlabel(void) {
  return (labelid++);
}

void genfreeregs(int keepreg) {
  cgfreeallregs(keepreg);
}

static void update_line(struct ASTnode *n) {
  // 如果我们在 AST 节点中改变了行号，
  // 将行号输出到汇编代码中
  if (n->linenum != 0 && Line != n->linenum) {
    Line = n->linenum;
    cglinenum(Line);
  }
}

// 为 IF 语句生成代码
// 以及可选的 ELSE 子句。
static int genIF(struct ASTnode *n, struct ASTnode *nleft,
		 struct ASTnode *nmid, struct ASTnode *nright,
		 int looptoplabel, int loopendlabel) {
  int Lfalse, Lend;

  // 生成两个标签：一个用于
  // 假的复合语句，另一个用于
  // 整个 IF 语句的结束。
  // 当没有 ELSE 子句时，Lfalse _就是_
  // 结束标签！
  Lfalse = genlabel();
  if (nright)
    Lend = genlabel();

  // 生成条件代码，然后
  // 跳转到假标签。
  genAST(nleft, Lfalse, looptoplabel, loopendlabel, n->op);
  genfreeregs(NOREG);

  // 生成真的复合语句
  genAST(nmid, NOLABEL, looptoplabel, loopendlabel, n->op);
  genfreeregs(NOREG);

  // 如果有可选的 ELSE 子句，
  // 生成跳转到结束的跳转
  if (nright)
    cgjump(Lend);

  // 现在是假标签
  cglabel(Lfalse);

  // 可选的 ELSE 子句：生成
  // 假的复合语句和结束标签
  if (nright) {
    genAST(nright, NOLABEL, looptoplabel, loopendlabel, n->op);
    genfreeregs(NOREG);
    cglabel(Lend);
  }

  return (NOREG);
}

// 为 WHILE 语句生成代码
static int genWHILE(struct ASTnode *n, struct ASTnode *nleft,
		    struct ASTnode *nright) {
  int Lstart, Lend;

  // 生成开始和结束标签
  // 并输出开始标签
  Lstart = genlabel();
  Lend = genlabel();
  cglabel(Lstart);

  // 生成条件代码，然后
  // 跳转到结束标签。
  genAST(nleft, Lend, Lstart, Lend, n->op);
  genfreeregs(NOREG);

  // 为循环体生成复合语句
  genAST(nright, NOLABEL, Lstart, Lend, n->op);
  genfreeregs(NOREG);

  // 最后输出跳回条件的跳转，
  // 和结束标签
  cgjump(Lstart);
  cglabel(Lend);
  return (NOREG);
}

// 为 SWITCH 语句生成代码
static int genSWITCH(struct ASTnode *n, int looptoplabel) {
  int *caseval, *caselabel;
  int Ljumptop, Lend;
  int i, reg, defaultlabel = 0, casecount = 0;
  int rightid;
  struct ASTnode *nleft;
  struct ASTnode *nright;
  struct ASTnode *c, *cleft;

  // 加载子节点
  nleft=loadASTnode(n->leftid,0);
  nright=loadASTnode(n->rightid,0);

  // 为 case 值和关联的标签创建数组。
  // 确保每个数组至少有一个位置。
  caseval = (int *) malloc((n->a_intvalue + 1) * sizeof(int));
  caselabel = (int *) malloc((n->a_intvalue + 1) * sizeof(int));

  // 为跳转表的顶部和 switch 语句的末尾生成标签。
  // 为末尾设置一个默认标签，以防我们没有 default。
  Ljumptop = genlabel();
  Lend = genlabel();
  defaultlabel = Lend;

  // 构建 case 值和标签数组
  for (i = 0, c = nright; c != NULL;) {
    // 为这个 case 获取一个标签。存储它
    // 和 case 值在数组中。
    // 记录它是否是 default case。
    caselabel[i] = genlabel();
    caseval[i] = c->a_intvalue;
    if (c->op == A_DEFAULT)
      defaultlabel = caselabel[i];
    else
      casecount++;

    i++;
    rightid= c->rightid;
    // 如果它是 nright，不要释放 c，
    // 因为 genAST() 会做这个
    if (c!=nright) freeASTnode(c);
    c = loadASTnode(rightid,0);
  }

  // 输出计算 switch 条件的代码
  reg = genAST(nleft, NOLABEL, NOLABEL, NOLABEL, 0);
  cgjump(Ljumptop);
  genfreeregs(reg);

  // 输出 switch 代码或 switch 表
  cgswitch(reg, casecount, Ljumptop, caselabel, caseval, defaultlabel);

  // 为每个 case 生成代码
  for (i = 0, c = nright; c != NULL; ) {
    // 生成 case 代码。传入结束的标签以供 break 使用。
    // 如果 case 没有 body，我们将进入下面的 body。
    cglabel(caselabel[i]);
    if (c->leftid) {
      // Looptoplabel 在这里，这样我们可以 'continue'，例如
      // while (...) {
      //   switch(...) {
      //     case ...: ...
      //               continue;
      //   }
      // }
      cleft= loadASTnode(c->leftid,0);
      genAST(cleft, NOLABEL, looptoplabel, Lend, 0);
      freeASTnode(cleft);
    }
    genfreeregs(NOREG);

    i++;
    rightid= c->rightid;
    freeASTnode(c);
    c= loadASTnode(rightid,0);
  }

  // 输出结束标签
  cglabel(Lend);
  free(caseval);
  free(caselabel);
  freeASTnode(nleft);
  return (NOREG);
}

// 为 A_LOGOR 操作生成代码。
// 如果父 AST 节点是 A_IF、A_WHILE、A_TERNARY
// 或 A_LOGAND，如果为假则跳转到标签。
// 如果是 A_LOGOR，如果为真则跳转到标签。
// 否则设置寄存器为 1 或 0 并返回它。
static int gen_logor(struct ASTnode *n,
		     struct ASTnode *nleft, struct ASTnode *nright,
		     int parentASTop, int label) {
  int Ltrue, Lfalse, Lend;
  int reg;
  int type;
  int makebool = 0;

  // 生成标签
  if (parentASTop == A_LOGOR) {
    Ltrue = label;
    Lfalse = genlabel();
  } else {
    Ltrue = genlabel();
    Lfalse = label;
  }
  Lend = genlabel();

  // 标记我们是否需要生成布尔值
  if (parentASTop != A_IF && parentASTop != A_WHILE &&
      parentASTop != A_TERNARY && parentASTop != A_LOGAND &&
      parentASTop != A_LOGOR) {
    makebool = 1;
    Ltrue = genlabel();
    Lfalse = genlabel();
  }

  // 生成左表达式的代码。
  // genAST() 可以做跳转并返回 NOREG。
  // 但如果我们得到一个寄存器，就自己做跳转。
  reg = genAST(nleft, Ltrue, NOLABEL, NOLABEL, A_LOGOR);
  if (reg != NOREG) {
    type = nleft->type;
    cgboolean(reg, A_LOGOR, Ltrue, type);
    genfreeregs(NOREG);
  }

  // 生成右表达式的代码，
  // 逻辑与左表达式相同。
  reg = genAST(nright, Ltrue, NOLABEL, NOLABEL, A_LOGOR);
  if (reg != NOREG) {
    type = nright->type;
    cgboolean(reg, A_LOGOR, Ltrue, type);
    genfreeregs(reg);
  }

  // 结果是假的。
  // 如果不需要生成布尔值，现在停止
  if (makebool == 0) {
    // 如果提供了假标签，跳转到假标签
    if (label == Lfalse) {
      cgjump(Lfalse);
      cglabel(Ltrue);
    }
    return (NOREG);
  }

  // 我们确实需要生成布尔值，但我们没有跳转
  type = n->type;
  cglabel(Lfalse);
  reg = cgloadboolean(reg, 0, type);
  cgjump(Lend);
  cglabel(Ltrue);
  reg = cgloadboolean(reg, 1, type);
  cglabel(Lend);
  return (reg);
}

// 为 A_LOGAND 操作生成代码。
// 如果父 AST 节点是 A_IF、A_WHILE、A_TERNARY
// 或 A_LOGAND，如果为假则跳转到标签。
// 如果是 A_LOGOR，如果为真则跳转到标签。
// 否则设置寄存器为 1 或 0 并返回它。
static int gen_logand(struct ASTnode *n,
		      struct ASTnode *nleft, struct ASTnode *nright,
		      int parentASTop, int label) {
  int Ltrue, Lfalse, Lend;
  int reg;
  int type;
  int makebool = 0;

  // 生成标签
  if (parentASTop == A_LOGOR) {
    Ltrue = label;
    Lfalse = genlabel();
  } else {
    Ltrue = genlabel();
    Lfalse = label;
  }

  Lend = genlabel();

  // 标记我们是否需要生成布尔值
  if (parentASTop != A_IF && parentASTop != A_WHILE &&
      parentASTop != A_TERNARY && parentASTop != A_LOGAND &&
      parentASTop != A_LOGOR) {
    makebool = 1;
    Ltrue = genlabel();
    Lfalse = genlabel();
  }

  // 生成左表达式的代码。
  // genAST() 可以做跳转并返回 NOREG。
  // 但如果我们得到一个寄存器，就自己做跳转。
  reg = genAST(nleft, Lfalse, NOLABEL, NOLABEL, A_LOGAND);
  if (reg != NOREG) {
    type = nleft->type;
    cgboolean(reg, A_LOGAND, Lfalse, type);
    genfreeregs(NOREG);
  }

  // 生成右表达式的代码，
  // 逻辑与左表达式相同。
  reg = genAST(nright, Lfalse, NOLABEL, NOLABEL, A_LOGAND);
  if (reg != NOREG) {
    type = nright->type;
    cgboolean(reg, A_LOGAND, Lfalse, type);
    genfreeregs(reg);
  }

  // 结果是真的。
  // 如果不需要生成布尔值，现在停止
  if (makebool == 0) {
    // 如果给了标签，跳转到该标签
    if (label == Ltrue) {
      cgjump(Ltrue);
      cglabel(Lfalse);
    }
    return (NOREG);
  }

  // 我们确实需要生成布尔值，但我们没有跳转
  type = n->type;
  cglabel(Ltrue);
  reg = cgloadboolean(reg, 1, type);
  cgjump(Lend);
  cglabel(Lfalse);
  reg = cgloadboolean(reg, 0, type);
  cglabel(Lend);
  return (reg);
}

// 生成函数调用参数的代码，
// 然后用这些参数调用函数。返回持有
// 函数返回值的寄存器。
static int gen_funccall(struct ASTnode *n) {
  struct ASTnode *gluetree;
  int i = 0, numargs = 0;
  int reg;
  int leftid;
  int *arglist = NULL;
  int *typelist = NULL;
  struct ASTnode *nleft;
  struct ASTnode *glueright;

  // 加载子节点
  nleft=loadASTnode(n->leftid,0);

  // 确定实际参数数量
  // 分配内存来保存参数临时变量列表。
  // 我们需要遍历参数列表来确定大小
  // XXX 我们需要在这里释放
  for (i = 0, gluetree = nleft; gluetree != NULL; ) {
    numargs++;
    i++;
    leftid= gluetree->leftid;
    // 如果 gluetree 是 nleft，不要释放它，
    // 因为 genAST() 会为我们做这个
    if (gluetree != nleft) freeASTnode(gluetree);
    gluetree = loadASTnode(leftid,0);
  }

  if (i != 0) {
    arglist = (int *) malloc(i * sizeof(int));
    if (arglist == NULL)
      fatal("malloc failed in gen_funccall");
    typelist = (int *) malloc(i * sizeof(int));
    if (typelist == NULL)
      fatal("malloc failed in gen_funccall");
  }

  // 如果有参数列表，从最后一个参数（右子节点）到第一个参数遍历这个列表。
  // 同时缓存每个表达式的类型
  for (i = 0, gluetree = nleft; gluetree != NULL;
				gluetree = loadASTnode(leftid,0)) {
    // 计算表达式的值
    glueright= loadASTnode(gluetree->rightid,0);
    arglist[i] =
      genAST(glueright, NOLABEL, NOLABEL, NOLABEL, gluetree->op);
    typelist[i++] = glueright->type;
    freeASTnode(glueright);
    leftid= gluetree->leftid;
    freeASTnode(gluetree);
  }

  // 调用函数并返回其结果
  reg= cgcall(n->sym, numargs, arglist, typelist);
  free(arglist);
  free(typelist);
  return(reg);
}

// 为三元表达式生成代码
static int gen_ternary(struct ASTnode *n, struct ASTnode *nleft,
		       struct ASTnode *nmid, struct ASTnode *nright) {
  int Lfalse, Lend;
  int reg, expreg;

  // 加载子节点
  nleft=loadASTnode(n->leftid,0);
  nmid=loadASTnode(n->midid,0);
  nright=loadASTnode(n->rightid,0);

  // 生成两个标签：一个用于
  // 假的表达式，一个用于
  // 整个表达式的结束
  Lfalse = genlabel();
  Lend = genlabel();

  // 生成条件代码，然后
  // 跳转到假标签。
  genAST(nleft, Lfalse, NOLABEL, NOLABEL, n->op);
  // genfreeregs(NOREG);

  // 获取一个寄存器来保存两个表达式的结果
  reg = cgallocreg(nleft->type);

  // 生成真表达式和假标签。
  // 将表达式结果移动到已知寄存器。
  expreg = genAST(nmid, NOLABEL, NOLABEL, NOLABEL, n->op);
  cgmove(expreg, reg, nmid->type);
  cgfreereg(expreg);
  cgjump(Lend);
  cglabel(Lfalse);

  // 生成假表达式和结束标签。
  // 将表达式结果移动到已知寄存器。
  expreg = genAST(nright, NOLABEL, NOLABEL, NOLABEL, n->op);
  cgmove(expreg, reg, nright->type);
  cgfreereg(expreg);
  cglabel(Lend);
  return (reg);
}

// 给定一个 AST、一个可选的标签和父节点的 AST op，
// 递归生成汇编代码。
// 返回具有树最终值的寄存器 id。
int genAST(struct ASTnode *n, int iflabel, int looptoplabel,
	   int loopendlabel, int parentASTop) {
  int leftreg = NOREG, rightreg = NOREG;
  int type = P_VOID;
  int id;
  int special = 0;
  struct ASTnode *nleft, *nmid, *nright;

  // 空树，什么都不做
  if (n == NULL)
    return (NOREG);

  // 加载子节点
  nleft=loadASTnode(n->leftid,0);
  nmid=loadASTnode(n->midid,0);
  nright=loadASTnode(n->rightid,0);

  // 更新输出中的行号
  update_line(n);

  // 我们在顶部有一些特定的 AST 节点处理，
  // 这样我们不会立即求值子子树
  switch (n->op) {
  case A_IF:
    special = 1;
    leftreg = genIF(n, nleft, nmid, nright, looptoplabel, loopendlabel);
    break;
  case A_WHILE:
    special = 1;
    leftreg = genWHILE(n, nleft, nright);
    break;
  case A_SWITCH:
    special = 1;
    leftreg = genSWITCH(n, looptoplabel);
    break;
  case A_FUNCCALL:
    special = 1;
    leftreg = gen_funccall(n);
    break;
  case A_TERNARY:
    special = 1;
    leftreg = gen_ternary(n, nleft, nmid, nright);
    break;
  case A_LOGOR:
    special = 1;
    leftreg = gen_logor(n, nleft, nright, parentASTop, iflabel);
    break;
  case A_LOGAND:
    special = 1;
    leftreg = gen_logand(n, nleft, nright, parentASTop, iflabel);
    break;
  case A_GLUE:
    // 执行每个子语句，并在每个子语句之后释放寄存器
    special = 1;
    if (nleft != NULL)
      genAST(nleft, iflabel, looptoplabel, loopendlabel, n->op);
    genfreeregs(NOREG);
    if (nright != NULL)
      genAST(nright, iflabel, looptoplabel, loopendlabel, n->op);
    genfreeregs(NOREG);
    leftreg = NOREG;
    break;
  case A_FUNCTION:
    // 在子树的代码之前生成函数的前导码。
    // 丑陋：使用函数的名称作为致命消息的 Infilename。
    special = 1;
    Infilename = n->sym->name;
    cgfuncpreamble(n->sym);
    genAST(nleft, NOLABEL, NOLABEL, NOLABEL, n->op);
    cgfuncpostamble(n->sym);
    leftreg = NOREG;
  }

  if (!special) {

    // 下面是通用 AST 节点处理

    // 获取左右子树的值
    if (nleft) {
      type = nleft->type;
      leftreg = genAST(nleft, NOLABEL, looptoplabel, loopendlabel, n->op);
    }
    if (nright) {
      type = nright->type;
      rightreg = genAST(nright, NOLABEL, looptoplabel, loopendlabel, n->op);
    }

    switch (n->op) {
    case A_ADD:
      leftreg = cgadd(leftreg, rightreg, type);
      break;
    case A_SUBTRACT:
      leftreg = cgsub(leftreg, rightreg, type);
      break;
    case A_MULTIPLY:
      leftreg = cgmul(leftreg, rightreg, type);
      break;
    case A_DIVIDE:
      leftreg = cgdiv(leftreg, rightreg, type);
      break;
    case A_MOD:
      leftreg = cgmod(leftreg, rightreg, type);
      break;
    case A_AND:
      leftreg = cgand(leftreg, rightreg, type);
      break;
    case A_OR:
      leftreg = cgor(leftreg, rightreg, type);
      break;
    case A_XOR:
      leftreg = cgxor(leftreg, rightreg, type);
      break;
    case A_LSHIFT:
      leftreg = cgshl(leftreg, rightreg, type);
      break;
    case A_RSHIFT:
      leftreg = cgshr(leftreg, rightreg, type);
      break;
    case A_EQ:
    case A_NE:
    case A_LT:
    case A_GT:
    case A_LE:
    case A_GE:
      // 如果父 AST 节点是 A_IF、A_WHILE、A_TERNARY、
      // A_LOGAND，生成比较操作，如果比较为假则跳转。
      // 如果是 A_LOGOR，如果为真则跳转。否则，
      // 比较寄存器并根据比较结果将其设置为 1 或 0。
      if (parentASTop == A_IF || parentASTop == A_WHILE ||
	  parentASTop == A_TERNARY || parentASTop == A_LOGAND ||
	  parentASTop == A_LOGOR) {
	leftreg = cgcompare_and_jump
	  (n->op, parentASTop, leftreg, rightreg, iflabel, nleft->type);
      } else {
	leftreg = cgcompare_and_set(n->op, leftreg, rightreg, nleft->type);
      }
      break;
    case A_INTLIT:
      leftreg = cgloadint(n->a_intvalue, n->type);
      break;
    case A_STRLIT:
      // 输出实际的字面量
      id = genglobstr(n->name);
      leftreg = cgloadglobstr(id);
      break;
    case A_IDENT:
      // 如果我们是右值或者我们被解引用，则加载我们的值
      if (n->rvalue || parentASTop == A_DEREF) {
	leftreg = cgloadvar(n->sym, n->op);
      } else
	leftreg = NOREG;
      break;
    case A_ASPLUS:
    case A_ASMINUS:
    case A_ASSTAR:
    case A_ASSLASH:
    case A_ASMOD:
    case A_ASSIGN:

      // 对于 '+=' 和类似的运算符，生成合适的代码
      // 并获取持有结果的寄存器。然后取左子节点，
      // 使其成为右子节点，这样我们就可以进入赋值代码。
      switch (n->op) {
      case A_ASPLUS:
	leftreg = cgadd(leftreg, rightreg, type);
	nright = nleft;
	break;
      case A_ASMINUS:
	leftreg = cgsub(leftreg, rightreg, type);
	nright = nleft;
	break;
      case A_ASSTAR:
	leftreg = cgmul(leftreg, rightreg, type);
	nright = nleft;
	break;
      case A_ASSLASH:
	leftreg = cgdiv(leftreg, rightreg, type);
	nright = nleft;
	break;
      case A_ASMOD:
	leftreg = cgmod(leftreg, rightreg, type);
	nright = nleft;
	break;
      }

      // 现在进入赋值代码
      // 我们是赋值给标识符还是通过指针赋值？
      switch (nright->op) {
      case A_IDENT:
	if (nright->sym->class == V_GLOBAL ||
	    nright->sym->class == V_EXTERN ||
	    nright->sym->class == V_STATIC) {
	  leftreg = cgstorglob(leftreg, nright->sym);
	} else {
	  leftreg = cgstorlocal(leftreg, nright->sym);
	}
	break;
      case A_DEREF:
	leftreg = cgstorderef(leftreg, rightreg, nright->type);
	break;
      default:
	fatald("Can't A_ASSIGN in genAST(), op", n->op);
      }
      break;
    case A_WIDEN:
      // 将子节点的类型加宽到父节点的类型
      leftreg = cgwiden(leftreg, nleft->type, n->type);
      break;
    case A_RETURN:
      cgreturn(leftreg, Functionid);
      leftreg = NOREG;
      break;
    case A_ADDR:
      // 如果我们有符号，获取其地址。否则，
      // 左寄存器已经持有地址，因为它是成员访问
      if (n->sym != NULL)
	leftreg = cgaddress(n->sym);
      break;
#ifdef SPLITSWITCH
      }

      // 我把 switch 语句分成了两个，这样
      // 6809 版本的编译器可以解析这个文件
      // 而不会耗尽空间。
    switch (n->op) {
#endif
    case A_DEREF:
      // 如果我们是右值，解引用获取我们指向的值，
      // 否则将其保留给 A_ASSIGN 以通过指针存储
      if (n->rvalue)
	leftreg = cgderef(leftreg, nleft->type);
      break;
    case A_SCALE:
      // 小优化：如果缩放值是已知的 2 的幂，则使用移位
      switch (n->a_size) {
      case 2:
	leftreg = cgshlconst(leftreg, 1, type);
	break;
      case 4:
	leftreg = cgshlconst(leftreg, 2, type);
	break;
      case 8:
	leftreg = cgshlconst(leftreg, 3, type);
	break;
      default:
	// 加载一个带有大小的寄存器，并将
	// leftreg 乘以这个大小
	rightreg = cgloadint(n->a_size, P_INT);
	leftreg = cgmul(leftreg, rightreg, type);
      }

      // 在某些架构上，指针类型与 int 类型不同。
      // 如果我们要缩放将成为地址偏移量的内容，
      // 加宽结果
      if (cgprimsize(n->type) > cgprimsize(type))
	leftreg = cgwiden(leftreg, type, n->type);

      break;
    case A_POSTINC:
    case A_POSTDEC:
      // 将变量的值加载到寄存器中并进行后置递增/递减
      leftreg = cgloadvar(n->sym, n->op);
      break;
    case A_PREINC:
    case A_PREDEC:
      // 将变量的值加载到寄存器中并进行前置递增/递减
      leftreg = cgloadvar(nleft->sym, n->op);
      break;
    case A_NEGATE:
      leftreg = cgnegate(leftreg, type);
      break;
    case A_INVERT:
      leftreg = cginvert(leftreg, type);
      break;
    case A_LOGNOT:
      leftreg = cglognot(leftreg, type);
      break;
    case A_TOBOOL:
      // 如果父 AST 节点是 IF、WHILE、TERNARY、
      // LOGAND 或 LOGOR 操作，生成比较操作后跟跳转。
      // 否则，根据其为零或非零将寄存器设置为 0 或 1
      leftreg = cgboolean(leftreg, parentASTop, iflabel, type);
      break;
    case A_BREAK:
      cgjump(loopendlabel);
      leftreg = NOREG;
      break;
    case A_CONTINUE:
      cgjump(looptoplabel);
      leftreg = NOREG;
      break;
    case A_CAST:
      leftreg = cgcast(leftreg, nleft->type, n->type);
      break;
#ifndef SPLITSWITCH
    default:
      fatald("Unknown AST operator", n->op);
#endif
    }
  }				// if (!special) 结束

  // 在返回之前释放 AST 子树
  // 有时 n->right 已被设置为 n->left
  // 例如通过 +=, -= 等操作。
  if (nright != nleft)
    freeASTnode(nright);
  freeASTnode(nleft);
  freeASTnode(nmid);
  return (leftreg);
}

void genpreamble() {
  cgpreamble();
}

void genpostamble() {
  cgpostamble();
}

void genglobsym(struct symtable *node) {
  cgglobsym(node);
}

// 生成一个全局字符串。
int genglobstr(char *strvalue) {
  int l = genlabel();
  cglitseg();
  cgglobstr(l, strvalue);
  cgtextseg();
  return (l);
}