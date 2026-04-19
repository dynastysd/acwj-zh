#include "defs.h"
#include "data.h"
#include "decl.h"

// 符号表函数
// Copyright (c) 2019 Warren Toomey, GPL3

// 将节点追加到由head或tail指向的单链表
void appendsym(struct symtable **head, struct symtable **tail,
	       struct symtable *node) {

  // 检查有效指针
  if (head == NULL || tail == NULL || node == NULL)
    fatal("appendsym中的head、tail或node为NULL");

  // 追加到列表
  if (*tail) {
    (*tail)->next = node;
    *tail = node;
  } else
    *head = *tail = node;
  node->next = NULL;
}

// 创建要添加到符号表列表的符号节点
// 设置节点的:
// + type: char、int等
// + ctype: struct/union的复合类型指针
// + 结构类型: var、function、array等
// + size: 元素数量，或endlabel:函数的结束标签
// + posn: 局部符号的位置信息
// 返回指向新节点的指针
struct symtable *newsym(char *name, int type, struct symtable *ctype,
			int stype, int class, int size, int posn) {

  // 获取新节点
  struct symtable *node = (struct symtable *) malloc(sizeof(struct symtable));
  if (node == NULL)
    fatal("无法在newsym中为符号表节点分配内存");

  // 填充值
  node->name = strdup(name);
  node->type = type;
  node->ctype = ctype;
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
struct symtable *addglob(char *name, int type, struct symtable *ctype,
			 int stype, int size) {
  struct symtable *sym = newsym(name, type, ctype, stype, C_GLOBAL, size, 0);
  appendsym(&Globhead, &Globtail, sym);
  return (sym);
}

// 将符号添加到局部符号列表
struct symtable *addlocl(char *name, int type, struct symtable *ctype,
			 int stype, int size) {
  struct symtable *sym = newsym(name, type, ctype, stype, C_LOCAL, size, 0);
  appendsym(&Loclhead, &Locltail, sym);
  return (sym);
}

// 将符号添加到参数列表
struct symtable *addparm(char *name, int type, struct symtable *ctype,
		 	 int stype, int size) {
  struct symtable *sym = newsym(name, type, ctype, stype, C_PARAM, size, 0);
  appendsym(&Parmhead, &Parmtail, sym);
  return (sym);
}

// 将符号添加到临时成员列表
struct symtable *addmemb(char *name, int type, struct symtable *ctype,
			 int stype, int size) {
  struct symtable *sym = newsym(name, type, ctype, stype, C_MEMBER, size, 0);
  appendsym(&Membhead, &Membtail, sym);
  return (sym);
}

// 将结构体添加到结构体列表
struct symtable *addstruct(char *name, int type, struct symtable *ctype,
			   int stype, int size) {
  struct symtable *sym = newsym(name, type, ctype, stype, C_STRUCT, size, 0);
  appendsym(&Structhead, &Structtail, sym);
  return (sym);
}

// 在特定列表中搜索符号
// 返回找到的节点的指针，如果未找到则返回NULL
static struct symtable *findsyminlist(char *s, struct symtable *list) {
  for (; list != NULL; list = list->next)
    if ((list->name != NULL) && !strcmp(s, list->name))
      return (list);
  return (NULL);
}

// 确定符号s是否在全局符号表中
// 返回找到的节点的指针，如果未找到则返回NULL
struct symtable *findglob(char *s) {
  return (findsyminlist(s, Globhead));
}

// 确定符号s是否在局部符号表中
// 返回找到的节点的指针，如果未找到则返回NULL
struct symtable *findlocl(char *s) {
  struct symtable *node;

  // 如果在函数体中，则查找参数
  if (Functionid) {
    node = findsyminlist(s, Functionid->member);
    if (node)
      return (node);
  }
  return (findsyminlist(s, Loclhead));
}

// 确定符号s是否在符号表中
// 返回找到的节点的指针，如果未找到则返回NULL
struct symtable *findsymbol(char *s) {
  struct symtable *node;

  // 如果在函数体中，则查找参数
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

// 在成员列表中查找成员
// 返回找到的节点的指针，如果未找到则返回NULL
struct symtable *findmember(char *s) {
  return (findsyminlist(s, Membhead));
}

// 在结构体列表中查找结构体
// 返回找到的节点的指针，如果未找到则返回NULL
struct symtable *findstruct(char *s) {
  return (findsyminlist(s, Structhead));
}

// 重置符号表的内容
void clear_symtable(void) {
  Globhead =   Globtail  =  NULL;
  Loclhead =   Locltail  =  NULL;
  Parmhead =   Parmtail  =  NULL;
  Membhead =   Membtail  =  NULL;
  Structhead = Structtail = NULL;
}

// 清除局部符号表中的所有条目
void freeloclsyms(void) {
  Loclhead = Locltail = NULL;
  Parmhead = Parmtail = NULL;
  Functionid = NULL;
}
