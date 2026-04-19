#include "defs.h"
#include "data.h"
#include "misc.h"
#include "tree.h"
#include "types.h"
#include "sym.h"

#undef MEMBDEBUG
#undef DEBUG

// 符号表函数
// Copyright (c) 2024 Warren Toomey, GPL3

// 我们有两个内存中的符号表。一个用于
// 类型（结构体、联合体、枚举、typedef）；另一个用于变量和函数。这些
// 缓存最近使用过的符号文件中的符号。
// 还有一个临时列表，我们在附加到符号的成员字段之前构建它。
// 这用于结构体、联合体和枚举。
// 它还保存我们当前正在解析的函数的参数和局部变量。
//
// Symhead 需要对 cgen.c 可见

struct symtable *Symhead = NULL;
static struct symtable *Symtail = NULL;
static struct symtable *Typehead = NULL;
static struct symtable *Typetail = NULL;
static struct symtable *Membhead = NULL;
static struct symtable *Membtail = NULL;

#ifdef DEBUG
static void dumptable(struct symtable *head, int indent);

// 结构类型字符串列表
char *Sstring[] = {
  "variable", "function", "array", "enumval", "strlit",
  "struct", "union", "enumtype", "typedef", "notype"
};

// 转储单个符号
static void dumpsym(struct symtable *sym, int indent) {
  int i;

  for (i = 0; i < indent; i++)
    printf(" ");
  switch (sym->type & (~0xf)) {
  case P_VOID: printf("void "); break;
  case P_CHAR: printf("char "); break;
  case P_INT:  printf("int "); break;
  case P_LONG: printf("long "); break;
  case P_STRUCT:
    printf("struct ");
    if (sym->ctype && sym->ctype->name)
      printf("%s ", sym->ctype->name);
    break;
  case P_UNION:
    printf("union ");
    if (sym->ctype && sym->ctype->name)
      printf("%s ", sym->ctype->name);
    break;
  default:
    printf("unknown type ");
  }

  for (i = 0; i < (sym->type & 0xf); i++) printf("*");
  printf("%s", sym->name);

  switch (sym->stype) {
  case S_VARIABLE:
    break;
  case S_FUNCTION: printf("()"); break;
  case S_ARRAY:    printf("[]"); break;
  case S_STRUCT:   printf(": struct"); break;
  case S_UNION:    printf(": union"); break;
  case S_ENUMTYPE: printf(": enum"); break;
  case S_ENUMVAL:  printf(": enumval"); break;
  case S_TYPEDEF:  printf(": typedef"); break;
  case S_STRLIT:   printf(": strlit"); break;
  default:	   printf(" unknown stype");
  }

  printf(" id %d", sym->id);

  switch (sym->class) {
  case V_GLOBAL: printf(": global"); break;
  case V_LOCAL:  printf(": local offset %d", sym->st_posn); break;
  case V_PARAM:  printf(": param offset %d", sym->st_posn); break;
  case V_EXTERN: printf(": extern"); break;
  case V_STATIC: printf(": static"); break;
  case V_MEMBER: printf(": member"); break;
  default: 	 printf(": unknown class");
  }

  if (sym->st_hasaddr != 0) printf(", hasaddr ");

  switch (sym->stype) {
  case S_VARIABLE: printf(", size %d", sym->size); break;
  case S_FUNCTION: printf(", %d params", sym->nelems); break;
  case S_ARRAY:    printf(", %d elems, size %d", sym->nelems, sym->size); break;
  }

  printf(", ctypeid %d, nelems %d st_posn %d\n",
	 sym->ctypeid, sym->nelems, sym->st_posn);

  if (sym->member != NULL) dumptable(sym->member, 4);
}

// 转储一个符号表
static void dumptable(struct symtable *head, int indent) {
  struct symtable *sym;

  for (sym = head; sym != NULL; sym = sym->next) dumpsym(sym, indent);
}

void dumpSymlists(void) {
  fprintf(stderr, "Typelist\n");
  fprintf(stderr, "--------\n");
  dumptable(Typehead, 0);
  fprintf(stderr, "\nSymlist\n");
  fprintf(stderr, "-------\n");
  dumptable(Symhead, 0);
  fprintf(stderr, "\nFunctionid\n");
  fprintf(stderr, "----------\n");
  dumptable(Functionid, 0);
  fprintf(stderr, "\nMemblist\n");
  fprintf(stderr, "--------\n");
  dumptable(Membhead, 0);
}
#endif

// 将节点追加到由 head 或 tail 指向的单链表中
static void appendSym(struct symtable **head, struct symtable **tail,
		      struct symtable *node) {

  // 检查有效指针
  if (head == NULL || tail == NULL || node == NULL)
    fatal("Either head, tail or node is NULL in appendSym");

  // 追加到列表
  if (*tail) {
    (*tail)->next = node; *tail = node;
  } else
    *head = *tail = node;
  node->next = NULL;
}

// 我们从符号文件加载的最后一个名称
static char SymText[TEXTLEN + 1];

#ifdef WRITESYMS
// 每个符号的唯一 id。序列化时我们需要这个，
// 以便可以找到变量的组合类型。
static int Symid = 1;

// 对于不是局部变量或参数的符号，
// 我们在创建后指向该符号。这
// 允许我们添加初始值或成员。
// 当我们要创建另一个非局部/参数符号时，
// 这个会被刷新到磁盘。

struct symtable *thisSym = NULL;

// 当我们将符号序列化到符号表文件时，
// 跟踪具有最高 id 的那个。在每次刷新
// 内存中的列表后，将最高 id 记录到 skipSymid 中。
// 然后，在下一次刷新时，不写出 id 小于或等于 skipSymid 的符号。
static int highestSymid = 0;
static int skipSymid = 0;

// 将一个符号序列化到符号表文件
static void serialiseSym(struct symtable *sym) {
  struct symtable *memb;

  if (sym->id > highestSymid) highestSymid = sym->id;

  if (sym->id <= skipSymid) {
#ifdef DEBUG
    fprintf(stderr, "NOT Writing %s %s id %d to disk\n",
	    Sstring[sym->stype], sym->name, sym->id);
#endif
    return;
  }

  // 一旦到达文件末尾，输出符号结构和名称
  fseek(Symfile, 0, SEEK_END);
#ifdef DEBUG
  fprintf(stderr, "Writing %s %s id %d to disk offset %ld\n",
	  Sstring[sym->stype], sym->name, sym->id, ftell(Symfile));
#endif
  fwrite(sym, sizeof(struct symtable), 1, Symfile);
  if (sym->name != NULL) {
    fputs(sym->name, Symfile); fputc(0, Symfile);
  }
  // 输出初始值（如果有的话）
  if (sym->initlist != NULL)
    fwrite(sym->initlist, sizeof(int), sym->nelems, Symfile);

  // 输出成员符号
#ifdef DEBUG
  if (sym->member != NULL) {
    fprintf(stderr, "%s has members\n", sym->name);
  }
#endif

  for (memb = sym->member; memb != NULL; memb = memb->next)
    serialiseSym(memb);
}

// 创建一个符号表节点。设置节点的：
// + type: char, int 等。
// + ctype: struct/union 的组合类型指针
// + structural type: var, function, array 等。
// + size: 元素数量，或 endlabel: 函数的结束标签
// + posn: 局部符号的位置信息
// 返回指向新节点的指针。
static struct symtable *newsym(char *name, int type, struct symtable *ctype,
			       int stype, int class, int nelems, int posn) {

  // 获取一个新节点
  struct symtable *node = (struct symtable *) malloc(sizeof(struct symtable));
  if (node == NULL)
    fatal("Unable to malloc a symbol table node in newsym");

  // 填充值
  node->id = Symid++;
#ifdef DEBUG
  fprintf(stderr, "Newsym %s %s id %d\n", Sstring[stype], name, node->id);
#endif
  if (name == NULL) node->name = NULL;
  else node->name = strdup(name);
  node->type = type;
  node->ctype = ctype;
  if (ctype != NULL) node->ctypeid = ctype->id;
  else node->ctypeid = 0;
  node->stype = stype;
  node->class = class;
  node->nelems = nelems;
  node->st_hasaddr = 0;

  // 对于指针和整数类型，设置符号的大小。
  // struct 和 union 声明手动设置这个。
  if (ptrtype(type) || inttype(type))
    node->size = nelems * typesize(type, ctype);

  node->st_posn = posn;
  node->next = NULL;
  node->member = NULL;
  node->initlist = NULL;
  return (node);
}

// 将新类型添加到类型列表。返回指向符号的指针。
struct symtable *addtype(char *name, int type, struct symtable *ctype,
			 int stype, int class, int nelems, int posn) {
  struct symtable *sym;
  thisSym = sym = newsym(name, type, ctype, stype, class, nelems, posn);

#ifdef DEBUG
  fprintf(stderr, "Added %s %s to Typelist\n",
	  Sstring[sym->stype], sym->name);
#endif
  appendSym(&Typehead, &Typetail, sym);
  Membhead = Membtail = NULL;
  return (sym);
}

// 将新符号添加到全局表。返回指向符号的指针。
struct symtable *addglob(char *name, int type, struct symtable *ctype,
			 int stype, int class, int nelems, int posn) {
  struct symtable *sym;
  thisSym = sym = newsym(name, type, ctype, stype, class, nelems, posn);

  // 对于结构体和联合体，从类型节点复制大小
  if (type == P_STRUCT || type == P_UNION) sym->size = ctype->size;

#ifdef DEBUG
  fprintf(stderr, "Added %s %s to Symlist\n", Sstring[sym->stype], sym->name);
#endif
  appendSym(&Symhead, &Symtail, sym);
  Membhead = Membtail = NULL;
  return (sym);
}

// 将符号添加到 thisSym 中的成员列表
struct symtable *addmemb(char *name, int type, struct symtable *ctype,
			 int class, int stype, int nelems) {
  struct symtable *sym = newsym(name, type, ctype, stype, class, nelems, 0);

  // 对于结构体和联合体，从类型节点复制大小
  if (type == P_STRUCT || type == P_UNION) sym->size = ctype->size;

  // 将这个添加到成员列表，并在需要时链接到 thisSym
  appendSym(&Membhead, &Membtail, sym);
#ifdef DEBUG
  fprintf(stderr, "Added %s %s to Memblist\n",
	  Sstring[sym->stype], sym->name);
#endif
  if (thisSym->member == NULL) {
    thisSym->member = Membhead;
#ifdef DEBUG
    fprintf(stderr, "Added %s to start of %s\n", name, thisSym->name);
#endif
  }
  return (sym);
}

// 将内存中符号表的内容刷新到文件。

void flushSymtable() {
  struct symtable *this;

  // 写出类型
  for (this = Typehead; this != NULL; this = this->next) {
    serialiseSym(this);
  }

  // 写出变量和函数。
  // 跳过无效符号
  for (this = Symhead; this != NULL; this = this->next) {
    serialiseSym(this);
  }

  skipSymid = highestSymid;
  freeSymtable();
}
#endif // WRITESYMS

// 当读取具有成员（结构体、联合体、函数）的全局变量时，
// 一旦遇到非成员就停止。我们需要记录那个符号的偏移量，
// 以便我们可以 fseek() 回去。否则，如果它也是一个全局变量，
// 我们将无法加载它。
static long lastSymOffset;

// 当 loadSym() 从磁盘加载符号成员时，我们需要一个链表。
// 我们不能使用 Membhead/tail，因为这可能在解析函数体时正在使用。
// 所以我们保留一个私有列表。
static struct symtable *Mhead, *Mtail;

// 给定一个 symtable 节点的指针，读取
// 磁盘符号表中的下一个条目。如果 loadit 为真，则始终执行此操作。
// 如果 recurse 为零，则只读取一个节点。
// 如果 loadit 为假，则加载数据并在以下情况下返回真：
// a) 符号匹配给定的名称和 stype，或 b) 匹配 id。
// 当没有剩余内容可读时返回 -1。
static int loadSym(struct symtable *sym, char *name,
		   int stype, int id, int loadit, int recurse) {
  struct symtable *memb;

#ifdef DEBUGTOUCH
if (name!=NULL)
  fprintf(stderr, "loadSym: name %s stype %d loadit %d recurse %d\n",
		name, stype, loadit, recurse);
else
  fprintf(stderr, "loadSym: id %d stype %d loadit %d recurse %d\n",
		id, stype, loadit, recurse);
#endif

  // 读取下一个节点。事先获取偏移量的副本
  lastSymOffset = ftell(Symfile);
  if (fread(sym, sizeof(struct symtable), 1, Symfile) != 1) return (-1);

  // 将符号名称放入单独的缓冲区
  if (sym->name != NULL) {
    fgetstr(SymText, TEXTLEN + 1, Symfile);
  }

#ifdef DEBUG
  if (sym->name != NULL)
    fprintf(stderr, "symoff %ld name %s stype %d\n",
		lastSymOffset, SymText, sym->stype);
  else
    fprintf(stderr, "symoff %ld id %d\n", lastSymOffset, sym->id);
#endif

  // 如果 loadit 关闭，查看 id 是否匹配。或者，
  // 查看名称是否匹配且 stype 是否匹配。
  // 对于后者，如果 NOTATYPE 匹配任何非类型且非成员、本地或参数的东西：我们正在
  // 尝试找一个变量、enumval 或函数。findlocl()
  // 会找到它（如果它是局部变量或参数）。只有当我们尝试找到一个全局变量、
  // enumval 或函数时才会到这里。
  if (loadit == 0) {
    if (id != 0 && sym->id == id) loadit = 1;
    if (name != NULL && !strcmp(name, SymText)) {
      if (stype == S_NOTATYPE && sym->stype < S_STRUCT
		      && sym->class < V_LOCAL) loadit = 1;
      if (stype >= S_STRUCT && stype == sym->stype) loadit = 1;
    }
  }

  // 是的，我们需要加载符号的其余部分
  if (loadit) {

    // 复制名称。
    sym->name = strdup(SymText);
    if (sym->name == NULL) fatal("Unable to malloc name in loadSym()");

#ifdef DEBUG
    if (sym->name == NULL) {
      fprintf(stderr, "loadSym found %s NONAME id %d loadit %d\n",
	      Sstring[sym->stype], sym->id, loadit);
    } else {
      fprintf(stderr, "loadSym found %s %s id %d loadit %d\n",
	      Sstring[sym->stype], sym->name, sym->id, loadit);
    }
#endif

    // 获取初始化列表。
    if (sym->initlist != NULL) {
      sym->initlist = (int *) malloc(sym->nelems * sizeof(int));
      if (sym->initlist == NULL)
	fatal("Unable to malloc initlist in loadSym()");
      fread(sym->initlist, sizeof(int), sym->nelems, Symfile);
    }

    // 如果我们不能递归加载更多节点，现在停止
    if (!recurse) {
#ifdef DEBUG
      fprintf(stderr, "loadSym found it - no recursion\n");
#endif
      return (1);
    }

    // 对于结构体、联合体和函数，加载并添加
    // 成员（或参数/局部变量）到成员列表
    if (sym->stype == S_STRUCT || sym->stype == S_UNION
			       || sym->stype == S_FUNCTION) {
      Mhead = Mtail = NULL;

      while (1) {
#ifdef DEBUG
fprintf(stderr, "loadSym: about to try loading members\n");
#endif
	memb = (struct symtable *) malloc(sizeof(struct symtable));
	if (memb == NULL)
	  fatal("Unable to malloc member in loadSym()");
#ifdef MEMBDEBUG
fprintf(stderr, "%p allocated\n", memb);
#endif
	// 获取下一个符号。当没有更多符号时停止，
	// 或者当符号不是成员、enumval、param 或 local 时停止
	if (loadSym(memb, NULL, 0, 0, 1, 0) != 1)
	  break;
	if (memb->class != V_LOCAL && memb->class != V_PARAM &&
	    memb->class != V_MEMBER)
	  break;
#ifdef DEBUG
fprintf(stderr, "loadSym: appending %s to member list\n", memb->name);
#endif
	appendSym(&Mhead, &Mtail, memb);
      }

      // 我们找到了一个非成员符号。查找回去
      // 到它所在的位置并释放未使用的结构。
      // 将成员列表附加到原始符号。
      fseek(Symfile, lastSymOffset, SEEK_SET);
#ifdef DEBUG
fprintf(stderr, "Seeked to lastSymOffset %ld as non-member id %d\n",
			lastSymOffset, memb->id);
#endif
#ifdef MEMBDEBUG
fprintf(stderr, "%p freed, unused memb\n", memb);
#endif
      free(memb);
      sym->member = Mhead;
      Mhead = Mtail = NULL;
    }
    return (1);
  } else {
    // 不匹配且 loadit 为 0。跳过任何初始化列表。
    if (sym->initlist != NULL)
      fseek(Symfile, sizeof(int) * sym->nelems, SEEK_CUR);
  }
  return (0);
}

// 给定一个名称或 id，在符号表文件中搜索匹配的下一个符号。
// 填充节点并在匹配时返回真。否则返回假。
static int findSyminfile(struct symtable *sym, char *name, int id, int stype) {
  int res;

#ifdef DEBUG
if (name!=NULL)
  fprintf(stderr, "findSyminfile: searching name %s stype %d\n", name, stype);
else
  fprintf(stderr, "findSyminfile: search id %d\n", id);
#endif

  // 从文件开头开始循环
  fseek(Symfile, 0, SEEK_SET);
  while (1) {
    // 下一个符号匹配吗？是，返回它
    res = loadSym(sym, name, stype, id, 0, 1);
    if (res == 1) return (1);
    if (res == -1) break;
  }
#ifdef DEBUG
  fprintf(stderr, "findSyminfile: not found\n");
#endif
  return (0);
}

// 确定符号名称或 id（如果不为零）是否是局部变量或参数。
// 返回指向找到的节点的指针，如果未找到则返回 NULL。
struct symtable *findlocl(char *name, int id) {
  struct symtable *this;

  // 我们必须在函数中
  if (Functionid == NULL) return (NULL);

#ifdef DEBUG
if (id!=0) fprintf(stderr, "findlocl() searching for id %d\n", id);
if (name!=NULL) fprintf(stderr, "findlocl() searching for name %s\n", name);
#endif

  for (this = Functionid->member; this != NULL; this = this->next) {
    if (id && this->id == id) return (this);
    if (name && !strcmp(this->name, name)) return (this);
  }

  return (NULL);
}

// 给定一个名称和一个 stype，搜索匹配的符号。
// 或者，如果 id 非零，则搜索具有该 id 的符号。
// 如有必要，将符号带入内存列表之一。
// 返回指向找到的节点的指针，如果未找到则返回 NULL。
struct symtable *findSymbol(char *name, int stype, int id) {
  struct symtable *this;
  struct symtable *sym;
  int notatype;

  // 如果我们不是在找类型，设置一个标志
  notatype = (stype == S_NOTATYPE || stype == S_ENUMVAL);

#ifdef DEBUG
  if (id != 0)
    fprintf(stderr, "Searching for symbol id %d in memory\n", id);
  else
    fprintf(stderr, "Searching for symbol %s %s in memory\n",
	    Sstring[stype], name);
#endif

  // 如果它不是类型，看看它是否是局部变量或参数
  if (id || notatype) {
    this = findlocl(name, id);
    if (this != NULL) return (this);

#ifdef DEBUG
fprintf(stderr, "Not in local, try the global Symlist\n");
#endif

    // 不是局部变量，所以搜索全局符号列表。
    for (this = Symhead; this != NULL; this = this->next) {
      if (id && this->id == id) return (this);
      if (name && !strcmp(this->name, name)) return (this);
    }
  }

#ifdef DEBUG
fprintf(stderr, "Not in , try the global Typelist\n");
#endif

  // 我们有一个 id 或者它是一个类型，
  // 搜索全局类型列表。
  // 对这个双重否定很抱歉 :-)
  if (id || !notatype) {
    for (this = Typehead; this != NULL; this = this->next) {
      if (id && this->id == id) return (this);
      if (name && !strcmp(this->name, name) && this->stype == stype)
	return (this);
    }
  }

#ifdef DEBUG
  fprintf(stderr, "  not in memory, try the file\n");
#endif

  // 不在内存中。尝试磁盘上的符号表
  sym = (struct symtable *) malloc(sizeof(struct symtable));
  if (sym == NULL) {
    fatal("Unable to malloc sym in findSyminlist()");
  }

  // 如果在文件中找到匹配项
  if (findSyminfile(sym, name, id, stype)) {
    // 将其添加到内存列表之一并返回它
    if (sym->stype < S_STRUCT)
      appendSym(&Symhead, &Symtail, sym);
    else
      appendSym(&Typehead, &Typetail, sym);

    // 如果符号指向组合类型，找到并链接它
    if (sym->ctype != NULL) {
#ifdef DEBUG
      fprintf(stderr, "About to findSymid on id %d for %s\n", sym->ctypeid,
	      sym->name);
#endif
      sym->ctype = findSymbol(NULL, 0, sym->ctypeid);
    }

    // 如果任何成员符号指向组合类型，同样处理
    for (this = sym->member; this != NULL; this = this->next)
      if (this->ctype != NULL) {
#ifdef DEBUG
	fprintf(stderr, "About to member findSymid on id %d for %s\n",
		this->ctypeid, this->name);
#endif
	this->ctype = findSymbol(NULL, 0, this->ctypeid);
      }
    return (sym);
  }
  free(sym);
  return (NULL);
}

// 在成员列表中找到成员。返回指向找到的节点的指针，如果未找到则返回 NULL。
struct symtable *findmember(char *s) {
  struct symtable *node;

  for (node = Membhead; node != NULL; node = node->next)
    if (!strcmp(s, node->name)) return (node);

  return (NULL);
}

// 在结构体列表中找到节点
// 返回指向找到的节点的指针，如果未找到则返回 NULL。
struct symtable *findstruct(char *s) {
  return (findSymbol(s, S_STRUCT, 0));
}

// 在联合体列表中找到节点
// 返回指向找到的节点的指针，如果未找到则返回 NULL。
struct symtable *findunion(char *s) {
  return (findSymbol(s, S_UNION, 0));
}

// 在枚举列表中找到枚举类型
// 返回指向找到的节点的指针，如果未找到则返回 NULL。
struct symtable *findenumtype(char *s) {
  return (findSymbol(s, S_ENUMTYPE, 0));
}

// 在枚举列表中找到枚举值
// 返回指向找到的节点的指针，如果未找到则返回 NULL。
struct symtable *findenumval(char *s) {
  return (findSymbol(s, S_ENUMVAL, 0));
}

// 在 typedef 列表中找到类型
// 返回指向找到的节点的指针，如果未找到则返回 NULL。
struct symtable *findtypedef(char *s) {
  return (findSymbol(s, S_TYPEDEF, 0));
}

// 释放符号的内存，返回符号的下一个指针
struct symtable *freeSym(struct symtable *sym) {
  struct symtable *next, *memb;

  if (sym == NULL) return (NULL);
  next = sym->next;
#ifdef MEMBDEBUG
  fprintf(stderr, "%p freeing\n", sym);
#endif
#ifdef DEBUG
  fprintf(stderr, "Freeing %s %s\n", Sstring[sym->stype], sym->name);
#endif

  // 释放任何成员
  for (memb = sym->member; memb != NULL;)
    memb = freeSym(memb);

  // 释放 initlist 和名称
  if (sym->initlist != NULL)
    free(sym->initlist);
  if (sym->name != NULL)
    free(sym->name);
  free(sym);
  return (next);
}

// 释放内存中符号表的内容
void freeSymtable() {
  struct symtable *this;

  for (this = Symhead; this != NULL;)
    this = freeSym(this);
  for (this = Typehead; this != NULL;)
    this = freeSym(this);
  Symhead = Symtail = Typehead = Typetail = NULL;
  Membhead = Membtail = Functionid = NULL;
}

// 循环遍历符号表文件。
// 加载所有类型和全局/静态变量。
void loadGlobals(void) {
  struct symtable *sym;
  int i;

  // 从文件开头开始。加载所有符号
  fseek(Symfile, 0, SEEK_SET);
  while (1) {
    // 加载下一个符号 + 成员 + initlist
    sym = (struct symtable *) malloc(sizeof(struct symtable));
    if (sym == NULL)
      fatal("Unable to malloc in allocateGlobals()");
    i = loadSym(sym, NULL, 0, 0, 1, 1);
    if (i == -1) {
      free(sym); break;
    }

    // 将任何类型添加到类型列表
    if (sym->stype >= S_STRUCT) {
      appendSym(&Typehead, &Typetail, sym); continue;
    }

    // 将任何全局/静态变量/数组/strlit 添加到符号列表
    if ((sym->class == V_GLOBAL || sym->class == V_STATIC) &&
	(sym->stype == S_VARIABLE || sym->stype == S_ARRAY ||
	 sym->stype == S_STRLIT)) {
      appendSym(&Symhead, &Symtail, sym);

      // 如果符号指向组合类型，找到并链接它
      if (sym->ctype != NULL) {
	sym->ctype = findSymbol(NULL, 0, sym->ctypeid);
      }
      continue;
    }

    // 没有添加到任何列表
    freeSym(sym);
  }
}