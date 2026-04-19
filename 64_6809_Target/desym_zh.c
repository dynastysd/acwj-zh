#include "defs.h"
#define extern_
#include "data.h"
#undef extern_

// 符号表反序列化器
// Copyright (c) 2024 Warren Toomey, GPL3

// 从 f FILE 中最多读取 count-1 个字符
// 并将它们存储在 s 缓冲区中。
// 用 NUL 终止 s 缓冲区。
// 如果无法读取或遇到 EOF 则返回 NULL。
// 否则，返回原始的 s 指针指针。
char *fgetstr(char *s, size_t count, FILE * f) {
  size_t i = count;
  size_t err;
  char ch;
  char *ret = s;

  while (i-- != 0) {
    err= fread(&ch, 1, 1, f);
    if (err!=1) {
      if (s == ret)
        return(NULL);
      break;
    }
    *s++ = ch;
    if (ch == 0)
      break;
  }
  *s = 0;
  return(ferror(f) ? (char *) NULL : ret);
}

// 读取一个符号。返回 -1 如果没有。
int deserialiseSym(struct symtable *sym, FILE *in) {
  struct symtable *memb, *last;

  // 从 in 读取一个符号结构
  if (fread(sym, sizeof(struct symtable), 1, in)!=1)
    return(-1);

  // 如果类型是 P_NONE，跳过此符号并读取另一个。
  // 这标志着 AST 树之间的分隔
  if (sym->type==P_NONE) {
    // 调试：printf("Skipping a P_NONE symbol\n");
    if (fread(sym, sizeof(struct symtable), 1, in)!=1)
      return(-1);
  }

  // 获取符号名称
  if (sym->name != NULL) {
    fgetstr(Text, TEXTLEN + 1, in);
    sym->name= strdup(Text);
  }

  // 获取任何初始值
  if (sym->initlist != NULL) {
    sym->initlist= (int *)malloc(sym->nelems* sizeof(int));
    fread(sym->initlist, sizeof(int), sym->nelems, in);
  }

  // 如果有任何成员，读取它们
  if (sym->member != NULL) {
    sym->member= last= NULL;

    while (1) {
      // 创建一个空的符号结构
      memb= (struct symtable *)malloc(sizeof(struct symtable));

      // 从 in 文件读取结构
      // 如果文件中没有符号则停止
      if (deserialiseSym(memb, in)== -1) return(0);

      // 将其附加到头部或最后一个
      if (sym->member==NULL) {
        sym->member= last= memb;
      } else {
	last->next= memb;
        last= memb;
      }

      // 如果没有下一个成员则停止
      if (memb->next == NULL) return(0);
    }
  }

  // 目前将 ctype 设置为 NULL
  sym->ctype=NULL;

  return(0);
}

void dumptable(struct symtable *head, int indent);

// 转储单个符号
void dumpsym(struct symtable *sym, int indent) {
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
      printf("struct ");
      break;
    case P_UNION:
      printf("union ");
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
    case S_STRUCT:
      printf(": struct");
      break;
    case S_UNION:
      printf(": union");
      break;
    case S_ENUMTYPE:
      printf(": enum");
      break;
    case S_ENUMVAL:
      printf(": enumval");
      break;
    case S_TYPEDEF:
      printf(": typedef");
      break;
    case S_STRLIT:
      printf(": strlit");
      break;
    default:
      printf(" unknown stype");
  }

  printf(" id %d", sym->id);

  switch (sym->class) {
    case V_GLOBAL:
      printf(": global");
      break;
    case V_LOCAL:
      printf(": local offset %d", sym->st_posn);
      break;
    case V_PARAM:
      printf(": param offset %d", sym->st_posn);
      break;
    case V_EXTERN:
      printf(": extern");
      break;
    case V_STATIC:
      printf(": static");
      break;
    case V_MEMBER:
      printf(": member");
      break;
    default:
      printf(": unknown class");
  }

  if (sym->st_hasaddr!=0)
    printf(", hasaddr ");

  switch (sym->stype) {
    case S_VARIABLE:
      printf(", size %d", sym->size);
      break;
    case S_FUNCTION:
      printf(", %d params", sym->nelems);
      break;
    case S_ARRAY:
      printf(", %d elems, size %d", sym->nelems, sym->size);
      break;
  }

  printf(", ctypeid %d, nelems %d st_posn %d\n",
	sym->ctypeid, sym->nelems, sym->st_posn);

  if (sym->initlist != NULL) {
    printf("  initlist: ");
    for (i=0; i< sym->nelems; i++)
      printf("%d ", sym->initlist[i]);
    printf("\n");
  }

  
  if (sym->member != NULL)
    dumptable(sym->member, 4);
}

// 转储一个符号表
void dumptable(struct symtable *head, int indent) {
  struct symtable *sym;

  for (sym = head; sym != NULL; sym = sym->next)
    dumpsym(sym, indent);
}

int main(int argc, char **argv) {
  FILE *in;
  struct symtable sym;

  if (argc !=2) {
    fprintf(stderr, "Usage: %s symbolfile\n", argv[0]); exit(1);
  }

  in= fopen(argv[1], "r");
  if (in==NULL) {
    fprintf(stderr, "Unable to open %s\n", argv[1]); exit(1);
  }

  while (1) {
    if (deserialiseSym(&sym, in)== -1) break;
    dumpsym(&sym, 0);
  }
  exit(0);
  return(0);
}