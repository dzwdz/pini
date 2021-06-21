#include "libtmt/tmt.h"
#include <errno.h>
#include <pty.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#define die(...) {fprintf(stderr, __VA_ARGS__); exit(1);}
#define errstr (strerror(errno)) // i'm tried of typing it over and over

TMT *vt;
pid_t child_pid;

/* runs a process in a new pty, returns the fd of the controlling end */
int run_pty(const char *cmd, const char *args[]) {
	int m, s;
	if (openpty(&m, &s, NULL, NULL, NULL) < 0)
		die("openpty(): %s\n", errstr);

	switch (child_pid = fork()) {
		case -1:
			die("fork(): %s\n", errstr);
		case 0:
			dup2(s, 0);
			dup2(s, 1);
			dup2(s, 2);

			// set the pty as the controlling terminal
			setsid();
			if (ioctl(s, TIOCSCTTY, NULL) < 0)
				die("ioctl(): %s\n", errstr);

			close(s);
			close(m);

			execvp(cmd, args);
			die("execvp(): %s\n", errstr);
		default:
			close(s);
			return m;
	}
}

void bridge_stdin(int target) {
	struct termios raw;
	char buf[1024];
	int len;

	switch (fork()) {
		case -1:
			die("fork(): %s\n", errstr);
		case 0:
			// enter raw mode
			// TODO restore
			tcgetattr(STDIN_FILENO, &raw);
			raw.c_iflag &= ~(ICRNL);
			raw.c_lflag &= ~(ECHO | ICANON | /*ISIG |*/ IEXTEN | ICRNL);
			tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

			while ((len = read(STDIN_FILENO, buf, sizeof(buf))) > 0) {
				write(target, buf, len);
			}

			die("bridge_stdin done");
		default:
			return;
	}
}

void vt_callback(tmt_msg_t m, TMT *vt, const void *a, void *p) {

}

int main() {
	const char *sh[] = { "/bin/sh", NULL };
	int pty = run_pty(sh[0], sh);

	//vt = tmt_open(25, 80, vt_callback, NULL, NULL);
	//if (!vt) die("tmt_open(): %s\n", errstr);

	bridge_stdin(pty);

	char buf[1024];
	int len;

	while ((len = read(pty, buf, sizeof(buf))) > 0) {
		write(STDIN_FILENO, buf, len);
		//tmt_write(vt, buf, len);
	}

	//tmt_close(vt);
}
