#include "defs.h"
#include "data.h"
#include "decl.h"

// 使用 QBE 中间语言生成 x86-64 代码的代码生成器。
// Copyright (c) 2019 Warren Toomey, GPL3

// 切换到文本段
void cgtextseg() {
}

// 切换到数据段
void cgdataseg() {
}

// 给定一个标量类型值，返回
// 匹配 QBE 类型的字符。
// 因为字符存储在栈上，
// 我们可以为 P_CHAR 返回 'w'。
char cgqbetype(int type) {
  if (ptrtype(type))
    return ('l');
  switch (type) {
    case P_VOID:
      return (' ');
    case P_CHAR:
      return ('w');
    case P_INT:
      return ('w');
    case P_LONG:
      return ('l');
    default:
      fatald("Bad type in cgqbetype:", type);
  }
  return (0);			// 保持 -Wall 开心
}

// 给定一个标量类型值，返回
// QBE 类型的大小（以字节为单位）。
int cgprimsize(int type) {
  if (ptrtype(type))
    return (8);
  switch (type) {
    case P_CHAR:
      return (1);
    case P_INT:
      return (4);
    case P_LONG:
      return (8);
    default:
      fatald("Bad type in cgprimsize:", type);
  }
  return (0);			// 保持 -Wall 开心
}

// 给定一个标量类型、一个现有的内存偏移量
//（尚未分配给任何东西）和一个方向（1 向上，-1 向下），
// 计算并返回适合此标量类型的
// 内存偏移量。这可能是原始偏移量，
// 也可能是原始偏移量的上方或下方。
int cgalign(int type, int offset, int direction) {
  int alignment;

  // 在 x86-64 上我们不需要这样做，但让我们
  // 在任何偏移量上对齐字符，并在 4 字节对齐上
  // 对齐 int/指针
  switch (type) {
    case P_CHAR:
      break;
    default:
      // 将我们现在拥有的任何东西对齐到 4 字节对齐。
      // 我把通用代码放在这里，以便它可以在其他地方重用。
      alignment = 4;
      offset = (offset + direction * (alignment - 1)) & ~(alignment - 1);
  }
  return (offset);
}

// 分配一个 QBE 临时变量
static int nexttemp = 0;
int cgalloctemp(void) {
  return (++nexttemp);
}

// 打印一个输出文件的前导码
void cgpreamble(char *filename) {
}

// 文件末尾不需要做任何事
void cgpostamble() {
}

// 布尔标志：此函数中是否已有 switch 语句？
static int used_switch;

// 打印函数前导码
void cgfuncpreamble(struct symtable *sym) {
  char *name = sym->name;
  struct symtable *parm, *locvar;
  int size, bigsize;
  int label;

  // 输出函数的名称和返回类型
  if (sym->class == C_GLOBAL)
    fprintf(Outfile, "export ");
  fprintf(Outfile, "function %c $%s(", cgqbetype(sym->type), name);

  // 输出参数名称和类型。对于任何需要地址的参数，
  // 在我们下面复制它们的值时更改它们的名称
  for (parm = sym->member; parm != NULL; parm = parm->next) {
    if (parm->st_hasaddr == 1)
      fprintf(Outfile, "%c %%.p%s, ", cgqbetype(parm->type), parm->name);
    else
      fprintf(Outfile, "%c %%%s, ", cgqbetype(parm->type), parm->name);
  }
  fprintf(Outfile, ") {\n");

  // 获取函数开始的标签
  label = genlabel();
  cglabel(label);

  // 对于任何需要地址的参数，
  // 在栈上为它们分配内存。QBE 不允许我们执行 alloc1，
  // 所以我们为字符分配 4 个字节。将值从
  // 参数复制到新内存位置。
  for (parm = sym->member; parm != NULL; parm = parm->next) {
    if (parm->st_hasaddr == 1) {
      size = cgprimsize(parm->type);
      bigsize = (size == 1) ? 4 : size;
      fprintf(Outfile, "  %%%s =l alloc%d 1\n", parm->name, bigsize);

      // 复制到分配的内存
      switch (size) {
	case 1:
	  fprintf(Outfile, "  storeb %%.p%s, %%%s\n", parm->name, parm->name);
	  break;
	case 4:
	  fprintf(Outfile, "  storew %%.p%s, %%%s\n", parm->name, parm->name);
	  break;
	case 8:
	  fprintf(Outfile, "  storel %%.p%s, %%%s\n", parm->name, parm->name);
      }
    }
  }

  // 为任何需要放在栈上的局部变量分配内存。有两个原因。
  // 第一个是地址被使用的局部变量。
  // 第二个是字符变量。我们需要这样做，因为 QBE 只能截断到 8 位
  // 用于内存中的位置
  for (locvar = Loclhead; locvar != NULL; locvar = locvar->next) {
    if (locvar->st_hasaddr == 1) {
      // 获取所有元素的总大小（如果是数组）。
      // 向上舍入到 8 的最接近倍数，以确保
      // 指针在 8 字节边界上对齐
      size = locvar->size * locvar->nelems;
      size = (size + 7) >> 3;
      fprintf(Outfile, "  %%%s =l alloc8 %d\n", locvar->name, size);
    } else if (locvar->type == P_CHAR) {
      locvar->st_hasaddr = 1;
      fprintf(Outfile, "  %%%s =l alloc4 1\n", locvar->name);
    }
  }

  used_switch = 0;		// 我们还没有输出 switch 处理代码
}

// 打印函数后导码
void cgfuncpostamble(struct symtable *sym) {
  cglabel(sym->st_endlabel);

  // 如果函数类型不是 void 则返回一个值
  if (sym->type != P_VOID)
    fprintf(Outfile, "  ret %%.ret\n}\n");
  else
    fprintf(Outfile, "  ret\n}\n");
}

// 将整数字面量值加载到临时变量中。
// 返回临时变量的编号。
int cgloadint(int value, int type) {
  // 获取一个新的临时变量
  int t = cgalloctemp();

  fprintf(Outfile, "  %%.t%d =%c copy %d\n", t, cgqbetype(type), value);
  return (t);
}

// 从变量加载值到临时变量中。
// 返回临时变量的编号。如果
// 操作是前或后递增/递减，
// 还要执行此操作。
int cgloadvar(struct symtable *sym, int op) {
  int r, posttemp, offset = 1;
  char qbeprefix;

  // 获取一个新的临时变量
  r = cgalloctemp();

  // 如果符号是指针，使用它指向的类型的
  // 大小作为任何递增或递减的大小。
  // 如果不是，则为 1。
  if (ptrtype(sym->type))
    offset = typesize(value_at(sym->type), sym->ctype);

  // 对于递减取负
  if (op == A_PREDEC || op == A_POSTDEC)
    offset = -offset;

  // 获取符号的相应 QBE 前缀
  qbeprefix = ((sym->class == C_GLOBAL) || (sym->class == C_STATIC) ||
	       (sym->class == C_EXTERN)) ? '$' : '%';

  // 如果我们有前操作
  if (op == A_PREINC || op == A_PREDEC) {
    if (sym->st_hasaddr || qbeprefix == '$') {
      // 获取一个新的临时变量
      posttemp = cgalloctemp();
      switch (sym->size) {
	case 1:
	  fprintf(Outfile, "  %%.t%d =w loadub %c%s\n", posttemp, qbeprefix,
		  sym->name);
	  fprintf(Outfile, "  %%.t%d =w add %%.t%d, %d\n", posttemp, posttemp,
		  offset);
	  fprintf(Outfile, "  storeb %%.t%d, %c%s\n", posttemp, qbeprefix,
		  sym->name);
	  break;
	case 4:
	  fprintf(Outfile, "  %%.t%d =w loadsw %c%s\n", posttemp, qbeprefix,
		  sym->name);
	  fprintf(Outfile, "  %%.t%d =w add %%.t%d, %d\n", posttemp, posttemp,
		  offset);
	  fprintf(Outfile, "  storew %%.t%d, %c%s\n", posttemp, qbeprefix,
		  sym->name);
	  break;
	case 8:
	  fprintf(Outfile, "  %%.t%d =l loadl %c%s\n", posttemp, qbeprefix,
		  sym->name);
	  fprintf(Outfile, "  %%.t%d =l add %%.t%d, %d\n", posttemp, posttemp,
		  offset);
	  fprintf(Outfile, "  storel %%.t%d, %c%s\n", posttemp, qbeprefix,
		  sym->name);
      }
    } else
      fprintf(Outfile, "  %c%s =%c add %c%s, %d\n",
	      qbeprefix, sym->name, cgqbetype(sym->type), qbeprefix,
	      sym->name, offset);
  }
  // 现在用值加载输出临时变量
  if (sym->st_hasaddr || qbeprefix == '$') {
    switch (sym->size) {
      case 1:
	fprintf(Outfile, "  %%.t%d =w loadub %c%s\n", r, qbeprefix,
		sym->name);
	break;
      case 4:
	fprintf(Outfile, "  %%.t%d =w loadsw %c%s\n", r, qbeprefix,
		sym->name);
	break;
      case 8:
	fprintf(Outfile, "  %%.t%d =l loadl %c%s\n", r, qbeprefix, sym->name);
    }
  } else
    fprintf(Outfile, "  %%.t%d =%c copy %c%s\n",
	    r, cgqbetype(sym->type), qbeprefix, sym->name);

  // 如果我们有后操作
  if (op == A_POSTINC || op == A_POSTDEC) {
    if (sym->st_hasaddr || qbeprefix == '$') {
      // 获取一个新的临时变量
      posttemp = cgalloctemp();
      switch (sym->size) {
	case 1:
	  fprintf(Outfile, "  %%.t%d =w loadub %c%s\n", posttemp, qbeprefix,
		  sym->name);
	  fprintf(Outfile, "  %%.t%d =w add %%.t%d, %d\n", posttemp, posttemp,
		  offset);
	  fprintf(Outfile, "  storeb %%.t%d, %c%s\n", posttemp, qbeprefix,
		  sym->name);
	  break;
	case 4:
	  fprintf(Outfile, "  %%.t%d =w loadsw %c%s\n", posttemp, qbeprefix,
		  sym->name);
	  fprintf(Outfile, "  %%.t%d =w add %%.t%d, %d\n", posttemp, posttemp,
		  offset);
	  fprintf(Outfile, "  storew %%.t%d, %c%s\n", posttemp, qbeprefix,
		  sym->name);
	  break;
	case 8:
	  fprintf(Outfile, "  %%.t%d =l loadl %c%s\n", posttemp, qbeprefix,
		  sym->name);
	  fprintf(Outfile, "  %%.t%d =l add %%.t%d, %d\n", posttemp, posttemp,
		  offset);
	  fprintf(Outfile, "  storel %%.t%d, %c%s\n", posttemp, qbeprefix,
		  sym->name);
      }
    } else
      fprintf(Outfile, "  %c%s =%c add %c%s, %d\n",
	      qbeprefix, sym->name, cgqbetype(sym->type), qbeprefix,
	      sym->name, offset);
  }
  // 返回带有值的临时变量
  return (r);
}

// 给定全局字符串的标签号，
// 将其地址加载到新的临时变量中
int cgloadglobstr(int label) {
  // 获取一个新的临时变量
  int r = cgalloctemp();
  fprintf(Outfile, "  %%.t%d =l copy $L%d\n", r, label);
  return (r);
}

// 将两个临时变量相加并返回
// 带有结果的临时变量的编号
int cgadd(int r1, int r2, int type) {
  fprintf(Outfile, "  %%.t%d =%c add %%.t%d, %%.t%d\n",
	  r1, cgqbetype(type), r1, r2);
  return (r1);
}

// 从第一个临时变量减去第二个并返回
// 带有结果的临时变量的编号
int cgsub(int r1, int r2, int type) {
  fprintf(Outfile, "  %%.t%d =%c sub %%.t%d, %%.t%d\n",
	  r1, cgqbetype(type), r1, r2);
  return (r1);
}

// 将两个临时变量相乘并返回
// 带有结果的临时变量的编号
int cgmul(int r1, int r2, int type) {
  fprintf(Outfile, "  %%.t%d =%c mul %%.t%d, %%.t%d\n",
	  r1, cgqbetype(type), r1, r2);
  return (r1);
}

// 将第一个临时变量除以第二个并返回
// 带有结果的临时变量的编号
int cgdivmod(int r1, int r2, int op, int type) {
  if (op == A_DIVIDE)
    fprintf(Outfile, "  %%.t%d =%c div %%.t%d, %%.t%d\n",
	    r1, cgqbetype(type), r1, r2);
  else
    fprintf(Outfile, "  %%.t%d =%c rem %%.t%d, %%.t%d\n",
	    r1, cgqbetype(type), r1, r2);
  return (r1);
}

// 两个临时变量按位 AND
int cgand(int r1, int r2, int type) {
  fprintf(Outfile, "  %%.t%d =%c and %%.t%d, %%.t%d\n",
	  r1, cgqbetype(type), r1, r2);
  return (r1);
}

// 两个临时变量按位 OR
int cgor(int r1, int r2, int type) {
  fprintf(Outfile, "  %%.t%d =%c or %%.t%d, %%.t%d\n",
	  r1, cgqbetype(type), r1, r2);
  return (r1);
}

// 两个临时变量按位 XOR
int cgxor(int r1, int r2, int type) {
  fprintf(Outfile, "  %%.t%d =%c xor %%.t%d, %%.t%d\n",
	  r1, cgqbetype(type), r1, r2);
  return (r1);
}

// 将 r1 左移 r2 位
int cgshl(int r1, int r2, int type) {
  fprintf(Outfile, "  %%.t%d =%c shl %%.t%d, %%.t%d\n",
	  r1, cgqbetype(type), r1, r2);
  return (r1);
}

// 将 r1 右移 r2 位
int cgshr(int r1, int r2, int type) {
  fprintf(Outfile, "  %%.t%d =%c shr %%.t%d, %%.t%d\n",
	  r1, cgqbetype(type), r1, r2);
  return (r1);
}

// 取反临时变量的值
int cgnegate(int r, int type) {
  fprintf(Outfile, "  %%.t%d =%c sub 0, %%.t%d\n", r, cgqbetype(type), r);
  return (r);
}

// 反转临时变量的值
int cginvert(int r, int type) {
  fprintf(Outfile, "  %%.t%d =%c xor %%.t%d, -1\n", r, cgqbetype(type), r);
  return (r);
}

// 逻辑否定临时变量的值
int cglognot(int r, int type) {
  char q = cgqbetype(type);
  fprintf(Outfile, "  %%.t%d =%c ceq%c %%.t%d, 0\n", r, q, q, r);
  return (r);
}

// 将布尔值（仅 0 或 1）
// 加载到给定的临时变量中
void cgloadboolean(int r, int val, int type) {
  fprintf(Outfile, "  %%.t%d =%c copy %d\n", r, cgqbetype(type), val);
}

// 将整数值转换为布尔值。如果是
// IF、WHILE、LOGAND 或 LOGOR 操作则跳转
int cgboolean(int r, int op, int label, int type) {
  // 获取下一条指令的标签
  int label2 = genlabel();

  // 获取比较的新临时变量
  int r2 = cgalloctemp();

  // 将临时变量转换为布尔值
  fprintf(Outfile, "  %%.t%d =l cne%c %%.t%d, 0\n", r2, cgqbetype(type), r);

  switch (op) {
    case A_IF:
    case A_WHILE:
    case A_LOGAND:
      fprintf(Outfile, "  jnz %%.t%d, @L%d, @L%d\n", r2, label2, label);
      break;
    case A_LOGOR:
      fprintf(Outfile, "  jnz %%.t%d, @L%d, @L%d\n", r2, label, label2);
      break;
  }

  // 输出下一条指令的标签
  cglabel(label2);
  return (r2);
}

// 使用给定符号 id 调用函数。
// 返回带有结果的临时变量
int cgcall(struct symtable *sym, int numargs, int *arglist, int *typelist) {
  int outr;
  int i;

  // 获取返回结果的新临时变量
  outr = cgalloctemp();

  // 调用函数
  if (sym->type == P_VOID)
    fprintf(Outfile, "  call $%s(", sym->name);
  else
    fprintf(Outfile, "  %%.t%d =%c call $%s(", outr, cgqbetype(sym->type),
	    sym->name);

  // 输出参数列表
  for (i = numargs - 1; i >= 0; i--) {
    fprintf(Outfile, "%c %%.t%d, ", cgqbetype(typelist[i]), arglist[i]);
  }
  fprintf(Outfile, ")\n");

  return (outr);
}

// 将临时变量左移一个常量。因为我们仅将其用于
// 地址计算，如需要则将类型扩展为 QBE 'l'
int cgshlconst(int r, int val, int type) {
  int r2 = cgalloctemp();
  int r3 = cgalloctemp();

  if (cgprimsize(type) < 8) {
    fprintf(Outfile, "  %%.t%d =l extsw %%.t%d\n", r2, r);
    fprintf(Outfile, "  %%.t%d =l shl %%.t%d, %d\n", r3, r2, val);
  } else
    fprintf(Outfile, "  %%.t%d =l shl %%.t%d, %d\n", r3, r, val);
  return (r3);
}

// 将临时变量的值存储到全局变量中
int cgstorglob(int r, struct symtable *sym) {

  // 我们可以存储到内存中的字节
  char q = cgqbetype(sym->type);
  if (sym->type == P_CHAR)
    q = 'b';

  fprintf(Outfile, "  store%c %%.t%d, $%s\n", q, r, sym->name);
  return (r);
}

// 将临时变量的值存储到局部变量中
int cgstorlocal(int r, struct symtable *sym) {

  // 如果变量在栈上，使用 store 指令
  if (sym->st_hasaddr) {
    fprintf(Outfile, "  store%c %%.t%d, %%%s\n",
	    cgqbetype(sym->type), r, sym->name);
  } else {
    fprintf(Outfile, "  %%%s =%c copy %%.t%d\n",
	    sym->name, cgqbetype(sym->type), r);
  }
  return (r);
}

// 生成全局符号但不是函数
void cgglobsym(struct symtable *node) {
  int size, type;
  int initvalue;
  int i;

  if (node == NULL)
    return;
  if (node->stype == S_FUNCTION)
    return;

  // 获取变量的大小（如果是数组则为其元素）
  // 和变量的类型
  if (node->stype == S_ARRAY) {
    size = typesize(value_at(node->type), node->ctype);
    type = value_at(node->type);
  } else {
    size = node->size;
    type = node->type;
  }

  // 生成全局标识和标签
  cgdataseg();
  if (node->class == C_GLOBAL)
    fprintf(Outfile, "export ");
  if ((node->type == P_STRUCT) || (node->type == P_UNION))
    fprintf(Outfile, "data $%s = align 8 { ", node->name);
  else
    fprintf(Outfile, "data $%s = align %d { ", node->name, cgprimsize(type));

  // 为一个或多个元素输出空间
  for (i = 0; i < node->nelems; i++) {

    // 获取任何初始值
    initvalue = 0;
    if (node->initlist != NULL)
      initvalue = node->initlist[i];

    // 为此类型生成空间
    switch (size) {
      case 1:
	fprintf(Outfile, "b %d, ", initvalue);
	break;
      case 4:
	fprintf(Outfile, "w %d, ", initvalue);
	break;
      case 8:
	// 生成指向字符串字面量的指针。将零值
	// 视为实际零，而不是标签 L0
	if (node->initlist != NULL && type == pointer_to(P_CHAR)
	    && initvalue != 0)
	  fprintf(Outfile, "l $L%d, ", initvalue);
	else
	  fprintf(Outfile, "l %d, ", initvalue);
	break;
      default:
	fprintf(Outfile, "z %d, ", size);
    }
  }
  fprintf(Outfile, "}\n");
}

// 生成全局字符串及其标签。
// 如果 append 为真，不输出标签。
void cgglobstr(int l, char *strvalue, int append) {
  char *cptr;
  if (!append)
    fprintf(Outfile, "data $L%d = { ", l);

  for (cptr = strvalue; *cptr; cptr++) {
    fprintf(Outfile, "b %d, ", *cptr);
  }
}

// NUL 终止全局字符串
void cgglobstrend(void) {
  fprintf(Outfile, " b 0 }\n");
}

// 比较指令列表，
// 按 AST 顺序：A_EQ、A_NE、A_LT、A_GT、A_LE、A_GE
static char *cmplist[] = { "ceq", "cne", "cslt", "csgt", "csle", "csge" };

// 比较两个临时变量并在为真时设置。
int cgcompare_and_set(int ASTop, int r1, int r2, int type) {
  int r3;
  char q = cgqbetype(type);

  // 检查 AST 操作的范围
  if (ASTop < A_EQ || ASTop > A_GE)
    fatal("Bad ASTop in cgcompare_and_set()");

  // 获取比较的新临时变量
  r3 = cgalloctemp();

  fprintf(Outfile, "  %%.t%d =%c %s%c %%.t%d, %%.t%d\n",
	  r3, q, cmplist[ASTop - A_EQ], q, r1, r2);
  return (r3);
}

// 生成一个标签
void cglabel(int l) {
  fprintf(Outfile, "@L%d\n", l);
}

// 生成跳转到标签的跳转
void cgjump(int l) {
  fprintf(Outfile, "  jmp @L%d\n", l);
}

// 反转跳转指令列表，
// 按 AST 顺序：A_EQ、A_NE、A_LT、A_GT、A_LE、A_GE
static char *invcmplist[] = { "cne", "ceq", "csge", "csle", "csgt", "cslt" };

// 比较两个临时变量并在为假时跳转。
int cgcompare_and_jump(int ASTop, int r1, int r2, int label, int type) {
  int label2;
  int r3;
  char q = cgqbetype(type);

  // 检查 AST 操作的范围
  if (ASTop < A_EQ || ASTop > A_GE)
    fatal("Bad ASTop in cgcompare_and_set()");

  // 获取下一条指令的标签
  label2 = genlabel();

  // 获取比较的新临时变量
  r3 = cgalloctemp();

  fprintf(Outfile, "  %%.t%d =%c %s%c %%.t%d, %%.t%d\n",
	  r3, q, invcmplist[ASTop - A_EQ], q, r1, r2);
  fprintf(Outfile, "  jnz %%.t%d, @L%d, @L%d\n", r3, label, label2);
  cglabel(label2);
  return (NOREG);
}

// 将临时变量中的值从旧类型
// 扩展到新类型，并返回带有
// 此新值的临时变量
int cgwiden(int r, int oldtype, int newtype) {
  char oldq = cgqbetype(oldtype);
  char newq = cgqbetype(newtype);

  // 获取一个新的临时变量
  int t = cgalloctemp();

  switch (oldtype) {
    case P_CHAR:
      fprintf(Outfile, "  %%.t%d =%c extub %%.t%d\n", t, newq, r);
      break;
    default:
      fprintf(Outfile, "  %%.t%d =%c exts%c %%.t%d\n", t, newq, oldq, r);
  }
  return (t);
}

// 生成从函数返回值的代码
void cgreturn(int reg, struct symtable *sym) {

  // 只有在我们有返回值时才返回值
  if (reg != NOREG)
    fprintf(Outfile, "  %%.ret =%c copy %%.t%d\n", cgqbetype(sym->type), reg);

  cgjump(sym->st_endlabel);
}

// 生成加载标识符地址的代码。
// 返回一个新的临时变量
int cgaddress(struct symtable *sym) {
  int r = cgalloctemp();
  char qbeprefix = ((sym->class == C_GLOBAL) || (sym->class == C_STATIC) ||
		    (sym->class == C_EXTERN)) ? '$' : '%';

  fprintf(Outfile, "  %%.t%d =l copy %c%s\n", r, qbeprefix, sym->name);
  return (r);
}

// 解引用指针以获取其指向的值
// 到新的临时变量中
int cgderef(int r, int type) {
  // 获取我们指向的类型
  int newtype = value_at(type);
  // 现在获取此类型的大小
  int size = cgprimsize(newtype);
  // 获取返回结果临时变量
  int ret = cgalloctemp();

  switch (size) {
    case 1:
      fprintf(Outfile, "  %%.t%d =w loadub %%.t%d\n", ret, r);
      break;
    case 4:
      fprintf(Outfile, "  %%.t%d =w loadsw %%.t%d\n", ret, r);
      break;
    case 8:
      fprintf(Outfile, "  %%.t%d =l loadl %%.t%d\n", ret, r);
      break;
    default:
      fatald("Can't cgderef on type:", type);
  }
  return (ret);
}

// 通过解引用的指针存储
int cgstorderef(int r1, int r2, int type) {
  // 获取类型的大小
  int size = cgprimsize(type);

  switch (size) {
    case 1:
      fprintf(Outfile, "  storeb %%.t%d, %%.t%d\n", r1, r2);
      break;
    case 4:
      fprintf(Outfile, "  storew %%.t%d, %%.t%d\n", r1, r2);
      break;
    case 8:
      fprintf(Outfile, "  storel %%.t%d, %%.t%d\n", r1, r2);
      break;
    default:
      fatald("Can't cgstoderef on type:", type);
  }
  return (r1);
}

// 在临时变量之间移动值
void cgmove(int r1, int r2, int type) {
  fprintf(Outfile, "  %%.t%d =%c copy %%.t%d\n", r2, cgqbetype(type), r1);
}

// 输出一个 gdb 指令，说明以下
// 汇编代码来自哪一行源代码
void cglinenum(int line) {
  // fprintf(Outfile, "\t.loc 1 %d 0\n", line);
}

// 将临时变量的值从旧类型更改为新类型。
int cgcast(int t, int oldtype, int newtype) {
  // 获取返回结果临时变量
  int ret = cgalloctemp();
  int oldsize, newsize;
  char qnew;

  // 如果新类型是指针
  if (ptrtype(newtype)) {
    // 如果旧类型也是指针则什么都不做
    if (ptrtype(oldtype))
      return (t);
    // 否则，从原始类型扩展到指针
    return (cgwiden(t, oldtype, newtype));
  }

  // 新类型不是指针
  // 获取新的 QBE 类型
  // 和类型大小（以字节为单位）
  qnew = cgqbetype(newtype);
  oldsize = cgprimsize(oldtype);
  newsize = cgprimsize(newtype);

  // 如果两者大小相同则什么都不做
  if (newsize == oldsize)
    return (t);

  // 如果新大小更小，我们可以复制，QBE 会截断它，
  // 否则使用 QBE 转换操作
  if (newsize < oldsize)
    fprintf(Outfile, " %%.t%d =%c copy %%.t%d\n", ret, qnew, t);
  else
    fprintf(Outfile, " %%.t%d =%c cast %%.t%d\n", ret, qnew, t);
  return (ret);
}