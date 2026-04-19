#include "defs.h"
#include "data.h"
#include "decl.h"

// 符号表函数
// Copyright (c) 2019 Warren Toomey, GPL3

// 将节点追加到由 head 或 tail 指向的单向链接列表
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
// + type: char、int 等。
// + structural type: var、function、array 等。
// + size: 元素数量，或 endlabel: 函数结束标签
// + posn: 局部符号的位置信息
// 返回指向新节点的指针
struct symtable *newsym(char *name, int type, int stype, int class,
			int size, int posn) {

  // 获取新节点
  struct symtable *node = (struct symtable *) malloc(sizeof(struct symtable));
  if (node == NULL)
    fatal("Unable to malloc a symbol table node in newsym");

  // 填充值
  node->name = strdup(name);
  node->type = type;
  node->stype = stype;
  node->class = class;
  node->size = size;
  node->posn = posn;
  node->next = NULL;
  node->member = NULL;

  // 生成任何全局空间
  if (class == C_GLOBAL)
    genglobsym(node);
  return (node);
}

// 将符号添加到全局符号列表
struct symtable *addglob(char *name, int type, int stype, int class, int size) {
  struct symtable *sym = newsym(name, type, stype, class, size, 0);
  appendsym(&Globhead, &Globtail, sym);
  return (sym);
}

// 将符号添加到局部符号列表
struct symtable *addlocl(char *name, int type, int stype, int class, int size) {
  struct symtable *sym = newsym(name, type, stype, class, size, 0);
  appendsym(&Loclhead, &Locltail, sym);
  return (sym);
}

// 将符号添加到参数列表
struct symtable *addparm(char *name, int type, int stype, int class, int size) {
  struct symtable *sym = newsym(name, type, stype, class, size, 0);
  appendsym(&Parmhead, &Parmtail, sym);
  return (sym);
}

// 在特定列表中搜索符号。
// 返回找到的节点的指针，如果未找到则返回 NULL。
static struct symtable *findsyminlist(char *s, struct symtable *list) {
  for (; list != NULL; list = list->next)
    if ((list->name != NULL) && !strcmp(s, list->name))
      return (list);
  return (NULL);
}

// 确定符号 s 是否在全局符号表中。
// 返回找到的节点的指针，如果未找到则返回 NULL。
struct symtable *findglob(char *s) {
  return (findsyminlist(s, Globhead));
}

// 确定符号 s 是否在局部符号表中。
// 返回找到的节点的指针，如果未找到则返回 NULL。
struct symtable *findlocl(char *s) {
  struct symtable *node;

  // 如果我们在函数体中，则查找参数
  if (Functionid) {
    node = findsyminlist(s, Functionid->member);
    if (node)
      return (node);
  }
  return (findsyminlist(s, Loclhead));
}

// 确定符号 s 是否在符号表中。
// 返回找到的节点的指针，如果未找到则返回 NULL。
struct symtable *findsymbol(char *s) {
  struct symtable *node;

  // 如果我们在函数体中，则查找参数
  if (Functionid) {
    node = findsyminlist(s, Functionid->member);
    if (node)
      return (node);
  }
  // 否则，尝试局部和全局符号列表
  node = findsyminlist(s, Loclhead);
  if (node)
    return (node);
  return (findsyminlist(s, Globhead));
}

// 查找复合类型。
// 返回找到的节点的指针，如果未找到则返回 NULL。
struct symtable *findcomposite(char *s) {
  return (findsyminlist(s, Comphead));
}

// 重置符号表的内容
void clear_symtable(void) {
  Globhead = Globtail = NULL;
  Loclhead = Locltail = NULL;
  Parmhead = Parmtail = NULL;
  Comphead = Comptail = NULL;
}

// 释放局部符号表中的所有条目
void freeloclsyms(void) {
  Loclhead = Locltail = NULL;
  Parmhead = Parmtail = NULL;
  Functionid = NULL;
}