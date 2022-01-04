#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#define KILO_VERSION "0.0.1"

#define CTRL_KEY(k) ((k) & 0x1f)

struct editorConfig {
	struct termios orig_termios;

	int screenrows;
	int screencols;

	int curx, cury; // cursor position
};

struct editorConfig E;

// append buffer structure
struct abuf {
	// TODO: keep a capacity as well, make sure we can resize the buffer independent of the length
	char *b;
	int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf* ab, const char* s, int len) {
	char* buf = realloc(ab->b, ab->len + len);
	memcpy(buf + ab->len, s, len);

	ab->b = buf;
	ab->len += len;
}

void abFree(struct abuf* ab) {
	if(ab->b) {
		free(ab->b);
		ab->b = NULL;
		ab->len = 0;
	}
}

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
	if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) < 0) die("tcsetattr");
}

void enableRawMode() {
	if(tcgetattr(STDIN_FILENO, &E.orig_termios) < 0) die("tcgetattr");
	atexit(disableRawMode);

	//  raw mode flags
	struct termios raw = E.orig_termios;
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

int getCursorPosition(int* rows, int* cols) {
	if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;
	// when the escape sequence is sent to the terminal, we get an input like
	// \x1b[{rows};{cols}R - so we read that into a buffer

	char buf[32];
	for(size_t i=0; i<sizeof(buf)-1; ++i) {
		if(read(STDIN_FILENO, &buf[i], 1) != 1) break; // reached end of input
		if(buf[i]=='R') break; // last character of the expected sequence
	}

	if(buf[0] != '\x1b' || buf[1] != '[') return -1; // first characters do not match
	int rc = sscanf(&buf[2], "%d;%d", rows, cols);
	if(rc != 2) return -1; // could not match the code successfully

	return 0;
}

// returns 0 on success, -1 on failure
int getWindowSize(int* rows, int* cols) {
	struct winsize ws;
	int rc = ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
	if(rc < 0 || ws.ws_col == 0) {
		// fallback case - move to bottom right and then read the position
		if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
		return getCursorPosition(rows, cols);
	}

	*rows = ws.ws_row;
	*cols = ws.ws_col;
	return 0;
}

void initEditor() {
	E.curx = E.cury = 0;

	// TODO: Update window size dynamically
	if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

// renders the display onto an append buffer
void editorDrawRows(struct abuf* ab) {
	for (int i=0; i<E.screenrows; ++i) {
		if(i == E.screenrows / 3) {
			char welcome[80];
			int welcomelen = snprintf(welcome, sizeof(welcome), "Kilo editor -- version %s", KILO_VERSION);
			if(welcomelen > E.screencols) welcomelen = E.screencols;

			int padding = (E.screencols - welcomelen) / 2;
			if(padding) {
				abAppend(ab, "~", 1);
				padding--;
			}
			while(padding--) abAppend(ab, " ", 1);

			abAppend(ab, welcome, welcomelen);
		} else {
			abAppend(ab, "~", 1);
		}
		abAppend(ab, "\x1b[K", 3); // clear current row

		if(i != E.screenrows-1) abAppend(ab, "\r\n", 2);
	}
}

void editorRefreshScreen() {
	struct abuf ab = ABUF_INIT;

	abAppend(&ab, "\x1b[?25l", 6); // hide cursor
	abAppend(&ab, "\x1b[H", 3); // reposition cursor

	editorDrawRows(&ab);

	char cursorBuf[32];
	int cursorBuflen = snprintf(cursorBuf, sizeof(cursorBuf), "\x1b[%d;%dH", E.cury+1, E.curx+1);
	abAppend(&ab, cursorBuf, cursorBuflen);

	abAppend(&ab, "\x1b[?25h", 6); // show cursor

	write(STDOUT_FILENO, ab.b, ab.len);
	abFree(&ab);
}

void editorProcesssKeypress() {
	char c = editorReadKey();
	if(c==CTRL_KEY('q')) die(NULL);
}

int main() {
	enableRawMode();
	initEditor();

	for(;;) {
		editorRefreshScreen();
		editorProcesssKeypress();
	}

	return 0;
}
