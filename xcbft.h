#ifndef _XCBFT
#define _XCBFT

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <math.h>

#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include <xcb/xcb.h>
#include <xcb/render.h>
#include <xcb/xcb_renderutil.h>
#include <xcb/xcb_xrm.h>

typedef struct {
	FT_Face face;
	FT_Library library;
	int descent;
	int ascent;
} XcbftFace;

typedef struct {
	xcb_render_glyphset_t glyphset;
	FT_Vector advance;
} XcbftGlyphsetAndAdvance;

typedef struct {
	FcChar32 *str;
	unsigned int length;
} UtfHolder;

typedef struct {
	int width;
	int height;
} XcbftTextExtents;

/* signatures */
/* static uint32_t xcb_color_to_uint32(xcb_render_color_t); */
UtfHolder xcbft_char_to_uint32(const char *str);
void xcbft_close(void);
void xcbft_close_glyphs(xcb_connection_t *, XcbftGlyphsetAndAdvance *);
xcb_render_picture_t xcbft_create_pen(xcb_connection_t*, xcb_render_color_t);
FT_Vector xcbft_draw_string_utf8(xcb_connection_t *, xcb_drawable_t, xcb_render_color_t, XcbftFace *, int, int, const char *);
XcbftFace* xcbft_font_open_name(const char *name, int dpi);
void xcbft_font_close(XcbftFace *);
long xcbft_get_dpi(xcb_connection_t *);
bool xcbft_init(void);
XcbftFace* xcbft_load_face(FcPattern *, int);
FT_Vector xcbft_load_glyph(xcb_connection_t *, xcb_render_glyphset_t, FT_Face, int, xcb_render_glyphinfo_t *);
XcbftGlyphsetAndAdvance* xcbft_load_glyphs(xcb_connection_t *, XcbftFace *, UtfHolder, xcb_render_glyphinfo_t **);
FcPattern* xcbft_query_fontsearch(FcChar8 *);
XcbftTextExtents xcbft_text_extents_utf8(xcb_connection_t *, XcbftFace *, const char *);
void xcbft_utf_holder_destroy(UtfHolder holder);

UtfHolder
xcbft_char_to_uint32(const char *str)
{
	UtfHolder holder;
	FcChar32 *output = NULL;
	int length = 0, shift = 0;

	/* there should be less than or same as the strlen of str */
	output = (FcChar32 *)malloc(sizeof(FcChar32)*strlen(str));
	if (!output) {
		puts("couldn't allocate mem for char_to_uint32");
	}

	while (*(str+shift)) {
		shift += FcUtf8ToUcs4((FcChar8*)(str+shift), output+length, strlen(str)-shift);
		length++;
	}

	holder.length = length;
	holder.str = output;

	return holder;
}

void
xcbft_utf_holder_destroy(UtfHolder holder)
{
	free(holder.str);
}

void
xcbft_close(void)
{
	FcFini();
}

bool
xcbft_init(void)
{
	FcBool status;

	status = FcInit();
	if (status == FcFalse) {
		fprintf(stderr, "Could not initialize fontconfig");
	}

	return status == FcTrue;
}

/* Inspired by https://www.codeproject.com/Articles/1202772/Color-Topics-for-Programmers */
/* static uint32_t */
/* xcb_color_to_uint32(xcb_render_color_t rgb) */
/* { */
	/* uint32_t sm1 = 65536 - 1; // from 2^16 */
	/* uint32_t scale = 256; // to 2^8 */

	/* return */
			/* (uint32_t) ( ((double)rgb.red/sm1   * (scale-1)) * scale * scale) */
		/* + (uint32_t) ( ((double)rgb.green/sm1 * (scale-1)) * scale) */
		/* + (uint32_t) ( ((double)rgb.blue/sm1  * (scale-1)) ); */
/* } */

XcbftFace*
xcbft_font_open_name(const char *name, int dpi)
{
	FcPattern *pattern = xcbft_query_fontsearch((FcChar8 *)name);
	if (!pattern)
		return NULL;

	XcbftFace *face = xcbft_load_face(pattern, dpi);
	FcPatternDestroy(pattern);

	return face;
}

/*
 * Do the font queries through fontconfig and return the info
 *
 * Assumes:
 *	Fontconfig is already init & cleaned outside
 *	the FcPattern return needs to be cleaned outside
 */
FcPattern*
xcbft_query_fontsearch(FcChar8 *fontquery)
{
	FcBool status;
	FcPattern *fc_finding_pattern, *pat_output;
	FcResult result;

	fc_finding_pattern = FcNameParse(fontquery);

	// to match we need to fix the pattern (fill unspecified info)
	FcDefaultSubstitute(fc_finding_pattern);
	status = FcConfigSubstitute(NULL, fc_finding_pattern, FcMatchPattern);
	if (status == FcFalse) {
		fprintf(stderr, "could not perform config font substitution");
		return NULL;
	}

	pat_output = FcFontMatch(NULL, fc_finding_pattern, &result);

	FcPatternDestroy(fc_finding_pattern);
	if (result == FcResultMatch) {
		return pat_output;
	} else if (result == FcResultNoMatch) {
		fprintf(stderr, "there wasn't a match");
	} else {
		fprintf(stderr, "the match wasn't as good as it should be");
	}
	return NULL;
}

void
xcbft_font_close(XcbftFace *face)
{
	// FT_Done_Face(face->face);
	FT_Done_FreeType(face->library);
}

XcbftFace*
xcbft_load_face(FcPattern *pattern, int dpi)
{
	FT_Error error;
	FT_Library library;
	XcbftFace *res;
	FcResult result;
	FcValue fc_file, fc_index, fc_matrix, fc_pixel_size;
	FT_Matrix ft_matrix;
	FT_Face face;

	error = FT_Init_FreeType(&library);
	if (error != FT_Err_Ok) {
		perror(NULL);
		return NULL;
	}
	
	result = FcPatternGet(pattern, FC_FILE, 0, &fc_file);
	if (result != FcResultMatch) {
		fprintf(stderr, "font has not file location");
		FT_Done_FreeType(library);
		return NULL;
	}

	result = FcPatternGet(pattern, FC_INDEX, 0, &fc_index);
	if (result != FcResultMatch) {
		fprintf(stderr, "font has no index, using 0 by default");
		fc_index.type = FcTypeInteger;
		fc_index.u.i = 0;
	}

	error = FT_New_Face(library, (const char *) fc_file.u.s, fc_index.u.i, &face);

	if (error == FT_Err_Unknown_File_Format) {
		fprintf(stderr, "wrong file format");
		FT_Done_Face(face);
		FT_Done_FreeType(library);
		return NULL;
	} else if (error == FT_Err_Cannot_Open_Resource) {
		fprintf(stderr, "could not open resource");
		FT_Done_Face(face);
		FT_Done_FreeType(library);
		return NULL;
	} else if (error) {
		fprintf(stderr, "another sort of error");
		FT_Done_Face(face);
		FT_Done_FreeType(library);
		return NULL;
	}

	result = FcPatternGet(pattern, FC_MATRIX, 0, &fc_matrix);
	if (result == FcResultMatch) {
		ft_matrix.xx = (FT_Fixed)(fc_matrix.u.m->xx * 0x10000L);
		ft_matrix.xy = (FT_Fixed)(fc_matrix.u.m->xy * 0x10000L);
		ft_matrix.yx = (FT_Fixed)(fc_matrix.u.m->yx * 0x10000L);
		ft_matrix.yy = (FT_Fixed)(fc_matrix.u.m->yy * 0x10000L);

		// apply the matrix
		FT_Set_Transform(face, &ft_matrix, NULL);
	}

	result = FcPatternGet(pattern, FC_PIXEL_SIZE, 0, &fc_pixel_size);
	if (result != FcResultMatch || fc_pixel_size.u.d == 0) {
		fprintf(stderr, "font has no pixel size, using 12 by default");
		fc_pixel_size.type = FcTypeInteger;
		fc_pixel_size.u.d = 12;
	}
	
	FT_Set_Char_Size(face, 0, (fc_pixel_size.u.d/((double)dpi/72.0))*64, dpi, dpi);
	if (error != FT_Err_Ok) {
		perror(NULL);
		fprintf(stderr, "could not set char size");
		FT_Done_Face(face);
		FT_Done_FreeType(library);
		return NULL;
	}

	res = (XcbftFace *)malloc(sizeof(XcbftFace));
	res->face = face;
	res->library = library;
	res->descent = -(int)(face->size->metrics.descender >> 6);
	res->ascent = (int)(face->size->metrics.ascender >> 6);
	return res;
}

XcbftTextExtents
xcbft_text_extents_utf8(xcb_connection_t *c, XcbftFace *face, const char *text)
{
	XcbftTextExtents res;
	xcb_render_glyphinfo_t *gi;
	int i;

	UtfHolder ctext = xcbft_char_to_uint32(text);
	XcbftGlyphsetAndAdvance *glyphset_advance = xcbft_load_glyphs(c, face, ctext, &gi);

	res.width = 0;
	res.height = 0;
	for (i = 0; i < ctext.length; i++) {
		res.width += gi[i].width > gi[i].x_off ? gi[i].width : gi[i].x_off;
		res.height += gi[i].height > gi[i].y_off ? gi[i].height : gi[i].y_off;
	}
	free(gi);

	xcbft_utf_holder_destroy(ctext);
	xcbft_close_glyphs(c, glyphset_advance);

	return res;
}

FT_Vector
xcbft_draw_string_utf8(xcb_connection_t *c, xcb_drawable_t pmap, xcb_render_color_t color, XcbftFace *face, int x, int y, const char *text)
{
	FT_Vector res;
	xcb_void_cookie_t cookie;
	uint32_t values[2];
	xcb_generic_error_t *error;
	xcb_render_picture_t picture;
	xcb_render_pictforminfo_t *fmt;
	UtfHolder ctext = xcbft_char_to_uint32(text);
	const xcb_render_query_pict_formats_reply_t *fmt_rep = xcb_render_util_query_formats(c);

	/* fmt = xcb_render_util_find_standard_format(fmt_rep, XCB_PICT_STANDARD_RGB_24); */
	fmt = xcb_render_util_find_standard_format(fmt_rep, XCB_PICT_STANDARD_ARGB_32);

	/* create the picture with its attribute and format */
	picture = xcb_generate_id(c);
	values[0] = XCB_RENDER_POLY_MODE_IMPRECISE;
	values[1] = XCB_RENDER_POLY_EDGE_SMOOTH;
	cookie = xcb_render_create_picture_checked(c,
		picture, /* pid */
		pmap, /* drawable from the user */
		fmt->id, /* format */
		XCB_RENDER_CP_POLY_MODE|XCB_RENDER_CP_POLY_EDGE,
		values); /* make it smooth */

	error = xcb_request_check(c, cookie);
	if (error) 
		fprintf(stderr, "ERROR: could not create picture : %d\n", error->error_code);

	/* create a 1x1 pixel pen (on repeat mode) of a certain color */
	xcb_render_picture_t fg_pen = xcbft_create_pen(c, color);

	/* load all the glyphs in a glyphset */
	XcbftGlyphsetAndAdvance *glyphset_advance = xcbft_load_glyphs(c, face, ctext, NULL);

	/* we now have a text stream - a bunch of glyphs basically */
	xcb_render_util_composite_text_stream_t *ts = xcb_render_util_composite_text_stream(glyphset_advance->glyphset, ctext.length, 0);

	/* draw the text at a certain positions */
	xcb_render_util_glyphs_32(ts, x, y, ctext.length, ctext.str);

	/* finally render using the repeated pen color on the picture (which is related to the pixmap) */
	xcb_render_util_composite_text(
		c, /* connection */
		XCB_RENDER_PICT_OP_OVER, /* op */
		fg_pen, /* src */
		picture, /* dst */
		0, /* fmt */
		0, /* src x */
		0, /* src y */
		ts); /* txt stream */

	res.x = glyphset_advance->advance.x;
	res.y = glyphset_advance->advance.y;

	xcb_render_util_composite_text_free(ts);
	xcb_render_free_picture(c, picture);
	xcb_render_free_picture(c, fg_pen);
	xcb_render_util_disconnect(c);
	xcbft_utf_holder_destroy(ctext);
	xcbft_close_glyphs(c, glyphset_advance);

	return res;
}

xcb_render_picture_t
xcbft_create_pen(xcb_connection_t *c, xcb_render_color_t color)
{
	xcb_render_pictforminfo_t *fmt;
	const xcb_render_query_pict_formats_reply_t *fmt_rep = xcb_render_util_query_formats(c);

	/* alpha can only be used with a picture containing a pixmap */
	fmt = xcb_render_util_find_standard_format(fmt_rep, XCB_PICT_STANDARD_ARGB_32);

	xcb_drawable_t root = xcb_setup_roots_iterator(xcb_get_setup(c)).data->root;

	xcb_pixmap_t pm = xcb_generate_id(c);
	xcb_create_pixmap(c, 32, pm, root, 1, 1);

	uint32_t values[1];
	values[0] = XCB_RENDER_REPEAT_NORMAL;

	xcb_render_picture_t picture = xcb_generate_id(c);

	xcb_render_create_picture(c, picture, pm, fmt->id, XCB_RENDER_CP_REPEAT, values);

	xcb_rectangle_t rect = { .x = 0, .y = 0, .width = 1, .height = 1 };

	xcb_render_fill_rectangles(c, XCB_RENDER_PICT_OP_OVER, picture, color, 1, &rect);

	xcb_free_pixmap(c, pm);
	return picture;
}

XcbftGlyphsetAndAdvance*
xcbft_load_glyphs(xcb_connection_t *c, XcbftFace *face, UtfHolder text, xcb_render_glyphinfo_t **gi)
{
	unsigned int i;
	/* int glyph_index; */
	xcb_render_glyphset_t gs;
	xcb_render_pictforminfo_t *fmt_a8;
	FT_Vector total_advance, glyph_advance;
	XcbftGlyphsetAndAdvance *glyphset_advance;
	const xcb_render_query_pict_formats_reply_t *fmt_rep = xcb_render_util_query_formats(c);

	total_advance.x = total_advance.y = 0;
	/* glyph_index = 0; */

	/* create a glyphset with a specific format */
	fmt_a8 = xcb_render_util_find_standard_format(fmt_rep, XCB_PICT_STANDARD_A_8);
	gs = xcb_generate_id(c);
	xcb_render_create_glyph_set(c, gs, fmt_a8->id);

	if (gi) {
		*gi = (xcb_render_glyphinfo_t *)calloc(text.length, sizeof(xcb_render_glyphinfo_t));
	}

	for (i = 0; i < text.length; i++) {
		/* If glyph not found, a box is drawn anyway; so no need to output that error */
		/* glyph_index = FT_Get_Char_Index(face->face, text.str[i]); */
		/* if (!glyph_index) */
			/* fprintf(stderr, "glyph not found"); */

		glyph_advance = xcbft_load_glyph(c, gs, face->face, text.str[i], gi ? &(*gi)[i] : NULL);
		total_advance.x += glyph_advance.x;
		total_advance.y += glyph_advance.y;
	}

	glyphset_advance = (XcbftGlyphsetAndAdvance *)malloc(sizeof(XcbftGlyphsetAndAdvance));
	glyphset_advance->advance = total_advance;
	glyphset_advance->glyphset = gs;
	return glyphset_advance;
}

void
xcbft_close_glyphs(xcb_connection_t *c, XcbftGlyphsetAndAdvance *glyphset_advance)
{
	xcb_render_free_glyph_set(c, glyphset_advance->glyphset);
	free(glyphset_advance);
}

FT_Vector
xcbft_load_glyph(xcb_connection_t *c, xcb_render_glyphset_t gs, FT_Face face, int charcode, xcb_render_glyphinfo_t *gi)
{
	uint32_t gid;
	int glyph_index;
	FT_Vector glyph_advance;
	xcb_render_glyphinfo_t ginfo;
	FT_Bitmap *bitmap;

	FT_Select_Charmap(face, ft_encoding_unicode);
	glyph_index = FT_Get_Char_Index(face, charcode);

	FT_Load_Glyph(face, glyph_index, FT_LOAD_RENDER | FT_LOAD_FORCE_AUTOHINT);

	bitmap = &face->glyph->bitmap;

	ginfo.x = -face->glyph->bitmap_left;
	ginfo.y = face->glyph->bitmap_top;
	ginfo.width = bitmap->width;
	ginfo.height = bitmap->rows;
	glyph_advance.x = face->glyph->advance.x >> 6;
	glyph_advance.y = face->glyph->advance.y >> 6;

	/* Experimental hack: fix for wrong ascent of emoji */
	/* TODO: Only recalculate offset for x when horizontal text and y only when vertical text */
	ginfo.x_off = glyph_advance.x > ginfo.width ? glyph_advance.x : ginfo.width;
	/* ginfo.y_off = glyph_advance.y > ginfo.height ? glyph_advance.y : ginfo.height; */
	ginfo.y_off = glyph_advance.y;

	if (gi) {
		gi->x = ginfo.x;
		gi->y = ginfo.y;
		gi->width = ginfo.width;
		gi->height = ginfo.height;
		gi->x_off = ginfo.x_off;
		gi->y_off = ginfo.y_off;
	}

	/* keep track of the max horiBearingY (yMax) and yMin */
	/* 26.6 fractional pixel format */
	/* yMax = face->glyph->metrics.horiBearingY/64; (yMax); */
	/* yMin = -(face->glyph->metrics.height - */
	/*		face->glyph->metrics.horiBearingY)/64; */

	gid = charcode;

	int stride = (ginfo.width+3)&~3;
	uint8_t *tmpbitmap = calloc(sizeof(uint8_t),stride*ginfo.height);
	int y;

	for (y = 0; y < ginfo.height; y++)
		memcpy(tmpbitmap+y*stride, bitmap->buffer+y*ginfo.width, ginfo.width);

	xcb_render_add_glyphs_checked(c,
		gs, 1, &gid, &ginfo, stride*ginfo.height, tmpbitmap);

	free(tmpbitmap);

	xcb_flush(c);
	return glyph_advance;
}

long
xcbft_get_dpi(xcb_connection_t *c)
{
	int i;
	long dpi = 0;
	long xres;
	xcb_xrm_database_t *xrm_db;

	xrm_db = xcb_xrm_database_from_default(c);
	if (xrm_db != NULL) {
		i = xcb_xrm_resource_get_long(xrm_db, "Xft.dpi", NULL, &dpi);
		xcb_xrm_database_free(xrm_db);

		if (i < 0)
			fprintf(stderr, "Could not fetch value of Xft.dpi from Xresources falling back to highest dpi found\n");
		else 
			return dpi;
		
	} else
		fprintf(stderr, "Could not open Xresources database falling back to highest dpi found\n");
	

	xcb_screen_iterator_t iter = xcb_setup_roots_iterator(xcb_get_setup(c));
	for (; iter.rem; xcb_screen_next(&iter)) {
		/*
		* Inspired by xdpyinfo
		*
		* there are 2.54 centimeters to an inch; so
		* there are 25.4 millimeters.
		*
		* dpi = N pixels / (M millimeters / (25.4 millimeters / 1 inch))
		*     = N pixels / (M inch / 25.4)
		*     = N * 25.4 pixels / M inch
		*/
		if (iter.data != NULL) {
			xres = ((((double) iter.data->width_in_pixels) * 25.4) / ((double) iter.data->width_in_millimeters));

			/* ignore y resolution for now */
			/* yres = ((((double) iter.data->height_in_pixels) * 25.4) / ((double) iter.data->height_in_millimeters)); */
			if (xres > dpi)
				dpi = xres;
		}
	}

	if (!dpi) {
		/* if everything fails use 96 */
		fprintf(stderr, "Could get highest dpi, using 96 as default\n");
		dpi = 96;
	}

	return dpi;
}

#endif /* _XCBFT */

