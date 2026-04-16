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

// 从输入文件获取下一个字符
static int next(void) {
  int c;

  if (Putback) {		// 如果有放回的字符
    c = Putback;		// 就使用它
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
// 即空白字符、换行符。返回我们
// 需要处理的第一个字符。
static int skip(void) {
  int c;

  c = next();
  while (' ' == c || '\t' == c || '\n' == c || '\r' == c || '\f' == c) {
    c = next();
  }
  return (c);
}

// 从输入文件中扫描并返回一个整数
// 字面量值
static int scanint(int c) {
  int k, val = 0;

  // 将每个字符转换为一个整数值
  while ((k = chrpos("0123456789", c)) >= 0) {
    val = val * 10 + k;
    c = next();
  }

  // 遇到非数字字符，放回去
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
  // 遇到无效字符，放回去。
  // NUL 终止 buf[] 并返回长度
  putback(c);
  buf[i] = '\0';
  return (i);
}

// 根据输入的单词返回匹配的
// 关键字词法单元号，如果不是关键字则返回 0。
// 根据首字母进行 switch 以避免
// 与所有关键字做 strcmp() 浪费时间。
static int keyword(char *s) {
  switch (*s) {
    case 'c':
      if (!strcmp(s, "char"))
	return (T_CHAR);
      break;
    case 'e':
      if (!strcmp(s, "else"))
	return (T_ELSE);
      break;
    case 'f':
      if (!strcmp(s, "for"))
	return (T_FOR);
      break;
    case 'i':
      if (!strcmp(s, "if"))
	return (T_IF);
      if (!strcmp(s, "int"))
	return (T_INT);
      break;
    case 'l':
      if (!strcmp(s, "long"))
	return (T_LONG);
      break;
    case 'p':
      if (!strcmp(s, "print"))
	return (T_PRINT);
      break;
    case 'r':
      if (!strcmp(s, "return"))
	return (T_RETURN);
      break;
    case 'w':
      if (!strcmp(s, "while"))
	return (T_WHILE);
      break;
    case 'v':
      if (!strcmp(s, "void"))
	return (T_VOID);
      break;
  }
  return (0);
}

// 指向被拒绝的词法单元的指针
static struct token *Rejtoken = NULL;

// 拒绝我们刚刚扫描到的词法单元
void reject_token(struct token *t) {
  if (Rejtoken != NULL)
    fatal("Can't reject token twice");
  Rejtoken = t;
}

// 扫描并返回在输入中找到的下一个词法单元。
// 如果词法单元有效则返回 1，如果没有剩余词法单元则返回 0。
int scan(struct token *t) {
  int c, tokentype;

  // 如果有任何被拒绝的词法单元，返回它
  if (Rejtoken != NULL) {
    t = Rejtoken;
    Rejtoken = NULL;
    return (1);
  }
  // 跳过空白字符
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
    case '{':
      t->token = T_LBRACE;
      break;
    case '}':
      t->token = T_RBRACE;
      break;
    case '(':
      t->token = T_LPAREN;
      break;
    case ')':
      t->token = T_RPAREN;
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
    case '&':
      if ((c = next()) == '&') {
	t->token = T_LOGAND;
      } else {
	putback(c);
	t->token = T_AMPER;
      }
      break;
    default:

      // 如果是数字，扫描
      // 整数字面量值
      if (isdigit(c)) {
	t->intvalue = scanint(c);
	t->token = T_INTLIT;
	break;
      } else if (isalpha(c) || '_' == c) {
	// 读取关键字或标识符
	scanident(c, Text, TEXTLEN);

	// 如果是已识别的关键字，返回该词法单元
	if ((tokentype = keyword(Text)) != 0) {
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

  // 找到了一个词法单元
  return (1);
}