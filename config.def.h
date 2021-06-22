#include "libtmt/tmt.h"

const char *FONT_PATH = "/usr/share/kbd/consolefonts/default8x16.psfu.gz";

inline void init_user() {
	// thin, and as tall as possible
	term.width  = 80;
	term.height = fb.height / font.height;
}

inline void position() {
	// centered
	term.x = (fb.width  - font.width  * term.width ) / 2;
	term.y = (fb.height - font.height * term.height) / 2;
}

uint32_t get_color(tmt_color_t color, bool fg) {
	// gruxbox
	switch (color) {
		case TMT_COLOR_BLACK:   return 0x1d2021;
		case TMT_COLOR_RED:     return 0xcc241d;
		case TMT_COLOR_GREEN:   return 0x98971a;
		case TMT_COLOR_YELLOW:  return 0xd79921;
		case TMT_COLOR_BLUE:    return 0x458588;
		case TMT_COLOR_MAGENTA: return 0xb16286;
		case TMT_COLOR_CYAN:    return 0x689d6a;
		case TMT_COLOR_WHITE:   return 0xa89984;

		default:           return fg ? 0xebdbb2  // foreground
		                             : 0x1d2021; // background
	}
}
