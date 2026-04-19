#include "defs.h"
#include "data.h"
#include "gen.h"
#include "misc.h"
#include "types.h"
#include "target.h"
#include "cg.h"

// Part 64: 6809 Target
// Copyright (c) 2019 Warren Toomey, GPL3

// 需要保留字面字符串列表，因为不能在代码中间生成它们
struct litlist {
  char *val;
  int label;
  struct litlist *next;
};

struct litlist *Strlithead;
struct litlist *Strlittail;

// 切换到文本段
void cgtextseg() {
}

// 切换到数据段
void cgdataseg() {
}

// 切换到字面量段
void cglitseg() {
}

// 释放寄存器/临时变量
void cgfreeallregs(int keepreg) {
}

void cgfreereg(int reg) {
}

// 给定标量类型值，返回匹配的 QBE 类型字符
// 因为字符存储在栈上，对 P_CHAR 返回 'w'
static int cgprimtype(int type) {
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
      fatald("Bad type in cgprimtype:", type);
  }
  return (0); // 保持 -Wall 开心
}

// 分配一个 QBE 临时变量
static int nexttemp = 0;
int cgalloctemp(void) {
  return (++nexttemp);
}

int cgallocreg(int type) {
  return(cgalloctemp());
}

// 打印输出文件的汇编前导码
void cgpreamble() {
  Strlithead= NULL;
  Strlittail= NULL;
}

// 打印所有全局字符串字面量
static void cgmakeglobstrs();
void cgpostamble() {
  cgmakeglobstrs();
}

// 布尔标志：此函数中是否已有 switch 语句
static int used_switch;

// 打印函数前导码
void cgfuncpreamble(struct symtable *sym) {
  char *name = sym->name;
  struct symtable *parm, *locvar;
  int size, bigsize;
  int label;

  // 输出函数名和返回类型
  if (sym->class == V_GLOBAL)
    fprintf(Outfile, "export ");
  fprintf(Outfile, "function %c $%s(", cgprimtype(sym->type), name);

  // 输出参数名和类型。对于需要地址的参数，
  // 在下面复制其值时更改其名称
  for (parm = sym->member; parm != NULL; parm = parm->next) {
    if (parm->class==V_LOCAL) break;

    // 丑陋的。所有参数都需要有地址
    parm->st_hasaddr = 1;
    if (parm->st_hasaddr == 1)
      fprintf(Outfile, "%c %%.p%s, ", cgprimtype(parm->type), parm->name);
    else
      fprintf(Outfile, "%c %%%s, ", cgprimtype(parm->type), parm->name);
  }
  fprintf(Outfile, ") {\n");

  // 获取函数开始的标签
  label = genlabel();
  cglabel(label);

  // 对于需要地址的参数，在栈上分配内存
  // QBE 不允许使用 alloc1，所以对字符分配 4 字节
  // 将参数的值复制到新内存位置
  for (parm = sym->member; parm != NULL; parm = parm->next) {
    if (parm->class==V_LOCAL) break;

    // 丑陋的。所有参数都需要有地址
    parm->st_hasaddr = 1;
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

  // 为需要放在栈上的局部变量分配内存。有两个原因：
  // 第一个是使用其地址的局部变量。第二个是字符变量
  // 需要这样做是因为 QBE 只能将内存中的位置截断到 8 位
  // 注意：局部变量在成员列表中位于参数之后
  for (locvar = parm; locvar != NULL; locvar = locvar->next) {
    if (locvar->st_hasaddr == 1) {
      // 获取所有元素的总大小（如果是数组）
      // 向上舍入到 8 的倍数，以确保指针对齐到 8 字节边界
      size = locvar->size * locvar->nelems;
      size = (size + 7) >> 3;
      fprintf(Outfile, "  %%%s =l alloc8 %d\n", locvar->name, size);
    } else if (locvar->type == P_CHAR) {
      locvar->st_hasaddr = 1;
      fprintf(Outfile, "  %%%s =l alloc4 1\n", locvar->name);
    }
  }

  used_switch = 0; // 尚未输出 switch 处理代码
}

// 打印函数后导码
void cgfuncpostamble(struct symtable *sym) {
  cglabel(sym->st_endlabel);

  // 如果函数类型不是 void，则返回值
  if (sym->type != P_VOID)
    fprintf(Outfile, "  ret %%.ret\n}\n");
  else
    fprintf(Outfile, "  ret\n}\n");
}

// 将整数字面量值加载到临时变量中
// 返回临时变量的编号
int cgloadint(int value, int type) {
  // 获取新的临时变量
  int t = cgalloctemp();

  fprintf(Outfile, "  %%.t%d =%c copy %d\n", t, cgprimtype(type), value);
  return (t);
}

// 从变量加载值到临时变量
// 返回临时变量的编号。如果是预或后递增/递减操作，
// 也执行此操作
int cgloadvar(struct symtable *sym, int op) {
  int r, posttemp, offset = 1;
  char qbeprefix;

  // 获取新的临时变量
  r = cgalloctemp();

  // 如果符号是指针，使用其所指向类型的大小作为递增或递减的偏移量
  // 否则为 1
  if (ptrtype(sym->type))
    offset = typesize(value_at(sym->type), sym->ctype);

  // 对递减取负偏移量
  if (op == A_PREDEC || op == A_POSTDEC)
    offset = -offset;

  // 获取符号对应的 QBE 前缀
  qbeprefix = ((sym->class == V_GLOBAL) || (sym->class == V_STATIC) ||
         (sym->class == V_EXTERN)) ? (char)'$' : (char)'%';

  // 如果有预操作
  if (op == A_PREINC || op == A_PREDEC) {
    if (sym->st_hasaddr || qbeprefix == '$') {
      // 获取新的临时变量
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
        qbeprefix, sym->name, cgprimtype(sym->type), qbeprefix,
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
        r, cgprimtype(sym->type), qbeprefix, sym->name);

  // 如果有后操作
  if (op == A_POSTINC || op == A_POSTDEC) {
    if (sym->st_hasaddr || qbeprefix == '$') {
      // 获取新的临时变量
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
        qbeprefix, sym->name, cgprimtype(sym->type), qbeprefix,
        sym->name, offset);
  }
  // 返回包含值的临时变量
  return (r);
}

// 给定全局字符串的标签号，
// 将其地址加载到新的临时变量中
int cgloadglobstr(int label) {
  // 获取新的临时变量
  int r = cgalloctemp();
  fprintf(Outfile, "  %%.t%d =l copy $L%d\n", r, label);
  return (r);
}

// 将两个临时变量相加并返回包含结果的临时变量编号
int cgadd(int r1, int r2, int type) {
  fprintf(Outfile, "  %%.t%d =%c add %%.t%d, %%.t%d\n",
      r1, cgprimtype(type), r1, r2);
  return (r1);
}

// 从第一个临时变量减去第二个并返回包含结果的临时变量编号
int cgsub(int r1, int r2, int type) {
  fprintf(Outfile, "  %%.t%d =%c sub %%.t%d, %%.t%d\n",
      r1, cgprimtype(type), r1, r2);
  return (r1);
}

// 将两个临时变量相乘并返回包含结果的临时变量编号
int cgmul(int r1, int r2, int type) {
  fprintf(Outfile, "  %%.t%d =%c mul %%.t%d, %%.t%d\n",
      r1, cgprimtype(type), r1, r2);
  return (r1);
}

// 用第一个临时变量除以第二个并返回包含结果的临时变量编号
int cgdiv(int r1, int r2, int type) {
  fprintf(Outfile, "  %%.t%d =%c div %%.t%d, %%.t%d\n",
      r1, cgprimtype(type), r1, r2);
  return (r1);
}

// 用第一个临时变量对第二个取模并返回包含结果的临时变量编号
int cgmod(int r1, int r2, int type) {
  fprintf(Outfile, "  %%.t%d =%c rem %%.t%d, %%.t%d\n",
      r1, cgprimtype(type), r1, r2);
  return (r1);
}

// 两个临时变量按位与
int cgand(int r1, int r2, int type) {
  fprintf(Outfile, "  %%.t%d =%c and %%.t%d, %%.t%d\n",
      r1, cgprimtype(type), r1, r2);
  return (r1);
}

// 两个临时变量按位或
int cgor(int r1, int r2, int type) {
  fprintf(Outfile, "  %%.t%d =%c or %%.t%d, %%.t%d\n",
      r1, cgprimtype(type), r1, r2);
  return (r1);
}

// 两个临时变量按位异或
int cgxor(int r1, int r2, int type) {
  fprintf(Outfile, "  %%.t%d =%c xor %%.t%d, %%.t%d\n",
      r1, cgprimtype(type), r1, r2);
  return (r1);
}

// 将 r1 左移 r2 位
int cgshl(int r1, int r2, int type) {
  fprintf(Outfile, "  %%.t%d =%c shl %%.t%d, %%.t%d\n",
      r1, cgprimtype(type), r1, r2);
  return (r1);
}

// 将 r1 右移 r2 位
int cgshr(int r1, int r2, int type) {
  fprintf(Outfile, "  %%.t%d =%c shr %%.t%d, %%.t%d\n",
      r1, cgprimtype(type), r1, r2);
  return (r1);
}

// 取反临时变量的值
int cgnegate(int r, int type) {
  fprintf(Outfile, "  %%.t%d =%c sub 0, %%.t%d\n", r, cgprimtype(type), r);
  return (r);
}

// 反转临时变量的值
int cginvert(int r, int type) {
  fprintf(Outfile, "  %%.t%d =%c xor %%.t%d, -1\n", r, cgprimtype(type), r);
  return (r);
}

// 逻辑取反临时变量的值
int cglognot(int r, int type) {
  int q = cgprimtype(type);
  fprintf(Outfile, "  %%.t%d =%c ceq%c %%.t%d, 0\n", r, q, q, r);
  return (r);
}

// 将布尔值（只有 0 或 1）加载到给定临时变量中
// 如果 r 是 NOREG，则分配临时变量
int cgloadboolean(int r, int val, int type) {
  if (r==NOREG) r= cgalloctemp();
  fprintf(Outfile, "  %%.t%d =%c copy %d\n", r, cgprimtype(type), val);
  return(r);
}

// 将整数值转换为 TOBOOL 操作的布尔值
// 如果是 IF、WHILE 操作则为真时跳转
// 如果是 LOGOR 操作则为假时跳转
int cgboolean(int r, int op, int label, int type) {
  // 获取下一条指令的标签
  int label2 = genlabel();

  // 获取用于比较的新临时变量
  int r2 = cgalloctemp();

  // 将临时变量转换为布尔值
  fprintf(Outfile, "  %%.t%d =l cne%c %%.t%d, 0\n", r2, cgprimtype(type), r);

  switch (op) {
    case A_IF:
    case A_WHILE:
    case A_TERNARY:
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

// 使用给定的符号 id 调用函数
// 返回包含结果的临时变量
int cgcall(struct symtable *sym, int numargs, int *arglist, int *typelist) {
  int outr;
  int i;

  // 获取用于返回结果的新临时变量
  outr = cgalloctemp();

  // 调用函数
  if (sym->type == P_VOID)
    fprintf(Outfile, "  call $%s(", sym->name);
  else
    fprintf(Outfile, "  %%.t%d =%c call $%s(", outr, cgprimtype(sym->type),
        sym->name);

  // 输出参数列表（逆序）
  for (i = numargs - 1; i >= 0; i--) {
    fprintf(Outfile, "%c %%.t%d, ", cgprimtype(typelist[i]), arglist[i]);
  }
  fprintf(Outfile, ")\n");

  return (outr);
}

// 将临时变量左移常量位。由于只用于地址计算，
// 需要时将类型扩展为 QBE 的 'l'
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

// 将临时变量的值存储到全局变量
int cgstorglob(int r, struct symtable *sym) {

  // 可以存储字节到内存
  int q = cgprimtype(sym->type);
  if (sym->type == P_CHAR)
    q = 'b';

  fprintf(Outfile, "  store%c %%.t%d, $%s\n", q, r, sym->name);
  return (r);
}

// 将临时变量的值存储到局部变量
int cgstorlocal(int r, struct symtable *sym) {

  // 如果变量在栈上，使用存储指令
  if (sym->st_hasaddr) {
    fprintf(Outfile, "  store%c %%.t%d, %%%s\n",
        cgprimtype(sym->type), r, sym->name);
  } else {
    fprintf(Outfile, "  %%%s =%c copy %%.t%d\n",
        sym->name, cgprimtype(sym->type), r);
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

  // 获取变量的大小（如果是数组则为其元素的大小）
  // 以及变量的类型
  if (node->stype == S_ARRAY) {
    size = typesize(value_at(node->type), node->ctype);
    type = value_at(node->type);
  } else {
    size = node->size;
    type = node->type;
  }

  // 生成全局标识符和标签
  cgdataseg();
  if (node->class == V_GLOBAL)
    fprintf(Outfile, "export ");
  if (node->ctype==NULL)
    fprintf(Outfile, "data $%s = align %d { ", node->name, cgprimsize(type));
  else
    fprintf(Outfile, "data $%s = align 8 { ", node->name);

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
        // 生成指向字符串字面量的指针。将零值视为实际的零，而不是标签 L0
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

// 暂存全局字符串以便稍后输出
void cgglobstr(int l, char *strvalue) {
  struct litlist *this;

  this= (struct litlist *)malloc(sizeof(struct litlist));
  this->val= strdup(strvalue);
  this->label= l;
  this->next= NULL;
  if (Strlithead==NULL) {
    Strlithead= Strlittail= this;
  } else {
    Strlittail->next= this; Strlittail= this;
  }
}

// 生成所有全局字符串及其标签
static void cgmakeglobstrs() {
  struct litlist *this;
  char *cptr;

  for (this= Strlithead; this!=NULL; this=this->next) {

    fprintf(Outfile, "data $L%d = { ", this->label);
    for (cptr = this->val; *cptr; cptr++) {
      fprintf(Outfile, "b %d, ", *cptr);
    }
    fprintf(Outfile, " b 0 }\n");
  }
}

// NUL 终止全局字符串
void cgglobstrend(void) {
}

// 比较指令列表，按 AST 顺序：A_EQ, A_NE, A_LT, A_GT, A_LE, A_GE
static char *cmplist[] = { "ceq", "cne", "cslt", "csgt", "csle", "csge" };

// 比较两个临时变量并在为真时设置
int cgcompare_and_set(int ASTop, int r1, int r2, int type) {
  int r3;
  int q = cgprimtype(type);

  // 检查 AST 操作的范围
  if (ASTop < A_EQ || ASTop > A_GE)
    fatal("Bad ASTop in cgcompare_and_set()");

  // 获取用于比较的新临时变量
  r3 = cgalloctemp();

  fprintf(Outfile, "  %%.t%d =%c %s%c %%.t%d, %%.t%d\n",
      r3, q, cmplist[ASTop - A_EQ], q, r1, r2);
  return (r3);
}

// 生成标签
void cglabel(int l) {
  fprintf(Outfile, "@L%d\n", l);
}

// 生成跳转到标签
void cgjump(int l) {
  int label;

  fprintf(Outfile, "  jmp @L%d\n", l);

  // 打印一个伪标签。这可以防止输出中有两个相邻的跳转，而 QBE 不喜欢这样
  label = genlabel();
  cglabel(label);
}

// 反转跳转指令列表，按 AST 顺序：A_EQ, A_NE, A_LT, A_GT, A_LE, A_GE
static char *invcmplist[] = { "cne", "ceq", "csge", "csle", "csgt", "cslt" };

// 比较两个临时变量并在为假时跳转
// 如果父操作是 A_LOGOR 则为真时跳转
int cgcompare_and_jump(int ASTop, int parentASTop,
                int r1, int r2, int label, int type) {
  int label2;
  int r3;
  int q = cgprimtype(type);
  char *cmpop;

  // 检查 AST 操作的范围
  if (ASTop < A_EQ || ASTop > A_GE)
    fatal("Bad ASTop in cgcompare_and_set()");

  cmpop= invcmplist[ASTop - A_EQ];
  if (parentASTop == A_LOGOR)
    cmpop= cmplist[ASTop - A_EQ];

  // 获取下一条指令的标签
  label2 = genlabel();

  // 获取用于比较的新临时变量
  r3 = cgalloctemp();

  fprintf(Outfile, "  %%.t%d =%c %s%c %%.t%d, %%.t%d\n",
      r3, q, cmpop, q, r1, r2);
  fprintf(Outfile, "  jnz %%.t%d, @L%d, @L%d\n", r3, label, label2);
  cglabel(label2);
  return (NOREG);
}

// 将临时变量中的值从旧类型扩展到新类型，
// 并返回包含此新值的临时变量
int cgwiden(int r, int oldtype, int newtype) {
  int oldq = cgprimtype(oldtype);
  int newq = cgprimtype(newtype);

  // 获取新的临时变量
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
void cgreturn(int r, struct symtable *sym) {
  // 只有在有返回值时才返回
  if (r != NOREG)
    fprintf(Outfile, "  %%.ret =%c copy %%.t%d\n", cgprimtype(sym->type), r);

  cgjump(sym->st_endlabel);
}

// 生成加载标识符地址的代码
// 返回新的临时变量
int cgaddress(struct symtable *sym) {
  int r = cgalloctemp();
  char qbeprefix = ((sym->class == V_GLOBAL) || (sym->class == V_STATIC) ||
           (sym->class == V_EXTERN)) ? (char)'$' : (char)'%';

  fprintf(Outfile, "  %%.t%d =l copy %c%s\n", r, qbeprefix, sym->name);
  return (r);
}

// 解引用指针，将其指向的值加载到新的临时变量中
int cgderef(int r, int type) {
  // 获取我们指向的类型
  int newtype = value_at(type);
  // 现在获取此类型的大小
  int size = cgprimsize(newtype);
  // 获取返回结果的临时变量
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

// 通过解引用指针存储
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

// 生成比较每个 switch 值并跳转到相应 case 标签的代码
void cgswitch(int reg, int casecount, int toplabel,
          int *caselabel, int *caseval, int defaultlabel) {
  int i, label;
  int rval, rcmp;

  // 为 case 值和比较获取两个临时变量
  rval= cgalloctemp();
  rcmp= cgalloctemp();

  // 在代码顶部输出标签
  cglabel(toplabel);

  for (i = 0; i < casecount; i++) {
    // 为跳过此 case 的代码获取标签
    label= genlabel();

    // 加载 case 值
    fprintf(Outfile, "  %%.t%d =w copy %d\n", rval, caseval[i]);

    // 将临时变量与 case 值比较
    fprintf(Outfile, "  %%.t%d =w ceqw %%.t%d, %%.t%d\n", rcmp, reg, rval);

    // 跳转到下一个比较或 case 代码
    fprintf(Outfile, "  jnz %%.t%d, @L%d, @L%d\n", rcmp, caselabel[i], label);
    cglabel(label);
  }

  // 没有 case 匹配，跳转到默认标签
  cgjump(defaultlabel);
}

// 在临时变量之间移动值
void cgmove(int r1, int r2, int type) {
  fprintf(Outfile, "  %%.t%d =%c copy %%.t%d\n", r2, cgprimtype(type), r1);
}

// 输出 gdb 指令，说明以下汇编代码来自哪个源代码行号
void cglinenum(int line) {
  // fprintf(Outfile, "\t.loc 1 %d 0\n", line);
}

// 将临时变量的类型从旧类型更改为新类型
int cgcast(int t, int oldtype, int newtype) {
  // 获取返回结果的临时变量
  int ret = cgalloctemp();
  int oldsize, newsize;
  int qnew;

  // 如果新类型是指针
  if (ptrtype(newtype)) {
    // 如果旧类型也是指针，则无需操作
    if (ptrtype(oldtype))
      return (t);
    // 否则，从原始类型扩展到指针
    return (cgwiden(t, oldtype, newtype));
  }

  // 新类型不是指针
  // 获取新的 QBE 类型
  // 以及类型大小（以字节为单位）
  qnew = cgprimtype(newtype);
  oldsize = cgprimsize(oldtype);
  newsize = cgprimsize(newtype);

  // 如果两者大小相同，则无需操作
  if (newsize == oldsize)
    return (t);

  // 如果新大小更小，可以复制，QBE 会截断它，
  // 否则使用 QBE 强制转换操作
  if (newsize < oldsize)
    fprintf(Outfile, " %%.t%d =%c copy %%.t%d\n", ret, qnew, t);
  else
    fprintf(Outfile, " %%.t%d =%c cast %%.t%d\n", ret, qnew, t);
  return (ret);
}