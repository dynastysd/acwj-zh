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
  // 如果我们改变了 AST 节点中的行号，
  // 则将行号输出到汇编代码中
  if (n->linenum != 0 && Line != n->linenum) {
    Line = n->linenum;
    cglinenum(Line);
  }
}

// 生成 IF 语句的代码
// 以及可选的 ELSE 子句。
static int genIF(struct ASTnode *n, int looptoplabel, int loopendlabel) {
  int Lfalse, Lend = 0;
  int r, r2;

  // 生成两个标签：一个用于
  // 假的复合语句，一个用于
  // 整体 IF 语句的结束。
  // 当没有 ELSE 子句时，Lfalse _就是_
  // 结束标签！
  Lfalse = genlabel();
  if (n->right)
    Lend = genlabel();

  // 生成条件代码
  r = genAST(n->left, Lfalse, NOLABEL, NOLABEL, n->op);
  // 测试条件是否为真。如果不是，跳转到假标签
  r2 = cgloadint(1, P_INT);
  cgcompare_and_jump(A_EQ, r, r2, Lfalse, P_INT);

  // 生成真复合语句
  genAST(n->mid, NOLABEL, looptoplabel, loopendlabel, n->op);

  // 如果有可选的 ELSE 子句，
  // 生成跳转到末尾的跳转
  if (n->right) {
    // QBE 不喜欢连续两个跳转指令，而且
    // 在真的 IF 部分末尾的 break 会导致这种情况。解决方案
    // 是在 IF 跳转之前插入一个标签。
    cglabel(genlabel());
    cgjump(Lend);
  }
  // 现在是假标签
  cglabel(Lfalse);

  // 可选 ELSE 子句：生成
  // 假复合语句和结束标签
  if (n->right) {
    genAST(n->right, NOLABEL, NOLABEL, loopendlabel, n->op);
    cglabel(Lend);
  }

  return (NOREG);
}

// 生成 WHILE 语句的代码
static int genWHILE(struct ASTnode *n) {
  int Lstart, Lend;
  int r, r2;

  // 生成开始和结束标签
  // 并输出开始标签
  Lstart = genlabel();
  Lend = genlabel();
  cglabel(Lstart);

  // 生成条件代码
  r = genAST(n->left, Lend, Lstart, Lend, n->op);
  // 测试条件是否为真。如果不是，跳转到结束标签
  r2 = cgloadint(1, P_INT);
  cgcompare_and_jump(A_EQ, r, r2, Lend, P_INT);

  // 生成循环体的复合语句
  genAST(n->right, NOLABEL, Lstart, Lend, n->op);

  // 最后输出跳回条件的跳转，
  // 和结束标签
  cgjump(Lstart);
  cglabel(Lend);
  return (NOREG);
}

// 生成 SWITCH 语句的代码
static int genSWITCH(struct ASTnode *n) {
  int *caselabel;
  int Lend;
  int Lcode = 0;
  int i, reg, r2, type;
  struct ASTnode *c;

  // 为 case 标签创建一个数组
  caselabel = (int *) malloc((n->a_intvalue + 1) * sizeof(int));
  if (caselabel == NULL)
    fatal("malloc failed in genSWITCH");

  // 因为 QBE 还不支持跳转表，
  // 我们只需评估 switch 条件然后
  // 做连续的比较和跳转，
  // 就像我们做连续的 if/else 一样

  // 为 switch 语句的末尾生成一个标签。
  Lend = genlabel();

  // 为每个 case 生成标签。将结束标签作为
  // 所有 case 之后的条目
  for (i = 0, c = n->right; c != NULL; i++, c = c->right)
    caselabel[i] = genlabel();
  caselabel[i] = Lend;

  // 输出计算 switch 条件的代码
  reg = genAST(n->left, NOLABEL, NOLABEL, NOLABEL, 0);
  type = n->left->type;

  // 遍历右子链表来
  // 为每个 case 生成代码
  for (i = 0, c = n->right; c != NULL; i++, c = c->right) {

    // 为 case 将跳转到的实际代码生成一个标签
    if (Lcode == 0)
      Lcode = genlabel();

    // 输出此 case 测试的标签
    cglabel(caselabel[i]);

    // 做比较和跳转，但如果是 default case 则不跳转
    if (c->op != A_DEFAULT) {
      // 如果值不匹配则跳转到下一个 case
      r2 = cgloadint(c->a_intvalue, type);
      cgcompare_and_jump(A_EQ, reg, r2, caselabel[i + 1], type);

      // 否则跳转到处理此 case 的代码
      cgjump(Lcode);
    }
    // 生成 case 代码。传入 break 的结束标签。
    // 如果 case 没有 body，我们将会落入下面的 body。
    // 重置 Lcode 以便在下一个循环中创建新的代码标签。
    if (c->left) {
      cglabel(Lcode);
      genAST(c->left, NOLABEL, NOLABEL, Lend, 0);
      Lcode = 0;
    }
  }

  // 现在输出结束标签。
  cglabel(Lend);
  return (NOREG);
}

// 生成
// A_LOGAND 或 A_LOGOR 操作的代码
static int gen_logandor(struct ASTnode *n) {
  // 生成两个标签
  int Lfalse = genlabel();
  int Lend = genlabel();
  int reg;
  int type;

  // 生成左表达式的代码
  // 然后跳转到假标签
  reg = genAST(n->left, NOLABEL, NOLABEL, NOLABEL, 0);
  type = n->left->type;
  cgboolean(reg, n->op, Lfalse, type);

  // 生成右表达式的代码
  // 然后跳转到假标签
  reg = genAST(n->right, NOLABEL, NOLABEL, NOLABEL, 0);
  type = n->right->type;
  cgboolean(reg, n->op, Lfalse, type);

  // 我们没有跳转，所以设置正确的布尔值
  if (n->op == A_LOGAND) {
    cgloadboolean(reg, 1, type);
    cgjump(Lend);
    cglabel(Lfalse);
    cgloadboolean(reg, 0, type);
  } else {
    cgloadboolean(reg, 0, type);
    cgjump(Lend);
    cglabel(Lfalse);
    cgloadboolean(reg, 1, type);
  }
  cglabel(Lend);
  return (reg);
}

// 生成函数调用参数的代码，
// 然后用这些参数调用函数。返回
// 保存函数返回值的临时变量。
static int gen_funccall(struct ASTnode *n) {
  struct ASTnode *gluetree;
  int i = 0, numargs = 0;
  int *arglist = NULL;
  int *typelist = NULL;

  // 确定实际参数数量
  for (gluetree = n->left; gluetree != NULL; gluetree = gluetree->left) {
    numargs++;
  }

  // 分配内存来保存参数临时变量列表。
  // 我们需要遍历参数列表来确定大小
  for (i = 0, gluetree = n->left; gluetree != NULL; gluetree = gluetree->left)
    i++;

  if (i != 0) {
    arglist = (int *) malloc(i * sizeof(int));
    if (arglist == NULL)
      fatal("malloc failed in gen_funccall");
    typelist = (int *) malloc(i * sizeof(int));
    if (typelist == NULL)
      fatal("malloc failed in gen_funccall");
  }
  // 如果有参数列表，从最后一个参数（右子）
  // 到第一个遍历此列表。
  // 同时缓存每个表达式的类型
  for (i = 0, gluetree = n->left; gluetree != NULL; gluetree = gluetree->left) {
    // 计算表达式的值
    arglist[i] =
      genAST(gluetree->right, NOLABEL, NOLABEL, NOLABEL, gluetree->op);
    typelist[i++] = gluetree->right->type;
  }

  // 调用函数并返回其结果
  return (cgcall(n->sym, numargs, arglist, typelist));
}

// 三元表达式生成代码
static int gen_ternary(struct ASTnode *n) {
  int Lfalse, Lend;
  int reg, expreg;
  int r, r2;

  // 生成两个标签：一个用于
  // 假表达式，一个用于
  // 整体表达式的结束
  Lfalse = genlabel();
  Lend = genlabel();

  // 生成条件代码
  r = genAST(n->left, Lfalse, NOLABEL, NOLABEL, n->op);
  // 测试条件是否为真。如果不是，跳转到假标签
  r2 = cgloadint(1, P_INT);
  cgcompare_and_jump(A_EQ, r, r2, Lfalse, P_INT);

  // 获取一个临时变量来保存两个表达式的结果
  reg = cgalloctemp();

  // 生成真表达式和假标签。
  // 将表达式结果移动到已知的临时变量中。
  expreg = genAST(n->mid, NOLABEL, NOLABEL, NOLABEL, n->op);
  cgmove(expreg, reg, n->mid->type);
  cgjump(Lend);
  cglabel(Lfalse);

  // 生成假表达式和结束标签。
  // 将表达式结果移动到已知的临时变量中。
  expreg = genAST(n->right, NOLABEL, NOLABEL, NOLABEL, n->op);
  cgmove(expreg, reg, n->right->type);
  cglabel(Lend);
  return (reg);
}

// 给定一个 AST、一个可选标签和
// 父级的 AST 操作，递归生成汇编代码。
// 返回树最终值的临时变量 id。
int genAST(struct ASTnode *n, int iflabel, int looptoplabel,
	   int loopendlabel, int parentASTop) {
  int leftreg = NOREG, rightreg = NOREG;
  int lefttype = P_VOID, type = P_VOID;
  struct symtable *leftsym = NULL;

  // 空树，什么都不做
  if (n == NULL)
    return (NOREG);

  // 更新输出中的行号
  update_line(n);

  // 我们有一些特定的 AST 节点处理在顶部，
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
      // 执行每个子语句，并在每个子语句
      // 之后释放临时变量
      if (n->left != NULL)
	genAST(n->left, iflabel, looptoplabel, loopendlabel, n->op);
      if (n->right != NULL)
	genAST(n->right, iflabel, looptoplabel, loopendlabel, n->op);
      return (NOREG);
    case A_FUNCTION:
      // 在子树中的代码之前生成函数的前导码
      cgfuncpreamble(n->sym);
      genAST(n->left, NOLABEL, NOLABEL, NOLABEL, n->op);
      cgfuncpostamble(n->sym);
      return (NOREG);
  }

  // 一般的 AST 节点处理在下方

  // 获取左右子树的值。同时获取类型
  if (n->left) {
    lefttype = type = n->left->type;
    leftsym = n->left->sym;
    leftreg = genAST(n->left, NOLABEL, NOLABEL, NOLABEL, n->op);
  }
  if (n->right) {
    type = n->right->type;
    rightreg = genAST(n->right, NOLABEL, NOLABEL, NOLABEL, n->op);
  }

  switch (n->op) {
    case A_ADD:
      return (cgadd(leftreg, rightreg, type));
    case A_SUBTRACT:
      return (cgsub(leftreg, rightreg, type));
    case A_MULTIPLY:
      return (cgmul(leftreg, rightreg, type));
    case A_DIVIDE:
      return (cgdivmod(leftreg, rightreg, A_DIVIDE, type));
    case A_MOD:
      return (cgdivmod(leftreg, rightreg, A_MOD, type));
    case A_AND:
      return (cgand(leftreg, rightreg, type));
    case A_OR:
      return (cgor(leftreg, rightreg, type));
    case A_XOR:
      return (cgxor(leftreg, rightreg, type));
    case A_LSHIFT:
      return (cgshl(leftreg, rightreg, type));
    case A_RSHIFT:
      return (cgshr(leftreg, rightreg, type));
    case A_EQ:
    case A_NE:
    case A_LT:
    case A_GT:
    case A_LE:
    case A_GE:
      return (cgcompare_and_set(n->op, leftreg, rightreg, lefttype));
    case A_INTLIT:
      return (cgloadint(n->a_intvalue, n->type));
    case A_STRLIT:
      return (cgloadglobstr(n->a_intvalue));
    case A_IDENT:
      // 如果我们是 rvalue 或正在被解引用，则加载我们的值
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

      // 对于 '+=' 及相关运算符，生成适当的代码
      // 并获取结果的临时变量。然后取左子节点，
      // 使其成为右子节点，这样我们就可以进入赋值代码。
      switch (n->op) {
	case A_ASPLUS:
	  leftreg = cgadd(leftreg, rightreg, type);
	  n->right = n->left;
	  break;
	case A_ASMINUS:
	  leftreg = cgsub(leftreg, rightreg, type);
	  n->right = n->left;
	  break;
	case A_ASSTAR:
	  leftreg = cgmul(leftreg, rightreg, type);
	  n->right = n->left;
	  break;
	case A_ASSLASH:
	  leftreg = cgdivmod(leftreg, rightreg, A_DIVIDE, type);
	  n->right = n->left;
	  break;
	case A_ASMOD:
	  leftreg = cgdivmod(leftreg, rightreg, A_MOD, type);
	  n->right = n->left;
	  break;
      }

      // 现在进入赋值代码
      // 我们是赋值给标识符还是通过指针赋值？
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
      // 将子节点的类型扩展为父节点的类型
      return (cgwiden(leftreg, lefttype, n->type));
    case A_RETURN:
      cgreturn(leftreg, Functionid);
      return (NOREG);
    case A_ADDR:
      // 如果我们有符号，获取其地址。否则，
      // 左边临时变量已经包含地址，因为
      // 它是成员访问
      if (n->sym != NULL)
	return (cgaddress(n->sym));
      else
	return (leftreg);
    case A_DEREF:
      // 如果我们是 rvalue，解引用以获取我们指向的值，
      // 否则将其留给 A_ASSIGN 通过指针存储
      if (n->rvalue)
	return (cgderef(leftreg, lefttype));
      else
	return (leftreg);
    case A_SCALE:
      // 小优化：如果
      // 缩放值是已知的 2 的幂，则使用移位
      switch (n->a_size) {
	case 2:
	  return (cgshlconst(leftreg, 1, type));
	case 4:
	  return (cgshlconst(leftreg, 2, type));
	case 8:
	  return (cgshlconst(leftreg, 3, type));
	default:
	  // 加载一个包含大小的临时变量
	  // 并将 leftreg 乘以这个大小
	  rightreg = cgloadint(n->a_size, P_INT);
	  return (cgmul(leftreg, rightreg, type));
      }
    case A_POSTINC:
    case A_POSTDEC:
      // 将变量的值加载到临时变量中并
      // 后递增/递减它
      return (cgloadvar(n->sym, n->op));
    case A_PREINC:
    case A_PREDEC:
      // 将变量的值加载到临时变量中并
      // 前递增/递减它
      return (cgloadvar(leftsym, n->op));
    case A_NEGATE:
      return (cgnegate(leftreg, type));
    case A_INVERT:
      return (cginvert(leftreg, type));
    case A_LOGNOT:
      return (cglognot(leftreg, type));
    case A_TOBOOL:
      // 如果父 AST 节点是 A_IF 或 A_WHILE，生成
      // 一个比较后跟一个跳转。否则，根据
      // 它的零性或非零性将临时变量设置为 0 或 1
      return (cgboolean(leftreg, parentASTop, iflabel, type));
    case A_BREAK:
      cgjump(loopendlabel);
      return (NOREG);
    case A_CONTINUE:
      cgjump(looptoplabel);
      return (NOREG);
    case A_CAST:
      return (cgcast(leftreg, lefttype, n->type));
    default:
      fatald("Unknown AST operator", n->op);
  }
  return (NOREG);		// 保持 -Wall 开心
}

void genpreamble(char *filename) {
  cgpreamble(filename);
}

void genpostamble() {
  cgpostamble();
}

void genglobsym(struct symtable *node) {
  cgglobsym(node);
}

// 生成一个全局字符串。
// 如果 append 为真，追加到
// 之前的 genglobstr() 调用。
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