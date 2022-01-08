// A super long line at the beginning just to test horizontal scrolling - lorem ipsum dolor sit amet consequitur yeah that's all I can remember
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

enum editorKey {
    ARROW_UP    = 1000,
    ARROW_LEFT,
    ARROW_DOWN,
    ARROW_RIGHT,
    PAGE_UP,
    PAGE_DOWN,
    HOME_KEY,
    END_KEY,
    DEL_KEY
};

struct erow {
    int size;
    char* chars;
};

struct editorConfig {
    struct termios orig_termios;

    int screenrows;
    int screencols;

    int curx, cury; // cursor position

    // editor buffer
    // TODO: Convert to ptr/len/capacity triplet as well
    // Or, better yet, statically allocate the structure
    int numrows;
    struct erow* rows;

    // offset of the first displayed row
    int rowoff;
    int coloff;
} E;

// append buffer structure
struct abuf {
    // TODO: keep a capacity as well, make sure we can resize the buffer independent of the length
    // This can be allocated statically as well, only changed at resize
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
    write(STDOUT_FILENO, "\x1b[2J", 4); // clear the screen after switching to raw mode
}

int editorReadKey() {
    char c = 0;
    int rc = read(STDIN_FILENO, &c, 1);
    if(rc < 0 && errno != EAGAIN) die("read");

    if(c == '\x1b') {
        // parsing escape sequence
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        if (seq[0] == '[') {
            if(isdigit(seq[1])) {
                // we need to read another character
                // since some escape codes are like \x1b[5~
                if(read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if(seq[2] != '~') return '\x1b';

                switch(seq[1]) {
                    case '1': return HOME_KEY;
                    case '3': return DEL_KEY;
                    case '4': return END_KEY;
                    case '5': return PAGE_UP;
                    case '6': return PAGE_DOWN;
                    case '7': return HOME_KEY;
                    case '8': return END_KEY;
                }
            }

            switch (seq[1]) {
            case 'A': return ARROW_UP;
            case 'B': return ARROW_DOWN;
            case 'C': return ARROW_RIGHT;
            case 'D': return ARROW_LEFT;

            case 'H': return HOME_KEY;
            case 'F': return END_KEY;
            }
        }

        if(seq[0] == 'O') {
            switch (seq[1]) {
            case 'H': return HOME_KEY;
            case 'F': return END_KEY;
            }
        }

        return '\x1b';
    }

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

void editorAppendRow(const char* row, size_t len) {
    E.rows = realloc(E.rows, sizeof(struct erow) * (E.numrows + 1));

    int at = E.numrows;
    E.rows[at].size  = len;
    E.rows[at].chars = malloc(len + 1);
    memcpy(E.rows[at].chars, row, len);
    E.rows[at].chars[len] = '\0';
    E.numrows++;
}

void editorOpen(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if(!fp) die("fopen");

    char* line = NULL;
    size_t linecap = 0;

    for(;;) {
        int rc = getline(&line, &linecap, fp);
        if(rc == -1) break;

        // stripping the final newline character
        while(line[rc-1] == '\n' || line[rc-1] == '\r') rc--;
        editorAppendRow(line, rc);
    }
        
    free(line);
    fclose(fp);
}

void initEditor() {
    E.curx = E.cury = 0;
    E.numrows = 0;
    E.rowoff = E.coloff = 0;
    E.rows = NULL;

    // TODO: Update window size dynamically
    if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

void editorScroll() {
    if (E.cury < E.rowoff) E.rowoff = E.cury;
    if (E.cury >= E.rowoff + E.screenrows) E.rowoff = E.cury - E.screenrows + 1;

    if (E.curx < E.coloff) E.coloff = E.curx;
    if (E.curx >= E.coloff + E.screencols) E.coloff = E.curx - E.screencols + 1;
}

// renders the display onto an append buffer
void editorDrawRows(struct abuf* ab) {
    for (int i=0; i<E.screenrows; ++i) {
        int rowNum = i + E.rowoff;
        if(rowNum >= E.numrows) {
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
        } else {
            int len = E.rows[rowNum].size - E.coloff;
            if(len < 0) len = 0;
            if(len > E.screencols) len = E.screencols;
            abAppend(ab, &E.rows[rowNum].chars[E.coloff], len);
        }

        abAppend(ab, "\x1b[K", 3); // clear current row

        if(i != E.screenrows-1) abAppend(ab, "\r\n", 2);
    }
}

void editorRefreshScreen() {
    editorScroll();

    struct abuf ab = ABUF_INIT;

    abAppend(&ab, "\x1b[?25l", 6); // hide cursor
    abAppend(&ab, "\x1b[H", 3); // reposition cursor

    editorDrawRows(&ab);

    char cursorBuf[32];
    int cursorBuflen = snprintf(cursorBuf, sizeof(cursorBuf), "\x1b[%d;%dH", E.cury-E.rowoff+1, E.curx-E.coloff+1);
    abAppend(&ab, cursorBuf, cursorBuflen);

    abAppend(&ab, "\x1b[?25h", 6); // show cursor

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

void editorMoveCursor(int c) {
    struct erow* row = (E.cury >= E.numrows) ? NULL : &E.rows[E.cury];
    int rowlen = 0;

    switch(c) {
    case ARROW_UP:
        if(E.cury) E.cury--;
        break;
    case ARROW_DOWN:
        if(E.cury < E.numrows) E.cury++;
        break;
    case ARROW_LEFT:
        if(E.curx) {
            E.curx--;
        } else if(E.cury) {
            E.cury--;
            rowlen = (E.cury >= E.numrows) ? 0 : E.rows[E.cury].size;
            E.curx = rowlen;
        }
        break;
    case ARROW_RIGHT:
        if(row && E.curx < row->size) {
            E.curx++;
        } else if(row && E.curx == row->size) {
            E.cury++;
            E.curx = 0;
        }
        break;
    }

    row = (E.cury >= E.numrows) ? NULL : &E.rows[E.cury];
    rowlen = row ? row->size : 0;
    if(E.curx >= rowlen) E.curx = rowlen;
}

void editorProcesssKeypress() {
    int c = editorReadKey();
    switch(c) {
    case CTRL_KEY('q'): 
        die(NULL);
        break;

    case HOME_KEY:
        E.curx = 0;
        break;
    case END_KEY:
        E.curx = E.screencols - 1;
        break;

    case PAGE_UP:
    case PAGE_DOWN:
        {
            int times = E.screenrows;
            while(times--)
                editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
        }
    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
        editorMoveCursor(c);
        break;
    }
}

int main(int argc, char** argv) {
    enableRawMode();
    initEditor();
    if(argc >= 2) {
        editorOpen(argv[1]);
    }

    for(;;) {
        editorRefreshScreen();
        editorProcesssKeypress();
    }

    return 0;
}
