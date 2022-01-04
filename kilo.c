#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

struct termios orig_termios;

void die(const char* msg) {
	perror(msg);
	exit(1);
}

void disableRawMode() {
	if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) < 0) die("tcsetattr");
}

void enableRawMode() {
	if(tcgetattr(STDIN_FILENO, &orig_termios) < 0) die("tcgetattr");
	atexit(disableRawMode);

	//  raw mode flags
	struct termios raw = orig_termios;
	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	raw.c_oflag &= ~(OPOST);
	raw.c_oflag |=  (CS8);
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

	// set a timeout for read()
	raw.c_cc[VMIN]  = 0;
	raw.c_cc[VTIME] = 1;

	if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) < 0) die("tcsetattr");
}

int main() {
	enableRawMode();

	for(;;) {
		char c = '\0';
		int rc = read(STDIN_FILENO, &c, 1);
		if(rc < 0 && errno != EAGAIN) die("read");
		if(c=='q') break;

		if(rc == 1) {
			if(iscntrl(c)) printf("%d\r\n", c);
			else printf("%d ('%c')\r\n", c, c);
		}
	}

	return 0;
}
