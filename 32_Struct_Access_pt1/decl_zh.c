#include "defs.h"
#include "data.h"
#include "decl.h"

// 声明解析
// Copyright (c) 2019 Warren Toomey, GPL3

static struct symtable *struct_declaration(void);


// 解析当前标记并返回
// 原始类型枚举值和指向
// 任何复合类型的指针。同时扫描下一个标记
int parse_type(struct symtable **ctype) {
  int type;
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
    case T_STRUCT:
      type = P_STRUCT;
      *ctype = struct_declaration();
      break;
    default:
      fatald("Illegal type, token", Token.token);
  }

  // 扫描一个或多个额外的'*'标记
  // 并确定正确的指针类型
  while (1) {
    if (Token.token != T_STAR)
      break;
    type = pointer_to(type);
    scan(&Token);
  }

  // 离开时下一个标记已被扫描
  return (type);
}

// variable_declaration: type identifier ';'
//        | type identifier '[' INTLIT ']' ';'
//        ;
//
// 解析标量变量或具有给定大小的数组的声明
// 标识符已被扫描且我们有类型
// class是变量的类别
// 返回符号表中变量条目的指针
struct symtable *var_declaration(int type, struct symtable *ctype, int class) {
  struct symtable *sym = NULL;

  // 查看这是否已被声明
  switch (class) {
    case C_GLOBAL:
      if (findglob(Text) != NULL)
	fatals("Duplicate global variable declaration", Text);
    case C_LOCAL:
    case C_PARAM:
      if (findlocl(Text) != NULL)
	fatals("Duplicate local variable declaration", Text);
    case C_MEMBER:
      if (findmember(Text) != NULL)
	fatals("Duplicate struct/union member declaration", Text);
  }

  // Text现在有标识符的名称
  // 如果下一个标记是'['
  if (Token.token == T_LBRACKET) {
    // 跳过'['
    scan(&Token);

    // 检查我们是否有数组大小
    if (Token.token == T_INTLIT) {
      // 将其添加为已知的数组并在汇编中生成其空间
      // 我们将其视为指向其元素类型的指针
      switch (class) {
	case C_GLOBAL:
	  sym =
	    addglob(Text, pointer_to(type), ctype, S_ARRAY, Token.intvalue);
	  break;
	case C_LOCAL:
	case C_PARAM:
	case C_MEMBER:
	  fatal
	    ("For now, declaration of non-global arrays is not implemented");
      }
    }
    // 确保我们有后续的']'
    scan(&Token);
    match(T_RBRACKET, "]");
  } else {
    // 将其添加为已知的标量
    // 并在汇编中生成其空间
    switch (class) {
      case C_GLOBAL:
	sym = addglob(Text, type, ctype, S_VARIABLE, 1);
	break;
      case C_LOCAL:
	sym = addlocl(Text, type, ctype, S_VARIABLE, 1);
	break;
      case C_PARAM:
	sym = addparm(Text, type, ctype, S_VARIABLE, 1);
	break;
      case C_MEMBER:
	sym = addmemb(Text, type, ctype, S_VARIABLE, 1);
	break;
    }
  }
  return (sym);
}

// var_declaration_list: <null>
//           | variable_declaration
//           | variable_declaration separate_token var_declaration_list ;
//
// 当解析函数参数时，separate_token是','
// 当解析结构体/联合的成员时，separate_token是';'
//
// 解析变量列表
// 将它们作为符号添加到符号表列表之一，并返回
// 变量数量。如果funcsym不为NULL，则存在现有函数
// 原型，因此比较每个变量的类型与此原型
static int var_declaration_list(struct symtable *funcsym, int class,
				int separate_token, int end_token) {
  int type;
  int paramcnt = 0;
  struct symtable *protoptr = NULL;
  struct symtable *ctype;

  // 如果有原型，获取指向第一个原型参数的指针
  if (funcsym != NULL)
    protoptr = funcsym->member;

  // 循环直到最终结束标记
  while (Token.token != end_token) {
    // 获取类型和标识符
    type = parse_type(&ctype);
    ident();

    // 检查此类型是否与原型匹配（如果有）
    if (protoptr != NULL) {
      if (type != protoptr->type)
	fatald("Type doesn't match prototype for parameter", paramcnt + 1);
      protoptr = protoptr->next;
    } else {
      // 根据class将新参数添加到正确的符号表列表
      var_declaration(type, ctype, class);
    }
    paramcnt++;

    // 此时必须是separate_token或')'
    if ((Token.token != separate_token) && (Token.token != end_token))
      fatald("Unexpected token in parameter list", Token.token);
    if (Token.token == separate_token)
      scan(&Token);
  }

  // 检查此列表中的参数数量是否与任何现有原型匹配
  if ((funcsym != NULL) && (paramcnt != funcsym->nelems))
    fatals("Parameter count mismatch for function", funcsym->name);

  // 返回参数计数
  return (paramcnt);
}

//
// function_declaration: type identifier '(' parameter_list ')' ;
//      | type identifier '(' parameter_list ')' compound_statement   ;
//
// 解析函数声明
// 标识符已被扫描且我们有类型
struct ASTnode *function_declaration(int type) {
  struct ASTnode *tree, *finalstmt;
  struct symtable *oldfuncsym, *newfuncsym = NULL;
  int endlabel, paramcnt;

  // Text有标识符的名称。如果存在且是函数，
  // 则获取id。否则，将oldfuncsym设置为NULL
  if ((oldfuncsym = findsymbol(Text)) != NULL)
    if (oldfuncsym->stype != S_FUNCTION)
      oldfuncsym = NULL;

  // 如果这是新的函数声明，获取
  // 结束标签的标签id，并将函数添加到符号表
  if (oldfuncsym == NULL) {
    endlabel = genlabel();
    // 假设：函数只返回标量类型，所以下面为NULL
    newfuncsym = addglob(Text, type, NULL, S_FUNCTION, endlabel);
  }
  // 扫描'('、任何参数和')'
  // 传入任何现有的函数原型指针
  lparen();
  paramcnt = var_declaration_list(oldfuncsym, C_PARAM, T_COMMA, T_RPAREN);
  rparen();

  // 如果这是新的函数声明，
  // 用参数数量更新函数符号条目。
  // 还要将参数列表复制到函数的节点中
  if (newfuncsym) {
    newfuncsym->nelems = paramcnt;
    newfuncsym->member = Parmhead;
    oldfuncsym = newfuncsym;
  }
  // 清除参数列表
  Parmhead = Parmtail = NULL;

  // 声明以分号结束，只是原型
  if (Token.token == T_SEMI) {
    scan(&Token);
    return (NULL);
  }
  // 这不仅仅是原型
  // 将Functionid全局设置为函数的符号指针
  Functionid = oldfuncsym;

  // 获取复合语句的AST树
  tree = compound_statement();

  // 如果函数类型不是P_VOID ..
  if (type != P_VOID) {

    // 如果函数中没有语句则报错
    if (tree == NULL)
      fatal("No statements in function with non-void type");

    // 检查复合语句中的最后一个AST操作是否是return语句
    finalstmt = (tree->op == A_GLUE) ? tree->right : tree;
    if (finalstmt == NULL || finalstmt->op != A_RETURN)
      fatal("No return for function with non-void type");
  }
  // 返回A_FUNCTION节点，其中包含函数符号指针
  // 和复合语句子树
  return (mkastunary(A_FUNCTION, type, tree, oldfuncsym, endlabel));
}

// 解析结构体声明。找到现有的
// 结构体声明，或构建结构体符号表
// 条目并返回其指针
static struct symtable *struct_declaration(void) {
  struct symtable *ctype = NULL;
  struct symtable *m;
  int offset;

  // 跳过struct关键字
  scan(&Token);

  // 查看是否有后续的结构体名称
  if (Token.token == T_IDENT) {
    // 查找任何匹配的复合类型
    ctype = findstruct(Text);
    scan(&Token);
  }
  // 如果下一个标记不是LBRACE，这是
  // 现有结构体类型的使用。返回类型指针
  if (Token.token != T_LBRACE) {
    if (ctype == NULL)
      fatals("unknown struct type", Text);
    return (ctype);
  }
  // 确保此结构体类型之前没有被定义
  if (ctype)
    fatals("previously defined struct", Text);

  // 构建结构体节点并跳过左花括号
  ctype = addstruct(Text, P_STRUCT, NULL, 0, 0);
  scan(&Token);

  // 扫描成员列表并附加
  // 到结构体类型的节点
  var_declaration_list(NULL, C_MEMBER, T_SEMI, T_RBRACE);
  rbrace();
  ctype->member = Membhead;
  Membhead = Membtail = NULL;

  // 设置初始成员的偏移量
  // 并找到其后的第一个可用字节
  m = ctype->member;
  m->posn = 0;
  offset = typesize(m->type, m->ctype);

  // 设置结构体中每个后续成员的位置
  for (m = m->next; m != NULL; m = m->next) {
    // 设置此成员的偏移量
    m->posn = genalign(m->type, offset, 1);

    // 获取此成员之后下一个可用字节的偏移量
    offset += typesize(m->type, m->ctype);
  }

  // 设置结构体的总大小
  ctype->size = offset;
  return (ctype);
}

// 解析一个或多个全局声明，
// 变量、函数或结构体
void global_declarations(void) {
  struct ASTnode *tree;
  struct symtable *ctype;
  int type;

  while (1) {
    // 当我们到达EOF时停止
    if (Token.token == T_EOF)
      break;

    // 获取类型
    type = parse_type(&ctype);

    // 我们可能刚刚解析了一个没有关联变量的结构体声明
    // 下一个标记可能是';'。如果是则循环回去。XXX
    // 我对这个不满意，因为它允许"struct fred;"作为一种接受的语句
    if (type == P_STRUCT && Token.token == T_SEMI) {
      scan(&Token);
      continue;
    }
    // 我们必须读取标识符以查看是函数声明的'('
    // 还是变量声明的','或';'
    // Text由ident()调用填充
    ident();
    if (Token.token == T_LPAREN) {

      // 解析函数声明
      tree = function_declaration(type);

      // 只是函数原型，没有代码
      if (tree == NULL)
	continue;

      // 一个真实的函数，为其生成汇编代码
      if (O_dumpAST) {
	dumpAST(tree, NOLABEL, 0);
	fprintf(stdout, "\n\n");
      }
      genAST(tree, NOLABEL, 0);

      // 现在释放与
      // 此函数关联的符号
      freeloclsyms();
    } else {

      // 解析全局变量声明
      // 并跳过尾部分号
      var_declaration(type, ctype, C_GLOBAL);
      semi();
    }
  }
}