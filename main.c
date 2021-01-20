#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
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
	ARROW_DOWN
};

/**
 * Data
 */

// Global configuration
struct editorConfig
{
	int cx, cy;
	int screenrows;
	int screencols;
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
char editorReadKey()
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
			switch(seq[1])
			{
			case 'A': return ARROW_UP;
			case 'B': return ARROW_DOWN;
			case 'C': return ARROW_RIGHT;
			case 'D': return ARROW_LEFT;
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

// Draw rows of the editor
void editorDrawRows(struct abuf *ab)
{
	int y;
	for (y = 0; y < CFG.screenrows; y++)
	{
		if (y == CFG.screenrows / 3)
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

		abAppend(ab, "\x1b[K", 3);  // Clear line from cursor right
		if (y < CFG.screenrows - 1)
			abAppend(ab, "\r\n", 2);
	}
}

void editorRefreshScreen()
{
	struct abuf ab = ABUF_INIT;
	
	abAppend(&ab, "\x1b[?25l", 6);  // Turn off the cursor
	abAppend(&ab, "\x1b[H", 3);

	editorDrawRows(&ab);

	// Set cursor position
	char buf[32];
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", CFG.cy + 1, CFG.cx + 1);
	abAppend(&ab, buf, strlen(buf));

	abAppend(&ab, "\x1b[H", 3);
	abAppend(&ab, "\x1b[?25h", 6);  // Turn on the cursor

	write(STDOUT_FILENO, ab.b, ab.len);
	abFree(&ab);
}

/**
 * Input
 */

void editorMoveCursor(char key)
{
	switch (key)
	{
	case ARROW_LEFT:
		CFG.cx--;
		break;
	case ARROW_RIGHT:
		CFG.cx++;
		break;
	case ARROW_UP:
		CFG.cy--;
		break;
	case ARROW_DOWN:
		CFG.cy++;
		break;
	}
}

void editorProcessKeypress()
{
	char c = editorReadKey();

	switch (c)
	{
	case CTRL_KEY('q'):
		write(STDOUT_FILENO, "\x1b[2J", 4);
		write(STDOUT_FILENO, "\x1b[H", 3);
		exit(0);
		break;

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

	if (getWindowSize(&CFG.screenrows, &CFG.screencols) == -1)
		die("Failed to get window size");
}

// Main entry point
int main()
{
	enableRawMode();
	initEditor();

	while (1)
	{
		editorRefreshScreen();
		editorProcessKeypress();
	}

	return 0;
}
