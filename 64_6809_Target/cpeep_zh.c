/* copt version 1.00 (C) Copyright Christopher W. Fraser 1984 */
/* Added out of memory checking and ANSI prototyping. DG 1999 */
/* Added %L - %N variables, %activate, regexp, %check. Zrin Z. 2002 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int rpn_eval(char *expr, char **vars);

#define HSIZE 107
#define MAXLINE 128
#define MAXFIRECOUNT 65535L
#define MAX_PASS 16

int debug = 0;

int global_again = 0; /* 标记规则集是否已更改 */
#define FIRSTLAB 'L'
#define LASTLAB 'N'
int nextlab = 1; /* 唯一标签计数器 */
int labnum[LASTLAB - FIRSTLAB + 1]; /* 唯一标签编号 */

/* 行节点：双向链表中的单行文本 */
struct lnode {
  char *l_text;
  struct lnode *l_prev, *l_next;
};

/* 优化规则节点 */
struct onode {
  struct lnode *o_old, *o_new; /* 匹配模式和替换文本 */
  struct onode *o_next;
  long firecount; /* 规则触发次数限制 */
} *opts, *activerule;

/* 打印从 beg 到 end（不包含）的所有行 */
void printlines(struct lnode *beg, struct lnode *end, FILE *out) {
  struct lnode *p;
  for (p = beg; p != end; p = p->l_next)
    fputs(p->l_text, out);
}

/* 打印完整的优化规则 */
void printrule(struct onode *o, FILE *out) {
  struct lnode *p = o->o_old;
  // 找到链表头部
  while (p->l_prev)
    p = p->l_prev;
  // 打印匹配模式
  printlines(p, NULL, out);
  fputs("=\n", out);
  // 打印替换文本
  printlines(o->o_new, NULL, out);
}

/* error - 报告错误并退出 */
void error(char *s) {
  fputs(s, stderr);
  if (activerule) {
    fputs("active rule:\n", stderr);
    printrule(activerule, stderr);
  }
  exit(1);
}

/* connect - 将 p1 连接到 p2 */
void connect(struct lnode *p1, struct lnode *p2) {
  if (p1 == NULL || p2 == NULL)
    error("connect: can't happen\n");
  p1->l_next = p2;
  p2->l_prev = p1;
}

// 哈希表节点
 static struct hnode {
    char *h_str;
    struct hnode *h_ptr;
} *htab[HSIZE];

/* install - 将字符串安装到字符串表中，返回共享的字符串指针 */
char *install(char *str) {
  char *p1, *p2, *s;
  int i;
  struct hnode *p;

  // 计算字符串的哈希值
  s = str;
  for (i = 0; *s; i += *s++);
  i = abs(i) % HSIZE;

  // 在哈希表中查找已存在的字符串
  for (p = htab[i]; p; p = p->h_ptr)
    for (p1 = str, p2 = p->h_str; *p1++ == *p2++;)
      if (p1[-1] == '\0')
        return (p->h_str);

  // 未找到，分配新节点
  p = (struct hnode *) malloc(sizeof(struct hnode));
  if (p == NULL)
    error("install 1: out of memory\n");

  p->h_str = (char *) malloc((s - str) + 1);
  if (p->h_str == NULL)
    error("install 2: out of memory\n");

  strcpy(p->h_str, str);
  p->h_ptr = htab[i];
  htab[i] = p;
  return (p->h_str);
}

/* insert - 在节点 p 之前插入新文本节点 s */
void insert(char *s, struct lnode *p) {
  struct lnode *n;

  n = (struct lnode *) malloc(sizeof(struct lnode));
  if (n == NULL)
    error("insert: out of memory\n");

  n->l_text = s;
  connect(p->l_prev, n);
  connect(n, p);
}

/* getlst - 从 fp 读取行，插入到 p1 和 p2 之间，直到遇到 quit 字符串 */
void getlst(FILE *fp, char *quit, struct lnode *p1, struct lnode *p2) {
  char lin[MAXLINE];

  connect(p1, p2);
  while (fgets(lin, MAXLINE, fp) != NULL && strcmp(lin, quit)) {
    insert(install(lin), p2);
  }
}

/* getlst_1 - 从 fp 读取行，插入到 p1 和 p2 之间
 * 跳过开头的空行和注释 */
void getlst_1(FILE *fp, char *quit, struct lnode *p1, struct lnode *p2) {
  char lin[MAXLINE];
  int firstline = 1;

  connect(p1, p2);
  while (fgets(lin, MAXLINE, fp) != NULL && strcmp(lin, quit)) {
    if (firstline) {
      char *p = lin;
      // 跳过以 # 开头的注释行
      if (lin[0] == '#') continue;
      // 跳过空行
      while (isspace(*p)) ++p;
      if (!*p) continue;
      firstline = 0;
    }

    insert(install(lin), p2);
  }
}

/* init - 读取优化规则文件 */
void init(FILE *fp) {
  struct lnode head, tail;
  struct onode *p, **next;

  next = &opts;
  while (*next)
    next = &((*next)->o_next);
  while (!feof(fp)) {
    p = (struct onode *) malloc((unsigned) sizeof(struct onode));
    if (p == NULL)
      error("init: out of memory\n");
    p->firecount = MAXFIRECOUNT;
    // 读取匹配模式（老规则）
    getlst_1(fp, "=\n", &head, &tail);
    head.l_next->l_prev = NULL;
    if (tail.l_prev)
      tail.l_prev->l_next = NULL;
    p->o_old = tail.l_prev;
    if (p->o_old == NULL) { /* 不创建空规则 */
      free(p);
      continue;
    }
    // 读取替换文本（新规则）
    getlst(fp, "====\n", &head, &tail);
    tail.l_prev->l_next = NULL;
    if (head.l_next)
      head.l_next->l_prev = NULL;
    p->o_new = head.l_next;

    *next = p;
    next = &p->o_next;
  }

  *next = NULL;
}

/* check - 检查规则中的条件
 * 格式: %check min <= %n <= max */
int check(char *pat, char **vars) {
  int low, high, x;
  char v;
  x = sscanf(pat, "%d <= %%%c <= %d", &low, &v, &high);
  if (x != 3 || !('0' <= v && v <= '9')) {
    fprintf(stderr, "warning: invalid use of '%%check' in \"%s\"\n", pat);
    fprintf(stderr, "format is '%%check min <= %%n <= max'\n");
    return 0;
  }

  if (vars[v - '0'] == 0) {
    fprintf(stderr, "error in pattern \"%s\"\n", pat);
    error("variable is not set\n");
  }

  if (sscanf(vars[v - '0'], "%d", &x) != 1)
    return 0;
  return low <= x && x <= high;
}

/* check_eval - 评估表达式并与预期值比较 */
int check_eval(char *pat, char **vars) {
  char expr[1024];
  int expected, x;

  x = sscanf(pat, "%d = %[^\n]s", &expected, expr);
  if (x != 2) {
    fprintf(stderr, "warning: invalid use of '%%check_eval' in \"%s\"\n",
        pat);
    fprintf(stderr, "format is '%%check_eval result = expr");
    return 0;
  }

  return expected == rpn_eval(expr, vars);
}

/* match - 将指令 ins 与模式 pat 匹配，并设置 vars 变量 */
int match(char *ins, char *pat, char **vars) {
  char *p, lin[MAXLINE], *start = pat;

  while (*ins && *pat)
    if (pat[0] == '%') {
      switch (pat[1]) {
      case '%':
        if (*pat != *ins++)
          return 0;
        pat += 2;
        break;
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
        if (pat[2] == '%' && pat[3] != '%') {
          fprintf(stderr, "error in \"%s\": ", start);
          error("input pattern %n% is not allowed\n");
        }

        for (p = lin; *ins && *ins != pat[2];)
          *p++ = *ins++;
        *p = 0;
        p = install(lin);
        if (vars[pat[1] - '0'] == 0)
          vars[pat[1] - '0'] = p;
        else if (vars[pat[1] - '0'] != p)
          return 0;
        pat += 2;
        continue;

      default:
        break;
      }

      if (*pat++ != *ins++)
        return 0;
    } else if (*pat++ != *ins++)
      return 0;

  return *pat == *ins; /* 比较字符串结尾 */
}

/* subst_imp - 返回将变量替换到模式后的结果 */
char *subst_imp(char *pat, char **vars) {
  static char errormsg[80];
  static char lin[MAXLINE];
  char num[30];
  char *s, *start = pat;
  int i;

  i = 0;
  for (;;) {
    // 处理 %% 转义为单个 %
    if (pat[0] == '%' && pat[1] == '%') {
      if (i < MAXLINE) {
        lin[i] = '%';
        ++i;
      }

      pat += 2;
    } else if (pat[0] == '%' && pat[1] >= FIRSTLAB && pat[1] <= LASTLAB) {
      // 处理 %L - %N 标签变量
      int il = pat[1] - FIRSTLAB;
      if (!labnum[il])
        labnum[il] = nextlab++;
      sprintf(num, "%d", labnum[il]);
      for (s = num; i < MAXLINE && (lin[i] = *s++) != 0; ++i);
      pat += 2;
    } else if (pat[0] == '%' && strncmp(pat, "%eval(", 6) == 0) {
      // 处理 %eval() 表达式求值
      char expr[1024];
      int x = 0, r;
      pat += 6;
      while (*pat != ')') {
        expr[x++] = *pat++;
      }

      expr[x] = 0;
      pat++;
      r = rpn_eval(expr, vars);
      sprintf(expr, "%d", r);
      for (s = expr; i < MAXLINE && *s; i++)
        lin[i] = *s++;
    } else if (pat[0] == '%' && isdigit(pat[1])) {
      // 处理 %0 - %9 变量
      if (vars[pat[1] - '0'] == 0) {
        sprintf(errormsg, "error: variable %c is not set in \"%s\"",
            pat[1], start);
        error(errormsg);
      }
      for (s = vars[pat[1] - '0']; i < MAXLINE && (lin[i] = *s++) != 0; i++);
      pat += 2;
    } else if (i >= MAXLINE)
      error("line too long\n");
    else if (!(lin[i++] = *pat++))
      return &lin[0];
  }
}

/* subst - 返回安装(将变量替换到模式后的结果) */
char *subst(char *pat, char **vars) {
  return install(subst_imp(pat, vars));
}

/* rep - 将 p1 和 p2 之间的行替换为替换文本 new */
struct lnode *rep(struct lnode *p1, struct lnode *p2, struct lnode *new,
          char **vars) {
  int i;
  struct lnode *p, *psav;

  // 重置标签编号
  for (i = 0; i < LASTLAB - FIRSTLAB + 1; ++i)
    labnum[i] = 0;

  // 删除旧的行
  for (p = p1->l_next; p != p2; p = psav) {
    psav = p->l_next;
    if (debug)
      fputs(p->l_text, stderr);
    free(p);
  }
  connect(p1, p2);
  if (debug)
    fputs("=\n", stderr);
  // 插入新的行
  for (; new; new = new->l_next) {
    insert(subst(new->l_text, vars), p2);
    if (debug)
      fputs(p2->l_prev->l_text, stderr);
  }
  if (debug)
    putc('\n', stderr);
  return p1->l_next;
}

/* copylist - 复制激活的规则；替换变量 */
struct lnode *copylist(struct lnode *source, struct lnode **pat,
                   struct lnode **sub, char **vars) {
  struct lnode head, tail, *more = NULL;
  int pattern = 1; /* 允许嵌套规则 */
  int i;
  connect(&head, &tail);
  head.l_prev = tail.l_next = NULL;

  // 重置标签编号
  for (i = 0; i < LASTLAB - FIRSTLAB + 1; ++i)
    labnum[i] = 0;

  for (; source; source = source->l_next) {
    if (pattern && strcmp(source->l_text, "=\n") == 0) {
      pattern = 0;
      if (head.l_next == &tail)
        error("error: empty pattern\n");
      *pat = tail.l_prev;
      head.l_next->l_prev = NULL;
      tail.l_prev->l_next = NULL;
      connect(&head, &tail);
      continue;
    }
    if (strcmp(source->l_text, "%activate\n") == 0) {
      if (pattern)
        error("error: %activate in pattern (before '=')\n");
      more = source->l_next;
      break;
    }
    insert(subst(source->l_text, vars), &tail);
  }
  if (head.l_next == &tail)
    *sub = NULL;
  else {
    head.l_next->l_prev = NULL;
    tail.l_prev->l_next = NULL;
    *sub = head.l_next;
  }
  return more;
}

/* opt - 尽可能替换以 r 结尾的指令 */
struct lnode *opt(struct lnode *r) {
  char *vars[10];
  int i, lines;
  struct lnode *c, *p;
  struct onode *o;
  static char *activated = "%activated ";

  for (o = opts; o; o = o->o_next) {
    activerule = o;
    if (o->firecount < 1)
      continue;
    c = r;
    p = o->o_old;
    if (debug) {
      fprintf(stderr, "Trying rule: ");
      printrule(o, stderr);
    }
    if (p == NULL)
      continue; /* 跳过空规则 */
    for (i = 0; i < 10; i++)
      vars[i] = 0;
    lines = 0;
    while (p && c) {
      if (strncmp(p->l_text, "%check", 6) == 0) {
        if (!check(p->l_text + 6, vars))
          break;
      } else if (strncmp(p->l_text, "%eval", 5) == 0) {
        if (!check_eval(p->l_text + 5, vars))
          break;
      } else {
        if (!match(c->l_text, p->l_text, vars))
          break;
        c = c->l_prev;
        ++lines;
      }
      p = p->l_prev;
    }
    if (p != NULL)
      continue;

    /* 减少触发计数 */
    --o->firecount;

    /* 检查 %once */
    if (o->o_new && strcmp(o->o_new->l_text, "%once\n") == 0) {
      struct lnode *tmp = o->o_new; /* 删除 %once 行 */
      o->o_new = o->o_new->l_next;
      o->o_new->l_prev = NULL;
      free(tmp);
      o->firecount = 0; /* 不再触发 */
    }

    /* 检查激活规则 */
    if (o->o_new && strcmp(o->o_new->l_text, "%activate\n") == 0) {
      /* 需要防止规则的重复激活 */
      char signature[300];
      struct lnode *lnp;
      struct onode *nn, *last;
      int skip = 0;
      /* 由于使用 install() 安装字符串，可以比较指针 */
      sprintf(signature, "%s%p%p%p%p%p%p%p%p%p%p\n",
          activated,
          vars[0], vars[1], vars[2], vars[3], vars[4],
          vars[5], vars[6], vars[7], vars[8], vars[9]);
      lnp = o->o_new->l_next;
      while (lnp && strncmp(lnp->l_text, activated, strlen(activated)) == 0) {
        if (strcmp(lnp->l_text, signature) == 0) {
          skip = 1;
          break;
        }
        lnp = lnp->l_next;
      }
      if (!lnp || skip)
        continue;
      insert(install(signature), lnp);

      if (debug) {
        fputs("matched pattern:\n", stderr);
        for (p = o->o_old; p->l_prev; p = p->l_prev);
        printlines(p, NULL, stderr);
        fputs("with:\n", stderr);
        printlines(c->l_next, r->l_next, stderr);
      }
      /* 允许创建多个规则 */
      last = o;
      while (lnp) {
        nn = (struct onode *)
          malloc((unsigned) sizeof(struct onode));
        if (nn == NULL)
          error("activate: out of memory\n");
        nn->o_old = 0, nn->o_new = 0;
        nn->firecount = MAXFIRECOUNT;
        lnp = copylist(lnp, &nn->o_old, &nn->o_new, vars);
        nn->o_next = last->o_next;
        last->o_next = nn;
        last = nn;
        if (debug) {
          fputs("activated rule:\n", stderr);
          printrule(nn, stderr);
        }
      }
      if (debug)
        fputs("\n", stderr);
      /* 后退一步，允许（更短的）激活规则按出现顺序匹配
         */
      while (--lines && r->l_prev)
        r = r->l_prev;
      global_again = 1; /* 标记有更改 */
      continue;
    }

    /* 触发规则 */
    r = rep(c, r->l_next, o->o_new, vars);
    activerule = 0;
    return r;
  }
  activerule = 0;
  return r->l_next;
}

/* #define _TESTING */

void usage(char *name) {
  fprintf(stderr, "Usage: %s [-D] [-o output] input rulesfile\n", name);
  exit(1);
}

/* main - 窥孔优化器 */
int main(int argc, char **argv) {
  FILE *fp, *infile, *outfile = stdout;
  int pass, option;
  struct lnode head, *p, tail;

  opts = NULL;
  activerule = NULL;
  htab[0]= NULL;

  if (argc < 3)
    usage(argv[0]);

  while ((option = getopt(argc, argv, "Do:")) != -1) {
    switch (option) {
    case 'D':
      debug = 1;
      break;
    case 'o':
      outfile = fopen(optarg, "w");
      if (outfile == NULL) {
        fprintf(stderr, "Unable to write to %s\n", optarg);
        exit(1);
      }
      break;
    default:
      usage(argv[0]);
    }
  }

  // 打开输入文件
  if ((infile = fopen(argv[optind], "r")) == NULL) {
    fprintf(stderr, "Can't open input file %s\n", argv[optind]);
    exit(1);
  }
  // 获取模式文件
  if ((fp = fopen(argv[optind + 1], "r")) == NULL) {
    fprintf(stderr, "Can't open patterns file %s\n", argv[optind + 1]);
    exit(1);
  }
  init(fp);

  getlst(infile, "", &head, &tail);

  head.l_text = tail.l_text = "";

  pass = 0;
  do {
    ++pass;
    if (debug)
      fprintf(stderr, "\n--- pass %d ---\n", pass);
    global_again = 0;
    for (p = head.l_next; p != &tail; p = opt(p));
  } while (global_again && pass < MAX_PASS);

  if (global_again) {
    fprintf(stderr, "error: maximum of %d passes exceeded\n", MAX_PASS);
    error("       check for recursive substitutions");
  }

  printlines(head.l_next, &tail, outfile);
  exit(0);
  return 1; /* 让编译器开心 */
}

#define STACKSIZE 20

int sp;
int stack[STACKSIZE];

/* push - 将值压入栈 */
void push(int l) {
  if (sp < STACKSIZE)
    stack[sp++] = l;
  ;
}

/* pop - 从栈顶弹出值 */
int pop(void) {
  if (sp > 0)
    return stack[--sp];
  return 0;
}

/* top - 返回栈顶值但不弹出 */
int top(void) {
  if (sp > 0)
    return stack[sp - 1];
  return 0;
}

/* rpn_eval - 逆波兰表达式求值 */
int rpn_eval(char *expr, char **vars) {
  char *ptr = expr;
  char *endptr;
  int op2;
  int n;

  sp = 0;

  while (*ptr) {
    switch (*ptr++) {
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
      n = strtol(ptr - 1, &endptr, 0);
      if (endptr == ptr - 1) {
        fprintf(stderr, "Optimiser error, cannot parse number: %s\n",
            ptr - 1);
        exit(1);
      }
      ptr = endptr;
      push(n);
      break;
    case '+':
      {
        int a = pop();
        int b = pop();
        int c = a + b;
        push(c);
      }
      break;
    case '*':
      push(pop() * pop());
      break;
    case '-':
      op2 = pop();
      push(pop() - op2);
      break;
    case '|':
      op2 = pop();
      push(pop() | op2);
      break;
    case '&':
      op2 = pop();
      push(pop() & op2);
      break;
    case '>':
      op2 = pop();
      push(pop() >> op2);
      break;
    case '<':
      op2 = pop();
      push(pop() << op2);
      break;
    case '/':
      op2 = pop();
      if (op2 != 0)
        push(pop() / op2);
      else
        return 0; /* 除以零 */
      break;
    case '%':
      if (isdigit(*ptr)) {
        /* 是变量 */
        char v = *ptr++;
        char *endpt2;
        char *val = vars[v - '0'];
        n = strtol(val, &endpt2, 0);
        if (endpt2 == val) {
          fprintf(stderr, "Optimiser error, cannot parse variable: %s\n",
              val);
          exit(1);
        }
        push(n);
      } else if (*ptr++ == '%') {
        op2 = pop();
        if (op2 != 0) {
          push(pop() % op2);
        } else {
          return 0; /* 除以零 */
        }
      }
      break;
    }
  }
  if (sp != 1) {
    int i;
    fprintf(stderr, "Exiting with a stack level of %d\n", sp);
    for (i = 0; i < sp; i++) {
      fprintf(stderr, "Stack level %d -> %d\n", i, stack[i]);
    }
  }
  return top();
}