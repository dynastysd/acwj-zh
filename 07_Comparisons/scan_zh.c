#include "defs.h"
#include "data.h"
#include "decl.h"

// 词法扫描
// Copyright (c) 2019 Warren Toomey, GPL3

// 返回字符 c 在字符串 s 中的位置，
// 如果未找到则返回 -1
static int chrpos(char *s, int c) {
  char *p;

  p = strchr(s, c);
  return (p ? p - s : -1);
}

// 从输入文件获取下一个字符。
static int next(void) {
  int c;

  if (Putback) {		// 如果有退回的字符，
    c = Putback;		// 则使用该字符
    Putback = 0;
    return (c);
  }

  c = fgetc(Infile);		// 从输入文件读取
  if ('\n' == c)
    Line++;			// 增加行计数
  return (c);
}

// 放回一个不需要的字符
static void putback(int c) {
  Putback = c;
}

// 跳过我们不需要处理的输入，
// 即空白符、换行符等。返回第一个
// 我们需要处理的字符。
static int skip(void) {
  int c;

  c = next();
  while (' ' == c || '\t' == c || '\n' == c || '\r' == c || '\f' == c) {
    c = next();
  }
  return (c);
}

// 从输入文件扫描并返回一个整数字面量
// 的值。
static int scanint(int c) {
  int k, val = 0;

  // 将每个字符转换为整数值
  while ((k = chrpos("0123456789", c)) >= 0) {
    val = val * 10 + k;
    c = next();
  }

  // 遇到非整数字符，将其放回。
  putback(c);
  return (val);
}

// 从输入文件扫描一个标识符并
// 存储到 buf[] 中。返回标识符的长度
static int scanident(int c, char *buf, int lim) {
  int i = 0;

  // 允许数字、字母和下划线
  while (isalpha(c) || isdigit(c) || '_' == c) {
    // 如果达到标识符长度限制则报错，
    // 否则追加到 buf[] 并获取下一个字符
    if (lim - 1 == i) {
      fatal("Identifier too long");
    } else if (i < lim - 1) {
      buf[i++] = c;
    }
    c = next();
  }
  // 遇到无效字符，将其放回。
  // NUL 终止 buf[] 并返回长度
  putback(c);
  buf[i] = '\0';
  return (i);
}

// 根据输入的单词，返回匹配的
// 关键字词法单元编号，如果不是关键字则返回 0。
// 通过首字母进行 switch 以避免
// 与所有关键字进行 strcmp() 比较浪费时间。
static int keyword(char *s) {
  switch (*s) {
  case 'i':
    if (!strcmp(s, "int"))
      return (T_INT);
    break;
  case 'p':
    if (!strcmp(s, "print"))
      return (T_PRINT);
    break;
  }
  return (0);
}

// 扫描并返回在输入中找到的下一个词法单元。
// 如果词法单元有效则返回 1，如果没有剩余词法单元则返回 0。
int scan(struct token *t) {
  int c, tokentype;

  // 跳过空白符
  c = skip();

  // 根据
  // 输入字符确定词法单元类型
  switch (c) {
  case EOF:
    t->token = T_EOF;
    return (0);
  case '+':
    t->token = T_PLUS;
    break;
  case '-':
    t->token = T_MINUS;
    break;
  case '*':
    t->token = T_STAR;
    break;
  case '/':
    t->token = T_SLASH;
    break;
  case ';':
    t->token = T_SEMI;
    break;
  case '=':
    if ((c = next()) == '=') {
      t->token = T_EQ;
    } else {
      putback(c);
      t->token = T_ASSIGN;
    }
    break;
  case '!':
    if ((c = next()) == '=') {
      t->token = T_NE;
    } else {
      fatalc("Unrecognised character", c);
    }
    break;
  case '<':
    if ((c = next()) == '=') {
      t->token = T_LE;
    } else {
      putback(c);
      t->token = T_LT;
    }
    break;
  case '>':
    if ((c = next()) == '=') {
      t->token = T_GE;
    } else {
      putback(c);
      t->token = T_GT;
    }
    break;
  default:

    // 如果是数字，则扫描
    // 字面整数值
    if (isdigit(c)) {
      t->intvalue = scanint(c);
      t->token = T_INTLIT;
      break;
    } else if (isalpha(c) || '_' == c) {
      // 读取关键字或标识符
      scanident(c, Text, TEXTLEN);

      // 如果是已识别的关键字，则返回该词法单元
      if (tokentype = keyword(Text)) {
	t->token = tokentype;
	break;
      }
      // 不是已识别的关键字，所以必须是标识符
      t->token = T_IDENT;
      break;
    }
    // 该字符不属于任何已识别的词法单元，报错
    fatalc("Unrecognised character", c);
  }

  // 找到一个词法单元
  return (1);
}