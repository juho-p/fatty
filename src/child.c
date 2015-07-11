// child.c (part of FaTTY)
// Copyright 2015 Juho Peltonen
// Based on mintty code by Andy Koppe
// Licensed under the terms of the GNU General Public License v3 or later.

#include "child.h"

#include "term.h"
#include "charset.h"

#include <pwd.h>
#include <fcntl.h>
#include <utmp.h>
#include <dirent.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/cygwin.h>

#if CYGWIN_VERSION_API_MINOR >= 93
#include <pty.h>
#else
int forkpty(int *, char *, struct termios *, struct winsize *);
#endif

#include <winbase.h>

#if CYGWIN_VERSION_DLL_MAJOR < 1007
#include <winnls.h>
#include <wincon.h>
#include <wingdi.h>
#include <winuser.h>
#endif

static void
error(struct term* term, char *action)
{
  char *msg;
  int len = asprintf(&msg, "Failed to %s: %s.", action, strerror(errno));
  if (len > 0) {
    term_write(term, msg, len);
    free(msg);
  }
}

void
child_create(struct child* child, struct term* term,
    char *argv[], struct winsize *winp)
{
  int pid;

  child->pty_fd = -1;
  child->term = term;

  string lang = cs_lang();

  // Create the child process and pseudo terminal.
  pid = forkpty(&child->pty_fd, 0, 0, winp);
  if (pid < 0) {
    child->pid = pid = 0;
    bool rebase_prompt = (errno == EAGAIN);
    error(term, "fork child process");
    if (rebase_prompt) {
      static const char msg[] =
        "\r\nDLL rebasing may be required. See 'rebaseall --help'.";
      term_write(term, msg, sizeof msg - 1);
    }
    term_hide_cursor(term);
  }
  else if (!pid) { // Child process.
#if CYGWIN_VERSION_DLL_MAJOR < 1007
    // Some native console programs require a console to be attached to the
    // process, otherwise they pop one up themselves, which is rather annoying.
    // Cygwin's exec function from 1.5 onwards automatically allocates a console
    // on an invisible window station if necessary. Unfortunately that trick no
    // longer works on Windows 7, which is why Cygwin 1.7 contains a new hack
    // for creating the invisible console.
    // On Cygwin versions before 1.5 and on Cygwin 1.5 running on Windows 7,
    // we need to create the invisible console ourselves. The hack here is not
    // as clever as Cygwin's, with the console briefly flashing up on startup,
    // but it'll do.
#if CYGWIN_VERSION_DLL_MAJOR == 1005
    DWORD win_version = GetVersion();
    win_version = ((win_version & 0xff) << 8) | ((win_version >> 8) & 0xff);
    if (win_version >= 0x0601)  // Windows 7 is NT 6.1.
#endif
      if (AllocConsole()) {
        HMODULE kernel = GetModuleHandle("kernel32");
        HWND (WINAPI *pGetConsoleWindow)(void) =
          (void *)GetProcAddress(kernel, "GetConsoleWindow");
        ShowWindowAsync(pGetConsoleWindow(), SW_HIDE);
      }
#endif

    // Reset signals
    signal(SIGHUP, SIG_DFL);
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    signal(SIGCHLD, SIG_DFL);

    // Mimick login's behavior by disabling the job control signals
    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);

    setenv("TERM", cfg.term, true);

    if (lang) {
      unsetenv("LC_ALL");
      unsetenv("LC_COLLATE");
      unsetenv("LC_CTYPE");
      unsetenv("LC_MONETARY");
      unsetenv("LC_NUMERIC");
      unsetenv("LC_TIME");
      unsetenv("LC_MESSAGES");
      setenv("LANG", lang, true);
    }

    // Terminal line settings
    struct termios attr;
    tcgetattr(0, &attr);
    attr.c_cc[VERASE] = cfg.backspace_sends_bs ? CTRL('H') : CDEL;
    attr.c_iflag |= IXANY | IMAXBEL;
    attr.c_lflag |= ECHOE | ECHOK | ECHOCTL | ECHOKE;
    tcsetattr(0, TCSANOW, &attr);

    // Invoke command
    execvp(child->cmd, argv);

    // If we get here, exec failed.
    fprintf(stderr, "%s: %s\r\n", child->cmd, strerror(errno));

#if CYGWIN_VERSION_DLL_MAJOR < 1005
    // Before Cygwin 1.5, the message above doesn't appear if we exit
    // immediately. So have a little nap first.
    usleep(200000);
#endif

    exit(255);
  }
  else { // Parent process.
    child->pid = pid;

    fcntl(child->pty_fd, F_SETFL, O_NONBLOCK);
    
    if (cfg.utmp) {
      char *dev = ptsname(child->pty_fd);
      if (dev) {
        struct utmp ut;
        memset(&ut, 0, sizeof ut);

        if (!strncmp(dev, "/dev/", 5))
          dev += 5;
        strlcpy(ut.ut_line, dev, sizeof ut.ut_line);

        if (dev[1] == 't' && dev[2] == 'y')
          dev += 3;
        else if (!strncmp(dev, "pts/", 4))
          dev += 4;
        strncpy(ut.ut_id, dev, sizeof ut.ut_id);

        ut.ut_type = USER_PROCESS;
        ut.ut_pid = pid;
        ut.ut_time = time(0);
        strlcpy(ut.ut_user, getlogin() ?: "?", sizeof ut.ut_user);
        gethostname(ut.ut_host, sizeof ut.ut_host);
        login(&ut);
      }
    }
  }
}

void
child_free(struct child* child)
{
  if (child->pty_fd >= 0)
    close(child->pty_fd);
  child->pty_fd = -1;
}

bool
child_is_alive(struct child* child)
{
    return child->pid;
}

bool
child_is_parent(struct child* child)
{
  if (!child->pid)
    return false;
  DIR *d = opendir("/proc");
  if (!d)
    return false;
  bool res = false;
  struct dirent *e;
  char fn[18] = "/proc/";
  while ((e = readdir(d))) {
    char *pn = e->d_name;
    if (isdigit((uchar)*pn) && strlen(pn) <= 6) {
      snprintf(fn + 6, 12, "%s/ppid", pn);
      FILE *f = fopen(fn, "r");
      if (!f)
        continue;
      pid_t ppid = 0;
      fscanf(f, "%u", &ppid);
      fclose(f);
      if (ppid == child->pid) {
        res = true;
        break;
      }
    }
  }
  closedir(d);
  return res;
}

void
child_write(struct child* child, const char *buf, uint len)
{
  if (child->pty_fd >= 0)
    write(child->pty_fd, buf, len);
}

void
child_printf(struct child* child, const char *fmt, ...)
{
  if (child->pty_fd >= 0) {
    va_list va;
    va_start(va, fmt);
    char *s;
    int len = vasprintf(&s, fmt, va);
    va_end(va);
    if (len >= 0)
      write(child->pty_fd, s, len);
    free(s);
  }
}

void
child_send(struct child* child, const char *buf, uint len)
{
  term_reset_screen(child->term);
  if (child->term->echoing)
    term_write(child->term, buf, len);
  child_write(child, buf, len);
}

void
child_sendw(struct child* child, const wchar *ws, uint wlen)
{
  char s[wlen * cs_cur_max];
  int len = cs_wcntombn(s, ws, sizeof s, wlen);
  if (len > 0)
    child_send(child, s, len);
}

void
child_resize(struct child* child, struct winsize *winp)
{
  if (child->pty_fd >= 0)
    ioctl(child->pty_fd, TIOCSWINSZ, winp);
}

wstring
child_conv_path(struct child* child, wstring wpath)
{
  int wlen = wcslen(wpath);
  int len = wlen * cs_cur_max;
  char path[len];
  len = cs_wcntombn(path, wpath, len, wlen);
  path[len] = 0;

  char *exp_path;  // expanded path
  if (*path == '~') {
    // Tilde expansion
    char *name = path + 1;
    char *rest = strchr(path, '/');
    if (rest)
      *rest++ = 0;
    else
      rest = "";
    char *base;
    if (!*name)
      base = child->home;
    else {
#if CYGWIN_VERSION_DLL_MAJOR >= 1005
      // Find named user's home directory
      struct passwd *pw = getpwnam(name);
      base = (pw ? pw->pw_dir : 0) ?: "";
#else
      // Pre-1.5 Cygwin simply copies HOME into pw_dir, which is no use here.
      base = "";
#endif
    }
    exp_path = asform("%s/%s", base, rest);
  }
  else if (*path != '/') {
#if CYGWIN_VERSION_DLL_MAJOR >= 1005
    // Handle relative paths. Finding the foreground process working directory
    // requires the /proc filesystem, which isn't available before Cygwin 1.5.

    // Find pty's foreground process, if any. Fall back to child process.
    int fg_pid = (child->pty_fd >= 0) ? tcgetpgrp(child->pty_fd) : 0;
    if (fg_pid <= 0)
      fg_pid = child->pid;

    char *cwd = 0;
    if (fg_pid > 0) {
      char proc_cwd[32];
      sprintf(proc_cwd, "/proc/%u/cwd", fg_pid);
      cwd = realpath(proc_cwd, 0);
    }

    exp_path = asform("%s/%s", cwd ?: child->home, path);
    free(cwd);
#else
    // If we're lucky, the path is relative to the home directory.
    exp_path = asform("%s/%s", child->home, path);
#endif
  }
  else
    exp_path = path;

#if CYGWIN_VERSION_DLL_MAJOR >= 1007
#if CYGWIN_VERSION_API_MINOR >= 222
  // CW_INT_SETLOCALE was introduced in API 0.222
  cygwin_internal(CW_INT_SETLOCALE);
#endif
  wchar *win_wpath = cygwin_create_path(CCP_POSIX_TO_WIN_W, exp_path);

  // Drop long path prefix if possible,
  // because some programs have trouble with them.
  if (win_wpath && wcslen(win_wpath) < MAX_PATH) {
    wchar *old_win_wpath = win_wpath;
    if (wcsncmp(win_wpath, L"\\\\?\\UNC\\", 8) == 0) {
      win_wpath = wcsdup(win_wpath + 6);
      win_wpath[0] = '\\';  // Replace "\\?\UNC\" prefix with "\\"
      free(old_win_wpath);
    }
    else if (wcsncmp(win_wpath, L"\\\\?\\", 4) == 0) {
      win_wpath = wcsdup(win_wpath + 4);  // Drop "\\?\" prefix
      free(old_win_wpath);
    }
  }
#else
  char win_path[MAX_PATH];
  cygwin_conv_to_win32_path(exp_path, win_path);
  wchar *win_wpath = newn(wchar, MAX_PATH);
  MultiByteToWideChar(0, 0, win_path, -1, win_wpath, MAX_PATH);
#endif

  if (exp_path != path)
    free(exp_path);

  return win_wpath;
}

void
child_fork(struct child* child, int argc, char *argv[])
{
  if (fork() == 0) {
    if (child->pty_fd >= 0)
      close(child->pty_fd);
    if (child_log_fd >= 0)
      close(child_log_fd);
    close(child_win_fd);

    // add child parameters
    int newparams = 4;
    char * * newargv = malloc((argc + newparams + 1) * sizeof(char *));
    int i = 0, j = 0;
    int addnew = 1;
    while (1) {
      if (addnew && (! argv[i] || strcmp (argv[i], "-e") == 0)) {
        addnew = 0;
        // insert additional parameters here
        newargv[j++] = "-o";
        char parbuf1[28];
        sprintf (parbuf1, "Rows=%d", child->term->rows);
        newargv[j++] = parbuf1;
        newargv[j++] = "-o";
        char parbuf2[31];
        sprintf (parbuf2, "Columns=%d", child->term->cols);
        newargv[j++] = parbuf2;
      }
      newargv[j] = argv[i];
      if (! argv[i])
        break;
      i++;
      j++;
    }

    execv("/proc/self/exe", newargv);
    exit(255);
  }
}
