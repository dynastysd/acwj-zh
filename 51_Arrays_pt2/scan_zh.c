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
  int c, l;

  if (Putback) {		// 如果有退回的字符
    c = Putback;		// 就使用它
    Putback = 0;
    return (c);
  }

  c = fgetc(Infile);		// 从输入文件读取

  while (c == '#') {		// 遇到预处理器语句
    scan(&Token);		// 获取行号到 l
    if (Token.token != T_INTLIT)
      fatals("Expecting pre-processor line number, got:", Text);
    l = Token.intvalue;

    scan(&Token);		// 获取文件名到 Text
    if (Token.token != T_STRLIT)
      fatals("Expecting pre-processor file name, got:", Text);

    if (Text[0] != '<') {	// 如果是真实的文件名
      if (strcmp(Text, Infilename))	// 而且不是当前文件
	Infilename = strdup(Text);	// 保存它。然后更新行号
      Line = l;
    }

    while ((c = fgetc(Infile)) != '\n');	// 跳过到行尾
    c = fgetc(Infile);		// 获取下一个字符
  }

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

// 从输入读取十六进制常量
static int hexchar(void) {
  int c, h, n = 0, f = 0;

  // 循环获取字符
  while (isxdigit(c = next())) {
    // 从字符转换为整数值
    h = chrpos("0123456789abcdef", tolower(c));
    // 加到运行的十六进制值上
    n = n * 16 + h;
    f = 1;
  }
  // 遇到非十六进制字符，放回
  putback(c);
  // 标志告诉我们从未看到任何十六进制字符
  if (!f)
    fatal("missing digits after '\\x'");
  if (n > 255)
    fatal("value out of range after '\\x'");
  return n;
}

// 从字符或字符串字面量返回下一个字符
static int scanch(void) {
  int i, c, c2;

  // 获取下一个输入字符并解释
  // 以反斜杠开头的元字符
  c = next();
  if (c == '\\') {
    switch (c = next()) {
      case 'a':
	return ('\a');
      case 'b':
	return ('\b');
      case 'f':
	return ('\f');
      case 'n':
	return ('\n');
      case 'r':
	return ('\r');
      case 't':
	return ('\t');
      case 'v':
	return ('\v');
      case '\\':
	return ('\\');
      case '"':
	return ('"');
      case '\'':
	return ('\'');
	// 通过读取字符直到遇到非八进制数字来处理八进制常量。
	// 在 c2 中构建八进制值并计算
	// # 位数。只允许3个八进制数字。
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
	for (i = c2 = 0; isdigit(c) && c < '8'; c = next()) {
	  if (++i > 3)
	    break;
	  c2 = c2 * 8 + (c - '0');
	}
	putback(c);		// 放回第一个非八进制字符
	return (c2);
      case 'x':
	return hexchar();
      default:
	fatalc("unknown escape sequence", c);
    }
  }
  return (c);			// 只是一个普通字符!
}

// 扫描并返回输入文件中的整数字面量值
static int scanint(int c) {
  int k, val = 0, radix = 10;

  // 假设基数是10，但如果以0开头
  if (c == '0') {
    // 且下一个字符是 'x'，则是十六进制
    if ((c = next()) == 'x') {
      radix = 16;
      c = next();
    } else
      // 否则是八进制
      radix = 8;

  }
  // 将每个字符转换为整数值
  while ((k = chrpos("0123456789abcdef", tolower(c))) >= 0) {
    if (k >= radix)
      fatalc("invalid digit in integer literal", c);
    val = val * radix + k;
    c = next();
  }

  // 遇到非整数字符，放回
  putback(c);
  return (val);
}

// 从输入文件扫描字符串字面量，
// 并存储到 buf[] 中。返回字符串的长度。
static int scanstr(char *buf) {
  int i, c;

  // 当我们有足够的缓冲区空间时循环
  for (i = 0; i < TEXTLEN - 1; i++) {
    // 获取下一个字符并追加到 buf
    // 遇到结尾双引号时返回
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
  // 遇到非有效字符，放回。
  // NUL 终止 buf[] 并返回长度
  putback(c);
  buf[i] = '\0';
  return (i);
}

// 给定输入中的一个词，返回匹配的
// 关键字标记号，如果不是关键字则返回0。
// 切换第一个字母，这样我们就不必
//浪费时间与所有关键字进行 strcmp() 比较。
static int keyword(char *s) {
  switch (*s) {
    case 'b':
      if (!strcmp(s, "break"))
	return (T_BREAK);
      break;
    case 'c':
      if (!strcmp(s, "case"))
	return (T_CASE);
      if (!strcmp(s, "char"))
	return (T_CHAR);
      if (!strcmp(s, "continue"))
	return (T_CONTINUE);
      break;
    case 'd':
      if (!strcmp(s, "default"))
	return (T_DEFAULT);
      break;
    case 'e':
      if (!strcmp(s, "else"))
	return (T_ELSE);
      if (!strcmp(s, "enum"))
	return (T_ENUM);
      if (!strcmp(s, "extern"))
	return (T_EXTERN);
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
    case 's':
      if (!strcmp(s, "sizeof"))
	return (T_SIZEOF);
      if (!strcmp(s, "static"))
	return (T_STATIC);
      if (!strcmp(s, "struct"))
	return (T_STRUCT);
      if (!strcmp(s, "switch"))
	return (T_SWITCH);
      break;
    case 't':
      if (!strcmp(s, "typedef"))
	return (T_TYPEDEF);
      break;
    case 'u':
      if (!strcmp(s, "union"))
	return (T_UNION);
      break;
    case 'v':
      if (!strcmp(s, "void"))
	return (T_VOID);
      break;
    case 'w':
      if (!strcmp(s, "while"))
	return (T_WHILE);
      break;
  }
  return (0);
}

// 标记字符串列表，用于调试目的
char *Tstring[] = {
  "EOF", "=", "+=", "-=", "*=", "/=",
  "?", "||", "&&", "|", "^", "&",
  "==", "!=", ",", ">", "<=", ">=", "<<", ">>",
  "+", "-", "*", "/", "++", "--", "~", "!",
  "void", "char", "int", "long",
  "if", "else", "while", "for", "return",
  "struct", "union", "enum", "typedef",
  "extern", "break", "continue", "switch",
  "case", "default", "sizeof", "static",
  "intlit", "strlit", ";", "identifier",
  "{", "}", "(", ")", "[", "]", ",", ".",
  "->", ":"
};

// 扫描并返回在输入中找到的下一个标记。
// 如果标记有效则返回1，如果没有剩余标记则返回0。
int scan(struct token *t) {
  int c, tokentype;

  // 如果有前瞻标记，返回此标记
  if (Peektoken.token != 0) {
    t->token = Peektoken.token;
    t->tokstr = Peektoken.tokstr;
    t->intvalue = Peektoken.intvalue;
    Peektoken.token = 0;
    return (1);
  }
  // 跳过空白
  c = skip();

  // 根据
  // 输入字符确定标记
  switch (c) {
    case EOF:
      t->token = T_EOF;
      return (0);
    case '+':
      if ((c = next()) == '+') {
	t->token = T_INC;
      } else if (c == '=') {
	t->token = T_ASPLUS;
      } else {
	putback(c);
	t->token = T_PLUS;
      }
      break;
    case '-':
      if ((c = next()) == '-') {
	t->token = T_DEC;
      } else if (c == '>') {
	t->token = T_ARROW;
      } else if (c == '=') {
	t->token = T_ASMINUS;
      } else if (isdigit(c)) {	// 负整数字面量
	t->intvalue = -scanint(c);
	t->token = T_INTLIT;
      } else {
	putback(c);
	t->token = T_MINUS;
      }
      break;
    case '*':
      if ((c = next()) == '=') {
	t->token = T_ASSTAR;
      } else {
	putback(c);
	t->token = T_STAR;
      }
      break;
    case '/':
      if ((c = next()) == '=') {
	t->token = T_ASSLASH;
      } else {
	putback(c);
	t->token = T_SLASH;
      }
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
    case '.':
      t->token = T_DOT;
      break;
    case ':':
      t->token = T_COLON;
      break;
    case '?':
      t->token = T_QUESTION;
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

	// 如果是识别的关键字，返回该标记
	if ((tokentype = keyword(Text)) != 0) {
	  t->token = tokentype;
	  break;
	}
	// 不是识别的关键字，所以必须是标识符
	t->token = T_IDENT;
	break;
      }
      // 该字符不属于任何识别的标记，报错
      fatalc("Unrecognised character", c);
  }

  // 找到了一个标记
  t->tokstr = Tstring[t->token];
  return (1);
}