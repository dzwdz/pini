#include "libtmt/tmt.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <pty.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

// fonts
#include "psf.h"
#include <zlib.h>

#define die(...) {fprintf(stderr, __VA_ARGS__); exit(1);}
#define errstr (strerror(errno)) // i'm tried of typing it over and over

struct {
	TMT *tmt;
	int fd;

	int width;
	int height;

	int x;
	int y;

	struct { int x; int y; } cursor;
} term;

struct {
	uint32_t *buf;
	int width;
	int height;
} fb;

struct {
	uint8_t *glyphs;
	int length;
	int charsize;
	int width;
	int height;
} font;

    void config_init();
    void center();
    void init_fb();
    void load_font(const char *path);
    void render_char(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg);
     int run_pty(const char *cmd, const char *args[]);
    void bridge_stdin(int target);
uint32_t get_color(tmt_color_t color, bool fg);
    void draw_line(TMT *vt, int y);
    void vt_callback(tmt_msg_t m, TMT *vt, const void *a, void *p);

// will land in config.h very soon
void config_init() {
	term.width  = 80;
	term.height = fb.height / font.height;
}

// TODO merge into a resize() fun
void center() {
	term.x = (fb.width  - font.width  * term.width ) / 2;
	term.y = (fb.height - font.height * term.height) / 2;
}

void init_fb() {
	struct fb_var_screeninfo info;
	int fd;

	fd = open("/dev/fb0", O_RDWR);
	if (fd < 0) die("couldn't open the fb: %s\n", errstr);
	
	if (ioctl(fd, FBIOGET_VSCREENINFO, &info) != 0)
		die("failed to get fb info\n");
	fb.width  = info.xres;
	fb.height = info.yres;

	fb.buf = mmap(NULL, 4 * fb.width * fb.height,
	              PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (fb.buf == MAP_FAILED)
		die("mmap(): %s\n", errstr);
}

void load_font(const char *path) {
	// shoutouts to https://cmcenroe.me/2018/01/30/fbclock.html
	gzFile file = gzopen(path, "r");
	struct psf2_header header;

	if (!file)
		die("gzopen failed");
	if (gzfread(&header, sizeof(header), 1, file) != 1)
		die("gzfread failed");

	font.length   = header.length;
	font.charsize = header.charsize;
	font.width    = header.width;
	font.height   = header.height;

	font.glyphs = malloc(sizeof(uint8_t) * font.length * font.charsize);
	if (gzfread(font.glyphs, font.charsize, font.length, file) != font.length)
		die("gzfread failed");
}

void render_char(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg) {
	// straight up stolen from https://cmcenroe.me/2018/01/30/fbclock.html
	if (c > font.length) return;
	uint8_t *glyph = &font.glyphs[c * font.charsize];
	uint32_t stride = font.charsize / font.height;
	for (uint32_t gy = 0; gy < font.height; gy++) {
		for (uint32_t gx = 0; gx < font.width; gx++) {
			uint8_t bits = glyph[gy * stride + gx / 8];
			uint8_t bit  = bits >> (7 - gx % 8) & 1;
			fb.buf[(y * font.height + gy + term.y) * fb.width
			      + x * font.width  + gx + term.x] = bit ? fg : bg;
		}
	}
}

/* runs a process in a new pty, returns the fd of the controlling end */
int run_pty(const char *cmd, const char *args[]) {
	struct winsize ws;
	int fd;
	ws.ws_col = term.width;
	ws.ws_row = term.height;

	switch (forkpty(&fd, NULL, NULL, &ws)) {
		case -1:
			die("forkpty(): %s\n", errstr);
		case 0:
			setenv("TERM", "ansi", 1);
			execvp(cmd, args);
			die("execvp(): %s\n", errstr);
		default:
			return fd;
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
			raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN | ICRNL);
			tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

			while ((len = read(STDIN_FILENO, buf, sizeof(buf))) > 0) {
				write(target, buf, len);
			}
		default:
			return;
	}
}

// TODO this is bad...
uint32_t get_color(tmt_color_t color, bool fg) {
	switch (color) {
		case TMT_COLOR_WHITE: return 0xebdbb2;
		case TMT_COLOR_BLACK: return 0x1d2021;
		default:              return fg ? get_color(TMT_COLOR_WHITE, fg)
		                                : get_color(TMT_COLOR_BLACK, fg);
	}
}

void draw_line(TMT *vt, int y) {
	const TMTSCREEN *s = tmt_screen(vt);

	for (size_t x = 0; x < s->ncol; x++) {
		TMTCHAR chr = s->lines[y]->chars[x];
		uint32_t fg = get_color(chr.a.fg, true);
		uint32_t bg = get_color(chr.a.bg, false);

		if ((term.cursor.x == x && term.cursor.y == y) ^ chr.a.reverse) {
			uint32_t tmp = fg;
			fg = bg;
			bg = tmp;
		}
		render_char(x, y, chr.c, fg, bg);
	}
}

void vt_callback(tmt_msg_t m, TMT *vt, const void *a, void *p) {
	const TMTSCREEN *s = tmt_screen(vt);
	const TMTPOINT *c = tmt_cursor(vt);
	int old_y;

	switch (m) {
		case TMT_MSG_BELL:
			// no.
			break;

		case TMT_MSG_UPDATE:
			for (size_t y = 0; y < s->nline; y++)
				if (s->lines[y]->dirty)
					draw_line(vt, y);
			tmt_clean(vt);
			break;

		case TMT_MSG_ANSWER:
			write(term.fd, a, strlen(a));
			break;

		case TMT_MSG_MOVED:
			old_y = term.cursor.y;
			term.cursor.x = c->c;
			term.cursor.y = c->r;
			draw_line(vt, term.cursor.y);
			if (old_y != term.cursor.y)
				draw_line(vt, old_y);
			break;

		case TMT_MSG_CURSOR:
			//printf("cursor state: %s\n", (const char *)a);
			break;
	}
}

int main() {
	const char *sh[] = { "/bin/sh", NULL };
	char buf[1024];
	int len;

	load_font("/usr/share/kbd/consolefonts/default8x16.psfu.gz");
	init_fb();
	config_init();

	center();

	term.fd = run_pty(sh[0], sh);
	term.tmt = tmt_open(term.height, term.width, vt_callback, NULL, NULL);
	if (!term.tmt) die("tmt_open(): %s\n", errstr);

	bridge_stdin(term.fd);

	while ((len = read(term.fd, buf, sizeof(buf))) > 0) {
		tmt_write(term.tmt, buf, len);
	}

	tmt_close(term.tmt);
}
