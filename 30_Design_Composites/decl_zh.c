#include "defs.h"
#include "data.h"
#include "decl.h"

// 声明解析
// Copyright (c) 2019 Warren Toomey, GPL3


// 解析当前标记并返回
// 一个基本类型枚举值。同时
// 扫描下一个标记
int parse_type(void) {
  int type;
  switch (Token.token) {
    case T_VOID:
      type = P_VOID;
      break;
    case T_CHAR:
      type = P_CHAR;
      break;
    case T_INT:
      type = P_INT;
      break;
    case T_LONG:
      type = P_LONG;
      break;
    default:
      fatald("Illegal type, token", Token.token);
  }

  // 扫描一个或多个 '*' 标记
  // 并确定正确的指针类型
  while (1) {
    scan(&Token);
    if (Token.token != T_STAR)
      break;
    type = pointer_to(type);
  }

  // 我们离开时下一个标记已经被扫描
  return (type);
}

// variable_declaration: type identifier ';'
//        | type identifier '[' INTLIT ']' ';'
//        ;
//
// 解析具有给定大小的标量变量或数组
// 的声明。
// 标识符已被扫描且我们有类型。
// class 是变量的类别
// 返回变量在符号表中的条目指针
struct symtable *var_declaration(int type, int class) {
  struct symtable *sym = NULL;

  // 检查这是否已经被声明
  switch (class) {
    case C_GLOBAL:
      if (findglob(Text) != NULL)
	fatals("Duplicate global variable declaration", Text);
    case C_LOCAL:
    case C_PARAM:
      if (findlocl(Text) != NULL)
	fatals("Duplicate local variable declaration", Text);
  }

  // Text 现在有标识符的名称。
  // 如果下一个标记是 '['
  if (Token.token == T_LBRACKET) {
    // 跳过 '['
    scan(&Token);

    // 检查我们是否有数组大小
    if (Token.token == T_INTLIT) {
      // 将其添加为已知数组并在汇编中生成其空间。
      // 我们将数组视为指向其元素类型的指针
      switch (class) {
	case C_GLOBAL:
	  sym =
	    addglob(Text, pointer_to(type), S_ARRAY, class, Token.intvalue);
	  break;
	case C_LOCAL:
	case C_PARAM:
	  fatal("For now, declaration of local arrays is not implemented");
      }
    }
    // 确保我们有后续的 ']'
    scan(&Token);
    match(T_RBRACKET, "]");
  } else {
    // 将其添加为已知的标量
    // 并在汇编中生成其空间
    switch (class) {
      case C_GLOBAL:
	sym = addglob(Text, type, S_VARIABLE, class, 1);
	break;
      case C_LOCAL:
	sym = addlocl(Text, type, S_VARIABLE, class, 1);
	break;
      case C_PARAM:
	sym = addparm(Text, type, S_VARIABLE, class, 1);
	break;
    }
  }
  return (sym);
}

// param_declaration: <null>
//           | variable_declaration
//           | variable_declaration ',' param_declaration
//
// 解析函数名后面括号中的参数。
// 将它们作为符号添加到符号表并返回
// 参数的数量。如果 funcsym 不为 NULL，则存在现有函数
// 原型，函数有这个符号表指针。
static int param_declaration(struct symtable *funcsym) {
  int type;
  int paramcnt = 0;
  struct symtable *protoptr = NULL;

  // 如果有原型，获取
  // 指向第一个原型参数的指针
  if (funcsym != NULL)
    protoptr = funcsym->member;

  // 循环直到最后的右括号
  while (Token.token != T_RPAREN) {
    // 获取类型和标识符
    // 并将其添加到符号表
    type = parse_type();
    ident();

    // 我们有一个现有的原型。
    // 检查此类型是否与原型匹配。
    if (protoptr != NULL) {
      if (type != protoptr->type)
	fatald("Type doesn't match prototype for parameter", paramcnt + 1);
      protoptr = protoptr->next;
    } else {
      // 将新参数添加到新参数列表
      var_declaration(type, C_PARAM);
    }
    paramcnt++;

    // 此时必须是 ',' 或 ')'
    switch (Token.token) {
      case T_COMMA:
	scan(&Token);
	break;
      case T_RPAREN:
	break;
      default:
	fatald("Unexpected token in parameter list", Token.token);
    }
  }

  // 检查此列表中的参数数量是否与
  // 任何现有原型匹配
  if ((funcsym != NULL) && (paramcnt != funcsym->nelems))
    fatals("Parameter count mismatch for function", funcsym->name);

  // 返回参数计数
  return (paramcnt);
}

//
// function_declaration: type identifier '(' parameter_list ')' ;
//      | type identifier '(' parameter_list ')' compound_statement   ;
//
// 解析函数声明。
// 标识符已被扫描且我们有类型。
struct ASTnode *function_declaration(int type) {
  struct ASTnode *tree, *finalstmt;
  struct symtable *oldfuncsym, *newfuncsym = NULL;
  int endlabel, paramcnt;

  // Text 有标识符的名称。如果存在且为
  // 函数，则获取 id。否则，将 oldfuncsym 设置为 NULL。
  if ((oldfuncsym = findsymbol(Text)) != NULL)
    if (oldfuncsym->stype != S_FUNCTION)
      oldfuncsym = NULL;

  // 如果这是一个新的函数声明，获取
  // 结束标签的标签 id，并将函数
  // 添加到符号表，
  if (oldfuncsym == NULL) {
    endlabel = genlabel();
    newfuncsym = addglob(Text, type, S_FUNCTION, C_GLOBAL, endlabel);
  }
  // 扫描 '('、任何参数和 ')'。
  // 传入任何现有的函数原型指针
  lparen();
  paramcnt = param_declaration(oldfuncsym);
  rparen();

  // 如果这是一个新的函数声明，
  // 用参数数量更新函数符号条目。
  // 同时将参数列表复制到函数的节点中。
  if (newfuncsym) {
    newfuncsym->nelems = paramcnt;
    newfuncsym->member = Parmhead;
    oldfuncsym = newfuncsym;
  }
  // 清空参数列表
  Parmhead = Parmtail = NULL;

  // 声明以分号结尾，只是原型。
  if (Token.token == T_SEMI) {
    scan(&Token);
    return (NULL);
  }
  // 这不仅仅是一个原型。
  // 将 Functionid 全局变量设置为函数的符号指针
  Functionid = oldfuncsym;

  // 获取复合语句的 AST 树
  tree = compound_statement();

  // 如果函数类型不是 P_VOID..
  if (type != P_VOID) {

    // 如果函数中没有语句则报错
    if (tree == NULL)
      fatal("No statements in function with non-void type");

    // 检查复合语句中的最后一个 AST 操作
    // 是否是 return 语句
    finalstmt = (tree->op == A_GLUE) ? tree->right : tree;
    if (finalstmt == NULL || finalstmt->op != A_RETURN)
      fatal("No return for function with non-void type");
  }
  // 返回一个 A_FUNCTION 节点，它有函数的符号指针
  // 和复合语句子树
  return (mkastunary(A_FUNCTION, type, tree, oldfuncsym, endlabel));
}


// 解析一个或多个全局声明，
// 变量或函数
void global_declarations(void) {
  struct ASTnode *tree;
  int type;

  while (1) {

    // 我们必须读取类型和标识符
    // 来查看是函数声明的 '('
    // 还是变量声明的 ',' 或 ';'。
    // Text 由 ident() 调用填充。
    type = parse_type();
    ident();
    if (Token.token == T_LPAREN) {

      // 解析函数声明
      tree = function_declaration(type);

      // 只是函数原型，没有代码
      if (tree == NULL)
	continue;

      // 一个真正的函数，为其生成汇编代码
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
      var_declaration(type, C_GLOBAL);
      semi();
    }

    // 当我们到达 EOF 时停止
    if (Token.token == T_EOF)
      break;
  }
}