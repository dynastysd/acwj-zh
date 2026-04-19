// sym.c - 符号表函数
// 版权所有 (c) 2024 Warren Toomey, GPL3

// 我们有两个内存中符号表。一个是
// 类型（结构、联合、枚举、typedef）的；
// 另一个是变量和函数的。这些
// 缓存从符号文件最近使用的符号。
// 还有一个临时列表，我们在其附加到符号的member字段之前构建它。
// 这用于结构、联合和枚举。
// 它也保存我们当前正在解析的函数的参数和局部变量。
//
// Symhead需要对cgen.c可见

struct symtable *addtype(char *name, int type, struct symtable *ctype,
				int stype, int class, int nelems, int posn);
struct symtable *addglob(char *name, int type, struct symtable *ctype,
				int stype, int class, int nelems, int posn);
struct symtable *addmemb(char *name, int type, struct symtable *ctype,
				          int class, int stype, int nelems);
struct symtable *findlocl(char *name, int id);
struct symtable *findSymbol(char *name, int stype, int id);
struct symtable *findmember(char *s);
struct symtable *findstruct(char *s);
struct symtable *findunion(char *s);
struct symtable *findenumtype(char *s);
struct symtable *findenumval(char *s);
struct symtable *findtypedef(char *s);
void loadGlobals(void);
struct symtable *freeSym(struct symtable *sym);
void freeSymtable(void);
void flushSymtable(void);
void dumpSymlists(void);

extern struct symtable *Symhead;