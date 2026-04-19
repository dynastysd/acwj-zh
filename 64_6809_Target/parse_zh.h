/* parse.c */
/* 解析器函数声明 */
int scan(struct token *t);
void match(int t, char *what);
void semi(void);
void lbrace(void);
void rbrace(void);
void lparen(void);
void rparen(void);
void ident(void);
void comma(void);
void serialiseAST(struct ASTnode *tree);