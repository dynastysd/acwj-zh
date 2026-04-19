// misc.c - 杂项函数
// 版权所有 (c) 2019 Warren Toomey, GPL3

// 打印致命消息
void fatal(char *s);
void fatals(char *s1, char *s2);
void fatald(char *s, int d);
void fatalc(char *s, int c);

// 从f FILE最多读取count-1个字符并存储到s缓冲区。
// 用NUL终止s缓冲区。
// 如果无法读取或遇到EOF则返回NULL。
// 否则，返回原始s指针。
char *fgetstr(char *s, size_t count, FILE * f);