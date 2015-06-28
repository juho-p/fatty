#ifndef CHILD_H
#define CHILD_H

#include <sys/termios.h>

struct term;

struct child
{
  char *home, *cmd;

  pid_t pid;
  bool killed;
  int pty_fd;
  int win_fd;
  int log_fd;
  struct term* term;
};

void child_create(struct child* child, struct term* term,
    char *argv[], struct winsize *winp);
void child_proc(struct child* child);
void child_kill(struct child* child, bool point_blank);
void child_write(struct child* child, const char *, uint len);
void child_printf(struct child* child, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void child_send(struct child* child, const char *, uint len);
void child_sendw(struct child* child, const wchar *, uint len);
void child_resize(struct child* child, struct winsize *winp);
bool child_is_alive(struct child* child);
bool child_is_parent(struct child* child);
wstring child_conv_path(struct child*, wstring);
void child_fork(struct child* child, int argc, char *argv[]);

#endif
