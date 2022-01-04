#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

#define CTRL_KEY(k) ((k) & 0x1f)

struct termios orig_termios;

// use die(NULL) for a successful exit
void die(const char* msg) {
	// make sure we go to the top-left of the screen
	write(STDOUT_FILENO, "\x1b[2J", 4);
	write(STDOUT_FILENO, "\x1b[H", 3);

	if(msg) {
		perror(msg);
		exit(1);
	} else {
		exit(0);
	}
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

char editorReadKey() {
	char c = 0;
	int rc = read(STDIN_FILENO, &c, 1);
	if(rc < 0 && errno != EAGAIN) die("read");

	return c;
}

void editorDrawRows() {
	for (int i=0; i<24; ++i) {
		write(STDOUT_FILENO, "~\r\n", 3);
	}
}

void editorRefreshScreen() {
	write(STDOUT_FILENO, "\x1b[2J", 4); // clear screen
	write(STDOUT_FILENO, "\x1b[H", 3); // reposition cursor

	editorDrawRows();

	write(STDOUT_FILENO, "\x1b[H", 3);
}

void editorProcesssKeypress() {
	char c = editorReadKey();
	if(c==CTRL_KEY('q')) die(NULL);
}

int main() {
	enableRawMode();

	for(;;) {
		editorRefreshScreen();
		editorProcesssKeypress();
	}

	return 0;
}
