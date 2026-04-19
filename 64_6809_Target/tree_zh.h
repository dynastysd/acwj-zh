// tree.c -  AST树函数
// 版权所有 (c) 2019,2024 Warren Toomey, GPL3

#ifndef WRITESYMS

// 我们记录最后加载的函数的id。
// 以及下面数组中的最高索引
static int lastFuncid= -1;
static int hiFuncid;

// 我们还保存一个AST节点偏移量数组，代表AST文件中的函数
long *Funcoffset;

// 给定一个AST节点id，从AST文件加载该AST节点。
// 如果设置了nextfunc，找到下一个是函数的AST节点。
// 分配并返回节点，如果找不到则返回NULL。
struct ASTnode *loadASTnode(int id, int nextfunc);

// 使用打开的AST文件和新建的索引文件，为AST文件中的每个AST节点构建一个AST文件偏移量列表。
void mkASTidxfile(void);
#endif // WRITESYMS