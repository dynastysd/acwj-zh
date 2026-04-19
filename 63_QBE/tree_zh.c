#include "defs.h"
#include "data.h"
#include "decl.h"

// AST 树函数
// Copyright (c) 2019 Warren Toomey, GPL3

// 构建并返回一个通用 AST 节点
struct ASTnode *mkastnode(int op, int type,
			  struct symtable *ctype,
			  struct ASTnode *left,
			  struct symtable *mid,
			  struct ASTnode *right,
			  struct symtable *sym, int intvalue) {
  struct ASTnode *n;

  // Malloc 一个新的 ASTnode
  n = (struct ASTnode *) malloc(sizeof(struct ASTnode));
  if (n == NULL)
    fatal("Unable to malloc in mkastnode()");

  // 复制字段值并返回它
  n->op = op;
  n->type = type;
  n->ctype = ctype;
  n->left = left;
  n->mid = mid;
  n->right = right;
  n->sym = sym;
  n->a_intvalue = intvalue;
  n->linenum = 0;
  return (n);
}


// 制作一个 AST 叶子节点
struct ASTnode *mkastleaf(int op, int type,
			  struct symtable *ctype,
			  struct symtable *sym, int intvalue) {
  return (mkastnode(op, type, ctype, NULL, NULL, NULL, sym, intvalue));
}

// 制作一个一元 AST 节点：只有一个子节点
struct ASTnode *mkastunary(int op, int type,
			   struct symtable *ctype,
			   struct ASTnode *left,
			   struct symtable *sym, int intvalue) {
  return (mkastnode(op, type, ctype, left, NULL, NULL, sym, intvalue));
}

// 生成并返回一个新的标签号
// 仅用于 AST 转储目的
static int dumpid = 1;
static int gendumplabel(void) {
  return (dumpid++);
}

// AST 节点名称列表
static char *astname[] = { NULL,
  "ASSIGN", "ASPLUS", "ASMINUS", "ASSTAR",
  "ASSLASH", "ASMOD", "TERNARY", "LOGOR",
  "LOGAND", "OR", "XOR", "AND", "EQ", "NE", "LT",
  "GT", "LE", "GE", "LSHIFT", "RSHIFT",
  "ADD", "SUBTRACT", "MULTIPLY", "DIVIDE", "MOD",
  "INTLIT", "STRLIT", "IDENT", "GLUE",
  "IF", "WHILE", "FUNCTION", "WIDEN", "RETURN",
  "FUNCCALL", "DEREF", "ADDR", "SCALE",
  "PREINC", "PREDEC", "POSTINC", "POSTDEC",
  "NEGATE", "INVERT", "LOGNOT", "TOBOOL", "BREAK",
  "CONTINUE", "SWITCH", "CASE", "DEFAULT", "CAST"
};

// 给定一个 AST 树，打印它并遵循
// genAST() 遵循的树遍历
void dumpAST(struct ASTnode *n, int label, int level) {
  int Lfalse, Lstart, Lend;
  int i;

  if (n == NULL)
    fatal("NULL AST node");
  if (n->op > A_CAST)
    fatald("Unknown dumpAST operator", n->op);

  // 特别处理 IF 和 WHILE 语句
  switch (n->op) {
    case A_IF:
      Lfalse = gendumplabel();
      for (i = 0; i < level; i++)
	fprintf(stdout, " ");
      fprintf(stdout, "IF");
      if (n->right) {
	Lend = gendumplabel();
	fprintf(stdout, ", end L%d", Lend);
      }
      fprintf(stdout, "\n");
      dumpAST(n->left, Lfalse, level + 2);
      dumpAST(n->mid, NOLABEL, level + 2);
      if (n->right)
	dumpAST(n->right, NOLABEL, level + 2);
      return;
    case A_WHILE:
      Lstart = gendumplabel();
      for (i = 0; i < level; i++)
	fprintf(stdout, " ");
      fprintf(stdout, "WHILE, start L%d\n", Lstart);
      Lend = gendumplabel();
      dumpAST(n->left, Lend, level + 2);
      if (n->right)
	dumpAST(n->right, NOLABEL, level + 2);
      return;
  }

  // 对于 A_GLUE 节点将级别重置为 -2
  if (n->op == A_GLUE) {
    level -= 2;
  } else {

    // 通用 AST 节点处理
    for (i = 0; i < level; i++)
      fprintf(stdout, " ");
    fprintf(stdout, "%s", astname[n->op]);
    switch (n->op) {
      case A_FUNCTION:
      case A_FUNCCALL:
      case A_ADDR:
      case A_PREINC:
      case A_PREDEC:
	if (n->sym != NULL)
	  fprintf(stdout, " %s", n->sym->name);
	break;
      case A_INTLIT:
	fprintf(stdout, " %d", n->a_intvalue);
	break;
      case A_STRLIT:
	fprintf(stdout, " rval label L%d", n->a_intvalue);
	break;
      case A_IDENT:
	if (n->rvalue)
	  fprintf(stdout, " rval %s", n->sym->name);
	else
	  fprintf(stdout, " %s", n->sym->name);
	break;
      case A_DEREF:
	if (n->rvalue)
	  fprintf(stdout, " rval");
	break;
      case A_SCALE:
	fprintf(stdout, " %d", n->a_size);
	break;
      case A_CASE:
	fprintf(stdout, " %d", n->a_intvalue);
	break;
      case A_CAST:
	fprintf(stdout, " %d", n->type);
	break;
    }
    fprintf(stdout, "\n");
  }

  // 通用 AST 节点处理
  if (n->left)
    dumpAST(n->left, NOLABEL, level + 2);
  if (n->mid)
    dumpAST(n->mid, NOLABEL, level + 2);
  if (n->right)
    dumpAST(n->right, NOLABEL, level + 2);
}