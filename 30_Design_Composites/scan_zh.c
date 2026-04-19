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

  if (Putback) {		// 如果有字符被放回，则使用该字符
    c = Putback;		// 这个被放回的字符
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

// 跳过不需要处理的输入，
// 即空白符、换行符。返回第一个
// 需要处理的字符。
static int skip(void) {
  int c;

  c = next();
  while (' ' == c || '\t' == c || '\n' == c || '\r' == c || '\f' == c) {
    c = next();
  }
  return (c);
}

// 从字符
// 或字符串字面量返回下一个字符
static int scanch(void) {
  int c;

  // 获取下一个输入字符并解释
  // 以反斜杠开头的元字符
  c = next();
  if (c == '\\') {
    switch (c = next()) {
      case 'a':
	return '\a';
      case 'b':
	return '\b';
      case 'f':
	return '\f';
      case 'n':
	return '\n';
      case 'r':
	return '\r';
      case 't':
	return '\t';
      case 'v':
	return '\v';
      case '\\':
	return '\\';
      case '"':
	return '"';
      case '\'':
	return '\'';
      default:
	fatalc("unknown escape sequence", c);
    }
  }
  return (c);			// 只是一个普通字符！
}

// 扫描并返回输入文件中的整数
// 字面量值。
static int scanint(int c) {
  int k, val = 0;

  // 将每个字符转换为整数值
  while ((k = chrpos("0123456789", c)) >= 0) {
    val = val * 10 + k;
    c = next();
  }

  // 遇到非整数字符，将其放回
  putback(c);
  return (val);
}

// 从输入文件扫描字符串字面量，
// 并存储到 buf[] 中。返回
// 字符串的长度。
static int scanstr(char *buf) {
  int i, c;

  // 当缓冲区空间足够时循环
  for (i = 0; i < TEXTLEN - 1; i++) {
    // 获取下一个字符并追加到 buf
    // 当遇到结尾双引号时返回
    if ((c = scanch()) == '"') {
      buf[i] = 0;
      return (i);
    }
    buf[i] = c;
  }
  // 缓冲区空间耗尽
  fatal("String literal too long");
  return (0);
}

// 从输入文件扫描标识符并
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
// 关键字标记号，如果不是关键字则返回 0。
// 根据第一个字母进行切换，这样就不必
//浪费时间对所有关键字进行 strcmp() 比较。
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

// 被拒绝的标记的指针
static struct token *Rejtoken = NULL;

// 拒绝我们刚刚扫描到的标记
void reject_token(struct token *t) {
  if (Rejtoken != NULL)
    fatal("Can't reject token twice");
  Rejtoken = t;
}

// 扫描并返回在输入中找到的下一个标记。
// 如果标记有效则返回 1，如果没有剩余标记则返回 0。
int scan(struct token *t) {
  int c, tokentype;

  // 如果有任何被拒绝的标记，则返回它
  if (Rejtoken != NULL) {
    t = Rejtoken;
    Rejtoken = NULL;
    return (1);
  }
  // 跳过空白符
  c = skip();

  // 根据
  // 输入字符确定标记类型
  switch (c) {
    case EOF:
      t->token = T_EOF;
      return (0);
    case '+':
      if ((c = next()) == '+') {
	t->token = T_INC;
      } else {
	putback(c);
	t->token = T_PLUS;
      }
      break;
    case '-':
      if ((c = next()) == '-') {
	t->token = T_DEC;
      } else {
	putback(c);
	t->token = T_MINUS;
      }
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
    case '[':
      t->token = T_LBRACKET;
      break;
    case ']':
      t->token = T_RBRACKET;
      break;
    case '~':
      t->token = T_INVERT;
      break;
    case '^':
      t->token = T_XOR;
      break;
    case ',':
      t->token = T_COMMA;
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
	putback(c);
	t->token = T_LOGNOT;
      }
      break;
    case '<':
      if ((c = next()) == '=') {
	t->token = T_LE;
      } else if (c == '<') {
	t->token = T_LSHIFT;
      } else {
	putback(c);
	t->token = T_LT;
      }
      break;
    case '>':
      if ((c = next()) == '=') {
	t->token = T_GE;
      } else if (c == '>') {
	t->token = T_RSHIFT;
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
    case '|':
      if ((c = next()) == '|') {
	t->token = T_LOGOR;
      } else {
	putback(c);
	t->token = T_OR;
      }
      break;
    case '\'':
      // 如果是引号，扫描
      // 字面量字符值和
      // 尾部引号
      t->intvalue = scanch();
      t->token = T_INTLIT;
      if (next() != '\'')
	fatal("Expected '\\'' at end of char literal");
      break;
    case '"':
      // 扫描字面量字符串
      scanstr(Text);
      t->token = T_STRLIT;
      break;
    default:
      // 如果是数字，扫描
      // 字面量整数值
      if (isdigit(c)) {
	t->intvalue = scanint(c);
	t->token = T_INTLIT;
	break;
      } else if (isalpha(c) || '_' == c) {
	// 读取关键字或标识符
	scanident(c, Text, TEXTLEN);

	// 如果是已识别的关键字，返回该标记
	if ((tokentype = keyword(Text)) != 0) {
	  t->token = tokentype;
	  break;
	}
	// 不是已识别的关键字，所以必须是标识符
	t->token = T_IDENT;
	break;
      }
      // 该字符不属于任何已识别的标记，报错
      fatalc("Unrecognised character", c);
  }

  // 找到了一个标记
  return (1);
}