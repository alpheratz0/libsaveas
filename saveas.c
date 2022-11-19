/*
	Copyright (C) 2022 <alpheratz99@protonmail.com>

	This program is free software; you can redistribute it and/or modify it under
	the terms of the GNU General Public License version 2 as published by the
	Free Software Foundation.

	This program is distributed in the hope that it will be useful, but WITHOUT ANY
	WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
	FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

	You should have received a copy of the GNU General Public License along with
	this program; if not, write to the Free Software Foundation, Inc., 59 Temple
	Place, Suite 330, Boston, MA 02111-1307 USA

*/

#include <png.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <setjmp.h>
#include <xcb/xcb.h>
#include <xcb/xcb_cursor.h>
#include <xcb/xcb_image.h>
#include <xcb/xcb_keysyms.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xproto.h>
#include <xcb/xkb.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include "fo.c"

#define WINDOW_WIDTH 400
#define WINDOW_HEIGHT 150
#define UNUSED __attribute__((unused))

static xcb_connection_t *conn;
static xcb_screen_t *screen;
static xcb_window_t window;
static xcb_gcontext_t gc;
static xcb_image_t *image;
static xcb_key_symbols_t *ksyms;
static xcb_cursor_context_t *cctx;
static xcb_cursor_t chand, carrow, cxterm, ccurrent;
static int32_t btnsave_x, btnsave_y, btnsave_width, btnsave_height;
static int32_t btncancel_x, btncancel_y, btncancel_width, btncancel_height;
static int32_t textbox_x, textbox_y, textbox_width, textbox_height;
static int32_t lblfilename_x, lblfilename_y, lblfilename_width, lblfilename_height;
static int textbox_focus;
static int textbox_length;
static size_t textbox_max_len;
char *textbox_text;
static uint32_t *wpx;
static int running, status;

static void
die(const char *fmt, ...)
{
	va_list args;

	fputs("libsaveas: ", stderr);
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	fputc('\n', stderr);
	exit(1);
}

static int
rect_contains_point(int rx, int ry, int rw, int rh, int x, int y)
{
	return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

static void
render_text(const char *text, uint32_t color, uint32_t x, uint32_t y, uint32_t area_width, uint32_t area_height)
{
	uint32_t cx, cy, gx, gy;
	unsigned char *glyph;
	const char *p;

	p = text;
	cx = x;
	cy = y;

	while (*p != '\0') {
		if (*p == '\n') {
			cx = x;
			cy += 10;
		} else if (*p == '\t') {
			cx += 7*4;
		} else {
			glyph = five_by_seven + *p*7;
			for (gy = 0; gy < 7; ++gy)
				for (gx = 0; gx < 5; ++gx)
					if (glyph[gy] & (1 << (4 - gx)) && cy+gy < WINDOW_HEIGHT && cy+gy-y < area_height
							&& cx+gx < WINDOW_WIDTH && cx+gx-x < area_width)
						wpx[(cy+gy)*WINDOW_WIDTH+cx+gx] = color;
			cx += 7;
		}

		++p;
	}
}

static void
render_rect(uint32_t color, int x, int y, int width, int height)
{
	int cx, cy;

	for (cx = x; cx < (x+width); ++cx)
		wpx[y*WINDOW_WIDTH+cx] = wpx[(y+height)*WINDOW_WIDTH+cx] = color;

	for (cy = y; cy < (y+height+1); ++cy)
		wpx[cy*WINDOW_WIDTH+x] = wpx[cy*WINDOW_WIDTH+x+width] = color;
}

static xcb_atom_t
get_atom(const char *name)
{
	xcb_atom_t atom;
	xcb_generic_error_t *error;
	xcb_intern_atom_cookie_t cookie;
	xcb_intern_atom_reply_t *reply;

	cookie = xcb_intern_atom(conn, 0, strlen(name), name);
	reply = xcb_intern_atom_reply(conn, cookie, &error);

	if (NULL != error)
		die("xcb_intern_atom failed with error code: %hhu", error->error_code);

	atom = reply->atom;
	free(reply);

	return atom;
}

static void
create_window(void)
{
    xcb_size_hints_t size_hints;

	running = 1;

	if (xcb_connection_has_error(conn = xcb_connect(NULL, NULL)))
		die("can't open display");

	if (NULL == (screen = xcb_setup_roots_iterator(xcb_get_setup(conn)).data))
		die("can't get default screen");

	if (xcb_cursor_context_new(conn, screen, &cctx) != 0)
		die("can't create cursor context");

	chand = xcb_cursor_load_cursor(cctx, "hand2");
	carrow = xcb_cursor_load_cursor(cctx, "left_ptr");
	cxterm = xcb_cursor_load_cursor(cctx, "xterm");
	ccurrent = carrow;

	if (NULL == (wpx = calloc(WINDOW_WIDTH * WINDOW_HEIGHT, sizeof(uint32_t))))
		die("error while calling malloc, no memory available");

	ksyms = xcb_key_symbols_alloc(conn);
	window = xcb_generate_id(conn);
	gc = xcb_generate_id(conn);

	xcb_create_window_aux(
		conn, screen->root_depth, window, screen->root, 0, 0,
		WINDOW_WIDTH, WINDOW_HEIGHT, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
		screen->root_visual, XCB_CW_EVENT_MASK,
		(const xcb_create_window_value_list_t []) {{
			.event_mask = XCB_EVENT_MASK_EXPOSURE |
			              XCB_EVENT_MASK_KEY_PRESS |
			              XCB_EVENT_MASK_BUTTON_PRESS |
			              XCB_EVENT_MASK_BUTTON_RELEASE |
			              XCB_EVENT_MASK_POINTER_MOTION |
			              XCB_EVENT_MASK_STRUCTURE_NOTIFY
		}}
	);

	xcb_create_gc(conn, gc, window, 0, NULL);

    xcb_icccm_size_hints_set_min_size(&size_hints, WINDOW_WIDTH, WINDOW_HEIGHT);
    xcb_icccm_size_hints_set_max_size(&size_hints, WINDOW_WIDTH, WINDOW_HEIGHT);
    xcb_icccm_set_wm_size_hints(conn, window, XCB_ATOM_WM_NORMAL_HINTS, &size_hints);

	image = xcb_image_create_native(
		conn, WINDOW_WIDTH, WINDOW_HEIGHT, XCB_IMAGE_FORMAT_Z_PIXMAP, screen->root_depth,
		wpx, sizeof(uint32_t) * WINDOW_WIDTH * WINDOW_HEIGHT, (uint8_t *)(wpx)
	);

	xcb_change_property(
		conn, XCB_PROP_MODE_REPLACE, window, get_atom("_NET_WM_NAME"),
		get_atom("UTF8_STRING"), 8, strlen("saveas"), "saveas"
	);

	xcb_change_property(
		conn, XCB_PROP_MODE_REPLACE, window, XCB_ATOM_WM_CLASS,
		XCB_ATOM_STRING, 8, strlen("saveas\0saveas\0"), "saveas\0saveas\0"
	);

	xcb_change_property(
		conn, XCB_PROP_MODE_REPLACE, window,
		get_atom("WM_PROTOCOLS"), XCB_ATOM_ATOM, 32, 1,
		(const xcb_atom_t []) { get_atom("WM_DELETE_WINDOW") }
	);

	xcb_change_property(
		conn, XCB_PROP_MODE_REPLACE, window,
		get_atom("_NET_WM_WINDOW_OPACITY"), XCB_ATOM_CARDINAL, 32, 1,
		(const uint8_t []) { 0xff, 0xff, 0xff, 0xff }
	);

	xcb_map_window(conn, window);

	xcb_xkb_use_extension(conn, XCB_XKB_MAJOR_VERSION, XCB_XKB_MINOR_VERSION);

	xcb_xkb_per_client_flags(
		conn, XCB_XKB_ID_USE_CORE_KBD,
		XCB_XKB_PER_CLIENT_FLAG_DETECTABLE_AUTO_REPEAT, 1, 0, 0, 0
	);

	xcb_flush(conn);
}

static void
destroy_window(void)
{
	xcb_free_gc(conn, gc);
	xcb_free_cursor(conn, chand);
	xcb_free_cursor(conn, carrow);
	xcb_free_cursor(conn, cxterm);
	xcb_key_symbols_free(ksyms);
	xcb_cursor_context_free(cctx);
	xcb_image_destroy(image);
	xcb_disconnect(conn);
}

static void
draw(void)
{
	int32_t title_x, title_y, title_width, title_height;

	memset(wpx, 30, sizeof(uint32_t) * WINDOW_WIDTH * WINDOW_HEIGHT);

	title_width = 7 * strlen("saveas");
	lblfilename_width = 7 * strlen("filename");
	textbox_width = WINDOW_WIDTH - 60 - lblfilename_width - 10;
	btnsave_width = 7 * strlen("save");
	btncancel_width = 7 * strlen("cancel");
	lblfilename_height = textbox_height = 16; 
	title_height = btnsave_height = btncancel_height = 20;

	title_x = (WINDOW_WIDTH - title_width) / 2;
	lblfilename_x = 30;
	textbox_x = lblfilename_x + lblfilename_width + 10;
	btnsave_x = WINDOW_WIDTH - btncancel_width - 30 - btnsave_width - 30;
	btncancel_x = WINDOW_WIDTH - btncancel_width - 30;
	title_y = 20;
	textbox_y = WINDOW_HEIGHT - btncancel_height - 30 - textbox_height - 15;
	lblfilename_y = textbox_y + (16-7)/2;
	btnsave_y = btncancel_y = WINDOW_HEIGHT - 30;

	render_text("saveas", 0xffffff, title_x, title_y, title_width, title_height);
	render_text("filename", textbox_focus ? 0x3bd97a : 0xffffff, lblfilename_x, lblfilename_y, lblfilename_width, lblfilename_height);
	render_text(textbox_text, textbox_focus ? 0x3bd97a : 0xffffff, textbox_x + 5, textbox_y + (16-7)/2, textbox_width - 10, 20);
	render_rect(textbox_focus ? 0x3bd97a : 0xffffff, textbox_x, textbox_y, textbox_width, textbox_height);
	render_text("save", 0xffffff, btnsave_x, btnsave_y, btnsave_width, btnsave_height);
	render_text("cancel", 0xffffff, btncancel_x, btncancel_y, btncancel_width, btncancel_height);
}

static void
swap_buffers(void)
{
	xcb_image_put(conn, window, gc, image, 0, 0, 0);
	xcb_flush(conn);
}

static void
h_client_message(xcb_client_message_event_t *ev)
{
	/* check if the wm sent a delete window message */
	/* https://www.x.org/docs/ICCCM/icccm.pdf */
	if (ev->data.data32[0] == get_atom("WM_DELETE_WINDOW")) {
		running = 0;
		status = 1;
	}
}

static void
h_expose(UNUSED xcb_expose_event_t *ev)
{
	draw();
	xcb_image_put(conn, window, gc, image, 0, 0, 0);
}

static char
get_char_from_keysym(xcb_keysym_t keysym)
{
	if (keysym >= XKB_KEY_a && keysym <= XKB_KEY_z)
		return 'a' + (keysym - XKB_KEY_a);
	if (keysym >= XKB_KEY_A && keysym <= XKB_KEY_Z)
		return 'A' + (keysym - XKB_KEY_A);
	if (keysym == XKB_KEY_comma)
		return ',';
	if (keysym == XKB_KEY_period)
		return '.';
	if (keysym == XKB_KEY_minus)
		return '-';
	if (keysym == XKB_KEY_underscore)
		return '_';
	if (keysym == XKB_KEY_space)
		return ' ';
	if (keysym == XKB_KEY_slash)
		return '/';
	if (keysym >= XKB_KEY_0 && keysym <= XKB_KEY_9)
		return '0' + (keysym - XKB_KEY_0);
	return '\0';
}

static void
h_key_press(xcb_key_press_event_t *ev)
{
	xcb_keysym_t key;
	char c;

	key = xcb_key_symbols_get_keysym(ksyms, ev->detail, ev->state);

	if (key == XKB_KEY_Escape) {
		if (textbox_focus) {
			textbox_focus = 0;
			draw();
			swap_buffers();
		}
		else {
			running = 0;
			status = 1;
		}
	} else if (key == XKB_KEY_Tab) {
		textbox_focus = !textbox_focus;
		draw();
		swap_buffers();
	} else if (key == XKB_KEY_BackSpace) {
		if (textbox_length > 0) {
			textbox_length -= 1;
			textbox_text[textbox_length] = '\0';
			draw();
			swap_buffers();
		}
	} else if (key == XKB_KEY_Return) {
		if (textbox_focus) {
			running = 0;
		}
	} else {
		if (textbox_focus) {
			c = get_char_from_keysym(key);
			if (c != '\0' && textbox_length < (int)textbox_max_len - 2) {
				textbox_text[textbox_length++] = c;
				textbox_text[textbox_length] = '\0';
			}
			draw();
			swap_buffers();
		}
	}
}

static void
h_button_press(xcb_button_press_event_t *ev)
{
	if (ev->detail == XCB_BUTTON_INDEX_1) {
		if (rect_contains_point(textbox_x, textbox_y, textbox_width, textbox_height, ev->event_x, ev->event_y))
			textbox_focus = 1;
		else {
			textbox_focus = 0;
			if (rect_contains_point(btnsave_x, btnsave_y, btnsave_width, btnsave_height, ev->event_x, ev->event_y)) {
				running = 0;
			}

			if (rect_contains_point(btncancel_x, btncancel_y, btncancel_width, btncancel_height, ev->event_x, ev->event_y)) {
				running = 0;
				status = 1;
			}
		}
	}

	draw();
	swap_buffers();
}

static void
h_motion_notify(xcb_motion_notify_event_t *ev)
{
	xcb_cursor_t next_cursor;

	next_cursor = carrow;

	if (rect_contains_point(btnsave_x, btnsave_y, btnsave_width, btnsave_height, ev->event_x, ev->event_y)
			|| rect_contains_point(btncancel_x, btncancel_y, btncancel_width, btncancel_height, ev->event_x, ev->event_y))
		next_cursor = chand;

	if (rect_contains_point(textbox_x, textbox_y, textbox_width, textbox_height, ev->event_x, ev->event_y))
		next_cursor = cxterm;

	if (next_cursor != ccurrent) {
		xcb_change_window_attributes(conn, window, XCB_CW_CURSOR, &next_cursor);
		xcb_flush(conn);
		ccurrent = next_cursor;
	}
}

static void
h_button_release(xcb_button_release_event_t *ev)
{
	switch (ev->detail) {
		case XCB_BUTTON_INDEX_1:
			break;
		case XCB_BUTTON_INDEX_2:
			break;
	}
}

static void
h_mapping_notify(xcb_mapping_notify_event_t *ev)
{
	if (ev->count > 0)
		xcb_refresh_keyboard_mapping(ksyms, ev);
}

extern int
saveas_show_popup(char *path, size_t max_len)
{
	xcb_generic_event_t *ev;

	textbox_text = path;
	textbox_max_len = max_len;
	textbox_length = 0;
	textbox_focus = 0;
	status = 0;

	create_window();

	while (running && (ev = xcb_wait_for_event(conn))) {
		switch (ev->response_type & ~0x80) {
			case XCB_CLIENT_MESSAGE:     h_client_message((void *)(ev)); break;
			case XCB_EXPOSE:             h_expose((void *)(ev)); break;
			case XCB_KEY_PRESS:          h_key_press((void *)(ev)); break;
			case XCB_BUTTON_PRESS:       h_button_press((void *)(ev)); break;
			case XCB_MOTION_NOTIFY:      h_motion_notify((void *)(ev)); break;
			case XCB_BUTTON_RELEASE:     h_button_release((void *)(ev)); break;
			case XCB_MAPPING_NOTIFY:     h_mapping_notify((void *)(ev)); break;
		}

		free(ev);
	}

	destroy_window();

	return status;
}
