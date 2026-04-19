#include "defs.h"
#define extern_
#include "data.h"
#undef extern_
#include "decl.h"
#include "gen.h"
#include "misc.h"
#include "sym.h"
#include "tree.h"

// C 解析器前端。
// Copyright (c) 2023 Warren Toomey, GPL3

#ifdef DEBUG
void print_token(struct token *t) {
    switch (t->token) {
    case T_INTLIT:
    case T_CHARLIT:
      printf("%02X: %d\n", t->token, t->intvalue);
      break;
    case T_STRLIT:
      printf("%02X: \"%s\"\n", t->token, Text);
      break;
    case T_FILENAME:
      printf("%02X: filename \"%s\"\n", t->token, Text);
      break;
    case T_LINENUM:
      printf("%02X: linenum %d\n", t->token, Text);
      break;
    case T_IDENT:
      printf("%02X: %s\n", t->token, Text);
      break;
    default:
      printf("%02X: %s\n", t->token, Tstring[t->token]);
    }
}
#endif


// 扫描并返回在输入中找到的下一个词元。
// 如果词元有效则返回 1，如果没有更多词元则返回 0。
int scan(struct token *t) {
  int intvalue;

  // 如果我们有向前查看的词元，返回此词元
  if (Peektoken.token != 0) {
    t->token = Peektoken.token;
    t->tokstr = Peektoken.tokstr;
    t->intvalue = Peektoken.intvalue;
    Peektoken.token = 0;
#ifdef DEBUG
    print_token(t);
#endif
    return (1);
  }

  // 我们循环是因为我们不想返回
  // T_FILENAME 或 T_LINENUM 词元
  while (1) {
    t->token = fgetc(stdin);
    if (t->token == EOF) {
      t->token = T_EOF;
      break;
    }

    switch (t->token) {
    case T_LINENUM:
      fread(&Line, sizeof(int), 1, stdin);
      continue;
    case T_FILENAME:
      if (Infilename!=NULL) free(Infilename);
      fgetstr(Text, TEXTLEN + 1, stdin);
      Infilename= strdup(Text);
      continue;
    case T_INTLIT:
    case T_CHARLIT:
      fread(&intvalue, sizeof(int), 1, stdin);
      t->intvalue = intvalue;
      break;
    case T_STRLIT:
    case T_IDENT:
      fgetstr(Text, TEXTLEN + 1, stdin);
      break;
    }
#ifdef DEBUG
    print_token(t);
#endif
    return (1);
  }
  return(0);
}

// 确保当前词元是 t，
// 并获取下一个词元。否则
// 抛出错误
void match(int t, char *what) {
  if (Token.token == t) {
    scan(&Token);
  } else {
    fatals("Expected", what);
  }
}

// 匹配分号并获取下一个词元
void semi(void) {
  match(T_SEMI, ";");
}

// 匹配左花括号并获取下一个词元
void lbrace(void) {
  match(T_LBRACE, "{");
}

// 匹配右花括号并获取下一个词元
void rbrace(void) {
  match(T_RBRACE, "}");
}

// 匹配左括号并获取下一个词元
void lparen(void) {
  match(T_LPAREN, "(");
}

// 匹配右括号并获取下一个词元
void rparen(void) {
  match(T_RPAREN, ")");
}

// 匹配标识符并获取下一个词元
void ident(void) {
  match(T_IDENT, "identifier");
}

// 匹配逗号并获取下一个词元
void comma(void) {
  match(T_COMMA, "comma");
}

// 将 AST 序列化到 Outfile
void serialiseAST(struct ASTnode *tree) {
  if (tree==NULL) return;

  // 转储此节点
  fwrite(tree, sizeof(struct ASTnode), 1, Outfile);

  // 转储任何字面量字符串/标识符
  if (tree->name!=NULL) {
    fputs(tree->name, Outfile);
    fputc(0, Outfile);
  }

  // 转储所有子节点
  serialiseAST(tree->left);
  serialiseAST(tree->mid);
  serialiseAST(tree->right);
}

// 从 stdin 解析词元流
// 并输出序列化的 AST 和符号表。
int main(int argc, char **argv) {

  if (argc <2 || argc >3) {
    fprintf(stderr, "Usage: %s symfile <astfile>\n", argv[0]);
    fprintf(stderr, "  如果未指定 astfile，则 AST 输出到 stdout\n");
    exit(1);
  }

  if (argc==3) {
    Outfile= fopen(argv[2], "w");
    if (Outfile == NULL) {
      fprintf(stderr, "Can't create %s\n", argv[2]); exit(1);
    }
  } else
    Outfile= stdout;

  Symfile= fopen(argv[1], "w+");
  if (Symfile == NULL) {
    fprintf(stderr, "Can't create %s\n", argv[1]); exit(1);
  }

  freeSymtable();		// 清除符号表
  scan(&Token);                 // 从输入获取第一个词元
  Peektoken.token = 0;		// 并设置没有向前查看的词元
  global_declarations();        // 解析全局声明
  flushSymtable();		// 刷新任何残留符号
  fclose(Symfile);
  exit(0);
  return(0);
}