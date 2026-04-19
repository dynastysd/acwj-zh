#include "defs.h"
#include "data.h"
#include "decl.h"

// 符号表函数
// Copyright (c) 2019 Warren Toomey, GPL3

// 将节点附加到由head或tail指向的单向链表
void appendsym(struct symtable **head, struct symtable **tail,
	       struct symtable *node) {

  // 检查有效指针
  if (head == NULL || tail == NULL || node == NULL)
    fatal("Either head, tail or node is NULL in appendsym");

  // 追加到列表
  if (*tail) {
    (*tail)->next = node;
    *tail = node;
  } else
    *head = *tail = node;
  node->next = NULL;
}

// 创建要添加到符号表列表的符号节点。
// 设置节点的：
// + type: char, int等
// + ctype: struct/union的组合类型指针
// + 结构类型: var, function, array等
// + size: 元素数量，或endlabel: 函数结束标签
// + posn: 局部符号的位置信息
// 返回指向新节点的指针
struct symtable *newsym(char *name, int type, struct symtable *ctype,
			int stype, int class, int nelems, int posn) {

  // 获取新节点
  struct symtable *node = (struct symtable *) malloc(sizeof(struct symtable));
  if (node == NULL)
    fatal("Unable to malloc a symbol table node in newsym");

  // 填充值
  if (name == NULL)
    node->name = NULL;
  else
    node->name = strdup(name);
  node->type = type;
  node->ctype = ctype;
  node->stype = stype;
  node->class = class;
  node->nelems = nelems;

  // 对于指针和整数类型，设置
  // 符号的大小。struct和union声明
  // 手动设置它们。
  if (ptrtype(type) || inttype(type))
    node->size = nelems * typesize(type, ctype);

  node->st_posn = posn;
  node->next = NULL;
  node->member = NULL;
  node->initlist = NULL;
  return (node);
}

// 将符号添加到全局符号列表
struct symtable *addglob(char *name, int type, struct symtable *ctype,
			 int stype, int class, int nelems, int posn) {
  struct symtable *sym =
    newsym(name, type, ctype, stype, class, nelems, posn);
  // 对于struct和union，从类型节点复制大小
  if (type == P_STRUCT || type == P_UNION)
    sym->size = ctype->size;
  appendsym(&Globhead, &Globtail, sym);
  return (sym);
}

// 将符号添加到局部符号列表
struct symtable *addlocl(char *name, int type, struct symtable *ctype,
			 int stype, int nelems) {
  struct symtable *sym = newsym(name, type, ctype, stype, C_LOCAL, nelems, 0);
  // 对于struct和union，从类型节点复制大小
  if (type == P_STRUCT || type == P_UNION)
    sym->size = ctype->size;
  appendsym(&Loclhead, &Locltail, sym);
  return (sym);
}

// 将符号添加到参数列表
struct symtable *addparm(char *name, int type, struct symtable *ctype,
			 int stype) {
  struct symtable *sym = newsym(name, type, ctype, stype, C_PARAM, 1, 0);
  appendsym(&Parmhead, &Parmtail, sym);
  return (sym);
}

// 将符号添加到临时成员列表
struct symtable *addmemb(char *name, int type, struct symtable *ctype,
			 int stype, int nelems) {
  struct symtable *sym =
    newsym(name, type, ctype, stype, C_MEMBER, nelems, 0);
  // 对于struct和union，从类型节点复制大小
  if (type == P_STRUCT || type == P_UNION)
    sym->size = ctype->size;
  appendsym(&Membhead, &Membtail, sym);
  return (sym);
}

// 将struct添加到struct列表
struct symtable *addstruct(char *name) {
  struct symtable *sym = newsym(name, P_STRUCT, NULL, 0, C_STRUCT, 0, 0);
  appendsym(&Structhead, &Structtail, sym);
  return (sym);
}

// 将struct添加到union列表
struct symtable *addunion(char *name) {
  struct symtable *sym = newsym(name, P_UNION, NULL, 0, C_UNION, 0, 0);
  appendsym(&Unionhead, &Uniontail, sym);
  return (sym);
}

// 将枚举类型或值添加到枚举列表。
// 类别是C_ENUMTYPE或C_ENUMVAL。
// 使用posn存储整数值。
struct symtable *addenum(char *name, int class, int value) {
  struct symtable *sym = newsym(name, P_INT, NULL, 0, class, 0, value);
  appendsym(&Enumhead, &Enumtail, sym);
  return (sym);
}

// 将typedef添加到typedef列表
struct symtable *addtypedef(char *name, int type, struct symtable *ctype) {
  struct symtable *sym = newsym(name, type, ctype, 0, C_TYPEDEF, 0, 0);
  appendsym(&Typehead, &Typetail, sym);
  return (sym);
}

// 在特定列表中搜索符号。
// 返回找到的节点的指针，如果未找到则返回NULL。
// 如果class不为零，也匹配给定的class
static struct symtable *findsyminlist(char *s, struct symtable *list,
				      int class) {
  for (; list != NULL; list = list->next)
    if ((list->name != NULL) && !strcmp(s, list->name))
      if (class == 0 || class == list->class)
	return (list);
  return (NULL);
}

// 确定符号s是否在全局符号表中。
// 返回找到的节点的指针，如果未找到则返回NULL。
struct symtable *findglob(char *s) {
  return (findsyminlist(s, Globhead, 0));
}

// 确定符号s是否在局部符号表中。
// 返回找到的节点的指针，如果未找到则返回NULL。
struct symtable *findlocl(char *s) {
  struct symtable *node;

  // 如果在函数体中，查找参数
  if (Functionid) {
    node = findsyminlist(s, Functionid->member, 0);
    if (node)
      return (node);
  }
  return (findsyminlist(s, Loclhead, 0));
}

// 确定符号s是否在符号表中。
// 返回找到的节点的指针，如果未找到则返回NULL。
struct symtable *findsymbol(char *s) {
  struct symtable *node;

  // 如果在函数体中，查找参数
  if (Functionid) {
    node = findsyminlist(s, Functionid->member, 0);
    if (node)
      return (node);
  }
  // 否则，尝试局部和全局符号列表
  node = findsyminlist(s, Loclhead, 0);
  if (node)
    return (node);
  return (findsyminlist(s, Globhead, 0));
}

// 在成员列表中查找成员
// 返回找到的节点的指针，如果未找到则返回NULL。
struct symtable *findmember(char *s) {
  return (findsyminlist(s, Membhead, 0));
}

// 在struct列表中查找struct
// 返回找到的节点的指针，如果未找到则返回NULL。
struct symtable *findstruct(char *s) {
  return (findsyminlist(s, Structhead, 0));
}

// 在union列表中查找struct
// 返回找到的节点的指针，如果未找到则返回NULL。
struct symtable *findunion(char *s) {
  return (findsyminlist(s, Unionhead, 0));
}

// 在枚举列表中查找枚举类型
// 返回找到的节点的指针，如果未找到则返回NULL。
struct symtable *findenumtype(char *s) {
  return (findsyminlist(s, Enumhead, C_ENUMTYPE));
}

// 在枚举列表中查找枚举值
// 返回找到的节点的指针，如果未找到则返回NULL。
struct symtable *findenumval(char *s) {
  return (findsyminlist(s, Enumhead, C_ENUMVAL));
}

// 在typedef列表中查找类型
// 返回找到的节点的指针，如果未找到则返回NULL。
struct symtable *findtypedef(char *s) {
  return (findsyminlist(s, Typehead, 0));
}

// 重置符号表的内容
void clear_symtable(void) {
  Globhead = Globtail = NULL;
  Loclhead = Locltail = NULL;
  Parmhead = Parmtail = NULL;
  Membhead = Membtail = NULL;
  Structhead = Structtail = NULL;
  Unionhead = Uniontail = NULL;
  Enumhead = Enumtail = NULL;
  Typehead = Typetail = NULL;
}

// 清除局部符号表中的所有条目
void freeloclsyms(void) {
  Loclhead = Locltail = NULL;
  Parmhead = Parmtail = NULL;
  Functionid = NULL;
}

// 从全局符号表中删除所有静态符号
void freestaticsyms(void) {
  // g指向当前节点，prev指向前一个节点
  struct symtable *g, *prev = NULL;

  // 遍历全局表查找静态条目
  for (g = Globhead; g != NULL; g = g->next) {
    if (g->class == C_STATIC) {

      // 如果有前一个节点，重新排列prev指针
      // 以跳过当前节点。如果不是，g是头，
      // 所以对Globhead做同样的处理
      if (prev != NULL)
	prev->next = g->next;
      else
	Globhead->next = g->next;

      // 如果g是尾，则将Globtail指向前一个节点
      //（如果有的话），或Globhead
      if (g == Globtail) {
	if (prev != NULL)
	  Globtail = prev;
	else
	  Globtail = Globhead;
      }
    }

    // 在移动到下一个节点之前，将prev指向g
    prev = g;
  }

  // Point prev at g before we move up to the next node
  prev = g;
}

// 转储单个符号
static void dumpsym(struct symtable *sym, int indent) {
  int i;

  for (i = 0; i < indent; i++)
    printf(" ");
  switch (sym->type & (~0xf)) {
    case P_VOID:
      printf("void ");
      break;
    case P_CHAR:
      printf("char ");
      break;
    case P_INT:
      printf("int ");
      break;
    case P_LONG:
      printf("long ");
      break;
    case P_STRUCT:
      if (sym->ctype != NULL)
	printf("struct %s ", sym->ctype->name);
      else
	printf("struct %s ", sym->name);
      break;
    case P_UNION:
      if (sym->ctype != NULL)
	printf("union %s ", sym->ctype->name);
      else
	printf("union %s ", sym->name);
      break;
    default:
      printf("unknown type ");
  }

  for (i = 0; i < (sym->type & 0xf); i++)
    printf("*");
  printf("%s", sym->name);

  switch (sym->stype) {
    case S_VARIABLE:
      break;
    case S_FUNCTION:
      printf("()");
      break;
    case S_ARRAY:
      printf("[]");
      break;
    default:
      printf(" unknown stype");
  }

  switch (sym->class) {
    case C_GLOBAL:
      printf(": global");
      break;
    case C_LOCAL:
      printf(": local");
      break;
    case C_PARAM:
      printf(": param");
      break;
    case C_EXTERN:
      printf(": extern");
      break;
    case C_STATIC:
      printf(": static");
      break;
    case C_STRUCT:
      printf(": struct");
      break;
    case C_UNION:
      printf(": union");
      break;
    case C_MEMBER:
      printf(": member");
      break;
    case C_ENUMTYPE:
      printf(": enumtype");
      break;
    case C_ENUMVAL:
      printf(": enumval");
      break;
    case C_TYPEDEF:
      printf(": typedef");
      break;
    default:
      printf(": unknown class");
  }

  switch (sym->stype) {
    case S_VARIABLE:
      if (sym->class == C_ENUMVAL)
	printf(", value %d\n", sym->st_posn);
      else
	printf(", size %d\n", sym->size);
      break;
    case S_FUNCTION:
      printf(", %d params\n", sym->nelems);
      break;
    case S_ARRAY:
      printf(", %d elems, size %d\n", sym->nelems, sym->size);
      break;
  }

  switch (sym->type & (~0xf)) {
    case P_STRUCT:
    case P_UNION:
      dumptable(sym->member, NULL, 4);
  }

  switch (sym->stype) {
    case S_FUNCTION:
      dumptable(sym->member, NULL, 4);
  }
}

// 转储一个符号表
void dumptable(struct symtable *head, char *name, int indent) {
  struct symtable *sym;

  if (head != NULL && name != NULL)
    printf("%s\n--------\n", name);
  for (sym = head; sym != NULL; sym = sym->next)
    dumpsym(sym, indent);
}

void dumpsymtables(void) {
  dumptable(Globhead, "Global", 0);
  printf("\n");
  dumptable(Enumhead, "Enums", 0);
  printf("\n");
  dumptable(Typehead, "Typedefs", 0);
}