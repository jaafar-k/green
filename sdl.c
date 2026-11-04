/* green - the PDF reader
 * Copyright (C) 2009 Florian Tobias Schandinat
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <termios.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <SDL.h>
#include <pango/pangocairo.h>
#include "green.h"


#define FLAG_QUIT	0x0001
#define FLAG_RENDER	0x0002


typedef enum
{
	NORMAL, GOTO, SEARCH, FIT, ROTATE, MIRROR, TOC, BOOKMARKS, FILEPICKER, HELP
	
}	RState;

typedef struct
{
	char	buff[64];
	unsigned char	used, cur;
	
}	IBuffer;


const Uint32	live_interval = 40;

static RState	g_state = NORMAL;
static IBuffer	g_input = {.buff = {0}, .used = 0, .cur = 0};

typedef struct TOCNode
{
	char	*title;
	int	page;
		// 0-based page index, -1 if none
	bool	open;
	int	n_children;
	struct TOCNode	**children;
	
}	TOCNode;

typedef struct
{
	TOCNode	*node;
	int	depth;
	
}	TOCEntry;

static TOCNode	*g_toc = NULL;
static TOCEntry	*g_toc_entries = NULL;
static int	g_toc_count = 0, g_toc_sel = 0, g_toc_doc = -1;
static bool	g_toc_active = false;

typedef struct
{
	char	*uri;
	int	page;
		// 0-based page index
}	Bookmark;

static Bookmark	*g_bookmarks = NULL;
static int	g_bm_count = 0;
static int	*g_bm_view = NULL;
static int	g_bm_view_count = 0, g_bm_sel = 0;
static bool	g_bm_active = false;
static bool	g_bm_loaded = false;

/* Miyoo Select: hold for chords, release alone opens bookmarks */
static bool	g_select_held = false;
static bool	g_select_chord = false;
/* Miyoo X/Y (LSHIFT/LALT): zoom on release so Shift+b etc. still work */
static bool	g_x_held = false;
static bool	g_x_chord = false;
static bool	g_y_held = false;
static bool	g_y_chord = false;

/* Miyoo Mini panel is mounted 180°: write logical upright coords remapped into the FB */
static int	g_rotate180 = 0;

/* Brief top-right status toast (e.g. bookmark toggle) */
static char	g_toast_msg[32];
static Uint32	g_toast_until = 0;
#define TOAST_MS	3000

/* In-app PDF/folder picker (Select+R1); names drawn with Pango for Arabic */
typedef struct
{
	char	*name;
	char	*path;
	int	is_dir;
}	FpEntry;

static FpEntry	*g_fp_entries = NULL;
static int	g_fp_count = 0, g_fp_sel = 0;
static char	g_fp_dir[512] = "";
static bool	g_fp_active = false;

/* Hold Up/Down to scrub TOC / bookmarks / help (Miyoo often has no SDL key repeat) */
static int	g_list_nav_dir = 0;
static Uint32	g_list_nav_next = 0;
#define LIST_NAV_DELAY_MS	280
#define LIST_NAV_RATE_MS	50

static bool	g_help_active = false;
static int	g_help_scroll = 0;

static const char	*g_help_lines[] = {
	"Miyoo controls",
	"",
	"D-pad          Scroll / navigate lists",
	"A              Fit width / confirm in menus",
	"B              Rotate 90 / back / close help",
	"X              Zoom in (tap)",
	"Y              Zoom out (tap)",
	"L1 / R1        Prev / next page",
	"L2 / R2        Jump -10 / +10 pages",
	"Start          Toggle table of contents",
	"Select (tap)   Toggle bookmarks",
	"Menu           Close overlay / close file",
	"",
	"Select chords",
	"",
	"Select+A       Night mode",
	"Select+B       Toggle bookmark",
	"Select+X       Mirror horizontal",
	"Select+Y       Mirror vertical",
	"Select+L1      Dual-page mode",
	"Select+R1      PDF / folder picker",
	"Select+L2      This help",
	"",
	"In ToC",
	"",
	"Up/Down        Move (hold to scrub)",
	"Left/Right     Collapse / expand",
	"A              Jump to section",
	"B / Start      Close",
	"",
	"In Bookmarks",
	"",
	"Up/Down        Move (hold to scrub)",
	"A              Jump to bookmark",
	"Y              Add current page",
	"X              Delete selected",
	"B              Close",
	"",
	"In File picker",
	"",
	"A / Right      Open folder or PDF",
	"B / Left       Parent folder / close",
	"Select+R1      Close picker",
};
#define HELP_LINE_COUNT	((int)(sizeof( g_help_lines ) / sizeof( g_help_lines[0] )))

static int	ViewWidth( Green_RTD *rtd );
static RState	OpenFilePicker( Green_RTD *rtd, unsigned short *flags );
static void	CloseFilePicker( unsigned short *flags );
static RState	OpenHelpPanel( unsigned short *flags );
static void	RenderHelp( SDL_Surface *display );
static RState	HelpInput( SDL_Event *event, unsigned short *flags );
static void	HelpStepScroll( int dir, int max_scroll );
static int	HelpMaxScroll( SDL_Surface *display );

static int	IsMiyoo( void )
{
	return access( "/mnt/SDCARD", F_OK ) == 0
		|| access( "/customer", F_OK ) == 0;
}

static SDL_Surface	*DrawSurface( void )
{
	return SDL_GetVideoSurface();
}

/* Map logical upright rect → physical FB rect (identity when not rotating) */
static void	FlipRect( SDL_Surface *s, SDL_Rect *r )
{
	if (!g_rotate180 || !s || !r)
		return;
	r->x = s->w - r->x - r->w;
	r->y = s->h - r->y - r->h;
}

static void	FillRect( SDL_Surface *s, SDL_Rect *r, Uint32 color )
{
	SDL_Rect	t;
	
	if (!r)
	{
		SDL_FillRect( s, NULL, color );
		return;
	}
	t = *r;
	FlipRect( s, &t );
	SDL_FillRect( s, &t, color );
}

/* Logical (lx,ly) → pointer into FB; horizontal scan uses DstXStepBytes */
static Uint8	*DstPixel( SDL_Surface *d, int lx, int ly )
{
	int	px = lx, py = ly;
	
	if (g_rotate180)
	{
		px = d->w - 1 - lx;
		py = d->h - 1 - ly;
	}
	return (Uint8 *)d->pixels + py * d->pitch + px * d->format->BytesPerPixel;
}

static int	DstXStepBytes( SDL_Surface *d )
{
	return g_rotate180 ? -d->format->BytesPerPixel : d->format->BytesPerPixel;
}

/* No full-frame rotate copy — pixels were written already remapped */
static void	Present( void )
{
	SDL_Surface	*v = SDL_GetVideoSurface();
	
	if (!v)
		return;
	if (g_rotate180)
		SDL_Flip( v );
	else
		SDL_UpdateRect( v, 0, 0, 0, 0 );
}

// Simple 5x7 bitmap font for basic ASCII characters (32-126)
// Each character is represented by 7 bytes, one per row
static const unsigned char bitmap_font[][7] = {
	{0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 32: space
	{0x04,0x04,0x04,0x04,0x00,0x04,0x00}, // 33: !
	{0x0A,0x0A,0x00,0x00,0x00,0x00,0x00}, // 34: "
	{0x0A,0x1F,0x0A,0x0A,0x1F,0x0A,0x00}, // 35: #
	{0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04}, // 36: $
	{0x18,0x19,0x02,0x04,0x08,0x13,0x03}, // 37: %
	{0x0C,0x12,0x14,0x08,0x15,0x12,0x0D}, // 38: &
	{0x04,0x04,0x00,0x00,0x00,0x00,0x00}, // 39: '
	{0x02,0x04,0x08,0x08,0x08,0x04,0x02}, // 40: (
	{0x08,0x04,0x02,0x02,0x02,0x04,0x08}, // 41: )
	{0x00,0x04,0x15,0x0E,0x15,0x04,0x00}, // 42: *
	{0x00,0x04,0x04,0x1F,0x04,0x04,0x00}, // 43: +
	{0x00,0x00,0x00,0x00,0x04,0x04,0x08}, // 44: ,
	{0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, // 45: -
	{0x00,0x00,0x00,0x00,0x00,0x04,0x00}, // 46: .
	{0x01,0x02,0x04,0x08,0x10,0x00,0x00}, // 47: /
	{0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, // 48: 0
	{0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, // 49: 1
	{0x0E,0x11,0x01,0x06,0x08,0x10,0x1F}, // 50: 2
	{0x0E,0x11,0x01,0x06,0x01,0x11,0x0E}, // 51: 3
	{0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, // 52: 4
	{0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, // 53: 5
	{0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, // 54: 6
	{0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, // 55: 7
	{0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, // 56: 8
	{0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}, // 57: 9
	{0x00,0x04,0x00,0x00,0x04,0x00,0x00}, // 58: :
	{0x00,0x04,0x00,0x00,0x04,0x04,0x08}, // 59: ;
	{0x02,0x04,0x08,0x10,0x08,0x04,0x02}, // 60: <
	{0x00,0x00,0x1F,0x00,0x1F,0x00,0x00}, // 61: =
	{0x08,0x04,0x02,0x01,0x02,0x04,0x08}, // 62: >
	{0x0E,0x11,0x01,0x02,0x04,0x00,0x04}, // 63: ?
	{0x0E,0x11,0x17,0x15,0x17,0x10,0x0E}, // 64: @
	{0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, // 65: A
	{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, // 66: B
	{0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, // 67: C
	{0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}, // 68: D
	{0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, // 69: E
	{0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, // 70: F
	{0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, // 71: G
	{0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, // 72: H
	{0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, // 73: I
	{0x07,0x02,0x02,0x02,0x02,0x12,0x0C}, // 74: J
	{0x11,0x12,0x14,0x18,0x14,0x12,0x11}, // 75: K
	{0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, // 76: L
	{0x11,0x1B,0x15,0x11,0x11,0x11,0x11}, // 77: M
	{0x11,0x19,0x15,0x13,0x11,0x11,0x11}, // 78: N
	{0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, // 79: O
	{0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, // 80: P
	{0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, // 81: Q
	{0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, // 82: R
	{0x0E,0x11,0x10,0x0E,0x01,0x11,0x0E}, // 83: S
	{0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, // 84: T
	{0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, // 85: U
	{0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, // 86: V
	{0x11,0x11,0x11,0x15,0x15,0x1B,0x11}, // 87: W
	{0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, // 88: X
	{0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, // 89: Y
	{0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, // 90: Z
	{0x0E,0x08,0x08,0x08,0x08,0x08,0x0E}, // 91: [
	{0x10,0x08,0x04,0x02,0x01,0x00,0x00}, // 92: backslash
	{0x0E,0x02,0x02,0x02,0x02,0x02,0x0E}, // 93: ]
	{0x04,0x0A,0x11,0x00,0x00,0x00,0x00}, // 94: ^
	{0x00,0x00,0x00,0x00,0x00,0x00,0x1F}, // 95: _
	{0x08,0x04,0x00,0x00,0x00,0x00,0x00}, // 96: `
	{0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F}, // 97: a
	{0x10,0x10,0x16,0x19,0x11,0x11,0x1E}, // 98: b
	{0x00,0x00,0x0E,0x10,0x10,0x11,0x0E}, // 99: c
	{0x01,0x01,0x0D,0x13,0x11,0x11,0x0F}, // 100: d
	{0x00,0x00,0x0E,0x11,0x1F,0x10,0x0E}, // 101: e
	{0x06,0x09,0x08,0x1C,0x08,0x08,0x08}, // 102: f
	{0x00,0x0F,0x11,0x11,0x0F,0x01,0x0E}, // 103: g
	{0x10,0x10,0x16,0x19,0x11,0x11,0x11}, // 104: h
	{0x04,0x00,0x0C,0x04,0x04,0x04,0x0E}, // 105: i
	{0x02,0x00,0x06,0x02,0x02,0x12,0x0C}, // 106: j
	{0x10,0x10,0x12,0x14,0x18,0x14,0x12}, // 107: k
	{0x0C,0x04,0x04,0x04,0x04,0x04,0x0E}, // 108: l
	{0x00,0x00,0x1A,0x15,0x15,0x11,0x11}, // 109: m
	{0x00,0x00,0x16,0x19,0x11,0x11,0x11}, // 110: n
	{0x00,0x00,0x0E,0x11,0x11,0x11,0x0E}, // 111: o
	{0x00,0x00,0x1E,0x11,0x1E,0x10,0x10}, // 112: p
	{0x00,0x00,0x0D,0x13,0x0F,0x01,0x01}, // 113: q
	{0x00,0x00,0x16,0x19,0x10,0x10,0x10}, // 114: r
	{0x00,0x00,0x0E,0x10,0x0E,0x01,0x1E}, // 115: s
	{0x08,0x08,0x1C,0x08,0x08,0x09,0x06}, // 116: t
	{0x00,0x00,0x11,0x11,0x11,0x13,0x0D}, // 117: u
	{0x00,0x00,0x11,0x11,0x11,0x0A,0x04}, // 118: v
	{0x00,0x00,0x11,0x11,0x15,0x15,0x0A}, // 119: w
	{0x00,0x00,0x11,0x0A,0x04,0x0A,0x11}, // 120: x
	{0x00,0x00,0x11,0x11,0x0F,0x01,0x0E}, // 121: y
	{0x00,0x00,0x1F,0x02,0x04,0x08,0x1F}, // 122: z
	{0x06,0x08,0x08,0x10,0x08,0x08,0x06}, // 123: {
	{0x04,0x04,0x04,0x00,0x04,0x04,0x04}, // 124: |
	{0x0C,0x02,0x02,0x01,0x02,0x02,0x0C}, // 125: }
	{0x00,0x08,0x15,0x02,0x00,0x00,0x00}  // 126: ~
};


static Uint32	MapDisplayPixel( Uint32 src, SDL_PixelFormat *fmt, bool night )
{
	Uint32	r = (src >> 16) & 0xFF,
		g = (src >> 8) & 0xFF,
		b = src & 0xFF;
	
	if (night)
	{
		r = 0xFF - r;
		g = 0xFF - g;
		b = 0xFF - b;
	}
	
	return (((r >> fmt->Rloss) << fmt->Rshift)
		| ((g >> fmt->Gloss) << fmt->Gshift)
		| ((b >> fmt->Bloss) << fmt->Bshift));
}

static Uint32	MapBlendPixel( Uint32 src, SDL_PixelFormat *fmt,
	Uint32 ia, Uint32 ar, Uint32 ag, Uint32 ab, bool night )
{
	Uint32	r = (((src >> 16) & 0xFF) * ia + ar) / 256,
		g = (((src >> 8) & 0xFF) * ia + ag) / 256,
		b = ((src & 0xFF) * ia + ab) / 256;
	
	if (night)
	{
		r = 0xFF - (r & 0xFF);
		g = 0xFF - (g & 0xFF);
		b = 0xFF - (b & 0xFF);
	}
	
	return (((r >> fmt->Rloss) << fmt->Rshift)
		| ((g >> fmt->Gloss) << fmt->Gshift)
		| ((b >> fmt->Bloss) << fmt->Bshift));
}

void	GetInput( IBuffer *input, SDL_Event *event )
{
	char	c;
	int i;
	
	switch (event->key.keysym.sym)
	{
		case SDLK_LEFT:
			if (input->cur)
				input->cur--;
			
			break;
		case SDLK_RIGHT:
			if (input->cur < input->used)
				input->cur++;
			
			break;
		case SDLK_BACKSPACE:
			if (!input->cur)
				break;
			
			input->cur--;
		case SDLK_DELETE:
			if (input->cur == input->used)
				break;
			
			for (i = input->cur; i < input->used-1; i++)
				input->buff[i] = input->buff[i+1];
			
			input->used--;
			break;
		case 'a'...'z':
			if (event->key.keysym.mod & KMOD_SHIFT)
				event->key.keysym.sym += 'A' - 'a';
		case '0'...'9':
		case '+':
		case '-':
		case '*':
		case '/':
		case '=':
		case '_':
		case '.':
		case ':':
		case ',':
		case ';':
		case '!':
		case '?':
		case '(':
		case ')':
		case '@':
		case ' ':
		case '$':
		case '&':
		case '#':
		case '\\':
			if (input->used == sizeof( input->buff ) - 1)
				break;
			
			for (i = input->cur; i < input->used; i++)
				input->buff[i+1] = input->buff[i];
			
			c = event->key.keysym.sym;
			input->buff[input->cur] = c;
			input->cur++;
			input->used++;
			break;
		default:
			break;
	}
	
	return;
}

void	RenderPage( Green_RTD *rtd, SDL_Rect dest, int xoff, int yoff, PopplerPage *page, double tscale, int page_num )
{
	PopplerRectangle	*rect;
	Green_Document	*doc = rtd->docs[rtd->doc_cur];
	SDL_Surface	*display = DrawSurface();
	SDL_PixelFormat	fmt = *display->format;
	cairo_surface_t	*surface;
	cairo_t		*context;
	void	*pixels;
	unsigned short	ar, ag, ab, ia;
	gdouble	tmp_d;
	double	pwidth, pheight;
	guint	i, n;
	GList	*list = NULL;
	Uint32	*src, *dst;
	int	x, y, rowstride, w, h, dir_x, dir_y;
	
	if (doc->search_str)
		list = poppler_page_find_text( page, doc->search_str );
	
	if (doc->cache.surface && doc->cache.page == page_num && doc->cache.tscale == tscale)
		surface = doc->cache.surface;
	else
	{
		Green_GetDimension( page, &w, &h, tscale, false );
		surface = cairo_image_surface_create( CAIRO_FORMAT_ARGB32, w, h );
		context = cairo_create( surface );
		cairo_save( context );
		cairo_scale( context, tscale, tscale );
		poppler_page_render( page, context );
		cairo_restore( context );
		cairo_set_operator( context, CAIRO_OPERATOR_DEST_OVER );
		cairo_set_source_rgb( context, 1., 1., 1. );
		cairo_paint( context );
		cairo_destroy( context );
		if (doc->cache.surface)
			cairo_surface_destroy( doc->cache.surface );
		
		doc->cache.surface = surface;
		doc->cache.page = page_num;
		doc->cache.tscale = tscale;
	}
	
	if (doc->rotation == 1)
	{
		dir_x = -1;
		dir_y = 1;
	}
	else if (doc->rotation == 2)
	{
		dir_x = -1;
		dir_y = -1;
	}
	else if (doc->rotation == 3)
	{
		dir_x = 1;
		dir_y = -1;
	}
	else
		dir_x = dir_y = 1;
	
	if (doc->mirrored)
		dir_y *= -1;
	
	pixels = cairo_image_surface_get_data( surface );
	rowstride = cairo_image_surface_get_stride( surface );
	SDL_LockSurface( display );
	if (doc->rotation % 2)
	{
		for (y = 0; y < dest.h; y++)
		{
			src = (Uint32 *)((char *)pixels + (xoff + dir_y * y + (dir_y < 0 ? dest.h - 1 : 0)) * 4
				+ (yoff + (dir_x < 0 ? dest.w - 1 : 0)) * rowstride);
			dst = (Uint32 *)DstPixel( display, dest.x, dest.y + y );
			for (x = 0; x < dest.w; x++)
			{
				*dst = MapDisplayPixel( *src, &fmt, rtd->night );
				src = (Uint32 *)((char *)src + dir_x * rowstride);
				dst = (Uint32 *)((Uint8 *)dst + DstXStepBytes( display ));
			}
		}
	}
	else
	{
		for (y = 0; y < dest.h; y++)
		{
			src = (Uint32 *)((char *)pixels + (yoff + dir_y * y + (dir_y < 0 ? dest.h - 1 : 0)) * rowstride
				+ (xoff + (dir_x < 0 ? dest.w - 1 : 0)) * 4);
			dst = (Uint32 *)DstPixel( display, dest.x, dest.y + y );
			for (x = 0; x < dest.w; x++)
			{
				*dst = MapDisplayPixel( *src, &fmt, rtd->night );
				src += dir_x;
				dst = (Uint32 *)((Uint8 *)dst + DstXStepBytes( display ));
			}
		}
	}
	
	if (list)
	{
		poppler_page_get_size( page, &pwidth, &pheight );
		ar = rtd->c_highlight.a * rtd->c_highlight.r;
		ag = rtd->c_highlight.a * rtd->c_highlight.g;
		ab = rtd->c_highlight.a * rtd->c_highlight.b;
		ia = 0xFF - rtd->c_highlight.a;
		n = g_list_length( list );
		for (i = 0; i < n; i++)
		{
			rect = g_list_nth_data( list, i );
			tmp_d = pheight - rect->y2;
			rect->y2 = pheight - rect->y1;
			rect->y1 = tmp_d;
			rect->x1 *= tscale;
			rect->y1 *= tscale;
			rect->x2 *= tscale;
			rect->y2 *= tscale;
			rect->x1 -= xoff;
			rect->y1 -= yoff;
			rect->x2 -= xoff;
			rect->y2 -= yoff;
			if (doc->rotation % 2)
			{
				tmp_d = rect->x1;
				rect->x1 = rect->y1;
				rect->y1 = tmp_d;
				tmp_d = rect->x2;
				rect->x2 = rect->y2;
				rect->y2 = tmp_d;
			}
			
			if (doc->mirrored)
			{
				tmp_d = rect->y1;
				rect->y1 = dest.h - rect->y2;
				rect->y2 = dest.h - tmp_d;
			}
			
			if (doc->rotation == 1)
			{
				tmp_d = rect->x1;
				rect->x1 = dest.w - rect->x2;
				rect->x2 = dest.w - tmp_d;
			}
			else if (doc->rotation == 2)
			{
				tmp_d = rect->x1;
				rect->x1 = dest.w - rect->x2;
				rect->x2 = dest.w - tmp_d;
				tmp_d = rect->y1;
				rect->y1 = dest.h - rect->y2;
				rect->y2 = dest.h - tmp_d;
			}
			else if (doc->rotation == 3)
			{
				tmp_d = rect->y1;
				rect->y1 = dest.h - rect->y2;
				rect->y2 = dest.h - tmp_d;
			}
			
			if (rect->x1 > dest.w)
				continue;
			else if (rect->x1 < 0)
				rect->x1 = 0;
			
			if (rect->x2 < 0)
				continue;
			else if (rect->x2 > dest.w)
				rect->x2 = dest.w;
			
			if (rect->y1 > dest.h)
				continue;
			else if (rect->y1 < 0)
				rect->y1 = 0;
			
			if (rect->y2 < 0)
				continue;
			else if (rect->y2 > dest.h)
				rect->y2 = dest.h;
			
			if (doc->rotation % 2)
			{
				for (y = rect->y1; y < (int)rect->y2; y++)
				{
					src = (Uint32 *)((char *)pixels + (xoff + dir_y * y + (dir_y < 0 ? dest.h - 1 : 0)) * 4
						+ (yoff + dir_x * (int)rect->x1 + (dir_x < 0 ? dest.w - 1 : 0)) * rowstride);
					dst = (Uint32 *)DstPixel( display, dest.x + (int)rect->x1, dest.y + y );
					for (x = rect->x1; x < (int)rect->x2; x++)
					{
						*dst = MapBlendPixel( *src, &fmt, ia, ar, ag, ab, rtd->night );
						src = (Uint32 *)((char *)src + dir_x * rowstride);
						dst = (Uint32 *)((Uint8 *)dst + DstXStepBytes( display ));
					}
				}
			}
			else
			{
				for (y = rect->y1; y < (int)rect->y2; y++)
				{
					src = (Uint32 *)((char *)pixels + (yoff + dir_y * y + (dir_y < 0 ? dest.h - 1 : 0)) * rowstride
						+ (xoff + dir_x * (int)rect->x1 + (dir_x < 0 ? dest.w - 1 : 0)) * 4);
					dst = (Uint32 *)DstPixel( display, dest.x + (int)rect->x1, dest.y + y );
					for (x = rect->x1; x < (int)rect->x2; x++)
					{
						*dst = MapBlendPixel( *src, &fmt, ia, ar, ag, ab, rtd->night );
						src += dir_x;
						dst = (Uint32 *)((Uint8 *)dst + DstXStepBytes( display ));
					}
				}
			}
		}
		
		g_list_free( list );
	}
	
	SDL_UnlockSurface( display );
	return;
}

// Draw a single character using bitmap font
void DrawChar( SDL_Surface *display, int x, int y, char c, Uint32 color )
{
	SDL_Rect pixel;
	int row, col;
	unsigned char bits;
	
	// Only handle printable ASCII characters
	if (c < 32 || c > 126)
		return;
	
	// Get the bitmap for this character
	const unsigned char *char_bitmap = bitmap_font[c - 32];
	
	// Draw the character pixel by pixel
	pixel.w = 2;  // Make pixels 2x2 for better visibility
	pixel.h = 2;
	
	for (row = 0; row < 7; row++)
	{
		bits = char_bitmap[row];
		for (col = 0; col < 5; col++)
		{
			if (bits & (1 << (4 - col)))
			{
				pixel.x = x + col * 2;
				pixel.y = y + row * 2;
				FillRect( display, &pixel, color );
			}
		}
	}
}

// Draw a string using bitmap font
void DrawString( SDL_Surface *display, int x, int y, const char *str, Uint32 color )
{
	int i;
	int char_width = 12; // 5 pixels * 2 + 2 pixel spacing
	
	for (i = 0; str[i] != '\0'; i++)
	{
		DrawChar( display, x + i * char_width, y, str[i], color );
	}
}

/* True if UTF-8 text contains Arabic / other strong RTL letters */
static int	TextHasRtl( const char *s )
{
	const unsigned char	*p = (const unsigned char *)s;
	
	if (!p)
		return 0;
	while (*p)
	{
		unsigned int	cp;
		
		if (*p < 0x80)
		{
			p++;
			continue;
		}
		if ((*p & 0xE0) == 0xC0 && p[1])
		{
			cp = ((*p & 0x1F) << 6) | (p[1] & 0x3F);
			p += 2;
		}
		else if ((*p & 0xF0) == 0xE0 && p[1] && p[2])
		{
			cp = ((*p & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
			p += 3;
		}
		else if ((*p & 0xF8) == 0xF0 && p[1] && p[2] && p[3])
		{
			cp = ((*p & 0x07) << 18) | ((p[1] & 0x3F) << 12)
				| ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
			p += 4;
		}
		else
		{
			p++;
			continue;
		}
		/* Arabic, Arabic Supplement/Extended, Presentation Forms, Hebrew */
		if ((cp >= 0x0590 && cp <= 0x08FF)
			|| (cp >= 0xFB1D && cp <= 0xFDFF)
			|| (cp >= 0xFE70 && cp <= 0xFEFF))
			return 1;
	}
	return 0;
}

/* Draw UTF-8 (Arabic-capable) via Pango; max_w ellipsizes. Returns used height. */
static int	DrawUtf8( SDL_Surface *display, int x, int y, int max_w, int line_h,
	const char *utf8, Uint8 r, Uint8 g, Uint8 b )
{
	cairo_surface_t		*cs;
	cairo_t			*cr;
	PangoLayout		*layout;
	PangoFontDescription	*desc;
	unsigned char		*src;
	int			stride, row, col, w, h;
	
	if (!utf8 || !*utf8 || max_w < 4 || line_h < 8)
		return 0;
	
	w = max_w;
	h = line_h;
	cs = cairo_image_surface_create( CAIRO_FORMAT_ARGB32, w, h );
	if (cairo_surface_status( cs ) != CAIRO_STATUS_SUCCESS)
		return 0;
	
	cr = cairo_create( cs );
	layout = pango_cairo_create_layout( cr );
	/* DejaVu covers Arabic; FreeSans / sans-serif as fallbacks */
	desc = pango_font_description_from_string( "DejaVu Sans,FreeSans,sans-serif 11" );
	pango_layout_set_font_description( layout, desc );
	pango_font_description_free( desc );
	pango_layout_set_text( layout, utf8, -1 );
	pango_layout_set_width( layout, w * PANGO_SCALE );
	pango_layout_set_height( layout, h * PANGO_SCALE );
	pango_layout_set_ellipsize( layout, PANGO_ELLIPSIZE_END );
	pango_layout_set_single_paragraph_mode( layout, TRUE );
	if (TextHasRtl( utf8 ))
		pango_context_set_base_dir( pango_layout_get_context( layout ), PANGO_DIRECTION_RTL );
	
	cairo_set_source_rgb( cr, r / 255.0, g / 255.0, b / 255.0 );
	cairo_move_to( cr, 0, 0 );
	pango_cairo_show_layout( cr, layout );
	cairo_surface_flush( cs );
	
	src = cairo_image_surface_get_data( cs );
	stride = cairo_image_surface_get_stride( cs );
	for (row = 0; row < h; row++)
	{
		Uint32	*pix = (Uint32 *)(src + row * stride);
		
		for (col = 0; col < w; col++)
		{
			Uint32	p = pix[col];
			Uint8	a = (p >> 24) & 0xFF;
			SDL_Rect	pixel;
			
			if (a < 32)
				continue;
			pixel.x = x + col;
			pixel.y = y + row;
			pixel.w = 1;
			pixel.h = 1;
			if (pixel.x >= 0 && pixel.y >= 0 && pixel.x < display->w && pixel.y < display->h)
				FillRect( display, &pixel, SDL_MapRGB( display->format, r, g, b ) );
		}
	}
	
	g_object_unref( layout );
	cairo_destroy( cr );
	cairo_surface_destroy( cs );
	return h;
}

static void	ShowToast( const char *msg )
{
	strncpy( g_toast_msg, msg, sizeof( g_toast_msg ) - 1 );
	g_toast_msg[sizeof( g_toast_msg ) - 1] = 0;
	g_toast_until = SDL_GetTicks() + TOAST_MS;
}

static void	RenderToast( SDL_Surface *display )
{
	SDL_Rect	box;
	int		char_width = 12, pad = 8, text_w, text_h = 14;
	Uint32		bg, fg;
	
	if (!g_toast_until)
		return;
	if (SDL_GetTicks() >= g_toast_until)
	{
		g_toast_until = 0;
		g_toast_msg[0] = 0;
		return;
	}
	
	text_w = (int)strlen( g_toast_msg ) * char_width;
	box.w = text_w + pad * 2;
	box.h = text_h + pad * 2;
	box.x = display->w - box.w - 8;
	box.y = 8;
	if (box.x < 0)
		box.x = 0;
	
	bg = SDL_MapRGB( display->format, 0x20, 0x20, 0x28 );
	fg = SDL_MapRGB( display->format, 0xFF, 0xFF, 0xCC );
	FillRect( display, &box, bg );
	DrawString( display, box.x + pad, box.y + pad, g_toast_msg, fg );
}

void	RenderSearchBox( SDL_Surface *display, IBuffer *input, RState state )
{
	SDL_Rect box, inner_box, cursor, text_bg;
	int text_height = 20;
	int box_padding = 10;
	int box_height = text_height + 2 * box_padding;
	int box_width = 600;
	int char_width = 12;
	char search_text[80];
	Uint32 white_color, gray_color, yellow_color;
	
	if (state != SEARCH && state != GOTO)
		return;
	
	// Get colors
	white_color = SDL_MapRGB( display->format, 255, 255, 255 );
	gray_color = SDL_MapRGB( display->format, 180, 180, 180 );
	yellow_color = SDL_MapRGB( display->format, 255, 255, 0 );
	
	// Draw outer border
	box.x = (display->w - box_width) / 2;
	box.y = display->h - box_height - 40;
	box.w = box_width;
	box.h = box_height;
	FillRect( display, &box, SDL_MapRGB( display->format, 200, 200, 200 ) );
	
	// Draw inner background
	inner_box.x = box.x + 2;
	inner_box.y = box.y + 2;
	inner_box.w = box.w - 4;
	inner_box.h = box.h - 4;
	FillRect( display, &inner_box, SDL_MapRGB( display->format, 40, 40, 40 ) );
	
	// Draw text background area
	text_bg.x = inner_box.x + box_padding;
	text_bg.y = inner_box.y + box_padding;
	text_bg.w = inner_box.w - 2 * box_padding;
	text_bg.h = text_height;
	FillRect( display, &text_bg, SDL_MapRGB( display->format, 30, 30, 30 ) );
	
	// Draw prompt
	const char *prompt = state == SEARCH ? "Search: " : "Go to page: ";
	DrawString( display, text_bg.x + 4, text_bg.y + 3, prompt, gray_color );
	
	// Draw input text
	if (input->used > 0)
	{
		// Null terminate for display
		memcpy( search_text, input->buff, input->used );
		search_text[input->used] = '\0';
		DrawString( display, text_bg.x + 4 + strlen(prompt) * char_width, text_bg.y + 3, search_text, white_color );
	}
	
	// Draw cursor
	cursor.x = text_bg.x + 4 + (strlen(prompt) + input->cur) * char_width - 2;
	cursor.y = text_bg.y + 2;
	cursor.w = 2;
	cursor.h = 16;
	
	// Blinking cursor effect
	if ((SDL_GetTicks() / 500) % 2 == 0)
		FillRect( display, &cursor, yellow_color );
}

static int	ViewWidth( Green_RTD *rtd )
{
	SDL_Surface	*display = DrawSurface();
	
	if (rtd->side_by_side && Green_IsDocValid( rtd, rtd->doc_cur )
		&& rtd->docs[rtd->doc_cur]->page_count > 1)
		return display->w / 2;
	
	return display->w;
}

static void	FreeTOC( TOCNode *node )
{
	int	i;
	
	if (!node)
		return;
	
	for (i = 0; i < node->n_children; i++)
		FreeTOC( node->children[i] );
	free( node->children );
	free( node->title );
	free( node );
	return;
}

static TOCNode	*BuildTOCNode( PopplerDocument *doc, PopplerIndexIter *iter )
{
	PopplerIndexIter	*child;
	PopplerAction		*action;
	PopplerDest		*dest;
	TOCNode			*node, **arr = NULL;
	int			n = 0;
	
	if (!iter)
		return NULL;
	
	node = calloc( 1, sizeof( *node ) );
	if (!node)
		return NULL;
	
	node->page = -1;
	node->open = poppler_index_iter_is_open( iter );
	action = poppler_index_iter_get_action( iter );
	if (action)
	{
		if (action->type == POPPLER_ACTION_GOTO_DEST && action->goto_dest.dest)
		{
			dest = action->goto_dest.dest;
			if (dest->type == POPPLER_DEST_NAMED && dest->named_dest)
			{
				PopplerDest	*resolved = poppler_document_find_dest( doc, dest->named_dest );
				if (resolved)
				{
					node->page = resolved->page_num;
					poppler_dest_free( resolved );
				}
			}
			else
				node->page = dest->page_num;
		}
		if (action->goto_dest.title)
			node->title = strdup( action->goto_dest.title );
		poppler_action_free( action );
	}
	if (!node->title)
		node->title = strdup( "" );
	
	child = poppler_index_iter_get_child( iter );
	if (child)
	{
		do
		{
			arr = realloc( arr, (n + 1) * sizeof( *arr ) );
			if (!arr)
			{
				poppler_index_iter_free( child );
				FreeTOC( node );
				return NULL;
			}
			arr[n] = BuildTOCNode( doc, child );
			n++;
		}	while (poppler_index_iter_next( child ));
		
		poppler_index_iter_free( child );
		node->children = arr;
		node->n_children = n;
	}
	
	return node;
}

static void	RebuildTOCFlat( void );

static void	BuildTOC( Green_RTD *rtd )
{
	PopplerIndexIter	*iter;
	TOCNode			**arr = NULL;
	int			n = 0;
	
	FreeTOC( g_toc );
	g_toc = NULL;
	if (!Green_IsDocValid( rtd, rtd->doc_cur ))
		return;
	
	iter = poppler_index_iter_new( rtd->docs[rtd->doc_cur]->doc );
	if (!iter)
		return;
	
	g_toc = calloc( 1, sizeof( *g_toc ) );
	if (!g_toc)
	{
		poppler_index_iter_free( iter );
		return;
	}
	g_toc->page = -1;
	
	do
	{
		arr = realloc( arr, (n + 1) * sizeof( *arr ) );
		if (!arr)
		{
			poppler_index_iter_free( iter );
			FreeTOC( g_toc );
			g_toc = NULL;
			return;
		}
		arr[n] = BuildTOCNode( rtd->docs[rtd->doc_cur]->doc, iter );
		n++;
	}	while (poppler_index_iter_next( iter ));
	
	poppler_index_iter_free( iter );
	g_toc->children = arr;
	g_toc->n_children = n;
	g_toc_sel = 0;
	RebuildTOCFlat();
	return;
}

static void	EnsureTOC( Green_RTD *rtd )
{
	if (!Green_IsDocValid( rtd, rtd->doc_cur ))
		return;
	
	if (g_toc_doc != rtd->doc_cur)
	{
		g_toc_doc = rtd->doc_cur;
		BuildTOC( rtd );
	}
	return;
}

static void	CollectVisible( TOCNode *node, int depth, TOCEntry **arr, int *count, int *cap )
{
	int	i;
	
	for (i = 0; i < node->n_children; i++)
	{
		TOCNode	*c = node->children[i];
		
		if (*count >= *cap)
		{
			*cap = *cap ? *cap * 2 : 32;
			*arr = realloc( *arr, *cap * sizeof( TOCEntry ) );
			if (!*arr)
				return;
		}
		
		(*arr)[*count].node = c;
		(*arr)[*count].depth = depth;
		(*count)++;
		if (c->open)
			CollectVisible( c, depth + 1, arr, count, cap );
	}
	
	return;
}

static void	RebuildTOCFlat( void )
{
	TOCEntry	*arr = NULL;
	int		count = 0, cap = 0;
	
	free( g_toc_entries );
	g_toc_entries = NULL;
	g_toc_count = 0;
	if (g_toc)
		CollectVisible( g_toc, 0, &arr, &count, &cap );
	g_toc_entries = arr;
	g_toc_count = count;
	if (g_toc_sel >= g_toc_count)
		g_toc_sel = g_toc_count ? g_toc_count - 1 : 0;
	return;
}

static void	RenderTOC( SDL_Surface *display, Green_RTD *rtd )
{
	SDL_Rect	panel, hl;
	Uint32		title_col, entry_col, sel_col, sel_bg;
	int		panel_w, x0, y0, line_h, max_lines, start, i, indent, text_w;
	Uint8		er, eg, eb;
	
	(void)rtd;
	if (!g_toc_active)
		return;
	
	panel_w = (display->w * 3) / 4;
	if (panel_w < 200)
		panel_w = display->w < 200 ? display->w : 200;
	
	panel.x = 0;
	panel.y = 0;
	panel.w = panel_w;
	panel.h = display->h;
	FillRect( display, &panel, SDL_MapRGB( display->format, 0x14, 0x14, 0x14 ) );
	
	title_col = SDL_MapRGB( display->format, 0xE8, 0xE8, 0xE8 );
	entry_col = SDL_MapRGB( display->format, 0xB8, 0xB8, 0xB8 );
	sel_col = SDL_MapRGB( display->format, 0xFF, 0xFF, 0x00 );
	sel_bg = SDL_MapRGB( display->format, 0x2E, 0x2E, 0x5E );
	
	x0 = 8;
	y0 = 8;
	DrawString( display, x0, y0, "Table of Contents", title_col );
	y0 += 22;
	line_h = 18;
	max_lines = (display->h - y0) / line_h;
	if (max_lines <= 0)
		return;
	
	if (g_toc_count <= 0)
	{
		DrawString( display, x0, y0, "No table of contents", entry_col );
		return;
	}
	
	start = 0;
	if (g_toc_count > max_lines)
	{
		start = g_toc_sel - max_lines + 1;
		if (start < 0)
			start = 0;
		if (start + max_lines > g_toc_count)
			start = g_toc_count - max_lines;
	}
	
	for (i = start; i < g_toc_count && i < start + max_lines; i++)
	{
		TOCNode	*n = g_toc_entries[i].node;
		int	y = y0 + (i - start) * line_h;
		
		if (i == g_toc_sel)
		{
			hl.x = 0;
			hl.y = y;
			hl.w = panel_w;
			hl.h = line_h;
			FillRect( display, &hl, sel_bg );
			er = 0xFF;
			eg = 0xFF;
			eb = 0x00;
		}
		else
		{
			er = 0xB8;
			eg = 0xB8;
			eb = 0xB8;
		}
		
		indent = x0 + g_toc_entries[i].depth * 14;
		if (n->n_children > 0)
		{
			if (n->open)
				DrawString( display, indent, y, "-", i == g_toc_sel ? sel_col : entry_col );
			else
				DrawString( display, indent, y, "+", i == g_toc_sel ? sel_col : entry_col );
		}
		
		text_w = panel_w - indent - 28;
		if (text_w < 8)
			text_w = 8;
		DrawUtf8( display, indent + 26, y, text_w, line_h, n->title ? n->title : "", er, eg, eb );
	}
	
	return;
}

static int	FindTOCIndex( TOCNode *target )
{
	int	i;
	
	for (i = 0; i < g_toc_count; i++)
		if (g_toc_entries[i].node == target)
			return i;
	
	return -1;
}

static int	FindParentIndex( TOCNode *target )
{
	int	i, j;
	
	for (i = 0; i < g_toc_count; i++)
		for (j = 0; j < g_toc_entries[i].node->n_children; j++)
			if (g_toc_entries[i].node->children[j] == target)
				return i;
	
	return -1;
}

static int	TOCGoToPage( Green_RTD *rtd, TOCNode *node, unsigned short *flags )
{
	Green_Document	*doc;
	
	if (!node || node->page < 0 || !Green_IsDocValid( rtd, rtd->doc_cur ))
		return 0;
	
	doc = rtd->docs[rtd->doc_cur];
	if (Green_GotoPage( doc, node->page, true ))
	{
		*flags |= FLAG_RENDER;
		return 1;
	}
	return 0;
}

static void	ListNavStop( void )
{
	g_list_nav_dir = 0;
	g_list_nav_next = 0;
}

static void	ListNavStart( int dir )
{
	g_list_nav_dir = dir;
	g_list_nav_next = SDL_GetTicks() + LIST_NAV_DELAY_MS;
}

static void	TOCStepSel( int dir )
{
	if (g_toc_count <= 0)
		return;
	if (dir > 0 && g_toc_sel < g_toc_count - 1)
		g_toc_sel++;
	else if (dir < 0 && g_toc_sel > 0)
		g_toc_sel--;
}

static void	BookmarkStepSel( int dir )
{
	if (g_bm_view_count <= 0)
		return;
	if (dir > 0 && g_bm_sel < g_bm_view_count - 1)
		g_bm_sel++;
	else if (dir < 0 && g_bm_sel > 0)
		g_bm_sel--;
}

static RState	TOCInput( Green_RTD *rtd, SDL_Event *event, unsigned short *flags )
{
	switch (event->key.keysym.sym)
	{
		case SDLK_TAB:
		case SDLK_ESCAPE:
		case SDLK_LCTRL:
			/* Menu / Miyoo B: close TOC */
			ListNavStop();
			g_toc_active = false;
			*flags |= FLAG_RENDER;
			return NORMAL;
		case SDLK_j:
		case SDLK_DOWN:
			TOCStepSel( 1 );
			ListNavStart( 1 );
			*flags |= FLAG_RENDER;
			break;
		case SDLK_k:
		case SDLK_UP:
			TOCStepSel( -1 );
			ListNavStart( -1 );
			*flags |= FLAG_RENDER;
			break;
		case SDLK_l:
		case SDLK_RIGHT:
			/* Expand hierarchical section */
			if (g_toc_count > 0)
			{
				TOCNode	*n = g_toc_entries[g_toc_sel].node;
				
				if (n->n_children > 0 && !n->open)
				{
					n->open = true;
					RebuildTOCFlat();
					*flags |= FLAG_RENDER;
				}
			}
			break;
		case SDLK_h:
		case SDLK_LEFT:
			/* Collapse section, or move to parent if already closed */
			if (g_toc_count > 0)
			{
				TOCNode	*n = g_toc_entries[g_toc_sel].node;
				
				if (n->n_children > 0 && n->open)
				{
					n->open = false;
					RebuildTOCFlat();
				}
				else
				{
					int	idx = FindParentIndex( n );
					if (idx >= 0)
						g_toc_sel = idx;
				}
			}
			*flags |= FLAG_RENDER;
			break;
		case SDLK_RETURN:
			/* Start toggles TOC closed */
			ListNavStop();
			g_toc_active = false;
			*flags |= FLAG_RENDER;
			return NORMAL;
		case SDLK_SPACE:
			/* A on Miyoo: jump to selection */
			if (g_toc_count > 0)
			{
				if (TOCGoToPage( rtd, g_toc_entries[g_toc_sel].node, flags ))
				{
					ListNavStop();
					g_toc_active = false;
					return NORMAL;
				}
			}
			break;
		default:
			break;
	}
	
	return TOC;
}

static char	*BookmarksPath( void )
{
	static char	path[512];
	const char	*home = getenv( "HOME" );
	
	if (!home || !*home)
		return NULL;
	if (snprintf( path, sizeof( path ), "%s/.green.bookmarks", home ) >= (int)sizeof( path ))
		return NULL;
	return path;
}

static void	RebuildBookmarkView( Green_RTD *rtd )
{
	Green_Document	*doc;
	int		i, n = 0;
	
	free( g_bm_view );
	g_bm_view = NULL;
	g_bm_view_count = 0;
	if (!Green_IsDocValid( rtd, rtd->doc_cur ))
	{
		g_bm_sel = 0;
		return;
	}
	
	doc = rtd->docs[rtd->doc_cur];
	for (i = 0; i < g_bm_count; i++)
		if (!strcmp( g_bookmarks[i].uri, doc->uri ))
			n++;
	
	if (!n)
	{
		g_bm_sel = 0;
		return;
	}
	
	g_bm_view = malloc( n * sizeof( *g_bm_view ) );
	if (!g_bm_view)
		return;
	
	n = 0;
	for (i = 0; i < g_bm_count; i++)
		if (!strcmp( g_bookmarks[i].uri, doc->uri ))
			g_bm_view[n++] = i;
	
	g_bm_view_count = n;
	if (g_bm_sel >= g_bm_view_count)
		g_bm_sel = g_bm_view_count ? g_bm_view_count - 1 : 0;
	return;
}

static int	CompareBookmarks( const void *a, const void *b )
{
	const Bookmark	*ba = a, *bb = b;
	int		cmp = strcmp( ba->uri, bb->uri );
	
	if (cmp)
		return cmp;
	return ba->page - bb->page;
}

static void	SaveBookmarks( void )
{
	FILE	*file;
	char	*path = BookmarksPath();
	int	i;
	
	if (!path)
		return;
	
	file = fopen( path, "w" );
	if (!file)
		return;
	
	for (i = 0; i < g_bm_count; i++)
		fprintf( file, "%d %s\n", g_bookmarks[i].page + 1, g_bookmarks[i].uri );
	
	fclose( file );
	return;
}

static void	LoadBookmarks( void )
{
	FILE	*file;
	char	*path = BookmarksPath();
	char	line[4096];
	
	if (g_bm_loaded || !path)
		return;
	
	g_bm_loaded = true;
	file = fopen( path, "r" );
	if (!file)
		return;
	
	while (fgets( line, sizeof( line ), file ))
	{
		char	*uri, *end;
		long	page;
		Bookmark	*tmp;
		
		if (line[0] == '#' || line[0] == '\n')
			continue;
		
		page = strtol( line, &uri, 10 );
		if (uri == line || page < 1)
			continue;
		while (*uri == ' ' || *uri == '\t')
			uri++;
		end = uri + strlen( uri );
		while (end > uri && (end[-1] == '\n' || end[-1] == '\r'))
			*--end = 0;
		if (!*uri)
			continue;
		
		tmp = realloc( g_bookmarks, (g_bm_count + 1) * sizeof( *g_bookmarks ) );
		if (!tmp)
			break;
		g_bookmarks = tmp;
		g_bookmarks[g_bm_count].uri = strdup( uri );
		g_bookmarks[g_bm_count].page = (int)page - 1;
		if (!g_bookmarks[g_bm_count].uri)
			break;
		g_bm_count++;
	}
	
	fclose( file );
	if (g_bm_count > 1)
		qsort( g_bookmarks, g_bm_count, sizeof( *g_bookmarks ), CompareBookmarks );
	return;
}

static int	FindBookmark( const char *uri, int page )
{
	int	i;
	
	for (i = 0; i < g_bm_count; i++)
		if (g_bookmarks[i].page == page && !strcmp( g_bookmarks[i].uri, uri ))
			return i;
	return -1;
}

static int	AddBookmark( Green_RTD *rtd )
{
	Green_Document	*doc;
	Bookmark	*tmp;
	char		*uri;
	int		idx;
	
	if (!Green_IsDocValid( rtd, rtd->doc_cur ))
		return 0;
	
	doc = rtd->docs[rtd->doc_cur];
	idx = FindBookmark( doc->uri, doc->page_cur );
	if (idx >= 0)
		return 0;
	
	uri = strdup( doc->uri );
	if (!uri)
		return 0;
	
	tmp = realloc( g_bookmarks, (g_bm_count + 1) * sizeof( *g_bookmarks ) );
	if (!tmp)
	{
		free( uri );
		return 0;
	}
	
	g_bookmarks = tmp;
	g_bookmarks[g_bm_count].uri = uri;
	g_bookmarks[g_bm_count].page = doc->page_cur;
	g_bm_count++;
	qsort( g_bookmarks, g_bm_count, sizeof( *g_bookmarks ), CompareBookmarks );
	SaveBookmarks();
	RebuildBookmarkView( rtd );
	idx = FindBookmark( doc->uri, doc->page_cur );
	if (idx >= 0)
	{
		int	i;
		for (i = 0; i < g_bm_view_count; i++)
			if (g_bm_view[i] == idx)
			{
				g_bm_sel = i;
				break;
			}
	}
	return 1;
}

static int	RemoveBookmarkAt( Green_RTD *rtd, int global_idx )
{
	int	i;
	
	if (global_idx < 0 || global_idx >= g_bm_count)
		return 0;
	
	free( g_bookmarks[global_idx].uri );
	for (i = global_idx; i < g_bm_count - 1; i++)
		g_bookmarks[i] = g_bookmarks[i + 1];
	g_bm_count--;
	if (!g_bm_count)
	{
		free( g_bookmarks );
		g_bookmarks = NULL;
	}
	else
	{
		Bookmark	*tmp = realloc( g_bookmarks, g_bm_count * sizeof( *g_bookmarks ) );
		if (tmp)
			g_bookmarks = tmp;
	}
	SaveBookmarks();
	RebuildBookmarkView( rtd );
	return 1;
}

static int	ToggleBookmark( Green_RTD *rtd )
{
	Green_Document	*doc;
	int		idx;
	
	if (!Green_IsDocValid( rtd, rtd->doc_cur ))
		return 0;
	
	doc = rtd->docs[rtd->doc_cur];
	idx = FindBookmark( doc->uri, doc->page_cur );
	if (idx >= 0)
	{
		if (RemoveBookmarkAt( rtd, idx ))
		{
			ShowToast( "Bookmark Removed" );
			return 1;
		}
		return 0;
	}
	if (AddBookmark( rtd ))
	{
		ShowToast( "Bookmark Added" );
		return 1;
	}
	return 0;
}

static void	RenderBookmarks( SDL_Surface *display, Green_RTD *rtd )
{
	SDL_Rect	panel, hl;
	Uint32		title_col, entry_col, sel_col, sel_bg, help_col;
	int		panel_w, x0, y0, line_h, max_lines, start, i;
	char		buf[64];
	
	if (!g_bm_active)
		return;
	
	panel_w = display->w / 3;
	if (panel_w > 500)
		panel_w = 500;
	if (panel_w < 200)
		panel_w = display->w < 200 ? display->w : 200;
	
	panel.x = 0;
	panel.y = 0;
	panel.w = panel_w;
	panel.h = display->h;
	FillRect( display, &panel, SDL_MapRGB( display->format, 0x14, 0x14, 0x14 ) );
	
	title_col = SDL_MapRGB( display->format, 0xE8, 0xE8, 0xE8 );
	entry_col = SDL_MapRGB( display->format, 0xB8, 0xB8, 0xB8 );
	sel_col = SDL_MapRGB( display->format, 0xFF, 0xFF, 0x00 );
	sel_bg = SDL_MapRGB( display->format, 0x2E, 0x2E, 0x5E );
	help_col = SDL_MapRGB( display->format, 0x88, 0x88, 0x88 );
	
	x0 = 8;
	y0 = 8;
	DrawString( display, x0, y0, "Bookmarks", title_col );
	y0 += 22;
	DrawString( display, x0, y0, "Y:add X:del A:go B:back", help_col );
	y0 += 20;
	line_h = 16;
	max_lines = (display->h - y0) / line_h;
	if (max_lines <= 0)
		return;
	
	if (g_bm_view_count <= 0)
	{
		DrawString( display, x0, y0, "No bookmarks", entry_col );
		return;
	}
	
	start = 0;
	if (g_bm_view_count > max_lines)
	{
		start = g_bm_sel - max_lines + 1;
		if (start < 0)
			start = 0;
		if (start + max_lines > g_bm_view_count)
			start = g_bm_view_count - max_lines;
	}
	
	for (i = start; i < g_bm_view_count && i < start + max_lines; i++)
	{
		Bookmark	*bm = &g_bookmarks[g_bm_view[i]];
		int		y = y0 + (i - start) * line_h;
		
		if (i == g_bm_sel)
		{
			hl.x = 0;
			hl.y = y;
			hl.w = panel_w;
			hl.h = line_h;
			FillRect( display, &hl, sel_bg );
		}
		
		snprintf( buf, sizeof( buf ), "Page %d", bm->page + 1 );
		DrawString( display, x0, y, buf, i == g_bm_sel ? sel_col : entry_col );
	}
	
	return;
}

static RState	OpenTOCPanel( Green_RTD *rtd, unsigned short *flags );

static RState	BookmarkInput( Green_RTD *rtd, SDL_Event *event, unsigned short *flags )
{
	switch (event->key.keysym.sym)
	{
		case SDLK_b:
		case SDLK_ESCAPE:
		case SDLK_LCTRL:
			/* b / Menu / Miyoo B: close */
			ListNavStop();
			g_bm_active = false;
			*flags |= FLAG_RENDER;
			return NORMAL;
		case SDLK_j:
		case SDLK_DOWN:
			BookmarkStepSel( 1 );
			ListNavStart( 1 );
			*flags |= FLAG_RENDER;
			break;
		case SDLK_k:
		case SDLK_UP:
			BookmarkStepSel( -1 );
			ListNavStart( -1 );
			*flags |= FLAG_RENDER;
			break;
		case 'a':
		case SDLK_PLUS:
		case SDLK_LALT:
			/* a / Miyoo Y: add bookmark for current page */
			if (AddBookmark( rtd ))
				*flags |= FLAG_RENDER;
			break;
		case SDLK_x:
		case SDLK_LSHIFT:
		case SDLK_DELETE:
			/* x / Miyoo X: delete (not R2/BACKSPACE — that is a shoulder) */
			if (g_bm_view_count > 0 && RemoveBookmarkAt( rtd, g_bm_view[g_bm_sel] ))
				*flags |= FLAG_RENDER;
			break;
		case SDLK_RETURN:
			/* Start: leave bookmarks and toggle TOC */
			ListNavStop();
			g_bm_active = false;
			return OpenTOCPanel( rtd, flags );
		case SDLK_SPACE:
			/* A on Miyoo: jump to selection */
			if (g_bm_view_count > 0 && Green_IsDocValid( rtd, rtd->doc_cur ))
			{
				Bookmark	*bm = &g_bookmarks[g_bm_view[g_bm_sel]];
				
				if (Green_GotoPage( rtd->docs[rtd->doc_cur], bm->page, true ))
				{
					ListNavStop();
					g_bm_active = false;
					*flags |= FLAG_RENDER;
					return NORMAL;
				}
			}
			break;
		default:
			break;
	}
	
	return BOOKMARKS;
}

void	Render( Green_RTD *rtd )
{
	Green_Document	*doc;
	PopplerPage	*page = NULL;
	SDL_Surface	*display = DrawSurface();
	SDL_Rect	rect;
	double	tscale;
	int	w, h, w2, h2;
	
	rect.x = rect.y = 0;
	rect.w = display->w;
	rect.h = display->h;
	if (rtd->night)
		FillRect( display, &rect, SDL_MapRGB( display->format, 0x10, 0x10, 0x10 ) );
	else
		FillRect( display, &rect, SDL_MapRGB( display->format, rtd->c_background.r, rtd->c_background.g, rtd->c_background.b ));
	if (!Green_IsDocValid( rtd, rtd->doc_cur ))
	{
		Present();
		return;
	}
	
	doc = rtd->docs[rtd->doc_cur];
	
	if (rtd->side_by_side && doc->page_count > 1)
	{
		int left_page, right_page;
		PopplerPage *left_page_obj = NULL, *right_page_obj = NULL;
		
		// Book-style pairs: 1-2, 3-4, 5-6, etc.
		// If current page is odd (1, 3, 5...), show current and next
		// If current page is even (2, 4, 6...), show previous and current
		if (doc->page_cur % 2 == 0) { // 0-indexed, so page 0 is page 1, page 1 is page 2
			left_page = doc->page_cur;
			right_page = doc->page_cur + 1;
		} else {
			left_page = doc->page_cur - 1;
			right_page = doc->page_cur;
		}
		
		
		if (left_page >= 0 && right_page < doc->page_count)
		{
			tscale = Green_Fit( doc, display->w / 2, display->h ) * doc->finescale;
			
			left_page_obj = poppler_document_get_page( doc->doc, left_page );
			right_page_obj = poppler_document_get_page( doc->doc, right_page );
			
			
			Green_GetDimension( left_page_obj, &w, &h, tscale, doc->rotation % 2 );
			Green_GetDimension( right_page_obj, &w2, &h2, tscale, doc->rotation % 2 );
			
			rect.w = w > display->w / 2 ? display->w / 2 : w;
			rect.h = h > display->h ? display->h : h;
			rect.x = (display->w / 2 - rect.w) / 2;
			rect.y = (display->h - rect.h) / 2;
			RenderPage( rtd, rect, doc->xoffset, doc->yoffset, left_page_obj, tscale, left_page );
			
			rect.w = w2 > display->w / 2 ? display->w / 2 : w2;
			rect.h = h2 > display->h ? display->h : h2;
			rect.x = display->w / 2 + (display->w / 2 - rect.w) / 2;
			rect.y = (display->h - rect.h) / 2;
			RenderPage( rtd, rect, doc->xoffset, doc->yoffset, right_page_obj, tscale, right_page );
			
			g_object_unref( G_OBJECT( left_page_obj ) );
			g_object_unref( G_OBJECT( right_page_obj ) );
		}
		else
		{
			tscale = Green_Fit( doc, display->w, display->h ) * doc->finescale;
			page = poppler_document_get_page( doc->doc, doc->page_cur );
			Green_GetDimension( page, &w, &h, tscale, doc->rotation % 2 );
			rect.w = w > display->w ? display->w : w;
			rect.h = h > display->h ? display->h : h;
			rect.x = (display->w - rect.w) / 2;
			rect.y = (display->h - rect.h) / 2;
			RenderPage( rtd, rect, doc->xoffset, doc->yoffset, page, tscale, doc->page_cur );
			g_object_unref( G_OBJECT( page ) );
		}
	}
	else
	{
		tscale = Green_Fit( doc, display->w, display->h ) * doc->finescale;
		page = poppler_document_get_page( doc->doc, doc->page_cur );
		Green_GetDimension( page, &w, &h, tscale, doc->rotation % 2 );
		rect.w = w > display->w ? display->w : w;
		rect.h = h > display->h ? display->h : h;
		rect.x = (display->w - rect.w) / 2;
		rect.y = (display->h - rect.h) / 2;
		RenderPage( rtd, rect, doc->xoffset, doc->yoffset, page, tscale, doc->page_cur );
		g_object_unref( G_OBJECT( page ) );
	}
	
	// Render search box if in search or goto mode
	RenderSearchBox( display, &g_input, g_state );
	
	// Render table of contents panel if active
	RenderTOC( display, rtd );
	
	// Render bookmarks panel if active
	RenderBookmarks( display, rtd );
	
	RenderFilePicker( display );
	
	RenderHelp( display );
	
	RenderToast( display );
	
	Present();
	return;
}

static void	CloseCurrentFile( Green_RTD *rtd, unsigned short *flags )
{
	int	i, any = 0;
	
	if (Green_IsDocValid( rtd, rtd->doc_cur ))
		Green_Close( rtd, rtd->doc_cur );
	
	for (i = 0; i < rtd->doc_count; i++)
		if (rtd->docs[i])
		{
			any = 1;
			break;
		}
	
	if (!any)
	{
		/* Exit to Onion file picker — skip empty-frame redraw (looks like a hang) */
		*flags |= FLAG_QUIT;
		*flags &= ~FLAG_RENDER;
	}
	else
	{
		Green_NextVaildDoc( rtd );
		*flags |= FLAG_RENDER;
	}
	return;
}

/* Page turn keeps finescale + pan; clamp offsets to the new page */
static int	GotoPageKeepView( Green_Document *doc, int page, int w, int h )
{
	if (!Green_GotoPage( doc, page, false ))
		return 0;
	Green_ValidateOffset( doc, w, h );
	return 1;
}

static void	SkipPages( Green_Document *doc, Green_RTD *rtd, int delta, int w, int h, unsigned short *flags )
{
	int	page;
	
	if (!doc)
		return;
	
	page = doc->page_cur + delta;
	if (page < 0)
		page = 0;
	else if (page >= doc->page_count)
		page = doc->page_count - 1;
	
	if (page == doc->page_cur)
		return;
	
	if (GotoPageKeepView( doc, page, w, h ))
		*flags |= FLAG_RENDER;
	return;
}

static void	HelpStepScroll( int dir, int max_scroll )
{
	if (max_scroll < 0)
		max_scroll = 0;
	if (dir > 0 && g_help_scroll < max_scroll)
		g_help_scroll++;
	else if (dir < 0 && g_help_scroll > 0)
		g_help_scroll--;
}

static int	HelpMaxScroll( SDL_Surface *display )
{
	int	line_h = 16, y0 = 48, visible;
	
	visible = (display->h - y0 - 8) / line_h;
	if (visible < 1)
		visible = 1;
	if (HELP_LINE_COUNT <= visible)
		return 0;
	return HELP_LINE_COUNT - visible;
}

static RState	OpenHelpPanel( unsigned short *flags )
{
	if (g_help_active)
	{
		ListNavStop();
		g_help_active = false;
		*flags |= FLAG_RENDER;
		return NORMAL;
	}
	
	g_toc_active = false;
	g_bm_active = false;
	g_fp_active = false;
	ListNavStop();
	g_help_active = true;
	g_help_scroll = 0;
	*flags |= FLAG_RENDER;
	return HELP;
}

static void	RenderHelp( SDL_Surface *display )
{
	SDL_Rect	panel;
	int		x0, y0, line_h, visible, i, max_scroll;
	Uint32		title_col, text_col, dim_col;
	
	if (!g_help_active)
		return;
	
	panel.x = 0;
	panel.y = 0;
	panel.w = display->w;
	panel.h = display->h;
	FillRect( display, &panel, SDL_MapRGB( display->format, 0x10, 0x10, 0x18 ) );
	
	title_col = SDL_MapRGB( display->format, 0xFF, 0xFF, 0xCC );
	text_col = SDL_MapRGB( display->format, 0xD0, 0xD0, 0xD0 );
	dim_col = SDL_MapRGB( display->format, 0x80, 0x80, 0x90 );
	
	x0 = 10;
	y0 = 8;
	DrawString( display, x0, y0, "Controls  (B: close)", title_col );
	y0 = 28;
	DrawString( display, x0, y0, "Up/Down scroll", dim_col );
	y0 = 48;
	line_h = 16;
	visible = (display->h - y0 - 8) / line_h;
	if (visible < 1)
		return;
	
	max_scroll = HelpMaxScroll( display );
	if (g_help_scroll > max_scroll)
		g_help_scroll = max_scroll;
	
	for (i = 0; i < visible && g_help_scroll + i < HELP_LINE_COUNT; i++)
	{
		const char	*line = g_help_lines[g_help_scroll + i];
		int		y = y0 + i * line_h;
		
		if (!line[0])
			continue;
		if (line[0] != ' ' && strchr( line, ' ' ) == NULL)
			DrawString( display, x0, y, line, title_col );
		else if (strncmp( line, "In ", 3 ) == 0 || !strcmp( line, "Miyoo controls" )
			|| !strcmp( line, "Select chords" ))
			DrawString( display, x0, y, line, title_col );
		else
			DrawString( display, x0, y, line, text_col );
	}
	
	if (max_scroll > 0 && g_help_scroll < max_scroll)
		DrawString( display, display->w - 50, display->h - 18, "more", dim_col );
}

static RState	HelpInput( SDL_Event *event, unsigned short *flags )
{
	SDL_Surface	*display = DrawSurface();
	int		max_scroll = HelpMaxScroll( display );
	SDLKey		sym = event->key.keysym.sym;
	
	/* Select+L2 toggles closed */
	if (g_select_held && sym == SDLK_TAB)
	{
		g_select_chord = true;
		ListNavStop();
		g_help_active = false;
		*flags |= FLAG_RENDER;
		return NORMAL;
	}
	
	switch (sym)
	{
		case SDLK_ESCAPE:
		case SDLK_HOME:
		case SDLK_LCTRL:
			ListNavStop();
			g_help_active = false;
			*flags |= FLAG_RENDER;
			return NORMAL;
		case SDLK_j:
		case SDLK_DOWN:
			HelpStepScroll( 1, max_scroll );
			ListNavStart( 1 );
			*flags |= FLAG_RENDER;
			break;
		case SDLK_k:
		case SDLK_UP:
			HelpStepScroll( -1, max_scroll );
			ListNavStart( -1 );
			*flags |= FLAG_RENDER;
			break;
		default:
			break;
	}
	
	return HELP;
}

static void	FreeFilePickerEntries( void )
{
	int	i;
	
	for (i = 0; i < g_fp_count; i++)
	{
		free( g_fp_entries[i].name );
		free( g_fp_entries[i].path );
	}
	free( g_fp_entries );
	g_fp_entries = NULL;
	g_fp_count = 0;
	g_fp_sel = 0;
}

static int	FpEntryCmp( const void *a, const void *b )
{
	const FpEntry	*ea = a, *eb = b;
	
	if (ea->is_dir != eb->is_dir)
		return eb->is_dir - ea->is_dir;
	return strcasecmp( ea->name, eb->name );
}

static int	IsPdfName( const char *name )
{
	size_t	n;
	
	if (!name)
		return 0;
	n = strlen( name );
	return n >= 4 && !strcasecmp( name + n - 4, ".pdf" );
}

static int	FpAddEntry( const char *name, const char *path, int is_dir )
{
	FpEntry	*tmp;
	
	tmp = realloc( g_fp_entries, (g_fp_count + 1) * sizeof( *g_fp_entries ) );
	if (!tmp)
		return 0;
	g_fp_entries = tmp;
	g_fp_entries[g_fp_count].name = strdup( name );
	g_fp_entries[g_fp_count].path = strdup( path );
	g_fp_entries[g_fp_count].is_dir = is_dir;
	if (!g_fp_entries[g_fp_count].name || !g_fp_entries[g_fp_count].path)
	{
		free( g_fp_entries[g_fp_count].name );
		free( g_fp_entries[g_fp_count].path );
		return 0;
	}
	g_fp_count++;
	return 1;
}

static void	ScanFilePickerDir( void )
{
	DIR		*dir;
	struct dirent	*ent;
	struct stat	st;
	char		path[1024];
	
	FreeFilePickerEntries();
	if (!g_fp_dir[0])
		return;
	
	/* Parent entry except at filesystem root */
	if (strcmp( g_fp_dir, "/" ) != 0)
		FpAddEntry( "..", "..", 1 );
	
	dir = opendir( g_fp_dir );
	if (!dir)
		return;
	
	while ((ent = readdir( dir )))
	{
		if (!strcmp( ent->d_name, "." ) || !strcmp( ent->d_name, ".." ))
			continue;
		if (ent->d_name[0] == '.')
			continue;
		
		if (snprintf( path, sizeof( path ), "%s/%s",
			strcmp( g_fp_dir, "/" ) == 0 ? "" : g_fp_dir, ent->d_name ) >= (int)sizeof( path ))
			continue;
		
		if (stat( path, &st ) != 0)
			continue;
		if (S_ISDIR( st.st_mode ))
			FpAddEntry( ent->d_name, path, 1 );
		else if (S_ISREG( st.st_mode ) && IsPdfName( ent->d_name ))
			FpAddEntry( ent->d_name, path, 0 );
	}
	closedir( dir );
	
	if (g_fp_count > 1)
		qsort( g_fp_entries, g_fp_count, sizeof( *g_fp_entries ), FpEntryCmp );
	g_fp_sel = 0;
}

static void	FilePickerGoUp( void )
{
	char	*slash;
	
	if (!strcmp( g_fp_dir, "/" ))
		return;
	slash = strrchr( g_fp_dir, '/' );
	if (!slash)
		return;
	if (slash == g_fp_dir)
		g_fp_dir[1] = 0;
	else
		*slash = 0;
	ScanFilePickerDir();
}

static void	InitFilePickerDir( void )
{
	const char	*candidates[] = {
		"/mnt/SDCARD/Media/PDF",
		"/mnt/SDCARD/Media",
		"/mnt/SDCARD",
		".",
		NULL
	};
	int	i;
	struct stat	st;
	
	if (g_fp_dir[0] && stat( g_fp_dir, &st ) == 0 && S_ISDIR( st.st_mode ))
		return;
	
	for (i = 0; candidates[i]; i++)
	{
		if (stat( candidates[i], &st ) == 0 && S_ISDIR( st.st_mode ))
		{
			strncpy( g_fp_dir, candidates[i], sizeof( g_fp_dir ) - 1 );
			g_fp_dir[sizeof( g_fp_dir ) - 1] = 0;
			return;
		}
	}
	strncpy( g_fp_dir, ".", sizeof( g_fp_dir ) - 1 );
}

static void	CloseFilePicker( unsigned short *flags )
{
	g_fp_active = false;
	*flags |= FLAG_RENDER;
}

static RState	OpenFilePicker( Green_RTD *rtd, unsigned short *flags )
{
	(void)rtd;
	if (g_fp_active)
	{
		CloseFilePicker( flags );
		return NORMAL;
	}
	
	g_toc_active = false;
	g_bm_active = false;
	g_help_active = false;
	InitFilePickerDir();
	ScanFilePickerDir();
	g_fp_active = true;
	*flags |= FLAG_RENDER;
	return FILEPICKER;
}

static int	FilePickerActivate( Green_RTD *rtd, unsigned short *flags )
{
	FpEntry	*e;
	int	id;
	
	if (g_fp_count <= 0 || g_fp_sel < 0 || g_fp_sel >= g_fp_count)
		return 0;
	
	e = &g_fp_entries[g_fp_sel];
	if (e->is_dir)
	{
		if (!strcmp( e->name, ".." ))
			FilePickerGoUp();
		else
		{
			strncpy( g_fp_dir, e->path, sizeof( g_fp_dir ) - 1 );
			g_fp_dir[sizeof( g_fp_dir ) - 1] = 0;
			ScanFilePickerDir();
		}
		*flags |= FLAG_RENDER;
		return 1;
	}
	
	id = Green_Open( rtd, e->path );
	if (id < 0)
	{
		ShowToast( "Open Failed" );
		*flags |= FLAG_RENDER;
		return 0;
	}
	
	rtd->doc_cur = id;
	g_toc_doc = -1;
	CloseFilePicker( flags );
	ShowToast( "Opened" );
	*flags |= FLAG_RENDER;
	return 1;
}

static void	RenderFilePicker( SDL_Surface *display )
{
	SDL_Rect	panel, hl;
	int		x0, y0, line_h, max_lines, start, i, text_w;
	Uint8		er, eg, eb;
	char		label[600];
	
	if (!g_fp_active)
		return;
	
	panel.x = 0;
	panel.y = 0;
	panel.w = display->w;
	panel.h = display->h;
	FillRect( display, &panel, SDL_MapRGB( display->format, 0x12, 0x12, 0x18 ) );
	
	x0 = 8;
	y0 = 8;
	DrawString( display, x0, y0, "Open PDF", SDL_MapRGB( display->format, 0xE8, 0xE8, 0xE8 ) );
	y0 += 18;
	DrawUtf8( display, x0, y0, display->w - 16, 18, g_fp_dir, 0x88, 0x88, 0xAA );
	y0 += 20;
	DrawString( display, x0, y0, "A:open  B:up/back  Select+R1:close",
		SDL_MapRGB( display->format, 0x70, 0x70, 0x70 ) );
	y0 += 20;
	
	line_h = 18;
	max_lines = (display->h - y0 - 8) / line_h;
	if (max_lines <= 0)
		return;
	
	if (g_fp_count <= 0)
	{
		DrawString( display, x0, y0, "No folders or PDFs here",
			SDL_MapRGB( display->format, 0xB8, 0xB8, 0xB8 ) );
		return;
	}
	
	start = 0;
	if (g_fp_count > max_lines)
	{
		start = g_fp_sel - max_lines + 1;
		if (start < 0)
			start = 0;
		if (start + max_lines > g_fp_count)
			start = g_fp_count - max_lines;
	}
	
	for (i = start; i < g_fp_count && i < start + max_lines; i++)
	{
		int	y = y0 + (i - start) * line_h;
		
		if (i == g_fp_sel)
		{
			hl.x = 0;
			hl.y = y;
			hl.w = display->w;
			hl.h = line_h;
			FillRect( display, &hl, SDL_MapRGB( display->format, 0x2E, 0x2E, 0x5E ) );
			er = 0xFF;
			eg = 0xFF;
			eb = 0x00;
		}
		else
		{
			er = 0xC0;
			eg = 0xC0;
			eb = 0xC0;
		}
		
		if (g_fp_entries[i].is_dir)
			snprintf( label, sizeof( label ), "[%s]", g_fp_entries[i].name );
		else
			snprintf( label, sizeof( label ), "%s", g_fp_entries[i].name );
		
		text_w = display->w - x0 - 8;
		DrawUtf8( display, x0, y, text_w, line_h, label, er, eg, eb );
	}
}

static RState	FilePickerInput( Green_RTD *rtd, SDL_Event *event, unsigned short *flags )
{
	SDLKey	sym = event->key.keysym.sym;
	
	/* Select+R1 while open: close */
	if (g_select_held && sym == 't')
	{
		g_select_chord = true;
		CloseFilePicker( flags );
		return NORMAL;
	}
	
	switch (sym)
	{
		case SDLK_ESCAPE:
		case SDLK_HOME:
		case SDLK_LCTRL:
		case SDLK_LEFT:
		case 'h':
		case SDLK_BACKSPACE:
			/* B / Menu / Left: parent folder, or close at root */
			if (strcmp( g_fp_dir, "/" ) == 0)
			{
				CloseFilePicker( flags );
				return NORMAL;
			}
			FilePickerGoUp();
			*flags |= FLAG_RENDER;
			break;
		case SDLK_j:
		case SDLK_DOWN:
			if (g_fp_count > 0 && g_fp_sel < g_fp_count - 1)
				g_fp_sel++;
			*flags |= FLAG_RENDER;
			break;
		case SDLK_k:
		case SDLK_UP:
			if (g_fp_count > 0 && g_fp_sel > 0)
				g_fp_sel--;
			*flags |= FLAG_RENDER;
			break;
		case SDLK_SPACE:
		case SDLK_RETURN:
		case SDLK_RIGHT:
		case 'l':
			if (FilePickerActivate( rtd, flags ) && !g_fp_active)
				return NORMAL;
			break;
		case 't':
			/* R1 alone while picker open: ignore page-turn */
			break;
		default:
			break;
	}
	
	return FILEPICKER;
}

static RState	OpenTOCPanel( Green_RTD *rtd, unsigned short *flags )
{
	/* Start / o: toggle TOC */
	if (g_toc_active)
	{
		g_toc_active = false;
		*flags |= FLAG_RENDER;
		return NORMAL;
	}
	
	if (!Green_IsDocValid( rtd, rtd->doc_cur ))
		return NORMAL;
	
	g_bm_active = false;
	g_fp_active = false;
	g_help_active = false;
	EnsureTOC( rtd );
	g_toc_active = true;
	*flags |= FLAG_RENDER;
	return TOC;
}

static RState	OpenBookmarksPanel( Green_RTD *rtd, unsigned short *flags )
{
	/* Select release / b: toggle bookmarks */
	if (g_bm_active)
	{
		g_bm_active = false;
		*flags |= FLAG_RENDER;
		return NORMAL;
	}
	
	if (!Green_IsDocValid( rtd, rtd->doc_cur ))
		return NORMAL;
	
	LoadBookmarks();
	g_toc_active = false;
	g_fp_active = false;
	g_help_active = false;
	RebuildBookmarkView( rtd );
	g_bm_active = true;
	*flags |= FLAG_RENDER;
	return BOOKMARKS;
}

/* Horizontal pan: ~1/5 view width (was full width — too coarse when zoomed) */
static int	HScrollStep( int vw )
{
	int	s = vw / 5;
	
	if (s < 16)
		s = 16;
	return s;
}

RState	NormalInput( Green_RTD *rtd, SDL_Event *event, unsigned short *flags )
{
	Green_Document	*doc = NULL;
	SDL_Surface	*display = DrawSurface();
	RState	state = NORMAL;
	int	f = 0, vw = ViewWidth( rtd ), hstep = HScrollStep( vw );
	SDLKey	sym = event->key.keysym.sym;
	
	if (Green_IsDocValid( rtd, rtd->doc_cur ))
		doc = rtd->docs[rtd->doc_cur];
	
	/* Select held: A/B/X/Y are chords; other keys still work but cancel tap-open */
	if (g_select_held)
	{
		switch (sym)
		{
			case SDLK_SPACE:
				/* Select+A: night mode */
				g_select_chord = true;
				rtd->night = !rtd->night;
				Green_SaveSettings( rtd );
				*flags |= FLAG_RENDER;
				return NORMAL;
			case SDLK_LCTRL:
				/* Select+B: toggle bookmark */
				g_select_chord = true;
				if (doc)
				{
					LoadBookmarks();
					ToggleBookmark( rtd );
					*flags |= FLAG_RENDER;
				}
				return NORMAL;
			case SDLK_LSHIFT:
				/* Select+X: mirror horizontally */
				g_select_chord = true;
				g_x_held = true;
				g_x_chord = true;
				if (doc)
				{
					Green_MirrorH( doc );
					*flags |= FLAG_RENDER;
				}
				return NORMAL;
			case SDLK_LALT:
				/* Select+Y: mirror vertically */
				g_select_chord = true;
				g_y_held = true;
				g_y_chord = true;
				if (doc)
				{
					Green_MirrorV( doc );
					*flags |= FLAG_RENDER;
				}
				return NORMAL;
			case 'e':
				/* Select+L1: toggle dual-page mode */
				g_select_chord = true;
				rtd->side_by_side = !rtd->side_by_side;
				*flags |= FLAG_RENDER;
				return NORMAL;
			case 't':
				/* Select+R1: PDF/folder picker */
				g_select_chord = true;
				return OpenFilePicker( rtd, flags );
			case SDLK_TAB:
				/* Select+L2: controls help */
				g_select_chord = true;
				return OpenHelpPanel( flags );
			case SDLK_RCTRL:
				return NORMAL;
			default:
				g_select_chord = true;
				break;
		}
	}
	
	switch (sym)
	{
		case 'q':
			*flags |= FLAG_QUIT;
			break;
		case 'c':
			CloseCurrentFile( rtd, flags );
			break;
		case 'g':
			state = GOTO;
			break;
		case SDLK_s:
		case '/':
			/* type s-SEARCHSTRING-<Enter> to search string */
			state = SEARCH;
			break;
		case 'n':
		case ';':
			if (!doc || !doc->search_str)
				break;
			
			doc->page_cur = Green_FindNext( doc, doc->page_cur + 1 );
			*flags |= FLAG_RENDER;
			break;
		case ',':
			if (!doc || !doc->search_str)
				break;
			
			doc->page_cur = Green_FindPrevious( doc, doc->page_cur );
			*flags |= FLAG_RENDER;
			break;
		case 'f':
			state = FIT;
			break;
		case SDLK_UP:
			if (!doc)
				break;
			
			// In side-by-side mode, check if we're at the top and need to go to previous page pair
			if (rtd->side_by_side && doc->yoffset == 0) {
				if (!Green_GotoPage( doc, doc->page_cur - 2, true ))
					break;
			} else {
				Green_ScrollRelative( doc, 0, - display->h * rtd->step, vw, display->h, 1 );
			}
			*flags |= FLAG_RENDER;
			break;
		case SDLK_k:
			if (!doc)
				break;
			
			// In side-by-side mode, check if we're at the top and need to go to previous page pair
			if (rtd->side_by_side && doc->yoffset == 0) {
				if (!Green_GotoPage( doc, doc->page_cur - 2, true ))
					break;
			} else {
				Green_ScrollRelative( doc, 0, - display->h * rtd->step, vw, display->h, 1 );
			}
			*flags |= FLAG_RENDER;
			break;
		case SDLK_DOWN:
			if (!doc)
				break;
			
			// In side-by-side mode, check if we're at the bottom and need to go to next page pair
			if (rtd->side_by_side) {
				int max_x, max_y;
				Green_GetScrollRegion( doc, vw, display->h, &max_x, &max_y );
				if (doc->yoffset >= max_y) {
					if (!Green_GotoPage( doc, doc->page_cur + 2, true ))
						break;
				} else {
					Green_ScrollRelative( doc, 0, display->h * rtd->step, vw, display->h, 1 );
				}
			} else {
				Green_ScrollRelative( doc, 0, display->h * rtd->step, vw, display->h, 1 );
			}
			*flags |= FLAG_RENDER;
			break;
                case SDLK_j:
			if (!doc)
				break;
			
			// In side-by-side mode, check if we're at the bottom and need to go to next page pair
			if (rtd->side_by_side) {
				int max_x, max_y;
				Green_GetScrollRegion( doc, vw, display->h, &max_x, &max_y );
				if (doc->yoffset >= max_y) {
					if (!Green_GotoPage( doc, doc->page_cur + 2, true ))
						break;
				} else {
					Green_ScrollRelative( doc, 0, display->h * rtd->step, vw, display->h, 1 );
				}
			} else {
				Green_ScrollRelative( doc, 0, display->h * rtd->step, vw, display->h, 1 );
			}
			*flags |= FLAG_RENDER;
			break;
		case SDLK_LEFT:
			if (!doc)
				break;
			
			Green_ScrollRelative( doc, -hstep, 0, vw, display->h, 1 );
			*flags |= FLAG_RENDER;
			break;
		case SDLK_h:
			if (!doc)
				break;
			
			Green_ScrollRelative( doc, -hstep, 0, vw, display->h, 1 );
			*flags |= FLAG_RENDER;
			break;
		case SDLK_l:
			if (!doc)
				break;
			
			Green_ScrollRelative( doc, hstep, 0, vw, display->h, 1 );
			*flags |= FLAG_RENDER;
			break;
		case SDLK_RIGHT:
			if (!doc)
				break;
			
			Green_ScrollRelative( doc, hstep, 0, vw, display->h, 1 );
			*flags |= FLAG_RENDER;
			break;
		case SDLK_PAGEUP:
			if (!doc)
				break;
			
			if (rtd->side_by_side) {
				if (!GotoPageKeepView( doc, doc->page_cur - 2, vw, display->h ))
					break;
			} else {
				if (!GotoPageKeepView( doc, doc->page_cur - 1, vw, display->h ))
					break;
			}
			
			*flags |= FLAG_RENDER;
			break;
		case SDLK_PAGEDOWN:
			if (!doc)
				break;
			
			if (rtd->side_by_side) {
				if (!GotoPageKeepView( doc, doc->page_cur + 2, vw, display->h ))
					break;
			} else {
				if (!GotoPageKeepView( doc, doc->page_cur + 1, vw, display->h ))
					break;
			}
			
			*flags |= FLAG_RENDER;
			break;
		case SDLK_m:
			if (!doc)
				break;
			
			state = MIRROR;
			break;
		case SDLK_r:
			if (!doc)
				break;
			
			state = ROTATE;
			break;
		case SDLK_PLUS:
		case SDLK_EQUALS:
			if (!doc)
				break;
			
			Green_Zoom( doc, vw, display->h, doc->finescale * rtd->zoomstep );
			*flags |= FLAG_RENDER;
			break;
		case SDLK_MINUS:
			if (!doc)
				break;
			
			Green_Zoom( doc, vw, display->h, doc->finescale / rtd->zoomstep );
			*flags |= FLAG_RENDER;
			break;
		case SDLK_SPACE:
			/* A on Miyoo Mini: fit width */
			if (!doc)
				break;
			
			doc->fit_method = WIDTH;
			doc->finescale = 1;
			doc->xoffset = 0;
			doc->yoffset = 0;
			*flags |= FLAG_RENDER;
			break;
		case SDLK_LCTRL:
			/* B on Miyoo Mini: rotate 90° */
			if (!doc)
				break;
			
			Green_RotateRight( doc );
			Green_ValidateOffset( doc, display->w, display->h );
			*flags |= FLAG_RENDER;
			break;
		case 'e':
			/* L1 on Miyoo Mini: previous page (keep zoom + pan) */
			if (!doc)
				break;
			
			if (rtd->side_by_side)
			{
				if (!GotoPageKeepView( doc, doc->page_cur - 2, vw, display->h ))
					break;
			}
			else if (!GotoPageKeepView( doc, doc->page_cur - 1, vw, display->h ))
				break;
			
			*flags |= FLAG_RENDER;
			break;
		case 't':
			/* R1 on Miyoo Mini: next page (keep zoom + pan) */
			if (!doc)
				break;
			
			if (rtd->side_by_side)
			{
				if (!GotoPageKeepView( doc, doc->page_cur + 2, vw, display->h ))
					break;
			}
			else if (!GotoPageKeepView( doc, doc->page_cur + 1, vw, display->h ))
				break;
			
			*flags |= FLAG_RENDER;
			break;
		case SDLK_TAB:
			/* L2 on Miyoo Mini: back 10 pages */
			SkipPages( doc, rtd, -10, vw, display->h, flags );
			break;
		case SDLK_BACKSPACE:
			/* R2 on Miyoo Mini: forward 10 pages */
			SkipPages( doc, rtd, 10, vw, display->h, flags );
			break;
		case SDLK_RETURN:
			/* Start on Miyoo Mini: table of contents */
			return OpenTOCPanel( rtd, flags );
		case SDLK_F12:
			f++;
		case SDLK_F11:
			f++;
		case SDLK_F10:
			f++;
		case SDLK_F9:
			f++;
		case SDLK_F8:
			f++;
		case SDLK_F7:
			f++;
		case SDLK_F6:
			f++;
		case SDLK_F5:
			f++;
		case SDLK_F4:
			f++;
		case SDLK_F3:
			f++;
		case SDLK_F2:
			f++;
		case SDLK_F1:
			if (!Green_IsDocValid( rtd, f ))
				break;
			
			rtd->doc_cur = f;
			*flags |= FLAG_RENDER;
			break;
		case 'o':
			/* Table of contents (also Start on Miyoo Mini) */
			return OpenTOCPanel( rtd, flags );
		case SDLK_b:
			if (!doc)
				break;
			
			LoadBookmarks();
			if (event->key.keysym.mod & (KMOD_LSHIFT | KMOD_RSHIFT))
			{
				/* Shift+b: toggle bookmark (also Select+B on Miyoo) */
				ToggleBookmark( rtd );
				*flags |= FLAG_RENDER;
				break;
			}
			
			return OpenBookmarksPanel( rtd, flags );
		case SDLK_d:
			rtd->side_by_side = !rtd->side_by_side;
			*flags |= FLAG_RENDER;
			break;
		case 'i':
			rtd->night = !rtd->night;
			Green_SaveSettings( rtd );
			*flags |= FLAG_RENDER;
			break;
		default:
			break;
	}
	
	return state;
}

Uint32	live_timer( Uint32 interval, void *param )
{
	SDL_Event	event;
	
	event.type = SDL_USEREVENT;
	SDL_PushEvent( &event );
	return interval;
}

int	Green_SDL_Main( Green_RTD *rtd )
{
	SDL_TimerID	timer = NULL;
	SDL_Surface	*display;
	SDL_Event	event;
	Uint32	mouse_last = 0, mouse_cur;
	Uint16	left_x = 0, left_y = 0, right_x = 0, right_y = 0;
	unsigned short	flags = FLAG_RENDER;
	unsigned char	event_count;
	char	*str;
	long	tmp;
	int	x, y, width, height;
	struct termios orig_termios, new_termios;
	
	/* Save current terminal settings and disable echo */
	if (tcgetattr(STDIN_FILENO, &orig_termios) == 0) {
		new_termios = orig_termios;
		new_termios.c_lflag &= ~(ECHO | ECHONL);
		tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);
	}
	
	if (SDL_Init( SDL_INIT_VIDEO | SDL_INIT_TIMER ))
	{
		fprintf( stderr, "SDL_Init failed: %s\n", SDL_GetError() );
		tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
		return 1;
	}
	
	SDL_WM_SetCaption( "green - the PDF reader", NULL );
	
	/* Miyoo: fixed 640x480; draw remapped 180° straight into the video surface */
	g_rotate180 = IsMiyoo();
	if (g_rotate180)
	{
		if (rtd->width <= 0)
			rtd->width = 640;
		if (rtd->height <= 0)
			rtd->height = 480;
		display = SDL_SetVideoMode( 640, 480, 32, SDL_SWSURFACE );
	}
	else
	{
		display = SDL_SetVideoMode( rtd->width, rtd->height, 0, SDL_SWSURFACE | SDL_ANYFORMAT | SDL_RESIZABLE | (rtd->flags&GREEN_FULLSCREEN ? SDL_FULLSCREEN : 0) );
	}
	if (!display)
	{
		SDL_Quit();
		fprintf( stderr, "SDL_SetVideoMode failed: %s\n", SDL_GetError() );
		tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
		return 2;
	}
	
	if (display->format->palette)
	{
		SDL_Quit();
		fprintf( stderr, "Palettes are not supported!\n" );
		tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
                return 3;
	}
	
	timer = SDL_AddTimer( live_interval, live_timer, NULL );
	mouse_last = SDL_GetTicks();
	if (!rtd->mouse.visibility)
		SDL_ShowCursor( SDL_DISABLE );
	
	LoadBookmarks();
	
	do
	{
		if ((flags & FLAG_QUIT))
			break;
		if (flags&FLAG_RENDER)
		{
			Render( rtd );
			flags ^= FLAG_RENDER;
		}
		
		event_count = 0;
		if (!SDL_WaitEvent( &event ))
		{
			SDL_Quit();
			tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
			return -1;
		}
		
		do
		{
			switch (event.type)
			{
				case SDL_QUIT:
					flags |= FLAG_QUIT;
					break;
				case SDL_KEYUP:
					if (event.key.keysym.sym == SDLK_UP
						|| event.key.keysym.sym == SDLK_DOWN
						|| event.key.keysym.sym == SDLK_k
						|| event.key.keysym.sym == SDLK_j)
						ListNavStop();
					
					if (event.key.keysym.sym == SDLK_RCTRL)
					{
						/* Select release: toggle bookmarks if no chord was used */
						if (g_select_held && !g_select_chord
							&& (g_state == NORMAL || g_state == BOOKMARKS || g_state == TOC)
							&& !g_fp_active && !g_help_active)
							g_state = OpenBookmarksPanel( rtd, &flags );
						g_select_held = false;
						g_select_chord = false;
					}
					else if (event.key.keysym.sym == SDLK_LSHIFT)
					{
						/* X tap: zoom in (skipped if used as Shift modifier / chord) */
						if (g_x_held && !g_x_chord && g_state == NORMAL
							&& Green_IsDocValid( rtd, rtd->doc_cur ))
						{
							Green_Document	*d = rtd->docs[rtd->doc_cur];
							
							Green_Zoom( d, ViewWidth( rtd ), display->h,
								d->finescale * rtd->zoomstep );
							flags |= FLAG_RENDER;
						}
						g_x_held = false;
						g_x_chord = false;
					}
					else if (event.key.keysym.sym == SDLK_LALT)
					{
						/* Y tap: zoom out */
						if (g_y_held && !g_y_chord && g_state == NORMAL
							&& Green_IsDocValid( rtd, rtd->doc_cur ))
						{
							Green_Document	*d = rtd->docs[rtd->doc_cur];
							
							Green_Zoom( d, ViewWidth( rtd ), display->h,
								d->finescale / rtd->zoomstep );
							flags |= FLAG_RENDER;
						}
						g_y_held = false;
						g_y_chord = false;
					}
					break;
				case SDL_KEYDOWN:
					if (event.key.keysym.sym == SDLK_RCTRL
						&& (g_state == NORMAL || g_state == BOOKMARKS
							|| g_state == TOC || g_state == FILEPICKER
							|| g_state == HELP))
					{
						/* Select press: wait for chord or release (toggle) */
						g_select_held = true;
						g_select_chord = false;
						break;
					}
					
					if (event.key.keysym.sym == SDLK_LSHIFT
						&& g_state == NORMAL && !g_select_held)
					{
						g_x_held = true;
						g_x_chord = false;
						break;
					}
					
					if (event.key.keysym.sym == SDLK_LALT
						&& g_state == NORMAL && !g_select_held)
					{
						g_y_held = true;
						g_y_chord = false;
						break;
					}
					
					/* Another key while X/Y held => treat as keyboard modifier, not zoom */
					if (g_x_held && event.key.keysym.sym != SDLK_LSHIFT)
						g_x_chord = true;
					if (g_y_held && event.key.keysym.sym != SDLK_LALT)
						g_y_chord = true;
					
					if (event.key.keysym.sym == SDLK_ESCAPE
						|| event.key.keysym.sym == SDLK_HOME)
					{
						g_select_held = false;
						g_select_chord = false;
						g_x_held = false;
						g_x_chord = false;
						g_y_held = false;
						g_y_chord = false;
						/* Menu on Miyoo Mini: leave overlays, else close file */
						if (g_state != NORMAL || g_toc_active || g_bm_active
							|| g_fp_active || g_help_active)
						{
							ListNavStop();
							g_state = NORMAL;
							if (g_toc_active)
							{
								g_toc_active = false;
								flags |= FLAG_RENDER;
							}
							if (g_bm_active)
							{
								g_bm_active = false;
								flags |= FLAG_RENDER;
							}
							if (g_fp_active)
								CloseFilePicker( &flags );
							if (g_help_active)
							{
								g_help_active = false;
								flags |= FLAG_RENDER;
							}
						}
						else
							CloseCurrentFile( rtd, &flags );
					}
					else if (g_state == HELP)
						g_state = HelpInput( &event, &flags );
					else if (g_state == FILEPICKER)
					{
						/* Select chords while picker is open */
						if (g_select_held && event.key.keysym.sym == 't')
						{
							g_select_chord = true;
							CloseFilePicker( &flags );
							g_state = NORMAL;
						}
						else
							g_state = FilePickerInput( rtd, &event, &flags );
					}
					else if (g_state == TOC)
						g_state = TOCInput( rtd, &event, &flags );
					else if (g_state == BOOKMARKS)
						g_state = BookmarkInput( rtd, &event, &flags );
					else if (event.key.keysym.sym == SDLK_RETURN
						&& (g_state == GOTO || g_state == SEARCH))
					{
						if (!Green_IsDocValid( rtd, rtd->doc_cur ))
							break;
						
						g_input.buff[g_input.used] = 0;
						switch (g_state)
						{
							case GOTO:
								tmp = strtol( g_input.buff, &str, 10 );
								if (*str || tmp <=0 || tmp > rtd->docs[rtd->doc_cur]->page_count )
									break;
								
								rtd->docs[rtd->doc_cur]->page_cur = tmp - 1;
								rtd->docs[rtd->doc_cur]->xoffset = 0;
								rtd->docs[rtd->doc_cur]->yoffset = 0;
								flags |= FLAG_RENDER;
								break;
							case SEARCH:
								free( rtd->docs[rtd->doc_cur]->search_str );
								rtd->docs[rtd->doc_cur]->search_str = NULL;
								if (!strlen( g_input.buff ))
									break;
								
								rtd->docs[rtd->doc_cur]->search_str = strdup( g_input.buff );
								tmp = Green_FindNext( rtd->docs[rtd->doc_cur], rtd->docs[rtd->doc_cur]->page_cur );
								if (tmp < 0)
								{
									free( rtd->docs[rtd->doc_cur]->search_str );
									rtd->docs[rtd->doc_cur]->search_str = NULL;
									break;
								}
								
								rtd->docs[rtd->doc_cur]->page_cur = tmp;
								flags |= FLAG_RENDER;
								break;
							default:
								break;
						}
						
						g_state = NORMAL;
					}
					else if (g_state == NORMAL)
					{
						g_state = NormalInput( rtd, &event, &flags );
						if (g_state != NORMAL)
						{
							g_input.used = 0;
							g_input.cur = 0;
						}
					}
					else if (g_state == GOTO || g_state == SEARCH)
						GetInput( &g_input, &event );
					else if (g_state == FIT)
					{
						g_state = NORMAL;
						if (!Green_IsDocValid( rtd, rtd->doc_cur ))
							break;
						
						if (event.key.keysym.sym == 'n')
							rtd->docs[rtd->doc_cur]->fit_method = NATURAL;
						else if (event.key.keysym.sym == 'w')
							rtd->docs[rtd->doc_cur]->fit_method = WIDTH;
						else if (event.key.keysym.sym == 'h')
							rtd->docs[rtd->doc_cur]->fit_method = HEIGHT;
						else if (event.key.keysym.sym == 'p')
							rtd->docs[rtd->doc_cur]->fit_method = PAGE;
						
						if (event.key.keysym.sym == 'n'
							|| event.key.keysym.sym == 'w'
							|| event.key.keysym.sym == 'h'
							|| event.key.keysym.sym == 'p')
						{
							rtd->docs[rtd->doc_cur]->finescale = 1;
							rtd->docs[rtd->doc_cur]->xoffset = 0;
							rtd->docs[rtd->doc_cur]->yoffset = 0;
							flags |= FLAG_RENDER;
						}
					}
					else if (g_state == MIRROR)
					{
						g_state = NORMAL;
						if (!Green_IsDocValid( rtd, rtd->doc_cur ))
							break;
						
						if (event.key.keysym.sym == 'h')
						{
							Green_MirrorH( rtd->docs[rtd->doc_cur] );
							flags |= FLAG_RENDER;
						}
						else if (event.key.keysym.sym == 'v')
						{
							Green_MirrorV( rtd->docs[rtd->doc_cur] );
							flags |= FLAG_RENDER;
						}
					}
					else if (g_state == ROTATE)
					{
						g_state = NORMAL;
						if (!Green_IsDocValid( rtd, rtd->doc_cur ))
							break;
						
						if (event.key.keysym.sym == 'l')
						{
							Green_RotateLeft( rtd->docs[rtd->doc_cur] );
							Green_ValidateOffset( rtd->docs[rtd->doc_cur], display->w, display->h );
							flags |= FLAG_RENDER;
						}
						else if (event.key.keysym.sym == 'r')
						{
							Green_RotateRight( rtd->docs[rtd->doc_cur] );
							Green_ValidateOffset( rtd->docs[rtd->doc_cur], display->w, display->h );
							flags |= FLAG_RENDER;
						}
					}
					
					break;
				case SDL_VIDEORESIZE:
					if (g_rotate180)
						break;
					display = SDL_SetVideoMode( event.resize.w, event.resize.h, 0, SDL_HWSURFACE | SDL_ANYFORMAT | SDL_RESIZABLE );
					if (!display)
					{
						SDL_Quit();
						fprintf( stderr, "SDL_SetVideoMode failed: %s\n", SDL_GetError() );
						tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
						return -5;
					}
					
					if (Green_IsDocValid( rtd, rtd->doc_cur ))
						Green_ValidateOffset( rtd->docs[rtd->doc_cur], ViewWidth( rtd ), display->h );
					
					flags |= FLAG_RENDER;
					break;
				case SDL_MOUSEMOTION:
					if (rtd->mouse.visibility > 0)
					{
						mouse_last = SDL_GetTicks();
						SDL_ShowCursor( SDL_ENABLE );
					}
					
					break;
				case SDL_MOUSEBUTTONDOWN:
					if (rtd->mouse.visibility > 0)
					{
						mouse_last = SDL_GetTicks();
						SDL_ShowCursor( SDL_ENABLE );
					}
					
					if (!(rtd->mouse.flags&0x01) || !Green_IsDocValid( rtd, rtd->doc_cur ))
						break;
					
					switch (event.button.button)
					{
						case SDL_BUTTON_LEFT:
							left_x = event.button.x;
							left_y = event.button.y;
							break;
						case SDL_BUTTON_RIGHT:
							right_x = event.button.x;
							right_y = event.button.y;
							break;
						case SDL_BUTTON_WHEELDOWN:
							Green_Zoom( rtd->docs[rtd->doc_cur], ViewWidth( rtd ), display->h, rtd->docs[rtd->doc_cur]->finescale * rtd->zoomstep );
							Render( rtd );
							break;
						case SDL_BUTTON_WHEELUP:
							Green_Zoom( rtd->docs[rtd->doc_cur], ViewWidth( rtd ), display->h, rtd->docs[rtd->doc_cur]->finescale / rtd->zoomstep );
							Render( rtd );
							break;
					}
					
					break;
				case SDL_MOUSEBUTTONUP:
					if (rtd->mouse.visibility)
					{
						mouse_last = SDL_GetTicks();
						SDL_ShowCursor( SDL_ENABLE );
					}
					
					if (!(rtd->mouse.flags&0x01) || !Green_IsDocValid( rtd, rtd->doc_cur ))
						break;
					
					if (event.button.button == SDL_BUTTON_RIGHT)
					{
						Green_ScrollRelative( rtd->docs[rtd->doc_cur], right_x - event.button.x, right_y - event.button.y, ViewWidth( rtd ), display->h, 1 );
						flags |= FLAG_RENDER;
					}
					
					break;
				case SDL_USEREVENT:
					// Force render if in search or goto mode for cursor blinking
					if (g_state == SEARCH || g_state == GOTO)
						flags |= FLAG_RENDER;
					
					/* Held D-pad Up/Down: keep scrolling TOC / bookmarks / help */
					if (g_list_nav_dir
						&& (g_state == TOC || g_state == BOOKMARKS || g_state == HELP)
						&& SDL_GetTicks() >= g_list_nav_next)
					{
						if (g_state == TOC)
							TOCStepSel( g_list_nav_dir );
						else if (g_state == BOOKMARKS)
							BookmarkStepSel( g_list_nav_dir );
						else
							HelpStepScroll( g_list_nav_dir, HelpMaxScroll( display ) );
						g_list_nav_next = SDL_GetTicks() + LIST_NAV_RATE_MS;
						flags |= FLAG_RENDER;
					}
					
					/* Keep redrawing while toast is up so it clears after TOAST_MS */
					if (g_toast_until)
					{
						if (SDL_GetTicks() >= g_toast_until)
							g_toast_until = 0;
						flags |= FLAG_RENDER;
					}
					
					if (rtd->mouse.visibility > 0)
					{
						mouse_cur = SDL_GetTicks();
						if ((Uint32)(mouse_cur - mouse_last) > rtd->mouse.visibility)
							SDL_ShowCursor( SDL_DISABLE );
					}
					
					SDL_GetMouseState( &x, &y );
					if (rtd->mouse.border_size && Green_IsDocValid( rtd, rtd->doc_cur ) && x >= 0 && y >= 0 && x <= display->w && y <= display->h)
					{
						width = display->w * rtd->mouse.border_size / 100;
						height = display->h * rtd->mouse.border_size / 100;
						
						if (x < width)
							width = -((width - x) * display->w / width * rtd->mouse.border_speed * live_interval / 1000);
						else if (x > display->w - width)
							width = (x + width - display->w) * display->w / width * rtd->mouse.border_speed * live_interval / 1000;
						else
							width = 0;
						
						if (y < height)
							height = -((height - y) * display->h / height * rtd->mouse.border_speed * live_interval / 1000);
						else if (y > display->h - height)
							height = (y + height - display->h) * display->h / height * rtd->mouse.border_speed * live_interval / 1000;
						else
							height = 0;
						
						if (width || height)
						{
							Green_ScrollRelative( rtd->docs[rtd->doc_cur], width, height, ViewWidth( rtd ), display->h, 0 );
							flags |= FLAG_RENDER;
						}
					}
					
					break;
			}
			
			event_count++;
			
		}	while (event_count && SDL_PollEvent( &event ));
		
	}	while (!(flags&FLAG_QUIT));
	
	Green_SaveAllLastPages( rtd );
	SDL_RemoveTimer( timer );
	SDL_Quit();
	
	/* Flush any remaining SDL events */
	SDL_Event dummy;
	while (SDL_PollEvent(&dummy));
	
	/* Clear terminal input buffer using tcflush */
	tcflush(STDIN_FILENO, TCIFLUSH);
	
	/* Restore original terminal settings */
	tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
	
	return 0;
}
