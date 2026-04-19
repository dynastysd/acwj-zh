#include "defs.h"
#include "data.h"
#include "decl.h"

// 符号表函数
// Copyright (c) 2019 Warren Toomey, GPL3

// 将节点追加到由 head 或 tail 指向的单向链表
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

// 创建一个要添加到符号表列表的符号节点。
// 设置节点的：
// + 类型：char、int 等。
// + ctype：struct/union 的组合类型指针
// + 结构类型：var、function、array 等。
// + 大小：元素数量，或 endlabel：函数的结束标签
// + posn：局部符号的位置信息
// 返回指向新节点的指针
struct symtable *newsym(char *name, int type, struct symtable *ctype,
			int stype, int class, int nelems, int posn) {

  // 获取一个新节点
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
  // 符号的大小。struct 和 union 声明
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
  struct symtable *sym = newsym(name, type, ctype, stype, class, nelems, posn);
  // 对于 struct 和 union，从类型节点复制大小
  if (type== P_STRUCT || type== P_UNION)
    sym->size = ctype->size;
  appendsym(&Globhead, &Globtail, sym);
  return (sym);
}

// 将符号添加到局部符号列表
struct symtable *addlocl(char *name, int type, struct symtable *ctype,
			 int stype, int nelems) {
  struct symtable *sym = newsym(name, type, ctype, stype, C_LOCAL, nelems, 0);
  // 对于 struct 和 union，从类型节点复制大小
  if (type== P_STRUCT || type== P_UNION)
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
  struct symtable *sym = newsym(name, type, ctype, stype, C_MEMBER, nelems, 0);
  // 对于 struct 和 union，从类型节点复制大小
  if (type== P_STRUCT || type== P_UNION)
    sym->size = ctype->size;
  appendsym(&Membhead, &Membtail, sym);
  return (sym);
}

// 将 struct 添加到 struct 列表
struct symtable *addstruct(char *name) {
  struct symtable *sym = newsym(name, P_STRUCT, NULL, 0, C_STRUCT, 0, 0);
  appendsym(&Structhead, &Structtail, sym);
  return (sym);
}

// 将 struct 添加到 union 列表
struct symtable *addunion(char *name) {
  struct symtable *sym = newsym(name, P_UNION, NULL, 0, C_UNION, 0, 0);
  appendsym(&Unionhead, &Uniontail, sym);
  return (sym);
}

// 将枚举类型或值添加到枚举列表。
// class 是 C_ENUMTYPE 或 C_ENUMVAL。
// 使用 posn 存储 int 值。
struct symtable *addenum(char *name, int class, int value) {
  struct symtable *sym = newsym(name, P_INT, NULL, 0, class, 0, value);
  appendsym(&Enumhead, &Enumtail, sym);
  return (sym);
}

// 将 typedef 添加到 typedef 列表
struct symtable *addtypedef(char *name, int type, struct symtable *ctype) {
  struct symtable *sym = newsym(name, type, ctype, 0, C_TYPEDEF, 0, 0);
  appendsym(&Typehead, &Typetail, sym);
  return (sym);
}

// 在特定列表中搜索符号。
// 返回找到的节点的指针，如果未找到则返回 NULL。
// 如果 class 不为零，也匹配给定的 class
static struct symtable *findsyminlist(char *s, struct symtable *list, int class) {
  for (; list != NULL; list = list->next)
    if ((list->name != NULL) && !strcmp(s, list->name))
      if (class==0 || class== list->class)
        return (list);
  return (NULL);
}

// 确定符号 s 是否在全局符号表中。
// 返回找到的节点的指针，如果未找到则返回 NULL。
struct symtable *findglob(char *s) {
  return (findsyminlist(s, Globhead, 0));
}

// 确定符号 s 是否在局部符号表中。
// 返回找到的节点的指针，如果未找到则返回 NULL。
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

// 确定符号 s 是否在符号表中。
// 返回找到的节点的指针，如果未找到则返回 NULL。
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
// 返回找到的节点的指针，如果未找到则返回 NULL。
struct symtable *findmember(char *s) {
  return (findsyminlist(s, Membhead, 0));
}

// 在 struct 列表中查找 struct
// 返回找到的节点的指针，如果未找到则返回 NULL。
struct symtable *findstruct(char *s) {
  return (findsyminlist(s, Structhead, 0));
}

// 在 union 列表中查找 struct
// 返回找到的节点的指针，如果未找到则返回 NULL。
struct symtable *findunion(char *s) {
  return (findsyminlist(s, Unionhead, 0));
}

// 在枚举列表中查找枚举类型
// 返回找到的节点的指针，如果未找到则返回 NULL。
struct symtable *findenumtype(char *s) {
  return (findsyminlist(s, Enumhead, C_ENUMTYPE));
}

// 在枚举列表中查找枚举值
// 返回找到的节点的指针，如果未找到则返回 NULL。
struct symtable *findenumval(char *s) {
  return (findsyminlist(s, Enumhead, C_ENUMVAL));
}

// 在 typedef 列表中查找类型
// 返回找到的节点的指针，如果未找到则返回 NULL。
struct symtable *findtypedef(char *s) {
  return (findsyminlist(s, Typehead, 0));
}

// 重置符号表的内容
void clear_symtable(void) {
  Globhead =   Globtail  =  NULL;
  Loclhead =   Locltail  =  NULL;
  Parmhead =   Parmtail  =  NULL;
  Membhead =   Membtail  =  NULL;
  Structhead = Structtail = NULL;
  Unionhead =  Uniontail =  NULL;
  Enumhead =   Enumtail =   NULL;
  Typehead =   Typetail =   NULL;
}

// 清除局部符号表中的所有条目
void freeloclsyms(void) {
  Loclhead = Locltail = NULL;
  Parmhead = Parmtail = NULL;
  Functionid = NULL;
}