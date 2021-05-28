/**
 * Includes
 */

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

/**
 * Defines
 */

#define KILO_VERSION "0.0.1"

#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey
{
	ARROW_LEFT = 1000,
	ARROW_RIGHT,
	ARROW_UP,
	ARROW_DOWN,
	DEL_KEY,
	HOME_KEY,
	END_KEY,
	PAGE_UP,
	PAGE_DOWN
};

/**
 * Data
 */

// Global configuration
typedef struct erow
{
	int size;
	char *chars;
} erow;

struct editorConfig
{
	int cx, cy;
	int rowoff;
	int screenrows;
	int screencols;
	int numrows;
	erow *row;
	struct termios orig_termios; // Store original terminal settings
} CFG;

// Print error and exit
void die(const char *s)
{
	write(STDOUT_FILENO, "\x1b[2J", 4);
	write(STDOUT_FILENO, "\x1b[H", 3);

	perror(s);
	exit(1);
}

/**
 * Terminal
 */

// Disable terminal raw mode
void disableRawMode()
{
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &CFG.orig_termios) == -1)
		die("Failed to restore original terminal settings");
}

// Enable terminal raw mode
void enableRawMode()
{
	if (tcgetattr(STDIN_FILENO, &CFG.orig_termios) == -1)
		die("Failed to fetch original terminal settings");
	atexit(disableRawMode);

	struct termios raw = CFG.orig_termios;
	raw.c_iflag &= ~(BRKINT); // Disable BREAK handling
	raw.c_iflag &= ~(ICRNL);  // Diable translation of carriage return to newline
	raw.c_iflag &= ~(INPCK);  // Disable input parity checking
	raw.c_iflag &= ~(ISTRIP); // Disable stripping off of 8th bit
	raw.c_iflag &= ~(IXON);   // Disable control keys to pause/resume transmission

	raw.c_oflag &= ~(OPOST);  // Diable implementation-defined output processing

	raw.c_lflag &= ~(ECHO);   // Don't echo keys to terminal
	raw.c_lflag &= ~(ICANON); // Disable canonical mode (input is available immediately)
	raw.c_lflag &= ~(IEXTEN); // Disable implementation-defined input processing
	raw.c_lflag &= ~(ISIG);   // Disiable signals

	raw.c_cflag |= (CS8);     // Set character size mask to 8 bits

	raw.c_cc[VMIN] = 0;       // Set minimum number of bytes before read() can return
	raw.c_cc[VTIME] = 1;      // Set read() timeout to 1/10th of a second
	
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
		die("Failed to set terminal raw mode");
}

// Read a key press
int editorReadKey()
{
	int nread;
	char c = '\0';
	while ((nread = read(STDIN_FILENO, &c, 1)) != 1)
	{
		if (nread == -1 && errno != EAGAIN)
			die("Failed to read byte");
	}

	if (c == '\x1b')
	{
		char seq[3];

		if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
		if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

		if (seq[0] == '[')
		{
			// Page up and page down
			if (seq[1] >= '0' && seq[1] <= '9')
			{
				if (read(STDIN_FILENO, &seq[2], 1) != 1)
					return '\x1b';
				if (seq[2] == '~')
				{
					switch (seq[1])
					{
					case '1': return HOME_KEY;
					case '3': return DEL_KEY;
					case '4': return END_KEY;
					case '5': return PAGE_UP;
					case '6': return PAGE_DOWN;
					case '7': return HOME_KEY;
					case '9': return END_KEY;
					}
				}
			}
			else
			{
				// Cursor keys
				switch(seq[1])
				{
				case 'A': return ARROW_UP;
				case 'B': return ARROW_DOWN;
				case 'C': return ARROW_RIGHT;
				case 'D': return ARROW_LEFT;
				case 'H': return HOME_KEY;
				case 'F': return END_KEY;
				}
			}
		}
		else if (seq[0] == 'O')
		{
			switch (seq[1])
			{
			case 'H': return HOME_KEY;
			case 'F': return END_KEY;
			}
		}

		return '\x1b';
	}
	
	return c;
}

// Fetches the current position of the cursor
int getCursorPosition(int *rows, int *cols)
{
	// Ask report the cursor position
	if (!rows || !cols || write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
		return -1;

	// Store report in buffer
	char buf[32];
	unsigned int i = 0;
	while (i < sizeof(buf) - 1)
	{
		if (read(STDIN_FILENO, &buf[i], 1) != 1)
			break;
		if (buf[i] == 'R')
			break;
		i++;
	}
	buf[i] = '\0';

	// Validate and try to fetch reported position
	if (buf[0] != '\x1b' || buf[1] != '[')
		return -1;
	if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
		return -1;

	return 0;
}

// Get the size of the window
int getWindowSize(int *rows, int *cols)
{
	if (!rows || !cols)
		return -1;

	struct winsize ws;
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
	{
		// If ioctl does not work, send cursor to position 999,999. This will set it the
		// maximum bounds of the screen itself
		if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) == 12)
		{
			return getCursorPosition(rows, cols);
		}
		return -1;
	}

	*cols = ws.ws_col;
	*rows = ws.ws_row;

	return 0;
}

/**
 * Row operations
 */

void editorAppendRow(char *s, size_t len)
{
	CFG.row = realloc(CFG.row, sizeof(erow) * (CFG.numrows + 1));

	int at = CFG.numrows;
	CFG.row[at].size = len;
	CFG.row[at].chars = malloc(len + 1);
	memcpy(CFG.row[at].chars, s, len);
	CFG.row[at].chars[len] = '\0';
	CFG.numrows++;
}

/**
 * File I/O
 */

void editorOpen(char *filename)
{
	FILE *fp = fopen(filename, "r");
	if (!fp) die("fopen");

	char *line = NULL;
	size_t linecap = 0;
	ssize_t linelen;
	while ((linelen = getline(&line, &linecap, fp)) != -1)
	{
		while (linelen > 0 && (line[linelen - 1] == '\n' ||
							   line[linelen - 1] == '\r'))
		{
			linelen--;
		}
		editorAppendRow(line, linelen);
	}
	free(line);
	fclose(fp);
}

/**
 * Append buffer
 */
 
// Define structure
struct abuf
{
	char *b;
	int len;
};

// Initialize buffer
#define ABUF_INIT { NULL, 0 }

// Add to append buffer
void abAppend(struct abuf *ab, const char *s, int len)
{
	char *new = realloc(ab->b, ab->len + len);
	
	if (!new)
		return;
		
	memcpy(&new[ab->len], s, len);
	ab->b = new;
	ab->len += len;
}

// Free the append buffer
void abFree(struct abuf *ab)
{
	free(ab->b);
}

/**
 * Output
 */

void editorScroll()
{
	if (CFG.cy < CFG.rowoff)
	{
		CFG.rowoff = CFG.cy;
	}
	if (CFG.cy >= CFG.rowoff + CFG.screenrows)
	{
		CFG.rowoff = CFG.cy - CFG.screenrows + 1;
	}
}

// Draw rows of the editor
void editorDrawRows(struct abuf *ab)
{
	int y;
	for (y = 0; y < CFG.screenrows; y++)
	{
		int filerow = y + CFG.rowoff;
		if (filerow >= CFG.numrows)
		{
			if (CFG.numrows == 0 && y == CFG.screenrows / 3)
			{
				char welcome[80];
				int welcomelen = snprintf(welcome, sizeof(welcome), "Kilo editor -- version %s", KILO_VERSION);
				if (welcomelen > CFG.screencols)
					welcomelen = CFG.screencols;
				int padding = (CFG.screencols - welcomelen) / 2;
				if (padding)
				{
					abAppend(ab, "~", 1);
					padding--;
				}
				while (padding--)
					abAppend(ab, " ", 1);
				abAppend(ab, welcome, welcomelen);
			}
			else
			{
				abAppend(ab, "~", 1);
			}
		}
		else
		{
			int len = CFG.row[filerow].size;
			if (len > CFG.screencols)
			{
				len = CFG.screencols;
			}
			abAppend(ab, CFG.row[filerow].chars, len);
		}

		abAppend(ab, "\x1b[K", 3);  // Clear line from cursor right
		if (y < CFG.screenrows - 1)
			abAppend(ab, "\r\n", 2);
	}
}

void editorRefreshScreen()
{
	editorScroll();

	struct abuf ab = ABUF_INIT;
	
	abAppend(&ab, "\x1b[?25l", 6);  // Turn off the cursor
	abAppend(&ab, "\x1b[H", 3);

	editorDrawRows(&ab);

	// Set cursor position
	char buf[32];
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (CFG.cy - CFG.rowoff) + 1, CFG.cx + 1);
	abAppend(&ab, buf, strlen(buf));

	abAppend(&ab, "\x1b[?25h", 6);  // Turn on the cursor

	write(STDOUT_FILENO, ab.b, ab.len);
	abFree(&ab);
}

/**
 * Input
 */

void editorMoveCursor(int key)
{
	switch (key)
	{
	case ARROW_LEFT:
		if (CFG.cx != 0)
			CFG.cx--;
		break;
	case ARROW_RIGHT:
		if (CFG.cx != CFG.screencols - 1)
			CFG.cx++;
		break;
	case ARROW_UP:
		if (CFG.cy != 0)
			CFG.cy--;
		break;
	case ARROW_DOWN:
		if (CFG.cy < CFG.numrows)
			CFG.cy++;
		break;
	}
}

void editorProcessKeypress()
{
	int c = editorReadKey();

	switch (c)
	{
	case CTRL_KEY('q'):
		write(STDOUT_FILENO, "\x1b[2J", 4);
		write(STDOUT_FILENO, "\x1b[H", 3);
		exit(0);
		break;

	case HOME_KEY:
		CFG.cx = 0;
		break;

	case END_KEY:
		CFG.cx = CFG.screencols - 1;
		break;

	case PAGE_UP:
	case PAGE_DOWN:
	{
		int times = CFG.screenrows;
		while (times--)
			editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
		break;
	}

	case ARROW_UP:
	case ARROW_DOWN:
	case ARROW_LEFT:
	case ARROW_RIGHT:
		editorMoveCursor(c);
		break;
	}
}

/**
 * Init
 */

// Initialze the global configuration
void initEditor()
{
	CFG.cx = 0;
	CFG.cy = 0;
	CFG.rowoff = 0;
	CFG.numrows = 0;
	CFG.row = NULL;

	if (getWindowSize(&CFG.screenrows, &CFG.screencols) == -1)
		die("Failed to get window size");
}

// Main entry point
int main(int argc, char *argv[])
{
	enableRawMode();
	initEditor();

	if (argc >= 2)
	{
		editorOpen(argv[1]);
	}

	while (1)
	{
		editorRefreshScreen();
		editorProcessKeypress();
	}

	return 0;
}
