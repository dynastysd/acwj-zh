#include "defs.h"
#include "misc.h"

// 词元扫描
// Copyright (c) 2019 Warren Toomey, GPL3

int Line = 1;                   // 当前行号
int Newlinenum=0;		// 标志：行号是否已更改
int Linestart = 1;              // 如果在行首则为真
int Putback = '\n';             // 扫描器放回的字符
char *Infilename;               // 我们正在解析的文件名
int Newfilename=0;		// 标志：文件名是否已更改
FILE *Infile;                   // 输入文件结构
struct token Token;             // 上一个扫描的词元
struct token Peektoken;         // 一个向前查看的词元
char Text[TEXTLEN + 1];         // 上一个扫描的标识符

int scan(struct token *t, int nocpp);

// 返回字符 c 在字符串 s 中的位置，
// 如果未找到 c 则返回 -1
static int chrpos(char *s, int c) {
  int i;
  for (i = 0; s[i] != '\0'; i++)
    if (s[i] == (char) c)
      return (i);
  return (-1);
}

// 从输入文件获取下一个字符。
static int next(void) {
  int c, l;

  if (Putback) {		// 如果有放回的字符，则使用该字符
    c = Putback;		// 放回
    Putback = 0;
    return (c);
  }

  c = fgetc(Infile);		// 从输入文件读取

  while (Linestart && c == '#') {	// 我们遇到了预处理器语句
    Linestart = 0;		// 不再在行首
    scan(&Token, 1);		// 将行号读入 l
    if (Token.token != T_INTLIT)
      fatals("Expecting pre-processor line number, got:", Text);
    l = Token.intvalue;

    scan(&Token, 1);		// 将文件名读入 Text
    if (Token.token != T_STRLIT)
      fatals("Expecting pre-processor file name, got:", Text);

    if (Text[0] != '<') {	// 如果这是真实的文件名
      if (strcmp(Text, Infilename)) {	// 而且不是我们当前的文件
	free(Infilename);
	Infilename = strdup(Text);	// 保存它。然后更新行号
        Newfilename=1;
      }
      Line = l;
      Newlinenum=1;
    }

    while ((c = fgetc(Infile)) != '\n');	// 跳到行尾
    c = fgetc(Infile);		// 并获取下一个字符
    Linestart = 1;		// 现在回到行首
  }

  Linestart = 0;		// 不再在行首
  if ('\n' == c) {
    Line++;			// 增加行计数
    Newlinenum=1;
    Linestart = 1;		// 现在回到行首
  }
  return (c);
}

// 放回一个不需要的字符
static void putback(int c) {
  Putback = c;
}

// 跳过我们不需要处理的输入，
// 即空白字符、换行符。返回第一个
// 我们需要处理的字符。
static int skip(void) {
  int c;

  c = next();
  while (' ' == c || '\t' == c || '\n' == c || '\r' == c || '\f' == c) {
    c = next();
  }
  return (c);
}

// 从输入中读取十六进制常量
static int hexchar(void) {
  int c, h, n = 0, f = 0;

  // 循环获取字符
  while (isxdigit(c = next())) {
    // 从字符转换为整数值
    h = chrpos("0123456789abcdef", tolower(c));

    // 添加到运行的十六进制值
    n = n * 16 + h;
    f = 1;
  }

  // 我们遇到了非十六进制字符，放回它
  putback(c);

  // 标志告诉我们从未见过任何十六进制字符
  if (!f)
    fatal("missing digits after '\\x'");
  if (n > 255)
    fatal("value out of range after '\\x'");

  return (n);
}

// 从字符或字符串字面量返回下一个字符。
// 如果此字符前面有反斜杠，则返回它是否被引用
static int scanch(int *slash) {
  int i, c, c2;
  *slash=0;

  // 获取下一个输入字符并解释
  // 以反斜杠开头的元字符
  c = next();
  if (c == '\\') {
    *slash=1;
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

	// 通过读取字符来处理八进制常量，
	// 直到遇到非八进制数字。
	// 在 c2 中构建八进制值并计算
	// # 位数在 i 中。最多允许 3 个八进制数字。
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
	return (hexchar());
      default:
        fatalc("unknown escape sequence", c);
    }
  }
  return (c);			// 只是普通的字符！
}

// 从输入文件扫描并返回整数字面量值。
static int scanint(int c) {
  int k, val = 0, radix = 10;

  // 假设 radix 是 10，但如果以 0 开头
  if (c == '0') {
    // 且下一个字符是 'x'，则是 radix 16
    if ((c = next()) == 'x') {
      radix = 16;
      c = next();
    } else
      // 否则，是 radix 8
      radix = 8;

  }
  // 将每个字符转换为一个整数值
  while ((k = chrpos("0123456789abcdef", tolower(c))) >= 0) {
    if (k >= radix)
      fatalc("invalid digit in integer literal", c);
    val = val * radix + k;
    c = next();
  }

  // 我们遇到了非整数字符，放回它。
  putback(c);
  return (val);
}

// 从输入文件扫描字符串字面量，
// 并将其存储在 buf[] 中。返回字符串的长度。
static int scanstr(char *buf) {
  int i, c;
  int slash;

  // 当我们有足够的缓冲区空间时循环
  for (i = 0; i < TEXTLEN - 1; i++) {
    // 获取下一个字符并附加到 buf
    // 当遇到结束双引号时返回
    //（该引号前面没有反斜杠）
    c = scanch(&slash);
    if (c == '"' && slash==0) {
      buf[i] = 0;
      return (i);
    }
    buf[i] = (char) c;
  }

  // 缓冲区空间耗尽
  fatal("String literal too long");
  return (0);
}

// 从输入文件扫描标识符并
// 将其存储在 buf[] 中。返回标识符的长度
static int scanident(int c, char *buf, int lim) {
  int i = 0;

  // 允许数字、字母和下划线
  while (isalpha(c) || isdigit(c) || '_' == c) {
    // 如果遇到标识符长度限制则出错，
    // 否则附加到 buf[] 并获取下一个字符
    if (lim - 1 == i) {
      fatal("Identifier too long");
    } else if (i < lim - 1) {
      buf[i++] = (char) c;
    }
    c = next();
  }

  // 我们遇到了非有效字符，放回它。
  // NUL 终止 buf[] 并返回长度
  putback(c);
  buf[i] = '\0';
  return (i);
}

// 给定输入中的一个词，返回匹配的
// 关键字词元号；如果不是关键字则返回 0。
// 切换第一个字母，这样我们就不必浪费时间
// 与所有关键字进行 strcmp()。
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
      if (!strcmp(s, "void")) {
	return (T_VOID);
}
      break;
    case 'w':
      if (!strcmp(s, "while"))
	return (T_WHILE);
      break;
  }
  return (0);
}

#ifdef DEBUG
// 词元字符串列表，用于调试目的
char *Tstring[] = {
  "EOF", "=", "+=", "-=", "*=", "/=", "%=",
  "?", "||", "&&", "|", "^", "&",
  "==", "!=", "<", ">", "<=", ">=", "<<", ">>",
  "+", "-", "*", "/", "%", "++", "--", "~", "!",
  "void", "char", "int", "long",
  "if", "else", "while", "for", "return",
  "struct", "union", "enum", "typedef",
  "extern", "break", "continue", "switch",
  "case", "default", "sizeof", "static",
  "intlit", "strlit", ";", "identifier",
  "{", "}", "(", ")", "[", "]", ",", ".",
  "->", ":", "...", "charlit", "filename", "linenum"
};
#endif

// 扫描并返回在输入中找到的下一个词元。
// 如果词元有效则返回 1，如果没有更多词元则返回 0。
// 如果 nocpp，则在新文件名或行号时不返回。
// 这是因为我们在解析新文件名和行号时使用 scan()
int scan(struct token *t, int nocpp) {
  int c, tokentype;
  int slash;

  // 跳过空白字符
  c = skip();

  if (nocpp==0) {
    // 如果文件名已更改，返回文件名
    if (Newfilename) {
      t->token = T_FILENAME;
      Newfilename=0;
      putback(c);
      return (1);
    }

    // 如果行号已更改，返回行号
    if (Newlinenum) {
      t->intvalue = Line;
      t->token = T_LINENUM;
      Newlinenum=0;
      putback(c);
      return (1);
    }
  }

  // 如果我们有向前查看的词元，返回此词元
  if (Peektoken.token != 0) {
    t->token = Peektoken.token;
    t->intvalue = Peektoken.intvalue;
    Peektoken.token = 0;
    return (1);
  }

  // 根据输入字符确定词元
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
    case '%':
      if ((c = next()) == '=') {
	t->token = T_ASMOD;
      } else {
	putback(c);
	t->token = T_MOD;
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
      if ((c = next()) == '.') {
        t->token = T_ELLIPSIS;
        if ((c = next()) != '.')
	  fatal("Expected '...', only got '..'\n");
      } else {
	putback(c);
        t->token = T_DOT;
      }
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
      // 尾随引号
      t->intvalue = scanch(&slash);
      t->token = T_CHARLIT;
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

	// 如果是已识别的关键字，返回该词元
	if ((tokentype = keyword(Text)) != 0) {
	  t->token = tokentype;
	  break;
	}
	// 不是已识别的关键字，所以它必须是标识符
	t->token = T_IDENT;
	break;
      }
      // 该字符不属于任何已识别的词元，出错
      fatalc("Unrecognised character", c);
  }

  // 我们找到了一个词元
  return (1);
}

// 从 stdin 读取代码行并输出词元流
int main() {
  int i;

  Infile= stdin;
  Infilename = strdup("");	// Cpp 还没有告诉我们文件名
  Peektoken.token = 0;          // 设置没有向前查看的词元
  scan(&Token, 0);              // 从输入获取第一个词元

  // 循环获取更多词元
  while (Token.token != T_EOF) {

    // 向标准输出发送词元的二进制流。
    // T_INTLIT 词元后面跟着 n 字节的字面量值。
    // T_STRLIT 和 T_IDENT 词元后面跟着 NUL 终止的字符串。
    fputc(Token.token, stdout);
    switch (Token.token) {
    case T_INTLIT:
    case T_CHARLIT:
      i= Token.intvalue;
      fwrite(&i, sizeof(int), 1, stdout);
      // fprintf(stderr, "%02X: %d\n", Token.token, Token.intvalue);
      break;
    case T_STRLIT:
      fputs(Text, stdout);
      fputc(0, stdout);
      // fprintf(stderr, "%02X: \"%s\"\n", Token.token, Text);
      break;
    case T_IDENT:
      fputs(Text, stdout);
      fputc(0, stdout);
      // fprintf(stderr, "%02X: %s\n", Token.token, Text);
      break;
    case T_FILENAME:
      fputs(Infilename, stdout);
      fputc(0, stdout);
      // fprintf(stderr, "%02X: %s\n", Token.token, Infilename);
      break;
    case T_LINENUM:
      fwrite(&Line, sizeof(int), 1, stdout);
      // fprintf(stderr, "%02X: %d\n", Token.token, Line);
      break;
    default:
      // fprintf(stderr, "%02X: %s\n", Token.token, Tstring[Token.token]);
    }
    scan(&Token, 0);
  }

  exit(0);
  return(0);
}