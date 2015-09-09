#include "win.hh"

#include <cstdlib>
#include <stdio.h>
#include <algorithm>

#include <sys/cygwin.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <cstring>
#include <fcntl.h>
#include <utmp.h>
#include <dirent.h>
#include <sys/ioctl.h>

int child_win_fd;
int child_log_fd = -1;

extern "C" {
#include "child.h"

void child_onexit(int sig) {
    for (auto& tab : win_tabs()) {
        if (tab.chld->pid)
            kill(-tab.chld->pid, SIGHUP);
    }
    signal(sig, SIG_DFL);
    kill(getpid(), sig);
}

static void sigexit(int sig) {
    child_onexit(sig);
}

void child_init() {
    // xterm and urxvt ignore SIGHUP, so let's do the same.
    signal(SIGHUP, SIG_IGN);

    signal(SIGINT, sigexit);
    signal(SIGTERM, sigexit);
    signal(SIGQUIT, sigexit);

    child_win_fd = open("/dev/windows", O_RDONLY);

    // Open log file if any
    if (*cfg.log) {
        if (!strcmp(cfg.log, "-"))
            child_log_fd = 1;
        else {
            child_log_fd = open(cfg.log, O_WRONLY | O_CREAT | O_TRUNC, 0600);
            if (child_log_fd < 0)
                fputs("Opening log file failed\n", stderr);
        }
    }
}

void child_proc() {
    // this code is ripped from child.c
    for (;;) {
        for (Tab& t : win_tabs()) {
            if (t.terminal->paste_buffer)
                term_send_paste(t.terminal.get());
        }

        struct timeval timeout = {0, 100000}, *timeout_p = 0;
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(child_win_fd, &fds);  
        int highfd = child_win_fd;
        for (Tab& t : win_tabs()) {
            if (t.chld->pty_fd > highfd) highfd = t.chld->pty_fd;
            if (t.chld->pty_fd >= 0) {
                FD_SET(t.chld->pty_fd, &fds);
            } else if (t.chld->pid) {
                int status;
                if (waitpid(t.chld->pid, &status, WNOHANG) == t.chld->pid) {
                    t.chld->pid = 0;
                }
                else {// Pty gone, but process still there: keep checking
                    timeout_p = &timeout;
                }
            }
        }

        if (select(highfd + 1, &fds, 0, 0, timeout_p) > 0) {
            for (Tab& t : win_tabs()) {
                struct child* child = t.chld.get();
                if (child->pty_fd >= 0 && FD_ISSET(child->pty_fd, &fds)) {
#if CYGWIN_VERSION_DLL_MAJOR >= 1005
                    static char buf[4096];
                    int len = read(child->pty_fd, buf, sizeof buf);
#else
                    // Pty devices on old Cygwin version deliver only 4 bytes at a time,
                    // so call read() repeatedly until we have a worthwhile haul.
                    static char buf[512];
                    uint len = 0;
                    do {
                        int ret = read(child->pty_fd, buf + len, sizeof buf - len);
                        if (ret > 0)
                            len += ret;
                        else
                            break;
                    } while (len < sizeof buf);
#endif
                    if (len > 0) {
                        term_write(child->term, buf, len);
                        if (child_log_fd >= 0)
                            write(child_log_fd, buf, len);
                    } else {
                        child->pty_fd = -1;
                        term_hide_cursor(child->term);
                    }
                }
                if (FD_ISSET(child_win_fd, &fds)) {
                    return;
                }
            }
        }
    }
}

static void kill_all_tabs(bool point_blank=false) {
    for (Tab& tab : win_tabs()) {
        struct child* child = tab.chld.get();
        kill(-child->pid, point_blank ? SIGKILL : SIGHUP);
        child->killed = true;
    }
}

void child_kill() { 
    kill_all_tabs();
    win_callback(500, []() {
        // We are still here even after half a second?
        // Really, lets just die. It would be really annoying not to...
        kill_all_tabs(true);
        exit(1);
    });
}

void child_terminate(struct child* child) {
    kill(-child->pid, SIGKILL);

    // Seems that sometimes cygwin leaves process in non-waitable and
    // non-alive state. The result for that is that there will be
    // unkillable tabs.
    //
    // This stupid hack solves the problem.
    //
    // TODO: Find out better way to solve this. Now the child processes are
    // not always cleaned up.
    int cpid = child->pid;
    win_callback(50, [cpid]() {
        auto& tabs = win_tabs();
        for (auto& t : tabs) {
            if (t.chld->pid == cpid) t.chld->pid = 0;
        }
    });
}

bool child_is_any_parent() {
    auto& tabs = win_tabs();
    return std::any_of(tabs.begin(), tabs.end(), [](Tab& tab) {
        return child_is_parent(tab.chld.get());
    });
}

}
