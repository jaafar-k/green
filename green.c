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

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <cairo.h>
#include "green.h"


typedef struct
{
	char	*uri;
	int	page;
		// 0-based
}	LastPageEntry;

static LastPageEntry	*g_last_pages = NULL;
static int	g_last_page_count = 0;
static bool	g_last_pages_loaded = false;


static char	*LastPagesPath( void )
{
	static char	path[512];
	const char	*home = getenv( "HOME" );
	
	if (!home || !*home)
		return NULL;
	if (snprintf( path, sizeof( path ), "%s/.green.pages", home ) >= (int)sizeof( path ))
		return NULL;
	return path;
}

static void	SaveLastPagesFile( void )
{
	FILE	*file;
	char	*path = LastPagesPath();
	int	i;
	
	if (!path)
		return;
	
	file = fopen( path, "w" );
	if (!file)
		return;
	
	for (i = 0; i < g_last_page_count; i++)
		fprintf( file, "%d %s\n", g_last_pages[i].page + 1, g_last_pages[i].uri );
	
	fclose( file );
	return;
}

static void	LoadLastPagesFile( void )
{
	FILE	*file;
	char	*path = LastPagesPath();
	char	line[4096];
	
	if (g_last_pages_loaded)
		return;
	
	g_last_pages_loaded = true;
	if (!path)
		return;
	
	file = fopen( path, "r" );
	if (!file)
		return;
	
	while (fgets( line, sizeof( line ), file ))
	{
		char	*uri, *end;
		long	page;
		LastPageEntry	*tmp;
		
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
		
		tmp = realloc( g_last_pages, (g_last_page_count + 1) * sizeof( *g_last_pages ) );
		if (!tmp)
			break;
		g_last_pages = tmp;
		g_last_pages[g_last_page_count].uri = strdup( uri );
		if (!g_last_pages[g_last_page_count].uri)
			break;
		g_last_pages[g_last_page_count].page = (int)page - 1;
		g_last_page_count++;
	}
	
	fclose( file );
	return;
}

static int	FindLastPageEntry( const char *uri )
{
	int	i;
	
	for (i = 0; i < g_last_page_count; i++)
		if (!strcmp( g_last_pages[i].uri, uri ))
			return i;
	return -1;
}

static void	StoreLastPage( Green_Document *doc, bool flush )
{
	LastPageEntry	*tmp;
	char		*uri;
	int		idx;
	
	if (!doc || !doc->uri)
		return;
	
	LoadLastPagesFile();
	idx = FindLastPageEntry( doc->uri );
	if (idx >= 0)
		g_last_pages[idx].page = doc->page_cur;
	else
	{
		uri = strdup( doc->uri );
		if (!uri)
			return;
		
		tmp = realloc( g_last_pages, (g_last_page_count + 1) * sizeof( *g_last_pages ) );
		if (!tmp)
		{
			free( uri );
			return;
		}
		
		g_last_pages = tmp;
		g_last_pages[g_last_page_count].uri = uri;
		g_last_pages[g_last_page_count].page = doc->page_cur;
		g_last_page_count++;
	}
	
	if (flush)
		SaveLastPagesFile();
	return;
}

static void	RestoreLastPage( Green_Document *doc )
{
	int	idx;
	
	if (!doc || !doc->uri)
		return;
	
	LoadLastPagesFile();
	idx = FindLastPageEntry( doc->uri );
	if (idx < 0)
		return;
	
	if (g_last_pages[idx].page >= 0 && g_last_pages[idx].page < doc->page_count)
		doc->page_cur = g_last_pages[idx].page;
	return;
}

char*	FilenameToURI( char *filename )
{
	const char	*prefix = "file:";
	char	abs = filename[0] == '/',
		*wd = abs ? NULL : getcwd( NULL, 0 ),
		*uri = malloc( strlen( prefix ) + strlen( filename ) + 1
			+ (abs ? 0 : strlen( wd ) + 1) );
	
	if (!uri || !(abs || wd))
	{
		free( uri );
		free( wd );
		return NULL;
	}
	
	if (abs)
		sprintf( uri, "%s%s", prefix, filename ); 
	else
	{
		sprintf( uri, "%s%s/%s", prefix, wd, filename );
		free( wd );
	}
	
	return uri;
}

void	Green_GetScrollRegion( Green_Document *doc, int w, int h, int *scroll_w, int *scroll_h )
{
	PopplerPage	*page;
	
	page = poppler_document_get_page( doc->doc, doc->page_cur );
	Green_GetDimension( page, scroll_w, scroll_h, Green_Fit( doc, w, h ) * doc->finescale, doc->rotation % 2 );
	g_object_unref( G_OBJECT( page ) );
	if (*scroll_w < w)
		*scroll_w = 0;
	else	
		*scroll_w -= w;
	
	if (*scroll_h < h)
		*scroll_h = 0;
	else	
		*scroll_h -= h;
	
	return;
}

int	Green_Open( Green_RTD *rtd, char *uri )
{
	Green_Document	**tmp, *doc = malloc( sizeof( *doc ) );
	int	i;
	
	if (!doc)
		return -1;
	
	if (!strncmp( uri, "file:", 5 ))
		doc->uri = strdup( uri );
	else
		doc->uri = FilenameToURI( uri );
	
	if (!doc->uri)
	{
		free( doc );
		return -1;
	}
	
	doc->doc = poppler_document_new_from_file( doc->uri, NULL, NULL );
	if (!doc->doc)
	{
		free( doc->uri );
		free( doc );
		return -2;
	}
	
	doc->page_count = poppler_document_get_n_pages( doc->doc );
	doc->page_cur = 0;
	doc->xoffset = 0;
	doc->yoffset = 0;
	doc->mirrored = false;
	doc->rotation = 0;
	doc->fit_method = rtd->fit_method;
	doc->finescale = 1;
	doc->search_str = NULL;
	doc->bb = rtd->bb;
	doc->cache.page = -1;
	doc->cache.tscale = 0;
	doc->cache.surface = NULL;
	RestoreLastPage( doc );
	for (i = 0; i < rtd->doc_count; i++)
	{
		if (rtd->docs[i])
			continue;
		
		rtd->docs[i] = doc;
		return i;
	}
	
	i = rtd->doc_count;
	tmp = realloc( rtd->docs, (rtd->doc_count + 1) * sizeof( *tmp ) );
	if (!tmp)
	{
		g_object_unref( G_OBJECT( doc->doc ) );
		free( doc->uri );
		free( doc );
		return -1;
	}
	
	tmp[i] = doc;
	rtd->docs = tmp;
	rtd->doc_count++;
	return i;
}

void	Green_Close( Green_RTD *rtd, int id )
{
	Green_Document	*doc;
	int	n;
	
	if (id < 0 || id >= rtd->doc_count || !rtd->docs[id])
		return;
	
	doc = rtd->docs[id];
	StoreLastPage( doc, true );
	g_object_unref( G_OBJECT( doc->doc ) );
	if (doc->cache.surface)
		cairo_surface_destroy( doc->cache.surface );
	free( doc->search_str );
	free( doc->uri );
	free( doc );
	rtd->docs[id] = NULL;
	if (id < rtd->doc_count - 1)
		return;
	
	for (n = id; n > 0 && !rtd->docs[n-1]; n-- );
	rtd->doc_count = n;
	rtd->docs = realloc( rtd->docs, n * sizeof( *rtd->docs ) );
	return;
}

void	Green_SaveAllLastPages( Green_RTD *rtd )
{
	int	i;
	
	if (!rtd)
		return;
	
	for (i = 0; i < rtd->doc_count; i++)
		if (rtd->docs[i])
			StoreLastPage( rtd->docs[i], false );
	
	SaveLastPagesFile();
	Green_SaveSettings( rtd );
	return;
}

static char	*SettingsPath( void )
{
	static char	path[512];
	const char	*home = getenv( "HOME" );
	
	if (!home || !*home)
		return NULL;
	if (snprintf( path, sizeof( path ), "%s/.green.settings", home ) >= (int)sizeof( path ))
		return NULL;
	return path;
}

void	Green_LoadSettings( Green_RTD *rtd )
{
	FILE	*file;
	char	*path = SettingsPath();
	char	line[128];
	
	if (!rtd || !path)
		return;
	
	file = fopen( path, "r" );
	if (!file)
		return;
	
	while (fgets( line, sizeof( line ), file ))
	{
		if (!strncmp( line, "night=", 6 ))
			rtd->night = (line[6] != '0');
		else if (!strncmp( line, "auto_zoom=", 10 ))
			rtd->auto_zoom = (line[10] != '0');
	}
	
	fclose( file );
	return;
}

void	Green_SaveSettings( Green_RTD *rtd )
{
	FILE	*file;
	char	*path = SettingsPath();
	
	if (!rtd || !path)
		return;
	
	file = fopen( path, "w" );
	if (!file)
		return;
	
	fprintf( file, "night=%d\n", rtd->night ? 1 : 0 );
	fprintf( file, "auto_zoom=%d\n", rtd->auto_zoom ? 1 : 0 );
	fclose( file );
	return;
}

/* Expand bbox (PDF pts, bottom-left origin) by margin; returns 0 if empty */
static int	UnionRect( double *x1, double *y1, double *x2, double *y2,
	double ax1, double ay1, double ax2, double ay2 )
{
	if (ax2 <= ax1 || ay2 <= ay1)
		return 0;
	if (*x2 <= *x1 || *y2 <= *y1)
	{
		*x1 = ax1; *y1 = ay1; *x2 = ax2; *y2 = ay2;
		return 1;
	}
	if (ax1 < *x1) *x1 = ax1;
	if (ay1 < *y1) *y1 = ay1;
	if (ax2 > *x2) *x2 = ax2;
	if (ay2 > *y2) *y2 = ay2;
	return 1;
}

static int	ContentBBoxTextImages( PopplerPage *page, double *x1, double *y1, double *x2, double *y2 )
{
	PopplerRectangle	*rects = NULL;
	guint	n = 0, i;
	GList	*images, *l;
	int	ok = 0;
	
	*x1 = *y1 = *x2 = *y2 = 0;
	if (poppler_page_get_text_layout( page, &rects, &n ) && rects && n)
	{
		for (i = 0; i < n; i++)
			ok |= UnionRect( x1, y1, x2, y2,
				rects[i].x1, rects[i].y1, rects[i].x2, rects[i].y2 );
		g_free( rects );
	}
	images = poppler_page_get_image_mapping( page );
	for (l = images; l; l = l->next)
	{
		PopplerImageMapping	*im = l->data;
		
		ok |= UnionRect( x1, y1, x2, y2,
			im->area.x1, im->area.y1, im->area.x2, im->area.y2 );
	}
	if (images)
		poppler_page_free_image_mapping( images );
	return ok;
}

/* Low-res ink scan; PDF bottom-left bbox out */
static int	ContentBBoxInkScan( PopplerPage *page, double pwidth, double pheight,
	double *x1, double *y1, double *x2, double *y2 )
{
	cairo_surface_t	*surface;
	cairo_t		*cr;
	unsigned char	*data;
	int	stride, w, h, row, col, minx, miny, maxx, maxy, found = 0;
	double	scale;
	
	*x1 = *y1 = *x2 = *y2 = 0;
	if (pwidth < 1 || pheight < 1)
		return 0;
	scale = 160.0 / pwidth;
	if (scale * pheight > 240)
		scale = 240.0 / pheight;
	if (scale < 0.05)
		scale = 0.05;
	w = (int)(pwidth * scale);
	h = (int)(pheight * scale);
	if (w < 8 || h < 8)
		return 0;
	
	surface = cairo_image_surface_create( CAIRO_FORMAT_ARGB32, w, h );
	if (cairo_surface_status( surface ) != CAIRO_STATUS_SUCCESS)
		return 0;
	cr = cairo_create( surface );
	cairo_scale( cr, scale, scale );
	cairo_set_source_rgb( cr, 1, 1, 1 );
	cairo_paint( cr );
	poppler_page_render( page, cr );
	cairo_destroy( cr );
	cairo_surface_flush( surface );
	
	data = cairo_image_surface_get_data( surface );
	stride = cairo_image_surface_get_stride( surface );
	minx = w; miny = h; maxx = 0; maxy = 0;
	for (row = 0; row < h; row++)
	{
		unsigned int	*pix = (unsigned int *)(data + row * stride);
		
		for (col = 0; col < w; col++)
		{
			unsigned int	p = pix[col];
			unsigned	r = (p >> 16) & 0xFF, g = (p >> 8) & 0xFF, b = p & 0xFF;
			
			/* Near-white / empty */
			if (r >= 250 && g >= 250 && b >= 250)
				continue;
			if (col < minx) minx = col;
			if (row < miny) miny = row;
			if (col > maxx) maxx = col;
			if (row > maxy) maxy = row;
			found = 1;
		}
	}
	cairo_surface_destroy( surface );
	if (!found || maxx < minx || maxy < miny)
		return 0;
	
	/* Pixel rows are top-down; PDF y is bottom-up */
	*x1 = minx / scale;
	*x2 = (maxx + 1) / scale;
	*y2 = pheight - (miny / scale);
	*y1 = pheight - ((maxy + 1) / scale);
	if (*x2 > pwidth) *x2 = pwidth;
	if (*y2 > pheight) *y2 = pheight;
	if (*x1 < 0) *x1 = 0;
	if (*y1 < 0) *y1 = 0;
	return (*x2 > *x1 && *y2 > *y1);
}

int	Green_ApplyAutoZoom( Green_Document *doc, int view_w, int view_h )
{
	PopplerPage	*page;
	double	pwidth, pheight, x1, y1, x2, y2, bw, bh, pad, fit, fs, top_y;
	double	area_page, area_box;
	int	ok;
	
	if (!doc || view_w < 16 || view_h < 16)
		return 0;
	
	page = poppler_document_get_page( doc->doc, doc->page_cur );
	if (!page)
		return 0;
	poppler_page_get_size( page, &pwidth, &pheight );
	
	ok = ContentBBoxTextImages( page, &x1, &y1, &x2, &y2 );
	bw = x2 - x1;
	bh = y2 - y1;
	area_page = pwidth * pheight;
	area_box = (bw > 0 && bh > 0) ? bw * bh : 0;
	/* Empty, tiny, or nearly full page → try ink scan */
	if (!ok || area_box < area_page * 0.02 || area_box > area_page * 0.92)
		ok = ContentBBoxInkScan( page, pwidth, pheight, &x1, &y1, &x2, &y2 );
	g_object_unref( page );
	
	if (!ok)
		return 0;
	bw = x2 - x1;
	bh = y2 - y1;
	if (bw < 8 || bh < 8)
		return 0;
	area_box = bw * bh;
	if (area_box > area_page * 0.95)
	{
		doc->finescale = 1;
		doc->xoffset = 0;
		doc->yoffset = 0;
		return 1;
	}
	
	pad = (pwidth < pheight ? pwidth : pheight) * 0.02;
	x1 -= pad; y1 -= pad; x2 += pad; y2 += pad;
	if (x1 < 0) x1 = 0;
	if (y1 < 0) y1 = 0;
	if (x2 > pwidth) x2 = pwidth;
	if (y2 > pheight) y2 = pheight;
	bw = x2 - x1;
	bh = y2 - y1;
	
	fit = Green_Fit( doc, view_w, view_h );
	if (fit <= 0)
		fit = 1;
	/* Page-fit the content box (unrotated); rotation≠0: zoom only */
	fs = view_w / (bw * fit);
	if (view_h / (bh * fit) < fs)
		fs = view_h / (bh * fit);
	if (fs < 1)
		fs = 1;
	if (fs > 8)
		fs = 8;
	doc->finescale = fs;
	
	top_y = pheight - y2;
	if (doc->rotation == 0 && !doc->mirrored)
	{
		doc->xoffset = (int)(x1 * fit * fs);
		doc->yoffset = (int)(top_y * fit * fs);
	}
	else
	{
		doc->xoffset = 0;
		doc->yoffset = 0;
	}
	Green_ValidateOffset( doc, view_w, view_h );
	return 1;
}

double	Green_Fit( Green_Document *doc, int w, int h )
{
	PopplerPage	*page;
	double	pwidth, pheight;
	
	if (doc->fit_method == NATURAL)
		return 1;
	
	page = poppler_document_get_page( doc->doc, doc->page_cur );
	if (doc->rotation % 2)
		poppler_page_get_size( page, &pheight, &pwidth );
	else
		poppler_page_get_size( page, &pwidth, &pheight );
	
	if (doc->fit_method == WIDTH)
		return w / pwidth;
	else if (doc->fit_method == HEIGHT)
		return h / pheight;
	else if (doc->fit_method == PAGE)
		return (w / pwidth <= h / pheight) ? w / pwidth : h / pheight;
	
	return 1;
}

void	Green_ScrollRelative( Green_Document *doc, int x, int y, int w, int h, int bb_flag )
{
	unsigned char	bb_mode;
	int	doc_dx, doc_dy, doc_max_x, doc_max_y,
		abs_x = x < 0 ? -x : x,
		abs_y = y < 0 ? -y : y,
		max_x, max_y;
	bool	left_border, right_border, top_border, bottom_border, bb_done = false;
		
	Green_GetScrollRegion( doc, w, h, &max_x, &max_y );
	if (doc->rotation % 2)
	{
		doc_max_x = max_y;
		doc_max_y = max_x;
	}
	else
	{
		doc_max_x = max_x;
		doc_max_y = max_y;
	}

	if (doc->rotation == 1)
	{
		doc_dx = doc->mirrored ? -y : y;
		doc_dy = -x;
		left_border = doc->yoffset == doc_max_y;
		right_border = doc->yoffset == 0;
		if (doc->mirrored)
		{
			top_border = doc->xoffset == doc_max_x;
			bottom_border = doc->xoffset == 0;
		}
		else
		{
			top_border = doc->xoffset == 0;
			bottom_border = doc->xoffset == doc_max_x;
		}
	}
	else if (doc->rotation == 2)
	{
		doc_dx = -x;
		doc_dy = doc->mirrored ? y : -y;
		left_border = doc->xoffset == doc_max_x;
		right_border = doc->xoffset == 0;
		if (doc->mirrored)
		{
			top_border = doc->yoffset == 0;
			bottom_border = doc->yoffset == doc_max_y;
		}
		else
		{
			top_border = doc->yoffset == doc_max_y;
			bottom_border = doc->yoffset == 0;
		}
	}
	else if (doc->rotation == 3)
	{
		doc_dx = doc->mirrored ? y : -y;
		doc_dy = x;
		left_border = doc->yoffset == 0;
		right_border = doc->yoffset == doc_max_y;
		if (doc->mirrored)
		{
			top_border = doc->xoffset == 0;
			bottom_border = doc->xoffset == doc_max_x;
		}
		else
		{
			top_border = doc->xoffset == doc_max_x;
			bottom_border = doc->xoffset == 0;
		}
	}
	else
	{
		doc_dx = x;
		doc_dy = doc->mirrored ? -y : y;
		left_border = doc->xoffset == 0;
		right_border = doc->xoffset == doc_max_x;
		if (doc->mirrored)
		{
			top_border = doc->yoffset == doc_max_y;
			bottom_border = doc->yoffset == 0;
		}
		else
		{
			top_border = doc->yoffset == 0;
			bottom_border = doc->yoffset == doc_max_y;
		}
	}
	
	if (bb_flag)
	{
		if (abs_x > abs_y && x > 0 && right_border)
		{
			if (!(doc->bb&0xF0) && (doc->bb&0x03))
			{
				bb_mode = doc->bb&0x03;
				if (bb_mode == 1)
					bb_done = Green_GotoPage( doc, doc->page_cur + 1, false );
				else if (bb_mode == 2)
					bb_done = Green_GotoPage( doc, doc->page_cur - 1, false );
				
				if (bb_done)
				{
					Green_GetScrollRegion( doc, w, h, &max_x, &max_y );
					if (doc->rotation == 1)
						doc->yoffset = 0;
					else if (doc->rotation == 2)
						doc->xoffset = max_x;
					else if (doc->rotation == 3)
						doc->yoffset = max_x;
					else
						doc->xoffset = 0;
					
					Green_ValidateOffset( doc, w, h );
				}
			}
			
			return;
		}
		else if (abs_x > abs_y && x < 0 && left_border)
		{
			if (!(doc->bb&0xF0) && (doc->bb&0x03))
			{
				bb_mode = doc->bb&0x03;
				if (bb_mode == 1)
					bb_done = Green_GotoPage( doc, doc->page_cur - 1, false );
				else if (bb_mode == 2)
					bb_done = Green_GotoPage( doc, doc->page_cur + 1, false );
				
				if (bb_done)
				{
					Green_GetScrollRegion( doc, w, h, &max_x, &max_y );
					if (doc->rotation == 1)
						doc->yoffset = max_x;
					else if (doc->rotation == 2)
						doc->xoffset = 0;
					else if (doc->rotation == 3)
						doc->yoffset = 0;
					else
						doc->xoffset = max_x;
					
					Green_ValidateOffset( doc, w, h );
				}

			}
			
			return;
		}
		else if (abs_x < abs_y && y > 0 && bottom_border)
		{
			if (!(doc->bb&0xF0) && (doc->bb&0x0C))
			{
				bb_mode = (doc->bb>>2)&0x03;
				if (bb_mode == 1)
					bb_done = Green_GotoPage( doc, doc->page_cur + 1, false );
				else if (bb_mode == 2)
					bb_done = Green_GotoPage( doc, doc->page_cur - 1, false );
				
				if (bb_done)
				{
					Green_GetScrollRegion( doc, w, h, &max_x, &max_y );
					if (doc->rotation == 1)
						doc->xoffset = doc->mirrored ? max_y : 0;
					else if (doc->rotation == 2)
						doc->yoffset = doc->mirrored ? 0 : max_y;
					else if (doc->rotation == 3)
						doc->xoffset = doc->mirrored ? 0 : max_y;
					else
						doc->yoffset = doc->mirrored ? max_y : 0;
					
					Green_ValidateOffset( doc, w, h );
				}
			}
			
			return;
		}
		else if (abs_x < abs_y && y < 0 && top_border)
		{
			if (!(doc->bb&0xF0) && (doc->bb&0x0C))
			{
				bb_mode = (doc->bb>>2)&0x03;
				if (bb_mode == 1)
					bb_done = Green_GotoPage( doc, doc->page_cur - 1, false );
				else if (bb_mode == 2)
					bb_done = Green_GotoPage( doc, doc->page_cur + 1, false );
				
				if (bb_done)
				{
					Green_GetScrollRegion( doc, w, h, &max_x, &max_y );
					if (doc->rotation == 1)
						doc->xoffset = doc->mirrored ? 0 : max_y;
					else if (doc->rotation == 2)
						doc->yoffset = doc->mirrored ? max_y : 0;
					else if (doc->rotation == 3)
						doc->xoffset = doc->mirrored ? max_y : 0;
					else
						doc->yoffset = doc->mirrored ? 0 : max_y;
					
					Green_ValidateOffset( doc, w, h );
				}
			}
			
			return;
		}
	}
	
	doc->xoffset += doc_dx;
	doc->yoffset += doc_dy;
	Green_ValidateOffset( doc, w, h );
	return;
}

void	Green_Zoom( Green_Document *doc, int width, int height, double new_fs )
{
	PopplerPage	*page;
	double	old_tscale, new_tscale;
	int	old_w, old_h, new_w, new_h;
	
	page = poppler_document_get_page( doc->doc, doc->page_cur );
	old_tscale = Green_Fit(doc, width, height) * doc->finescale;
	doc->finescale = new_fs;
	new_tscale = Green_Fit(doc, width, height) * new_fs;
	Green_GetDimension( page, &old_w, &old_h, old_tscale, doc->rotation % 2 );
	Green_GetDimension( page, &new_w, &new_h, new_tscale, doc->rotation % 2 );
	g_object_unref( G_OBJECT( page ) );
	
	if (doc->rotation % 2 == 0)
		doc->xoffset = doc->xoffset * new_tscale / old_tscale
			+ (new_tscale - old_tscale) * width / 2 / new_tscale;
	else
		doc->xoffset = doc->xoffset * new_tscale / old_tscale
			+ (new_tscale - old_tscale) * height / 2 / new_tscale;
	
	if (doc->rotation % 2 == 0)
		doc->yoffset = doc->yoffset * new_tscale / old_tscale
			+ (new_tscale - old_tscale) * height / 2 / new_tscale;
	else
		doc->yoffset = doc->yoffset * new_tscale / old_tscale
			+ (new_tscale - old_tscale) * width / 2 / new_tscale;
	
	Green_ValidateOffset( doc, width, height );
	return;
}

int	Green_FindNext( Green_Document *doc, int start )
{
	PopplerPage	*page;
	GList	*list;
	int	i, res = -1;
	
	for (i = 0; i < doc->page_count; i++)
	{
		page = poppler_document_get_page( doc->doc, (start + i) % doc->page_count );
		list = poppler_page_find_text( page, doc->search_str );
		g_object_unref( G_OBJECT( page ) );
		if (list)
		{
			g_list_free( list );
			res = (start + i) % doc->page_count;
			break;
		}
	}
	
	return res;
}

int	Green_FindPrevious( Green_Document *doc, int start )
{
	PopplerPage	*page;
	GList	*list;
	int	i, res = -1;
	
	for (i = 0; i < doc->page_count; i++)
	{
		int page_idx = (start - i - 1 + doc->page_count) % doc->page_count;
		page = poppler_document_get_page( doc->doc, page_idx );
		list = poppler_page_find_text( page, doc->search_str );
		g_object_unref( G_OBJECT( page ) );
		if (list)
		{
			g_list_free( list );
			res = page_idx;
			break;
		}
	}
	
	return res;
}
