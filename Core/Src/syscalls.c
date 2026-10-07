/* Minimal newlib stubs. All logging uses Logging/Src/log.c directly; these
 * exist only so newlib-nano's reentrant wrappers link without warnings. */
#include <errno.h>
#include <sys/stat.h>

int _close(int fd) { (void)fd; errno = EBADF; return -1; }
int _lseek(int fd, int off, int whence) { (void)fd; (void)off; (void)whence; return 0; }
int _read(int fd, char *p, int n) { (void)fd; (void)p; (void)n; return 0; }
int _write(int fd, const char *p, int n) { (void)fd; (void)p; return n; }
int _fstat(int fd, struct stat *st) { (void)fd; st->st_mode = S_IFCHR; return 0; }
int _isatty(int fd) { (void)fd; return 1; }
