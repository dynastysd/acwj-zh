#include "defs.h"
#include "data.h"
#include "decl.h"

// 声明解析
// Copyright (c) 2019 Warren Toomey, GPL3

static struct symtable *composite_declaration(int type);
static int typedef_declaration(struct symtable **ctype);
static int type_of_typedef(char *name, struct symtable **ctype);
static void enum_declaration(void);

// 解析当前标记并返回基本类型枚举值，
// 指向任何组合类型的指针，并可能修改
// 类型的类别。
int parse_type(struct symtable **ctype, int *class) {
  int type, exstatic = 1;

  // 查看类别是否已更改为 extern（稍后，static）
  while (exstatic) {
    switch (Token.token) {
      case T_EXTERN:
	*class = C_EXTERN;
	scan(&Token);
	break;
      default:
	exstatic = 0;
    }
  }

  // 现在处理实际类型关键字
  switch (Token.token) {
    case T_VOID:
      type = P_VOID;
      scan(&Token);
      break;
    case T_CHAR:
      type = P_CHAR;
      scan(&Token);
      break;
    case T_INT:
      type = P_INT;
      scan(&Token);
      break;
    case T_LONG:
      type = P_LONG;
      scan(&Token);
      break;

      // 对于以下情况，如果解析后有 ';'，
      // 则没有类型，返回 -1。
      // 示例: struct x {int y; int z};
    case T_STRUCT:
      type = P_STRUCT;
      *ctype = composite_declaration(P_STRUCT);
      if (Token.token == T_SEMI)
	type = -1;
      break;
    case T_UNION:
      type = P_UNION;
      *ctype = composite_declaration(P_UNION);
      if (Token.token == T_SEMI)
	type = -1;
      break;
    case T_ENUM:
      type = P_INT;		// 枚举实际上是 int
      enum_declaration();
      if (Token.token == T_SEMI)
	type = -1;
      break;
    case T_TYPEDEF:
      type = typedef_declaration(ctype);
      if (Token.token == T_SEMI)
	type = -1;
      break;
    case T_IDENT:
      type = type_of_typedef(Text, ctype);
      break;
    default:
      fatals("Illegal type, token", Token.tokstr);
  }
  return (type);
}

// 给定由 parse_type() 解析的类型，扫描任何后续
// '*' 标记并返回新类型
int parse_stars(int type) {

  while (1) {
    if (Token.token != T_STAR)
      break;
    type = pointer_to(type);
    scan(&Token);
  }
  return (type);
}

// 解析强制转换中出现的类型
int parse_cast(void) {
  int type, class;
  struct symtable *ctype;

  // 获取括号内的类型
  type= parse_stars(parse_type(&ctype, &class));

  // 进行一些错误检查。肯定可以做更多
  if (type == P_STRUCT || type == P_UNION || type == P_VOID)
    fatal("Cannot cast to a struct, union or void type");
  return(type);
}

// 给定一个类型，解析字面量表达式并确保
// 这个表达式的类型与给定类型匹配。
// 解析表达式前面的任何类型强制转换。
// 如果是整数字面量，返回此值。
// 如果是字符串字面量，返回字符串的标签号。
int parse_literal(int type) {
  struct ASTnode *tree;

  // 解析表达式并优化生成的 AST 树
  tree= optimise(binexpr(0));

  // 如果有强制转换，获取子节点并
  // 将其标记为具有强制转换的类型
  if (tree->op == A_CAST) {
    tree->left->type= tree->type;
    tree= tree->left;
  }

  // 树现在必须是整数或字符串字面量
  if (tree->op != A_INTLIT && tree->op != A_STRLIT)
    fatal("Cannot initialise globals with a general expression");

  // 如果类型是 char *
  if (type == pointer_to(P_CHAR)) {
    // 我们有字符串字面量，返回标签号
    if (tree->op == A_STRLIT)
      return(tree->a_intvalue);
    // 我们有零整数字面量，所以这是 NULL
    if (tree->op == A_INTLIT && tree->a_intvalue==0)
      return(0);
  }

  // 我们只在这里处理整数字面量。输入类型
  // 是整数类型，足够宽以容纳字面量值
  if (inttype(type) && typesize(type, NULL) >= typesize(tree->type, NULL))
    return(tree->a_intvalue);

  fatal("Type mismatch: literal vs. variable");
  return(0);	// 保持 -Wall 高兴
}

// 给定标量变量的类型、名称和类别，
// 解析任何初始化值并为其分配存储。
// 返回变量的符号表条目。
static struct symtable *scalar_declaration(char *varname, int type,
					   struct symtable *ctype,
					   int class,
					   struct ASTnode **tree) {
  struct symtable *sym=NULL;
  struct ASTnode *varnode, *exprnode;
  *tree= NULL;

  // 将其添加为已知的标量
  switch (class) {
    case C_EXTERN:
    case C_GLOBAL:
      sym= addglob(varname, type, ctype, S_VARIABLE, class, 1, 0);
      break;
    case C_LOCAL:
      sym= addlocl(varname, type, ctype, S_VARIABLE, 1);
      break;
    case C_PARAM:
      sym= addparm(varname, type, ctype, S_VARIABLE);
      break;
    case C_MEMBER:
      sym= addmemb(varname, type, ctype, S_VARIABLE, 1);
      break;
  }

  // 变量正在被初始化
  if (Token.token == T_ASSIGN) {
    // 只可能用于全局或局部
    if (class != C_GLOBAL && class != C_LOCAL)
      fatals("Variable can not be initialised", varname);
    scan(&Token);

    // 全局变量必须被赋予字面量值
    if (class == C_GLOBAL) {
      // 为变量创建一个初始值并解析此值
      sym->initlist= (int *)malloc(sizeof(int));
      sym->initlist[0]= parse_literal(type);
    }
    if (class == C_LOCAL) {
      // 使用变量制作一个 A_IDENT AST 节点
      varnode = mkastleaf(A_IDENT, sym->type, sym, 0);

      // 获取赋值的表达式，设为 rvalue
      exprnode = binexpr(0);
      exprnode->rvalue = 1;

      // 确保表达式的类型与变量匹配
      exprnode = modify_type(exprnode, varnode->type, 0);
      if (exprnode == NULL)
        fatal("Incompatible expression in assignment");

      // 制作赋值 AST 树
      *tree = mkastnode(A_ASSIGN, exprnode->type, exprnode,
					NULL, varnode, NULL, 0);
    }
  }

  // 生成任何全局空间
  if (class == C_GLOBAL)
    genglobsym(sym);

  return (sym);
}

// 给定变量的类型、名称和类别，
// 解析数组的大小（如果有）。然后解析任何初始化
// 值并为其分配存储。
// 返回变量的符号表条目。
static struct symtable *array_declaration(char *varname, int type,
					  struct symtable *ctype, int class) {

  struct symtable *sym;	// 新符号表条目
  int nelems= -1;	// 假设不会给出元素数量
  int maxelems;		// 初始化列表中的最大元素数量
  int *initlist;	// 初始元素列表 
  int i=0, j;

  // 跳过 '['
  scan(&Token);

  // 查看是否有数组大小
  if (Token.token != T_RBRACKET) {
    nelems= parse_literal(P_INT);
    if (nelems <= 0)
      fatald("Array size is illegal", nelems);
  }

  // 确保有后续的 ']'
  match(T_RBRACKET, "]");

  // 将其添加为已知的数组。我们将其视为
  // 指向其元素类型的指针
  switch (class) {
    case C_EXTERN:
    case C_GLOBAL:
      sym = addglob(varname, pointer_to(type), ctype, S_ARRAY, class,
		  0, 0);
      break;
    default:
      fatal("For now, declaration of non-global arrays is not implemented");
  }

  // 数组初始化
  if (Token.token == T_ASSIGN) {
    if (class != C_GLOBAL)
      fatals("Variable can not be initialised", varname);
    scan(&Token);

    // 获取后续的左花括号
    match(T_LBRACE, "{");

#define TABLE_INCREMENT 10

    // 如果数组已经有 nelems，则分配那么多元素
    // 在列表中。否则，从 TABLE_INCREMENT 开始。
    if (nelems != -1)
      maxelems= nelems;
    else
      maxelems= TABLE_INCREMENT;
    initlist= (int *)malloc(maxelems *sizeof(int));

    // 循环从列表中获取新的字面量值
    while (1) {

      // 检查我们是否可以添加下一个值，然后解析并添加它
      if (nelems != -1 && i == maxelems)
        fatal("Too many values in initialisation list");

      initlist[i++]= parse_literal(type);

      // 如果原始大小未设置且已达到当前列表末尾，
      // 则增加列表大小
      if (nelems == -1 && i == maxelems) {
        maxelems += TABLE_INCREMENT;
        initlist= (int *)realloc(initlist, maxelems *sizeof(int));
      }

      // 遇到右花括号时离开
      if (Token.token == T_RBRACE) {
        scan(&Token);
	break;
      }

      // 下一个标记必须是逗号
      comma();
    }

    // 将 initlist 中未使用的元素置零。
    // 将列表附加到符号表条目
    for (j=i; j < sym->nelems; j++) initlist[j]=0;
    if (i > nelems) nelems = i;
    sym->initlist= initlist;
  }

  // 设置数组的大小和元素数量
  sym->nelems= nelems;
  sym->size= sym->nelems * typesize(type, ctype);
  // 生成任何全局空间
  if (class == C_GLOBAL)
    genglobsym(sym);
  return (sym);
}

// 给定指向正在声明的新函数的指针，
// 以及可能指向该函数先前声明的指针，
// 解析参数列表并根据先前的声明进行交叉检查。
// 返回参数数量
static int param_declaration_list(struct symtable *oldfuncsym,
				  struct symtable *newfuncsym) {
  int type, paramcnt = 0;
  struct symtable *ctype;
  struct symtable *protoptr = NULL;
  struct ASTnode *unused;

  // 获取指向第一个原型参数的指针
  if (oldfuncsym != NULL)
    protoptr = oldfuncsym->member;

  // 循环获取任何参数
  while (Token.token != T_RPAREN) {

    // 如果第一个标记是 'void'
    if (Token.token == T_VOID) {
      // 查看下一个标记。如果是 ')'，则函数
      // 没有参数，所以离开循环。
      scan(&Peektoken);
      if (Peektoken.token == T_RPAREN) {
	// 将 Peektoken 移入 Token
        paramcnt= 0; scan(&Token); break;
      }
    }

    // 获取下一个参数的类型
    type = declaration_list(&ctype, C_PARAM, T_COMMA, T_RPAREN, &unused);
    if (type == -1)
      fatal("Bad type in parameter list");

    // 确保此参数的类型与原型匹配
    if (protoptr != NULL) {
      if (type != protoptr->type)
	fatald("Type doesn't match prototype for parameter", paramcnt + 1);
      protoptr = protoptr->next;
    }
    paramcnt++;

    // 遇到右括号时停止
    if (Token.token == T_RPAREN)
      break;
    // 我们需要逗号作为分隔符
    comma();
  }

  if (oldfuncsym != NULL && paramcnt != oldfuncsym->nelems)
    fatals("Parameter count mismatch for function", oldfuncsym->name);

  // 返回参数数量
  return (paramcnt);
}


//
// function_declaration: type identifier '(' parameter_list ')' ;
//      | type identifier '(' parameter_list ')' compound_statement   ;
//
// 解析函数的声明。
static struct symtable *function_declaration(char *funcname, int type,
					     struct symtable *ctype,
					     int class) {
  struct ASTnode *tree, *finalstmt;
  struct symtable *oldfuncsym, *newfuncsym = NULL;
  int endlabel, paramcnt;

  // Text 有标识符的名称。如果存在且是
  // 函数，则获取 id。否则，将 oldfuncsym 设为 NULL。
  if ((oldfuncsym = findsymbol(funcname)) != NULL)
    if (oldfuncsym->stype != S_FUNCTION)
      oldfuncsym = NULL;

  // 如果这是新的函数声明，获取
  // 结束标签的标签 id，并将函数
  // 添加到符号表，
  if (oldfuncsym == NULL) {
    endlabel = genlabel();
    // 假设：函数只返回标量类型，所以下面为 NULL
    newfuncsym =
      addglob(funcname, type, NULL, S_FUNCTION, C_GLOBAL, 0, endlabel);
  }
  // 扫描 '('、任何参数和 ')'。
  // 传入任何现有的函数原型指针
  lparen();
  paramcnt = param_declaration_list(oldfuncsym, newfuncsym);
  rparen();

  // 如果这是新的函数声明，
  // 用参数数量更新函数符号条目。
  // 还将参数列表复制到函数的节点。
  if (newfuncsym) {
    newfuncsym->nelems = paramcnt;
    newfuncsym->member = Parmhead;
    oldfuncsym = newfuncsym;
  }
  // 清除参数列表
  Parmhead = Parmtail = NULL;

  // 声明以分号结尾，只是原型。
  if (Token.token == T_SEMI)
    return (oldfuncsym);

  // 这不仅仅是一个原型。
  // 将 Functionid 全局设置为函数的符号指针
  Functionid = oldfuncsym;

  // 获取复合语句的 AST 树并标记
  // 我们尚未解析循环或 switch
  Looplevel = 0;
  Switchlevel = 0;
  lbrace();
  tree = compound_statement(0);
  rbrace();

  // 如果函数类型不是 P_VOID ..
  if (type != P_VOID) {

    // 如果函数中没有语句则报错
    if (tree == NULL)
      fatal("No statements in function with non-void type");

    // 检查复合语句中最后一个 AST 操作是否是 return 语句
    finalstmt = (tree->op == A_GLUE) ? tree->right : tree;
    if (finalstmt == NULL || finalstmt->op != A_RETURN)
      fatal("No return for function with non-void type");
  }
  // 构建 A_FUNCTION 节点，其中包含函数的符号指针
  // 和复合语句子树
  tree = mkastunary(A_FUNCTION, type, tree, oldfuncsym, endlabel);

  // 对 AST 树进行优化
  tree= optimise(tree);

  // 如果请求则转储 AST 树
  if (O_dumpAST) {
    dumpAST(tree, NOLABEL, 0);
    fprintf(stdout, "\n\n");
  }
  // 为其生成汇编代码
  genAST(tree, NOLABEL, NOLABEL, NOLABEL, 0);

  // 现在释放与
  // 此函数关联的符号
  freeloclsyms();
  return (oldfuncsym);
}

// 解析组合类型声明：struct 或 union。
// 要么找到现有的 struct/union 声明，要么构建
// 一个 struct/union 符号表条目并返回其指针。
static struct symtable *composite_declaration(int type) {
  struct symtable *ctype = NULL;
  struct symtable *m;
  struct ASTnode *unused;
  int offset;
  int t;

  // 跳过 struct/union 关键字
  scan(&Token);

  // 查看是否有后续的 struct/union 名称
  if (Token.token == T_IDENT) {
    // 查找任何匹配的组合类型
    if (type == P_STRUCT)
      ctype = findstruct(Text);
    else
      ctype = findunion(Text);
    scan(&Token);
  }
  // 如果下一个标记不是 LBRACE，这是
  // 现有 struct/union 类型的用法。
  // 返回类型的指针。
  if (Token.token != T_LBRACE) {
    if (ctype == NULL)
      fatals("unknown struct/union type", Text);
    return (ctype);
  }
  // 确保这个 struct/union 类型之前没有定义过
  if (ctype)
    fatals("previously defined struct/union", Text);

  // 构建组合类型并跳过左花括号
  if (type == P_STRUCT)
    ctype = addstruct(Text);
  else
    ctype = addunion(Text);
  scan(&Token);

  // 扫描成员列表
  while (1) {
    // 获取下一个成员。m 作为虚拟变量使用
    t= declaration_list(&m, C_MEMBER, T_SEMI, T_RBRACE, &unused);
    if (t== -1)
      fatal("Bad type in member list");
    if (Token.token == T_SEMI)
      scan(&Token);
    if (Token.token == T_RBRACE)
      break;
  }

  // 附加到 struct 类型的节点
  rbrace();
  if (Membhead==NULL)
    fatals("No members in struct", ctype->name);
  ctype->member = Membhead;
  Membhead = Membtail = NULL;

  // 设置初始成员的偏移量
  // 并找到其后第一个空闲字节
  m = ctype->member;
  m->st_posn = 0;
  offset = typesize(m->type, m->ctype);

  // 在组合类型中设置每个后续成员的位置
  // union 很简单。对于 struct，对齐成员并找到下一个空闲字节
  for (m = m->next; m != NULL; m = m->next) {
    // 设置此成员的偏移量
    if (type == P_STRUCT)
      m->st_posn = genalign(m->type, offset, 1);
    else
      m->st_posn = 0;

    // 获取此成员之后下一个空闲字节的偏移量
    offset += typesize(m->type, m->ctype);
  }

  // 设置组合类型的整体大小
  ctype->size = offset;
  return (ctype);
}

// 解析枚举声明
static void enum_declaration(void) {
  struct symtable *etype = NULL;
  char *name= NULL;
  int intval = 0;

  // 跳过 enum 关键字。
  scan(&Token);

  // 如果有后续的枚举类型名称，获取
  // 指向任何现有枚举类型节点的指针。
  if (Token.token == T_IDENT) {
    etype = findenumtype(Text);
    name = strdup(Text);	// 因为它很快就会被覆盖
    scan(&Token);
  }
  // 如果下一个标记不是 LBRACE，检查
  // 我们有枚举类型名称，然后返回
  if (Token.token != T_LBRACE) {
    if (etype == NULL)
      fatals("undeclared enum type:", name);
    return;
  }
  // 我们确实有 LBRACE。跳过它
  scan(&Token);

  // 如果我们有枚举类型名称，确保它
  // 之前没有声明过。
  if (etype != NULL)
    fatals("enum type redeclared:", etype->name);
  else
    // 为此标识符构建枚举类型节点
    etype = addenum(name, C_ENUMTYPE, 0);

  // 循环获取所有枚举值
  while (1) {
    // 确保我们有标识符
    // 复制它以防有整数字面量即将出现
    ident();
    name = strdup(Text);

    // 确保这个枚举值之前没有声明过
    etype = findenumval(name);
    if (etype != NULL)
      fatals("enum value redeclared:", Text);

    // 如果下一个标记是 '='，跳过它并
    // 获取后续的整数字面量
    if (Token.token == T_ASSIGN) {
      scan(&Token);
      if (Token.token != T_INTLIT)
	fatal("Expected int literal after '='");
      intval = Token.intvalue;
      scan(&Token);
    }
    // 为此标识符构建枚举值节点。
    // 为下一个枚举标识符递增值。
    etype = addenum(name, C_ENUMVAL, intval++);

    // 遇到右花括号则退出，否则获取逗号
    if (Token.token == T_RBRACE)
      break;
    comma();
  }
  scan(&Token);			// 跳过右花括号
}

// 解析 typedef 声明并返回它
// 所代表的类型和 ctype
static int typedef_declaration(struct symtable **ctype) {
  int type, class = 0;

  // 跳过 typedef 关键字。
  scan(&Token);

  // 获取关键字后面的实际类型
  type = parse_type(ctype, &class);
  if (class != 0)
    fatal("Can't have extern in a typedef declaration");

  // 查看 typedef 标识符是否已存在
  if (findtypedef(Text) != NULL)
    fatals("redefinition of typedef", Text);

  // 获取任何后续的 '*' 标记
  type = parse_stars(type);

  // 它不存在，所以添加到 typedef 列表
  addtypedef(Text, type, *ctype);
  scan(&Token);
  return (type);
}

// 给定 typedef 名称，返回它代表的类型
static int type_of_typedef(char *name, struct symtable **ctype) {
  struct symtable *t;

  // 在列表中查找 typedef
  t = findtypedef(name);
  if (t == NULL)
    fatals("unknown type", name);
  scan(&Token);
  *ctype = t->ctype;
  return (t->type);
}

// 解析变量或函数的声明。
// 类型和任何后续的 '*' 已被扫描，
// 我们有标识符在 Token 变量中。
// class 参数是变量的类别。
// 返回符号表中条目指向的指针
static struct symtable *symbol_declaration(int type, struct symtable *ctype,
					   int class,
					   struct ASTnode **tree) {
  struct symtable *sym = NULL;
  char *varname = strdup(Text);

  // 确保我们有标识符。
  // 我们在上面复制了它，这样我们就可以扫描更多标记，例如
  // 局部变量的赋值表达式。
  ident();

  // 处理函数声明
  if (Token.token == T_LPAREN) {
    return (function_declaration(varname, type, ctype, class));
  }
  // 查看此数组或标量变量是否已声明
  switch (class) {
    case C_EXTERN:
    case C_GLOBAL:
      if (findglob(varname) != NULL)
	fatals("Duplicate global variable declaration", varname);
    case C_LOCAL:
    case C_PARAM:
      if (findlocl(varname) != NULL)
	fatals("Duplicate local variable declaration", varname);
    case C_MEMBER:
      if (findmember(varname) != NULL)
	fatals("Duplicate struct/union member declaration", varname);
  }

  // 将数组或标量变量添加到符号表
  if (Token.token == T_LBRACKET)
    sym = array_declaration(varname, type, ctype, class);
  else
    sym = scalar_declaration(varname, type, ctype, class, tree);
  return (sym);
}

// 解析有初始类型的符号列表。
// 返回符号的类型。et1 和 et2 是结束标记。
int declaration_list(struct symtable **ctype, int class, int et1, int et2,
		     struct ASTnode **gluetree) {
  int inittype, type;
  struct symtable *sym;
  struct ASTnode *tree;
  *gluetree= NULL;

  // 获取初始类型。如果是 -1，则是
  // 组合类型定义，返回此值
  if ((inittype = parse_type(ctype, &class)) == -1)
    return (inittype);

  // 现在解析符号列表
  while (1) {
    // 查看此符号是否是指针
    type = parse_stars(inittype);

    // 解析此符号
    sym = symbol_declaration(type, *ctype, class, &tree);

    // 我们解析了函数，没有列表，所以离开
    if (sym->stype == S_FUNCTION) {
      if (class != C_GLOBAL)
        fatal("Function definition not at global level");
      return (type);
    }

    // 将局部声明中的任何 AST 树粘合在一起
    // 以构建要执行的一系列赋值
    if (*gluetree== NULL)
      *gluetree= tree;
    else
      *gluetree = mkastnode(A_GLUE, P_NONE, *gluetree, NULL, tree, NULL, 0);

    // 我们在列表的末尾，离开
    if (Token.token == et1 || Token.token == et2)
      return (type);

    // 否则，我们需要逗号作为分隔符
    comma();
  }
}

// 解析一个或多个全局声明，
// 变量、函数或 struct
void global_declarations(void) {
  struct symtable *ctype;
  struct ASTnode *unused;

  while (Token.token != T_EOF) {
    declaration_list(&ctype, C_GLOBAL, T_SEMI, T_EOF, &unused);
    // 跳过任何分号和右花括号
    if (Token.token == T_SEMI)
      scan(&Token);
  }
}