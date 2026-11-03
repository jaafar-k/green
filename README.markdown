GREEN - A light weight PDF reader
=======================================

NAME
----

green - a lightweight PDF reader for the Framebuffer using libpoppler

SYNOPSIS
--------

`green` [`options`] <`PDF file 1`> *[PDF file 2]* ...

DESCRIPTION
-----------

`green` is meant to be a light PDF reader for the Linux Framebuffer. 
However, it can also be used inside a graphical X11 Session like GNOME or
MATE. 

`green` features:

 - uses libpoppler for PDF reading
 - uses SDL to support various frontends (including framebuffer)
 - multiple documents
 - single page mode
 - side-by-side (double page) mode
 - fit width, height or page
 - zooming
 - goto page
 - search function
 - table of contents navigation
 - user bookmarks (persistent)
 - night mode
 - scheme support


OPTIONS
-------

`-fit=`
  with one of *none, width, height* or *page* tp select the program wide page fitting mode.
`-width=` 
  with an integer greate equal zero (in pixels) to specify the startup width of the window.
`-height=` 
  with an integer greate equal zero (in pixels) to specify the startup height of the window.
`-fullscreen=`
  startup in fullscreen mode.
`-nofullscreen=`
  startup in window mode.
`-config=`
  with a file name of a configuration file.
`-scheme=`
  with an `<id list>` (see below) to select a different scheme.

PROGRAM OPERATION
------------------
`o` - Show/hide the table of contents.  
`<F<n>>` - Go to the n-th document.  
`ESC` / Menu - Escape current input mode, or close file when idle.    
`q` - Quit


NAVIGATION INSIDE A DOCUMENT
----------------------------
`<h, left arrow>` - Scroll left.  
`<l, right arrow>` - Scroll right.  
`<j, down arrow>` - Scroll down.  
`<k, up arrow>` - Scroll up.  
`<pg up>` - Go to previous page.  
`<pg dn>` - Go to next page.  
`<g<n>RETURN>` - Go to page n.  
`<+,->` - Zoom in, Zoom out.  
`i` - Toggle night mode (inverted colors).  
`d` - Toggle side-by-side (double page) mode.  
`c` - close document.

### TABLE OF CONTENTS
`o` - Enter the table of contents (for PDFs that have one).  
`<j, k>` - Move the selection up and down.  
`<RETURN>` - Go to the selected section and close the table of contents.  
`<l>` - Expand a section to show its subsections; on a leaf section, jump to it.  
`<h>` - Collapse a section, hiding its subsections.  
`<ESC>` - Leave the table of contents without jumping.

### MIYOO MINI CONTROLS
Miyoo buttons are exposed as SDL keys (Onion/MinUI). Mapped as:

| Button | Action |
|--------|--------|
| D-pad | Scroll / navigate |
| A | Fit to width |
| B | Rotate 90° |
| X | Zoom in |
| Y | Zoom out |
| L1 / R1 | Previous / next page |
| L2 / R2 | Jump back / forward 10 pages |
| Start | Table of contents |
| Select (tap) | Bookmarks menu |
| Select + A | Toggle night mode |
| Select + B | Toggle bookmark on current page |
| Select + X | Mirror horizontally |
| Select + Y | Mirror vertically |
| Select + L1 | Toggle dual-page mode |
| Menu | Close file (quit if none left) |

Select is a hold-modifier: chords fire on the face button; a plain Select release opens bookmarks.

### BOOKMARKS
`b` - Open the bookmarks panel for the current document.  
`B` (`Shift+b`) - Toggle a bookmark on the current page.  
In the bookmarks panel:  
`<j, k>` - Move the selection up and down.  
`<RETURN>` - Jump to the selected bookmark.  
`a` - Add a bookmark for the current page.  
`x` - Delete the selected bookmark.  
`<ESC>` / `b` - Close the bookmarks panel.

### FITTING
`fn` - disable page fitting mode.
`fw` - fit page width.
`fh` - fit page height.
`fp` - fit whole page.

### SEARCHING 
`s<X><RETURN> - Start search for string X.`
`n` - Show next result.

### OTHER STUFF
When starting green in Framebuffer console you might see an error regarding the mouse. 
If you don't need mouse in the console:

    SDL_NOMOUSE=1 ./green 

Should work around the problem. Other wise you should be able to use the mouse in the 
Framebuffer as none root user. 
On Debian based distributions:

Create new file `/etc/udev/rules.d/99-input.rules`:

    # file /etc/udev/rules.d/99-input.rules
    KERNEL=="mice", NAME="input/%k", MODE="664", GROUP="input"
    KERNEL=="mouse*", NAME="input/%k", MODE="664", GROUP="input"

Then issue:
    
    groupadd input
    usermod -a -G input [your_username]

Restart your computer and you should be able to use the mouse with SDL. 

FILES
-----
*$(HOME)/.green.conf*     
  Per user configuration file.  

*$(HOME)/.green.bookmarks*  
  Persistent user bookmarks (page + document URI).  

*$(HOME)/.green.pages*  
  Last-viewed page per document (restored on reopen).  

*/usr/local/etc/green.conf*  
  The system wide configuration file.   


AUTHOR
------
The Green source code may be downloaded from <http://github.com/schandinat/green/>.   
Green is Licensed under GNU GPL version 3.  
This man page was written for the Debian GNU / Linux System by Oz Nahum <nahumoz@gmail.com>.

CHANGES IN THIS FORK
--------------------
This fork adds the following on top of the original `green`:

 - **Side-by-side (double page) mode** (`d`): shows two pages at once with
   book-style pairing (1-2, 3-4, ...). PageUp/PageDown and j/k step through
   page pairs.
 - **Table of contents** (`TAB`): for PDFs that have an outline, opens a
   navigable table of contents panel. Use `j`/`k` to move, `Enter` to jump to
   a section, `l` to expand and `h` to collapse subsections, `Esc` to close.
 - **Night mode** (`i`): inverts the page colors for reading in the dark.
 - **Fixed scrolling while zoomed in double-page mode**: the scroll region is
   now computed against the actual (half-width) page viewport and the page
   offset is applied when rendering both pages, so zooming in no longer
   requires several key presses (or scrolling into blank space) to reach the
   next page pair.
 - **Search backwards** (`,`): in addition to `n` for the next result, `,`
   jumps to the previous search result.
 - **Bookmarks** (`b` / `Shift+b`): save pages per document to
   `~/.green.bookmarks`. Open the panel with `b`, toggle the current page
   with `Shift+b`, add/delete from the panel with `a`/`x`.
 - **Last page restore**: remembers the current page per document in
   `~/.green.pages` and jumps back there when the file is opened again.
 - **Fast page display on weak devices**: page rasters are capped to the
   screen/pane size (no oversized bitmaps), and single-page mode shows a
   1/4-scale preview immediately, then refines to full screen quality on
   the next idle tick.
 - **Miyoo Mini controls**: handheld mapping with Select chords (tap Select =
   bookmarks, Start = TOC, Select+A/B/X/Y = night / bookmark / mirrors).

