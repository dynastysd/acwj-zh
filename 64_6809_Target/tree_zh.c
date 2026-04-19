#include "defs.h"
#include "data.h"
#include "misc.h"
#include "parse.h"
#include "sym.h"
#include "gen.h"

// AST 树函数
// Copyright (c) 2019,2024 Warren Toomey, GPL3

#undef DEBUG

// 用于枚举 AST 节点
static int nodeid= 1;

// 构建并返回一个通用的 AST 节点
struct ASTnode *mkastnode(int op, int type,
			  struct symtable *ctype,
			  struct ASTnode *left,
			  struct ASTnode *mid,
			  struct ASTnode *right,
			  struct symtable *sym, int intvalue) {
  struct ASTnode *n;

  // Malloc 一个新的 ASTnode
  n = (struct ASTnode *) malloc(sizeof(struct ASTnode));
  if (n == NULL)
    fatal("Unable to malloc in mkastnode()");

  // 复制字段值并返回它
  n->nodeid= nodeid++;
  n->op = op;
  n->type = type;
  n->ctype = ctype;
  n->left = left;
  n->mid = mid;
  n->right = right;
  n->leftid= 0;
  n->midid= 0;
  n->rightid= 0;
#ifdef DEBUG
fprintf(stderr, "mkastnodeA l %d m %d r %d\n", n->leftid, n->midid, n->rightid);
#endif
  if (left!=NULL) n->leftid= left->nodeid;
  if (mid!=NULL) n->midid= mid->nodeid;
  if (right!=NULL) n->rightid= right->nodeid;
#ifdef DEBUG
fprintf(stderr, "mkastnodeB l %d m %d r %d\n", n->leftid, n->midid, n->rightid);
#endif
  n->sym = sym;
  if (sym != NULL) {
    n->name= sym->name;
    n->symid= sym->id;
  } else {
    n->name= NULL;
    n->symid= 0;
  }
  n->a_intvalue = intvalue;
  n->linenum = 0;
  n->rvalue = 0;
  return (n);
}


// 创建一个 AST 叶子节点
struct ASTnode *mkastleaf(int op, int type,
			  struct symtable *ctype,
			  struct symtable *sym, int intvalue) {
  return (mkastnode(op, type, ctype, NULL, NULL, NULL, sym, intvalue));
}

// 创建一个一元 AST 节点：只有一个子节点
struct ASTnode *mkastunary(int op, int type,
			   struct symtable *ctype,
			   struct ASTnode *left,
			   struct symtable *sym, int intvalue) {
  return (mkastnode(op, type, ctype, left, NULL, NULL, sym, intvalue));
}

// 释放给定的 AST 节点
void freeASTnode(struct ASTnode *tree) {
  if (tree==NULL) return;
  if (tree->name != NULL) free(tree->name);
  free(tree);
}

// 释放树的内容。可能因为树优化，
// 有时 left 和 right 是相同的子节点。
// 如果要求释放名称，则释放它们。
void freetree(struct ASTnode *tree, int freenames) {
  if (tree==NULL) return;

  if (tree->left!=NULL) freetree(tree->left, freenames);
  if (tree->mid!=NULL) freetree(tree->mid, freenames);
  if (tree->right!=NULL && tree->right!=tree->left)
					freetree(tree->right, freenames);
  if (freenames && tree->name != NULL) free(tree->name);
  free(tree);
}

#ifndef WRITESYMS

// 我们记录上次加载的函数的 id。
// 以及下面数组中的最高索引
static int lastFuncid= -1;
static int hiFuncid;

// 我们还保留一个 AST 节点偏移量数组，代表
// AST 文件中的函数
long *Funcoffset;

// 给定一个 AST 节点 id，从 AST 文件加载该 AST 节点。
// 如果设置了 nextfunc，则找到下一个是函数的 AST 节点。
// 分配并返回节点，如果找不到则返回 NULL。
struct ASTnode *loadASTnode(int id, int nextfunc) {
  long offset, idxoff;
  struct ASTnode *node;

  // 如果没有东西要做，就什么都不做
  if (id==0 && nextfunc==0) return(NULL);

#ifdef DEBUG
  fprintf(stderr, "loadASTnode id %d nextfunc %d\n", id, nextfunc);

  if (id < 0)
    fatal("negative id in loadASTnode()");
#endif

  // 确定节点的偏移量。
  // 使用函数偏移量数组，或者
  // 否则使用 AST 索引文件
  if (nextfunc==1) {
    lastFuncid++;
    if (lastFuncid > hiFuncid)
      return(NULL);
    offset= Funcoffset[lastFuncid];
  } else {
    idxoff= id * sizeof(long);
    fseek(Idxfile, idxoff, SEEK_SET);
    fread(&offset, sizeof(long), 1, Idxfile);
  }

  // 分配一个节点
  node= (struct ASTnode *)malloc(sizeof(struct ASTnode));
  if (node==NULL)
    fatal("Cannot malloc an AST node in loadASTnode");

  // 从 AST 文件读取节点。如果遇到 EOF 则放弃
  fseek(Infile, offset, SEEK_SET);
  if (fread(node, sizeof(struct ASTnode), 1, Infile)!=1) {
    free(node); return(NULL);
  }

#ifdef DEBUG
  // 检查我们加载的节点是我们想要的节点
  if (id!=0 && id!=node->nodeid)
    fprintf(stderr, "Wanted AST node id %d, got %d\n", id, node->nodeid);
#endif

  // 如果有字符串/标识符字面量，获取它
  if (node->name!=NULL) {
    fgetstr(Text, TEXTLEN + 1, Infile);
    node->name= strdup(Text);
    if (node->name==NULL)
      fatal("Unable to malloc string literal in deserialiseAST()");

#ifndef DETREE
    // 如果这不是字符串字面量
    // 搜索实际的符号并链接它
    if (node->op != A_STRLIT) {
      node->sym= findSymbol(NULL, 0, node->symid);
      if (node->sym==NULL)
        fatald("Can't find symbol with id", node->symid);
    }
#endif
  }

  // 将指针设置为 NULL 以防止误用！
  node->left= node->mid= node->right= NULL;

#ifndef DETREE
  // 如果这是一个函数，设置全局的
  // Functionid 并为其创建一个 endlabel。
  // 也更新 lastFuncnode。
  if (node->op== A_FUNCTION) {
    Functionid= node->sym;
    Functionid->st_endlabel= genlabel();
  }
#endif

  // 返回我们找到的节点
#ifdef DEBUG
  fprintf(stderr, "Found AST node id %d\n", node->nodeid);
#endif
  return(node);
}

// 使用打开的 AST 文件和新创建的
// 索引文件，为 AST 文件中的每个 AST 节点
// 构建一个 AST 文件偏移量列表。
void mkASTidxfile(void) {
  struct ASTnode *node;
  long offset, idxoff;

  // 分配一个节点和至少一些 Funcoffset 区域
  node= (struct ASTnode *)malloc(sizeof(struct ASTnode));
  Funcoffset= (long *)malloc(sizeof(long));
  if (node==NULL || Funcoffset==NULL)
    fatal("Cannot malloc an AST node in loadASTnode");

  while (1) {
    // 获取当前偏移量
    offset = ftell(Infile);
#ifdef DEBUG
    if (sizeof(long)==4)
      fprintf(stderr, "A offset %ld sizeof ASTnode %d\n", offset,
			sizeof(struct ASTnode));
    else
      fprintf(stderr, "A offset %ld sizeof ASTnode %ld\n", offset,
			sizeof(struct ASTnode));
#endif

    // 读取下一个节点，如果没有则停止
    if (fread(node, sizeof(struct ASTnode), 1, Infile)!=1) {
      break;
    }
#ifdef DEBUG
    fprintf(stderr, "Node %d at offset %ld\n", node->nodeid, offset);
    fprintf(stderr, "Node %d left %d mid %d right %d\n", node->nodeid,
        node->leftid, node->midid, node->rightid);
#endif

    // 如果有字符串/标识符字面量，获取它
    if (node->name!=NULL) {
      fgetstr(Text, TEXTLEN + 1, Infile);
#ifdef DEBUG
    fprintf(stderr, "  name %s\n", Text);
#endif
    }
    
    // 将节点的偏移量保存在文件中其索引位置。
    idxoff= node->nodeid * sizeof(long);

    fseek(Idxfile, idxoff, SEEK_SET);
    fwrite(&offset, sizeof(long), 1, Idxfile);

    // 如果此节点是函数，增加
    // 函数索引数组的大小并保存偏移量
    if (node->op==A_FUNCTION) {
      lastFuncid++;
      Funcoffset= (long *)realloc(Funcoffset, sizeof(long)* (lastFuncid+1));
      Funcoffset[lastFuncid]= offset;
    }
  }

  // 在我们开始使用数组之前重置
  hiFuncid= lastFuncid; lastFuncid= -1;
  free(node);
}
#endif // WRITESYMS