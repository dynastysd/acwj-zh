#include "defs.h"
#include "data.h"
#include "decl.h"

// x86-64代码生成器（NASM语法）
// Copyright (c) 2019 Warren Toomey, GPL3

// 标记我们正在输出到哪个段的标志
enum { no_seg, text_seg, data_seg } currSeg = no_seg;

void cgtextseg() {
  if (currSeg != text_seg) {
    fputs("\tsection .text\n", Outfile);
    currSeg = text_seg;
  }
}

void cgdataseg() {
  if (currSeg != data_seg) {
    fputs("\tsection .data\n", Outfile);
    currSeg = data_seg;
  }
}

// 给定标量类型值，返回
// 类型的大小（以字节为单位）。
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
  return (0);			// 保持-Wall满意
}

// 给定一个标量类型、一个现有的内存偏移量
// （尚未分配给任何东西）和一个方向（1是向上，-1是向下），
// 计算并返回适合此标量类型的对齐内存偏移量。
// 这可能是原始偏移量，也可能是原始偏移量的上方或下方。
int cgalign(int type, int offset, int direction) {
  int alignment;

  // 在x86-64上我们不需要这样做，但让我们
  // 在任何偏移量上对齐char，在4字节对齐上对齐int/指针
  switch (type) {
    case P_CHAR:
      return (offset);
    case P_INT:
    case P_LONG:
      break;
    default:
      if (!ptrtype(type))
        fatald("Bad type in cg_align:", type);
  }

  // 这里我们有int或long。在4字节偏移量上对齐它。
  // 我把通用代码放在这里以便可以重用。
  alignment = 4;
  offset = (offset + direction * (alignment - 1)) & ~(alignment - 1);
  return (offset);
}

// 下一个局部变量相对于栈基址指针的位置。
// 我们将偏移量存储为正数，以使对齐栈指针更容易
static int localOffset;
static int stackOffset;

// 创建新局部变量的位置。
int newlocaloffset(int type) {
  // 递减偏移量至少4字节
  // 并在栈上分配
  localOffset += (cgprimsize(type) > 4) ? cgprimsize(type) : 4;
  return (-localOffset);
}

// 可用寄存器及其名称的列表。
// 我们还需要字节和双字寄存器的列表。
// 列表还包括用于保存函数参数的寄存器
#define NUMFREEREGS 4
#define FIRSTPARAMREG 9		// 第一个参数寄存器在上述寄存器列表中的位置
static int freereg[NUMFREEREGS];
static char *reglist[]  = { "r10",  "r11", "r12", "r13", "r9", "r8", "rcx", "rdx", "rsi",
"rdi"  };
static char *breglist[]  = { "r10b",  "r11b",  "r12b",  "r13b",  "r9b",  "r8b",  "cl",  "dl",  "sil",
"dil"  };
static char *dreglist[]  = { "r10d",  "r11d",  "r12d",  "r13d",  "r9d",  "r8d",  "ecx",  "edx",
"esi", "edi"  };

// 将所有寄存器设置为可用
// 但如果reg为正，则不释放该寄存器。
void freeall_registers(int keepreg) {
  int i;
  for (i = 0; i < NUMFREEREGS; i++)
    if (i != keepreg)
      freereg[i] = 1;
}

// 分配一个免费寄存器。返回
// 寄存器的编号。如果没有可用寄存器则死亡。
int alloc_register(void) {
  int i;

  for (i = 0; i < NUMFREEREGS; i++) {
    if (freereg[i]) {
      freereg[i] = 0;
      return (i);
    }
  }
  fatal("Out of registers");
  return (NOREG);		// 保持-Wall满意
}

// 将寄存器返回到可用寄存器列表。
// 检查它是否已经在那里。
static void free_register(int reg) {
  if (freereg[reg] != 0)
    fatald("Error trying to free register", reg);
  freereg[reg] = 1;
}

// 打印汇编前导码
void cgpreamble() {
  freeall_registers(NOREG);
  fputs("\textern\tprintint\n", Outfile);
  fputs("\textern\tprintchar\n", Outfile);
  fputs("\textern\topen\n", Outfile);
  fputs("\textern\tclose\n", Outfile);
  fputs("\textern\tread\n", Outfile);
  fputs("\textern\twrite\n", Outfile);
  fputs("\textern\tprintf\n", Outfile);
  cgtextseg();
  fprintf(Outfile,
	  "; internal switch(expr) routine\n"
	  "; rsi = switch table, rax = expr\n"
	  "; from SubC: http://www.t3x.org/subc/\n"
	  "\n"
	  "switch:\n"
	  "        push   rsi\n"
	  "        mov    rsi, rdx\n"
	  "        mov    rbx, rax\n"
	  "        cld\n"
	  "        lodsq\n"
	  "        mov    rcx, rax\n"
	  "next:\n"
	  "        lodsq\n"
	  "        mov    rdx, rax\n"
	  "        lodsq\n"
	  "        cmp    rbx, rdx\n"
	  "        jnz    no\n"
	  "        pop    rsi\n"
	  "        jmp    rax\n"
	  "no:\n"
	  "        loop   next\n"
	  "        lodsq\n"
	  "        pop    rsi\n" "        jmp     rax\n" "\n");
}

// 没什么可做的
void cgpostamble() {
}

// 打印函数前导码
void cgfuncpreamble(struct symtable *sym) {
  char *name = sym->name;
  struct symtable *parm, *locvar;
  int cnt;
  int paramOffset = 16;		// 任何推送的参数从此栈偏移量开始
  int paramReg = FIRSTPARAMREG;	// 在上述寄存器列表中第一个参数寄存器的索引

  // 在文本段中输出，重置局部偏移量
  cgtextseg();
  localOffset = 0;

  // 输出函数开始，保存rsp和rbp
  if (sym->class == C_GLOBAL)
    fprintf(Outfile, "\tglobal\t%s\n", name);
  fprintf(Outfile,
	  "%s:\n" "\tpush\trbp\n"
	  "\tmov\trbp, rsp\n", name);

  // 将任何在寄存器中的参数复制到栈，最多六个。
  // 其余参数已经在栈上。
  for (parm = sym->member, cnt = 1; parm != NULL; parm = parm->next, cnt++) {
    if (cnt > 6) {
      parm->st_posn = paramOffset;
      paramOffset += 8;
    } else {
      parm->st_posn = newlocaloffset(parm->type);
      cgstorlocal(paramReg--, parm);
    }
  }

  // 对于其余的，如果是参数那么它们
  // 已经在栈上。如果是局部，则创建一个栈位置。
  for (locvar = Loclhead; locvar != NULL; locvar = locvar->next) {
    locvar->st_posn = newlocaloffset(locvar->type);
  }

  // 将栈指针对齐到16的倍数
  // 小于其之前的值
  stackOffset = (localOffset + 15) & ~15;
  fprintf(Outfile, "\tadd\trsp, %d\n", -stackOffset);
}

// 打印函数后导码
void cgfuncpostamble(struct symtable *sym) {
  cglabel(sym->st_endlabel);
  fprintf(Outfile, "\tadd\trsp, %d\n", stackOffset);
  fputs("\tpop	rbp\n" "\tret\n", Outfile);
}

// 将整数字面量值加载到寄存器中。
// 返回寄存器的编号。
// 对于x86-64，我们不需要担心类型。
int cgloadint(int value, int type) {
  // 获取一个新寄存器
  int r = alloc_register();

  fprintf(Outfile, "\tmov\t%s, %d\n", reglist[r], value);
  return (r);
}

// 将变量值加载到寄存器中。
// 返回寄存器的编号。如果
// 操作是前或后增/减，
// 也要执行此操作。
int cgloadglob(struct symtable *sym, int op) {
  // 获取一个新寄存器
  int r = alloc_register();

  if (cgprimsize(sym->type) == 8) {
    if (op == A_PREINC)
      fprintf(Outfile, "\tinc\tqword [%s]\n", sym->name);
    if (op == A_PREDEC)
      fprintf(Outfile, "\tdec\tqword [%s]\n", sym->name);
    fprintf(Outfile, "\tmov\t%s, [%s]\n", reglist[r], sym->name);
    if (op == A_POSTINC)
      fprintf(Outfile, "\tinc\tqword [%s]\n", sym->name);
    if (op == A_POSTDEC)
      fprintf(Outfile, "\tdec\tqword [%s]\n", sym->name);
  } else
  // 输出初始化代码
  switch (sym->type) {
    case P_CHAR:
      if (op == A_PREINC)
        fprintf(Outfile, "\tinc\tbyte [%s]\n", sym->name);
      if (op == A_PREDEC)
        fprintf(Outfile, "\tdec\tbyte [%s]\n", sym->name);
      fprintf(Outfile, "\tmovzx\t%s, byte [%s]\n", reglist[r], 
              sym->name);
      if (op == A_POSTINC)
        fprintf(Outfile, "\tinc\tbyte [%s]\n", sym->name);
      if (op == A_POSTDEC)
        fprintf(Outfile, "\tdec\tbyte [%s]\n", sym->name);
      break;
    case P_INT:
      if (op == A_PREINC)
        fprintf(Outfile, "\tinc\tdword [%s]\n", sym->name);
      if (op == A_PREDEC)
        fprintf(Outfile, "\tdec\tdword [%s]\n", sym->name);
      fprintf(Outfile, "\tmovsx\t%s, word [%s]\n", dreglist[r], 
              sym->name);
      fprintf(Outfile, "\tmovsxd\t%s, %s\n", reglist[r], dreglist[r]);
      if (op == A_POSTINC)
        fprintf(Outfile, "\tinc\tdword [%s]\n", sym->name);
      if (op == A_POSTDEC)
        fprintf(Outfile, "\tdec\tdword [%s]\n", sym->name);
      break;
    default:
      fatald("Bad type in cgloadglob:", sym->type);
  }
  return (r);
}

// 将局部变量值加载到寄存器中。
// 返回寄存器的编号。如果
// 操作是前或后增/减，
// 也要执行此操作。
int cgloadlocal(struct symtable *sym, int op) {
  // 获取一个新寄存器
  int r = alloc_register();

  // 输出初始化代码
  if (cgprimsize(sym->type) == 8) {
    if (op == A_PREINC)
      fprintf(Outfile, "\tinc\tqword\t[rbp+%d]\n", sym->st_posn);
    if (op == A_PREDEC)
      fprintf(Outfile, "\tdec\tqword\t[rbp+%d]\n", sym->st_posn);
    fprintf(Outfile, "\tmov\t%s, [rbp+%d]\n", reglist[r],
            sym->st_posn);
    if (op == A_POSTINC)
      fprintf(Outfile, "\tinc\tqword\t[rbp+%d]\n", sym->st_posn);
    if (op == A_POSTDEC)
      fprintf(Outfile, "\tdec\tqword\t[rbp+%d]\n", sym->st_posn);
  } else
  switch (sym->type) {
    case P_CHAR:
      if (op == A_PREINC)
        fprintf(Outfile, "\tinc\tbyte\t[rbp+%d]\n", sym->st_posn);
      if (op == A_PREDEC)
        fprintf(Outfile, "\tdec\tbyte\t[rbp+%d]\n", sym->st_posn);
      fprintf(Outfile, "\tmovzx\t%s, byte [rbp+%d]\n", reglist[r], 
              sym->st_posn);
      if (op == A_POSTINC)
        fprintf(Outfile, "\tinc\tbyte\t[rbp+%d]\n", sym->st_posn);
      if (op == A_POSTDEC)
        fprintf(Outfile, "\tdec\tbyte\t[rbp+%d]\n", sym->st_posn);
      break;
    case P_INT:
      if (op == A_PREINC)
        fprintf(Outfile, "\tinc\tdword\t[rbp+%d]\n", sym->st_posn);
      if (op == A_PREDEC)
        fprintf(Outfile, "\tdec\tdword\t[rbp+%d]\n", sym->st_posn);
      fprintf(Outfile, "\tmovsx\t%s, dword [rbp+%d]\n", reglist[r], 
              sym->st_posn);
      fprintf(Outfile, "\tmovsxd\t%s, %s\n", reglist[r], dreglist[r]);
      if (op == A_POSTINC)
      if (op == A_POSTINC)
        fprintf(Outfile, "\tinc\tdword\t[rbp+%d]\n", sym->st_posn);
      if (op == A_POSTDEC)
        fprintf(Outfile, "\tdec\tdword\t[rbp+%d]\n", sym->st_posn);
      break;
    default:
      fatald("Bad type in cgloadlocal:", sym->type);
  }
  return (r);
}

// 给定全局字符串的标签号，
// 将其地址加载到新寄存器中
int cgloadglobstr(int label) {
  // 获取一个新寄存器
  int r = alloc_register();
  fprintf(Outfile, "\tmov\t%s, L%d\n", reglist[r], label);
  return (r);
}

// 将两个寄存器相加并返回
// 带有结果的寄存器编号
int cgadd(int r1, int r2) {
  fprintf(Outfile, "\tadd\t%s, %s\n", reglist[r2], reglist[r1]);
  free_register(r1);
  return (r2);
}

// 从第一个寄存器减去第二个寄存器并
// 返回带有结果的寄存器编号
int cgsub(int r1, int r2) {
  fprintf(Outfile, "\tsub\t%s, %s\n", reglist[r1], reglist[r2]);
  free_register(r2);
  return (r1);
}

// 将两个寄存器相乘并返回
// 带有结果的寄存器编号
int cgmul(int r1, int r2) {
  fprintf(Outfile, "\timul\t%s, %s\n", reglist[r2], reglist[r1]);
  free_register(r1);
  return (r2);
}

// 将第一个寄存器除以第二个寄存器并
// 返回带有结果的寄存器编号
int cgdiv(int r1, int r2) {
  fprintf(Outfile, "\tmov\trax, %s\n", reglist[r1]);
  fprintf(Outfile, "\tcqo\n");
  fprintf(Outfile, "\tidiv\t%s\n", reglist[r2]);
  fprintf(Outfile, "\tmov\t%s, rax\n", reglist[r1]);
  free_register(r2);
  return (r1);
}

int cgand(int r1, int r2) {
  fprintf(Outfile, "\tand\t%s, %s\n", reglist[r2], reglist[r1]);
  free_register(r1);
  return (r2);
}

int cgor(int r1, int r2) {
  fprintf(Outfile, "\tor\t%s, %s\n", reglist[r2], reglist[r1]);
  free_register(r1);
  return (r2);
}

int cgxor(int r1, int r2) {
  fprintf(Outfile, "\txor\t%s, %s\n", reglist[r2], reglist[r1]);
  free_register(r1);
  return (r2);
}

int cgshl(int r1, int r2) {
  fprintf(Outfile, "\tmov\tcl, %s\n", breglist[r2]);
  fprintf(Outfile, "\tshl\t%s, cl\n", reglist[r1]);
  free_register(r2);
  return (r1);
}

int cgshr(int r1, int r2) {
  fprintf(Outfile, "\tmov\tcl, %s\n", breglist[r2]);
  fprintf(Outfile, "\tshr\t%s, cl\n", reglist[r1]);
  free_register(r2);
  return (r1);
}

// 求反寄存器的值
int cgnegate(int r) {
  fprintf(Outfile, "\tneg\t%s\n", reglist[r]);
  return (r);
}

// 反转寄存器的值
int cginvert(int r) {
  fprintf(Outfile, "\tnot\t%s\n", reglist[r]);
  return (r);
}

// 逻辑上求反寄存器的值
int cglognot(int r) {
  fprintf(Outfile, "\ttest\t%s, %s\n", reglist[r], reglist[r]);
  fprintf(Outfile, "\tsete\t%s\n", breglist[r]);
  fprintf(Outfile, "\tmovzx\t%s, %s\n", reglist[r], breglist[r]);
  return (r);
}

// 逻辑OR两个寄存器并返回
// 带有结果（1或0）的寄存器
int cglogor(int r1, int r2) {
  // 生成两个标签
  int Ltrue = genlabel();
  int Lend = genlabel();

  // 测试r1，如果为真则跳转到真标签
  fprintf(Outfile, "\ttest\t%s, %s\n", reglist[r1], reglist[r1]);
  fprintf(Outfile, "\tjne\tL%d\n", Ltrue);

  // 测试r2，如果为真则跳转到真标签
  fprintf(Outfile, "\ttest\t%s, %s\n", reglist[r2], reglist[r2]);
  fprintf(Outfile, "\tjne\tL%d\n", Ltrue);

  // 没有跳转，所以结果为假
  fprintf(Outfile, "\tmov\t%s, 0\n", reglist[r1]);
  fprintf(Outfile, "\tjmp\tL%d\n", Lend);

  // 有人跳转到真标签，所以结果为真
  cglabel(Ltrue);
  fprintf(Outfile, "\tmov\t%s, 1\n", reglist[r1]);
  cglabel(Lend);
  free_register(r2);
  return(r1);
}

// 逻辑AND两个寄存器并返回
// 带有结果（1或0）的寄存器
int cglogand(int r1, int r2) {
  // 生成两个标签
  int Lfalse = genlabel();
  int Lend = genlabel();

  // 测试r1，如果不为真则跳转到假标签
  fprintf(Outfile, "\ttest\t%s, %s\n", reglist[r1], reglist[r1]);
  fprintf(Outfile, "\tje\tL%d\n", Lfalse);

  // 测试r2，如果不为真则跳转到假标签
  fprintf(Outfile, "\ttest\t%s, %s\n", reglist[r2], reglist[r2]);
  fprintf(Outfile, "\tje\tL%d\n", Lfalse);

  // 没有跳转，所以结果为真
  fprintf(Outfile, "\tmov\t%s, 1\n", reglist[r1]);
  fprintf(Outfile, "\tjmp\tL%d\n", Lend);

  // 有人跳转到假标签，所以结果为假
  cglabel(Lfalse);
  fprintf(Outfile, "\tmov\t%s, 0\n", reglist[r1]);
  cglabel(Lend);
  free_register(r2);
  return(r1);
}

// 将整数值转换为布尔值。如果是
// IF或WHILE操作则跳转
int cgboolean(int r, int op, int label) {
  fprintf(Outfile, "\ttest\t%s, %s\n", reglist[r], reglist[r]);
  if (op == A_IF || op == A_WHILE)
    fprintf(Outfile, "\tje\tL%d\n", label);
  else {
    fprintf(Outfile, "\tsetnz\t%s\n", breglist[r]);
    fprintf(Outfile, "\tmovzx\t%s, byte %s\n", reglist[r], breglist[r]);
  }
  return (r);
}

// 使用给定的符号id调用函数。
// 弹出在栈上推送的任何参数。
// 返回带有结果的寄存器
int cgcall(struct symtable *sym, int numargs) {
  // 获取一个新寄存器
  int outr = alloc_register();
  // 调用函数
  fprintf(Outfile, "\tcall\t%s\n", sym->name);
  // 删除在栈上推送的任何参数
  if (numargs > 6) 
    fprintf(Outfile, "\tadd\trsp, %d\n", 8 * (numargs - 6));
  // 将返回值复制到我们的寄存器
  fprintf(Outfile, "\tmov\t%s, rax\n", reglist[outr]);
  return (outr);
}

// 给定带有参数值的寄存器，
// 将此参数复制到第argposn'th个参数中，
// 以为将来的函数调用做准备。
// 注意argposn是1、2、3、4、...，永远不为零。
void cgcopyarg(int r, int argposn) {

  // 如果这在第六个参数之上，只需将
  // 寄存器压入栈。我们依赖于按照x86-64的正确顺序
  // 连续调用参数
  if (argposn > 6) {
    fprintf(Outfile, "\tpush\t%s\n", reglist[r]);
  } else {
    // 否则，将值复制到用于保存参数值的
    // 六个寄存器之一
    fprintf(Outfile, "\tmov\t%s, %s\n", 
	    reglist[FIRSTPARAMREG - argposn + 1], reglist[r]);
  }
}

// 将寄存器左移一个常量
int cgshlconst(int r, int val) {
  fprintf(Outfile, "\tsal\t%s, %d\n", reglist[r], val);
  return (r);
}

// 将寄存器的值存储到变量中
int cgstorglob(int r, struct symtable *sym) {
  if (cgprimsize(sym->type) == 8) {
    fprintf(Outfile, "\tmov\t[%s], %s\n", sym->name, reglist[r]);
  } else
  switch (sym->type) {
    case P_CHAR:
      fprintf(Outfile, "\tmov\t[%s], %s\n", sym->name, breglist[r]);
      break;
    case P_INT:
      fprintf(Outfile, "\tmov\t[%s], %s\n", sym->name, dreglist[r]);
      break;
    default:
      fatald("Bad type in cgloadglob:", sym->type);
  }
  return (r);
}

// 将寄存器的值存储到局部变量中
int cgstorlocal(int r, struct symtable *sym) {
  if (cgprimsize(sym->type) == 8) {
    fprintf(Outfile, "\tmov\tqword\t[rbp+%d], %s\n", sym->st_posn,
            reglist[r]);
  } else
  switch (sym->type) {
    case P_CHAR:
      fprintf(Outfile, "\tmov\tbyte\t[rbp+%d], %s\n", sym->st_posn,
              breglist[r]);
      break;
    case P_INT:
      fprintf(Outfile, "\tmov\tdword\t[rbp+%d], %s\n", sym->st_posn,
              dreglist[r]);
      break;
    default:
      fatald("Bad type in cgstorlocal:", sym->type);
  }
  return (r);
}

// 生成全局符号但不包括函数
void cgglobsym(struct symtable *node) {
  int size, type;
  int initvalue;
  int i;

  if (node == NULL)
    return;
  if (node->stype == S_FUNCTION)
    return;

  // 获取变量的大小（或如果是数组则是其元素）
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
    fprintf(Outfile, "\tglobal\t%s\n", node->name);
  fprintf(Outfile, "%s:\n", node->name);

  // 为一个或多个元素输出空间
  for (i = 0; i < node->nelems; i++) {

    // 获取任何初始值
    initvalue = 0;
    if (node->initlist != NULL)
      initvalue = node->initlist[i];

    // 为此类型生成空间
    // 原始版本
    switch(size) {
      case 1:
        fprintf(Outfile, "\tdb\t%d\n", initvalue);
        break;
      case 4:
        fprintf(Outfile, "\tdd\t%d\n", initvalue);
        break;
      case 8:
        // 生成指向字符串字面量的指针。 将零值
        // 视为实际零，而不是标签L0
        if (node->initlist != NULL && type == pointer_to(P_CHAR) && initvalue != 0)
          fprintf(Outfile, "\tdq\tL%d\n", initvalue);
        else
          fprintf(Outfile, "\tdq\t%d\n", initvalue);
        break;
      default:
        for (i = 0; i < size; i++) 
          fprintf(Outfile, "\tdb\t0\n");
        /* 紧凑版本使用times而不是loop
        fprintf(Outfile, "\ttimes\t%d\tdb\t0\n", size);
        */
    }
  }

}

// 生成全局字符串及其起始标签。
// 如果append为true，不输出标签。
void cgglobstr(int l, char *strvalue, int append) {
  char *cptr;
  if (!append)
    cglabel(l);
  for (cptr = strvalue; *cptr; cptr++) {
    fprintf(Outfile, "\tdb\t%d\n", *cptr);
  }
}

void cgglobstrend(void) {
  fprintf(Outfile, "\tdb\t0\n");
}

// 比较指令列表，
// 按AST顺序：A_EQ, A_NE, A_LT, A_GT, A_LE, A_GE
static char *cmplist[] =
  { "sete", "setne", "setl", "setg", "setle", "setge" };

// 比较两个寄存器并在为真时设置。
int cgcompare_and_set(int ASTop, int r1, int r2) {

  // 检查AST操作的范围
  if (ASTop < A_EQ || ASTop > A_GE)
    fatal("Bad ASTop in cgcompare_and_set()");

  fprintf(Outfile, "\tcmp\t%s, %s\n", reglist[r1], reglist[r2]);
  fprintf(Outfile, "\t%s\t%s\n", cmplist[ASTop - A_EQ], breglist[r2]);
  fprintf(Outfile, "\tmovzx\t%s, %s\n", reglist[r2], breglist[r2]);
  free_register(r1);
  return (r2);
}

// 生成一个标签
void cglabel(int l) {
  fprintf(Outfile, "L%d:\n", l);
}

// 生成跳转到标签
void cgjump(int l) {
  fprintf(Outfile, "\tjmp\tL%d\n", l);
}

// 反转跳转指令列表，
// 按AST顺序：A_EQ, A_NE, A_LT, A_GT, A_LE, A_GE
static char *invcmplist[] = { "jne", "je", "jge", "jle", "jg", "jl" };

// 比较两个寄存器并在为假时跳转。
int cgcompare_and_jump(int ASTop, int r1, int r2, int label) {

  // 检查AST操作的范围
  if (ASTop < A_EQ || ASTop > A_GE)
    fatal("Bad ASTop in cgcompare_and_set()");

  fprintf(Outfile, "\tcmp\t%s, %s\n", reglist[r1], reglist[r2]);
  fprintf(Outfile, "\t%s\tL%d\n", invcmplist[ASTop - A_EQ], label);
  freeall_registers(NOREG);
  return (NOREG);
}

// 将寄存器中的值从旧类型加宽到新类型，
// 并返回带有此新值的寄存器
int cgwiden(int r, int oldtype, int newtype) {
  // 没什么可做的
  return (r);
}

// 生成从函数返回值代码
void cgreturn(int reg, struct symtable *sym) {

  // 只有当我们有返回值时才返回
  if (reg != NOREG) {
    // 在这里处理指针，因为我们不能将它们放入switch语句
    if (ptrtype(sym->type))
      fprintf(Outfile, "\tmov\trax, %s\n", reglist[reg]);
    else {
      // 根据函数类型生成代码
      switch (sym->type) {
        case P_CHAR:
          fprintf(Outfile, "\tmovzx\teax, %s\n", breglist[reg]);
          break;
        case P_INT:
          fprintf(Outfile, "\tmov\teax, %s\n", dreglist[reg]);
          break;
        case P_LONG:
          fprintf(Outfile, "\tmov\trax, %s\n", reglist[reg]);
          break;
        default:
          fatald("Bad function type in cgreturn:", sym->type);
      }
    }
  }
  cgjump(sym->st_endlabel);
}

// 生成将标识符的地址加载到变量中的代码。返回一个 新寄存器
int cgaddress(struct symtable *sym) {
  int r = alloc_register();

  if (sym->class == C_GLOBAL || sym->class == C_STATIC)
    fprintf(Outfile, "\tmov\t%s, %s\n", reglist[r], sym->name);
  else
    fprintf(Outfile, "\tlea\t%s, [rbp+%d]\n", reglist[r],
            sym->st_posn);
  return (r);
}

// 解引用指针以获取其指向的值
// 到同一寄存器中
int cgderef(int r, int type) {
  // 获取我们指向的类型
  int newtype = value_at(type);
  // 现在获取此类型的大小
  int size = cgprimsize(newtype);

  switch (size) {
    case 1:
      fprintf(Outfile, "\tmovzx\t%s, byte [%s]\n", reglist[r], reglist[r]);
      break;
    case 2:
      fprintf(Outfile, "\tmovsx\t%s, dword [%s]\n", reglist[r], reglist[r]);
      break;
    case 4:
    case 8:
      fprintf(Outfile, "\tmov\t%s, [%s]\n", reglist[r], reglist[r]);
      break;
    default:
      fatald("Can't cgderef on type:", type);
  }
  return (r);
}

// 通过解引用指针存储
int cgstorderef(int r1, int r2, int type) {
  //获取类型的大小
  int size = cgprimsize(type);

  switch (size) {
    case 1:
      fprintf(Outfile, "\tmov\t[%s], byte %s\n", reglist[r2], breglist[r1]);
      break;
    case 2:
    case 4:
    case 8:
      fprintf(Outfile, "\tmov\t[%s], %s\n", reglist[r2], reglist[r1]);
      break;
    default:
      fatald("Can't cgstoderef on type:", type);
  }
  return (r1);
}

// 生成switch跳转表和加载寄存器
// 并调用switch()代码
void cgswitch(int reg, int casecount, int toplabel,
	      int *caselabel, int *caseval, int defaultlabel) {
  int i, label;

  // 获取switch表的标签
  label = genlabel();
  cglabel(label);

  // 启发式。如果没有case，创建一个
  // 指向default case的case
  if (casecount == 0) {
    caseval[0] = 0;
    caselabel[0] = defaultlabel;
    casecount = 1;
  }
  // 生成switch跳转表。
  fprintf(Outfile, "\tdq\t%d\n", casecount);
  for (i = 0; i < casecount; i++)
    fprintf(Outfile, "\tdq\t%d, L%d\n", caseval[i], caselabel[i]);
  fprintf(Outfile, "\tdq\tL%d\n", defaultlabel);

  // 加载特定寄存器
  cglabel(toplabel);
  fprintf(Outfile, "\tmov\trax, %s\n", reglist[reg]);
  fprintf(Outfile, "\tmov\trdx, L%d\n", label);
  fprintf(Outfile, "\tjmp\tswitch\n");
}

// 在寄存器之间移动值
void cgmove(int r1, int r2) {
  fprintf(Outfile, "\tmov\t%s, %s\n", reglist[r2], reglist[r1]);
}