# Good fonts for XCB #

A project about creating something similar to Xft but for the XCB.
This fork is an extensive re-write to make the it more similar to Xft and to strip out unneeded functionality.

## Usage ##

```C
#include "xcbft.h"

/* ... */

xcb_render_color_t text_color;
XcbftFace *font = NULL;

// The pixmap we want to draw over
xcb_pixmap_t pmap = xcb_generate_id(c);

/* ... pmap stuffs fill and others ... */

// The fonts to use and the text in unicode
char *fontname = "times:style=bold:pixelsize=30";
const char *text = "Héllo ༃𐤋𐤊탄ཀ𐍊";

// initialize
xcbft_init();

// get the dpi from the resources or the screen if not available
int dpi = xcbft_get_dpi(c);

// load the font
if (!(font = xcbft_font_open_name(fontname, dpi)))
	fprintf(stderr, "could not load font");

if (font) {
	// select a specific color
	text_color.red =	0x4242;
	text_color.green = 0x4242;
	text_color.blue = 0x4242;
	text_color.alpha = 0xFFFF;

	// draw on the drawable (pixmap here) pmap at position (50,60) the text
	// with the color we chose and the faces we chose
	xcbft_draw_string_utf8(
	c, // X connection
	pmap, // win or pixmap
	text_color,
	font,
	50, 60, // x, y
	text);

	// no need for the text and the faces
	xcbft_font_close(font);
}

xcbft_close();

/* ... */

```

Depends on : `xcb xcb-render xcb-renderutil xcb-xrm freetype2 fontconfig`  

