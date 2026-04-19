#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include "dirs.h"
#include "wcc.h"

// 编译器设置和顶层执行
// Copyright (c) 2024 Warren Toomey, GPL3

// 阶段列表
#define CPP_PHASE	0
#define TOK_PHASE	1
#define PARSE_PHASE	2
#define GEN_PHASE	3
#define QBEPEEP_PHASE	4	// 要么运行 QBE，要么运行窥孔优化器
#define ASM_PHASE	5
#define LINK_PHASE	6

// 一个用于保存文件名链表的结构
struct filelist {
  char *name;
  struct filelist *next;
};

#define CPU_QBE		1
#define CPU_6809	2

// 全局变量
int cpu = CPU_QBE;		// 我们针对的 CPU/平台
int last_phase = LINK_PHASE;	// 最后一个阶段
int verbose = 0;		// 是否打印阶段详情？
int keep_tempfiles = 0;		// 是否保留临时文件？
char *outname = NULL;		// 输出文件名，如果有的话
char *initname;			// 给我们的文件名

				// 命令和目标文件列表
char **phasecmd;
char **cppflags;
char **preobjs;
char **postobjs;

				// -D 预处理器词添加到这里
#define MAXCPPEXTRA 20
char *cppextra[20];
int cppxindex=0;

				// 临时文件和目标文件列表
struct filelist *Tmphead, *Tmptail;
struct filelist *Objhead, *Objtail;

#define MAXCMDARGS 500
char *cmdarg[MAXCMDARGS];	// 命令参数列表
int cmdcount = 0;		// 命令参数数量

// 改变初始文件名的最后一个字母
char *alter_suffix(char ch) {
  char *str = strdup(initname);
  char *cptr = str + strlen(str) - 1;
  *cptr = ch;
  return (str);
}

// 将名称添加到临时文件列表
void addtmpname(char *name) {
  struct filelist *this;
  this = (struct filelist *) malloc(sizeof(struct filelist));
  this->name = name;
  this->next = NULL;
  if (Tmphead == NULL) Tmphead = Tmptail = this;
  else { Tmptail->next = this; Tmptail = this; }
}

// 删除临时文件并退出
void Exit(int val) {
  struct filelist *this;

  if (keep_tempfiles == 0)
    for (this = Tmphead; this != NULL; this = this->next)
      unlink(this->name);
  exit(val);
}

// 将名称添加到目标文件列表
void addobjname(char *name) {
  struct filelist *this;
  this = (struct filelist *) malloc(sizeof(struct filelist));
  this->name = name;
  this->next = NULL;
  if (Objhead == NULL) Objhead = Objtail = this;
  else { Objtail->next = this; Objtail = this; }
}

// 清除命令参数列表
void clear_cmdarg(void) {
  cmdcount = 0;
}

// 将参数添加到命令参数列表
void add_cmdarg(char *str) {
  if (cmdcount == MAXCMDARGS) {
    fprintf(stderr, "Out of space in cmdargs\n");
    Exit(1);
  }
  cmdarg[cmdcount++] = str;
}

// 如果字符串以 '.' 结尾
// 然后是给定字符则返回 1，否则返回 0
int endswith(char *str, char ch) {
  int len = strlen(str);
  if (len < 2) return (0);
  if (str[len - 1] != ch) return (0);
  if (str[len - 2] != '.') return (0);
  return (1);
}

// 给定文件名，打开它进行写入或退出
FILE *fopenw(char *filename) {
  FILE *f = fopen(filename, "w");
  if (f == NULL) {
    fprintf(stderr, "Unable to write to file %s\n", filename);
    Exit(1);
  }
  return (f);
}

// 给定文件名和所需的后缀，
// 返回可以写入的临时文件名，
// 如果无法创建临时文件则返回 NULL
char *newtempfile(char *origname, char *suffix) {
  char *name;
  FILE *handle;

  // 第一次尝试：只需将后缀添加到原始名称
  name = (char *) malloc(strlen(origname) + strlen(suffix) + 1);
  if (name != NULL) {
    strcpy(name, origname);
    strcat(name, suffix);

    // 现在尝试打开它
    handle = fopen(name, "w");
    if (handle != NULL) { fclose(handle); addtmpname(name); return (name); }
  }

  // 那个文件名不行。尝试在 TMPDIR 中创建一个
  name = (char *) malloc(strlen(TMPDIR) + strlen(suffix) + 20);
  if (name == NULL)
    return (NULL);
  sprintf(name, "%s/%s_XXXXXX", TMPDIR, suffix);

  // 现在尝试打开它
  handle = fopenw(name);
  if (handle != NULL) { fclose(handle); addtmpname(name); return (name); }
  return (NULL);
}

// 运行带有 cmdarg[] 中参数的命令。
// 根据需要通过打开 in/out 来替换 stdin/stdout。
// 如果命令没有 Exit(0)，则停止。
void run_command(char *in, char *out) {
  int i, pid, wstatus;
  FILE *fh;

  if (verbose) {
    fprintf(stderr, "Doing: ");
    for (i = 0; cmdarg[i] != NULL; i++) fprintf(stderr, "%s ", cmdarg[i]);
    fprintf(stderr, "\n");
    if (in != NULL) fprintf(stderr, "  redirecting stdin from %s\n", in);
    if (out != NULL) fprintf(stderr, "  redirecting stdout to %s\n", out);
  }

  pid = fork();
  switch (pid) {
  case -1:
    fprintf(stderr, "fork failed\n");
    Exit(1);

    // 子进程
  case 0:
    // 根据需要关闭 stdin/stdout
    if (in != NULL) {
      fh = freopen(in, "r", stdin);
      if (fh == NULL) {
	fprintf(stderr, "Unable to freopen %s for reading\n", in);
	Exit(1);
      }
    }

    if (out != NULL) {
      fh = freopen(out, "w", stdout);
      if (fh == NULL) {
	fprintf(stderr, "Unable to freopen %s for writing\n", out);
	Exit(1);
      }
    }

    execvp(cmdarg[0], cmdarg);
    fprintf(stderr, "exec %s failed\n", cmdarg[0]);

    // 父进程：等待子进程干净地退出
  default:
    if (waitpid(pid, &wstatus, 0) == -1) {
      fprintf(stderr, "waitpid failed\n");
      Exit(1);
    }

    // 获取子进程的退出状态，如果退出状态非零则让父进程退出(1)
    if (WIFEXITED(wstatus)) {
      if (WEXITSTATUS(wstatus) != 0) Exit(1);
    } else {
      fprintf(stderr, "child phase didn't exit\n"); Exit(1);
    }

    // 子阶段成功
    return;
  }
}

// 使用 C 预处理器预处理文件
char *do_preprocess(char *name) {
  int i;
  char *tempname;

  // 构建命令
  clear_cmdarg();
  add_cmdarg(phasecmd[CPP_PHASE]);
  for (i = 0; cppflags[i] != NULL; i++)
    add_cmdarg(cppflags[i]);
  for (i = 0; i < cppxindex; i++) {
    add_cmdarg("-D");
    add_cmdarg(cppextra[i]);
  }
  add_cmdarg(name);
  add_cmdarg(NULL);

  // 如果这是最后一个阶段，使用 outname
  // 作为输出文件，如果是 NULL 则使用 stdout。
  if (last_phase == CPP_PHASE) {
    run_command(NULL, outname);
    Exit(0);
  }

  // 不是最后一个阶段，创建一个临时文件
  tempname = newtempfile(initname, "_cpp");
  run_command(NULL, tempname);
  return (tempname);
}

// 汇编给定的文件名
char *do_assemble(char *name) {
  char *tempname;

  // 如果这是最后一个阶段，如果 outname 不为 NULL 则使用它，
  // 否则更改原始文件的后缀
  if (last_phase == ASM_PHASE) {
    if (outname == NULL)
      outname = alter_suffix('o');
    tempname = outname;
  } else {
    // 不是最后一个阶段，创建一个临时文件名
    tempname = newtempfile(initname, "_o");
  }

  // 构建并运行汇编器命令
  clear_cmdarg();
  add_cmdarg(phasecmd[ASM_PHASE]);
  add_cmdarg("-o");
  add_cmdarg(tempname);
  add_cmdarg(name);
  add_cmdarg(NULL);
  run_command(NULL, NULL);

  // 如果我们是最后一个阶段，现在停止
  if (last_phase == ASM_PHASE)
    Exit(0);

  return (tempname);
}

// 运行多个编译器阶段以将预处理后的 C 文件转换为汇编文件
char *do_compile(char *name) {
  char *tokname, *symname, *astname;
  char *idxname, *qbename, *asmname;

  // 我们需要运行扫描器、解析器和代码生成器。
  // 为扫描器的输出获取一个临时文件名。
  tokname = newtempfile(initname, "_tok");

  // 构建并运行扫描器命令
  clear_cmdarg();
  add_cmdarg(phasecmd[TOK_PHASE]);
  add_cmdarg(NULL);
  run_command(name, tokname);

  // 为解析器的输出获取临时文件名
  symname = newtempfile(initname, "_sym");
  astname = newtempfile(initname, "_ast");

  // 构建并运行解析器命令
  clear_cmdarg();
  add_cmdarg(phasecmd[PARSE_PHASE]);
  add_cmdarg(symname);
  add_cmdarg(astname);
  add_cmdarg(NULL);
  run_command(tokname, NULL);

  // 即使我们不使用，也要获取一些临时文件名。
  idxname = newtempfile(initname, "_idx");
  qbename = newtempfile(initname, "_qbe");
  asmname = newtempfile(initname, "_s");

  // 如果这个阶段（编译到汇编）是最后一个，
  // 如果 outname 不为 NULL 则使用它，
  // 否则更改原始文件的后缀。
  if (last_phase == GEN_PHASE) {
    if (outname == NULL)
      outname = alter_suffix('s');
    asmname = outname;
  }

  // 在运行代码生成器之前，检查下一个阶段（QBE 或窥孔）是否存在。
  // 如果不存在，我们直接进入汇编代码，所以更改输出文件的名称
  if (phasecmd[QBEPEEP_PHASE] == NULL) {
    qbename = asmname;
  }

  // 构建并运行代码生成器命令
  clear_cmdarg();
  add_cmdarg(phasecmd[GEN_PHASE]);
  add_cmdarg(symname);
  add_cmdarg(astname);
  add_cmdarg(idxname);
  add_cmdarg(NULL);
  run_command(NULL, qbename);

  // 如果需要，构建并运行 QBE 命令或窥孔优化器
  if (phasecmd[QBEPEEP_PHASE] != NULL) {
    clear_cmdarg();
    add_cmdarg(phasecmd[QBEPEEP_PHASE]);
    if (cpu== CPU_QBE) {
      add_cmdarg("-o");
      add_cmdarg(asmname);
      add_cmdarg(qbename);
    }
    if (cpu== CPU_6809) {
      add_cmdarg("-o");
      add_cmdarg(asmname);
      add_cmdarg(qbename);
      add_cmdarg(LIB6809DIR "/rules.6809");
    }
    add_cmdarg(NULL);
    run_command(NULL, NULL);
  }

  // 如果我们是最后一个阶段，现在停止
  if (last_phase == GEN_PHASE)
    Exit(0);

  return (asmname);
}

// 链接最终的可执行文件和所有目标文件
void do_link(void) {
  int i;
  struct filelist *this;

  // 构建命令
  clear_cmdarg();
  add_cmdarg(phasecmd[LINK_PHASE]);
  add_cmdarg("-o");
  add_cmdarg(outname);

  // 插入必须首先出现的任何文件
  for (i = 0; preobjs[i] != NULL; i++)
    add_cmdarg(preobjs[i]);

  // 现在添加所有目标文件名和库名
  for (this = Objhead; this != NULL; this = this->next)
    add_cmdarg(this->name);

  // 插入必须最后出现的任何文件
  for (i = 0; postobjs[i] != NULL; i++)
    add_cmdarg(postobjs[i]);
  add_cmdarg(NULL);
  run_command(NULL, NULL);
}

// 给定 CPU/平台名称，更改阶段程序和目标文件
void set_phaseprograms(char *cpuname) {
  if (!strcmp(cpuname, "qbe")) {
    phasecmd = qbephasecmd;
    cppflags = qbecppflags;
    preobjs = qbepreobjs;
    postobjs = qbepostobjs;
    cpu= CPU_QBE;
    return;
  }
  if (!strcmp(cpuname, "6809")) {
    phasecmd = phasecmd6809;
    cppflags = cppflags6809;
    preobjs = preobjs6809;
    postobjs = postobjs6809;
    cpu= CPU_6809;
    return;
  }
  fprintf(stderr, "Unknown CPU/patform: %s\n", cpuname);
  Exit(1);
}


// 如果启动不正确则打印用法
static void usage(char *prog) {
  fprintf(stderr, "Usage: %s [-vcESX] [-D ...] [-m CPU] [-o outfile] file [file ...]\n",
	  prog);
  fprintf(stderr,
	  "       -v give verbose output of the compilation stages\n");
  fprintf(stderr, "       -c generate object files but don't link them\n");
  fprintf(stderr, "       -E pre-process the file, output on stdout\n");
  fprintf(stderr, "       -S generate assembly files but don't link them\n");
  fprintf(stderr, "       -X keep temporary files for debugging\n");
  fprintf(stderr, "       -D ..., set a pre-processor define\n");
  fprintf(stderr, "       -m CPU, set the CPU e.g. -m 6809, -m qbe\n");
  fprintf(stderr, "       -o outfile, produce the outfile executable file\n");
  Exit(1);
}


// 主程序：检查参数，或者如果没有参数则打印用法

int main(int argc, char **argv) {
  int i, opt;

  phasecmd = qbephasecmd;
  cppflags = qbecppflags;
  preobjs = qbepreobjs;
  postobjs = qbepostobjs;

  // 获取选项
  if (argc < 2)
    usage(argv[0]);
  while ((opt = getopt(argc, argv, "vcESXo:m:D:")) != -1) {
    switch (opt) {
    case 'v': verbose = 1; break;
    case 'c': last_phase = ASM_PHASE; break;
    case 'E': last_phase = CPP_PHASE; break;
    case 'S': last_phase = GEN_PHASE; break;
    case 'X': keep_tempfiles = 1; break;
    case 'm': set_phaseprograms(optarg); break;
    case 'o': outname = optarg; break;
    case 'D': if (cppxindex >= MAXCPPEXTRA) {
		fprintf(stderr, "Too many -D arguments\n"); Exit(1);
	      }
	      cppextra[cppxindex]= optarg; cppxindex++; break;
    }
  }

  // 现在处理参数后面的文件名
  if (optind >= argc) usage(argv[0]);
  for (i = optind; i < argc; i++) {
    initname = argv[i];

    if (endswith(argv[i], 'c')) {
      // C 源文件，执行所有主要阶段
      addobjname(do_assemble(do_compile(do_preprocess(argv[i]))));
    } else if (endswith(argv[i], 's')) {
      // 汇编文件，只需汇编
      addobjname(do_assemble(argv[i]));
    } else if (endswith(argv[i], 'o')) {
      // 将目标文件添加到列表
      addobjname(argv[i]);
    } else {
      fprintf(stderr, "Input file with unrecognised suffix: %s\n", argv[i]);
      usage(argv[0]);
    }
  }

  // 现在将所有目标文件链接在一起
  if (outname == NULL) outname = AOUT;
  do_link();

  Exit(0);
  return (0);
}