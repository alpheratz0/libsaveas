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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <xcb/xcb.h>
#include <xcb/xcb_cursor.h>
#include <xcb/xcb_image.h>
#include <xcb/xcb_keysyms.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xproto.h>
#include <xcb/xkb.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include "fo.c"

#define UNUSED __attribute__((unused))

#define WIDTH                (400)
#define HEIGHT               (130)

struct label {
	int x, y, width, height;
	const char *text;
	uint32_t color;
};

struct button {
	int x, y, width, height;
	const char *text;
	void (*action)(void);
	uint32_t color;
};

struct textbox {
	int x, y, width, height, focused;
	char *text;
	size_t len, capacity;
	uint32_t color, focused_color;
};

static xcb_connection_t *conn;
static xcb_screen_t *screen;
static xcb_window_t window;
static xcb_gcontext_t gc;
static xcb_image_t *image;
static xcb_key_symbols_t *ksyms;
static xcb_cursor_context_t *cctx;
static xcb_cursor_t chand, carrow, cxterm, ccurrent;
static uint32_t *wpx;
static int running, status;
static void ok_button_cb(void);
static void cancel_button_cb(void);

static struct label labels[] = {
	{ 30, 30, 154, 7, "enter a filename below", 0xffffff }
};

static struct button buttons[] = {
	{ 290, 93, 28, 7, "save",   ok_button_cb,     0xffffff },
	{ 328, 93, 42, 7, "cancel", cancel_button_cb, 0xffffff },
};

static struct textbox textbox = {
	30, 57, 340, 16, 0, NULL, 0, 0, 0xffffff, 0xc7ff66,
};

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

static int
rect_contains_point(int rx, int ry, int rw, int rh, int x, int y)
{
	return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

static void
render_text(const char *text, uint32_t color, uint32_t x, uint32_t y,
            uint32_t area_width, uint32_t area_height)
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
					if (glyph[gy] & (1 << (4 - gx)) && cy+gy < HEIGHT && cy+gy-y < area_height
							&& cx+gx < WIDTH && cx+gx-x < area_width)
						wpx[(cy+gy)*WIDTH+cx+gx] = color;
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
		wpx[y*WIDTH+cx] = wpx[(y+height)*WIDTH+cx] = color;

	for (cy = y; cy < (y+height+1); ++cy)
		wpx[cy*WIDTH+x] = wpx[cy*WIDTH+x+width] = color;
}

static void
render_label(struct label *label)
{
	render_text(label->text, label->color, label->x, label->y, label->width, label->height);
}

static void
render_button(struct button *button)
{
	render_text(button->text, button->color, button->x, button->y, button->width, button->height);
}

static void
render_textbox(struct textbox *tb)
{
	render_text(tb->text, tb->color, tb->x + 5, tb->y + (tb->height - 7) / 2, tb->width - 10, tb->height);
	render_rect(tb->focused ? tb->focused_color : tb->color, tb->x, tb->y, tb->width, tb->height);
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

	if (NULL == (wpx = calloc(WIDTH * HEIGHT, sizeof(uint32_t))))
		die("error while calling malloc, no memory available");

	ksyms = xcb_key_symbols_alloc(conn);
	window = xcb_generate_id(conn);
	gc = xcb_generate_id(conn);

	xcb_create_window_aux(
		conn, screen->root_depth, window, screen->root, 0, 0,
		WIDTH, HEIGHT, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
		screen->root_visual, XCB_CW_EVENT_MASK,
		(const xcb_create_window_value_list_t []) {{
			.event_mask = XCB_EVENT_MASK_EXPOSURE |
			              XCB_EVENT_MASK_KEY_PRESS |
			              XCB_EVENT_MASK_BUTTON_PRESS |
			              XCB_EVENT_MASK_POINTER_MOTION |
			              XCB_EVENT_MASK_STRUCTURE_NOTIFY
		}}
	);

	xcb_create_gc(conn, gc, window, 0, NULL);

    xcb_icccm_size_hints_set_min_size(&size_hints, WIDTH, HEIGHT);
    xcb_icccm_size_hints_set_max_size(&size_hints, WIDTH, HEIGHT);
    xcb_icccm_set_wm_size_hints(conn, window, XCB_ATOM_WM_NORMAL_HINTS, &size_hints);

	image = xcb_image_create_native(
		conn, WIDTH, HEIGHT, XCB_IMAGE_FORMAT_Z_PIXMAP, screen->root_depth,
		wpx, sizeof(uint32_t) * WIDTH * HEIGHT, (uint8_t *)(wpx)
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
	size_t i;

	memset(wpx, 30, sizeof(uint32_t) * WIDTH * HEIGHT);

	for (i = 0; i < sizeof(labels)/sizeof(labels[0]); ++i)
		render_label(&labels[i]);

	for (i = 0; i < sizeof(buttons)/sizeof(buttons[0]); ++i)
		render_button(&buttons[i]);

	render_textbox(&textbox);
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

static void
h_key_press(xcb_key_press_event_t *ev)
{
	xcb_keysym_t key;
	char c;

	key = xcb_key_symbols_get_keysym(ksyms, ev->detail, ev->state);

	if (key == XKB_KEY_Escape) {
		if (textbox.focused) {
			textbox.focused = 0;
			draw();
			swap_buffers();
		}
		else {
			running = 0;
			status = 1;
		}
	} else if (key == XKB_KEY_Tab) {
		textbox.focused = !textbox.focused;
		draw();
		swap_buffers();
	} else if (key == XKB_KEY_BackSpace) {
		if (textbox.len > 0) {
			textbox.len -= 1;
			textbox.text[textbox.len] = '\0';
			draw();
			swap_buffers();
		}
	} else if (key == XKB_KEY_Return) {
		if (textbox.focused) {
			running = 0;
		}
	} else {
		if (textbox.focused) {
			c = get_char_from_keysym(key);
			if (c != '\0' && textbox.len < textbox.capacity - 2) {
				textbox.text[textbox.len++] = c;
				textbox.text[textbox.len] = '\0';
			}
			draw();
			swap_buffers();
		}
	}
}

static void
h_button_press(xcb_button_press_event_t *ev)
{
	size_t i;

	if (ev->detail == XCB_BUTTON_INDEX_1) {
		for (i = 0; i < sizeof(buttons)/sizeof(buttons[0]); ++i)
			if (rect_contains_point(buttons[i].x, buttons[i].y, buttons[i].width, buttons[i].height, ev->event_x, ev->event_y))
				if (buttons[i].action)
					buttons[i].action();

		textbox.focused = rect_contains_point(textbox.x, textbox.y, textbox.width, textbox.height, ev->event_x, ev->event_y);
	}

	draw();
	swap_buffers();
}

static void
h_motion_notify(xcb_motion_notify_event_t *ev)
{
	size_t i;
	xcb_cursor_t next_cursor;

	next_cursor = carrow;

	for (i = 0; i < sizeof(buttons)/sizeof(buttons[0]); ++i)
		if (rect_contains_point(buttons[i].x, buttons[i].y, buttons[i].width, buttons[i].height, ev->event_x, ev->event_y))
			next_cursor = chand;

	if (rect_contains_point(textbox.x, textbox.y, textbox.width, textbox.height, ev->event_x, ev->event_y))
		next_cursor = cxterm;

	if (next_cursor == ccurrent)
		return;

	xcb_change_window_attributes(conn, window, XCB_CW_CURSOR, &next_cursor);
	xcb_flush(conn);
	ccurrent = next_cursor;
}

static void
h_mapping_notify(xcb_mapping_notify_event_t *ev)
{
	if (ev->count > 0)
		xcb_refresh_keyboard_mapping(ksyms, ev);
}

static void
ok_button_cb(void)
{
	running = 0;
}

static void
cancel_button_cb(void)
{
	running = 0;
	status = 1;
}

extern int
saveas_show_popup(char *path, size_t max_len)
{
	xcb_generic_event_t *ev;

	textbox.capacity = max_len;
	textbox.focused = 0;
	textbox.text = path;
	textbox.len = 0;

	status = 0;

	create_window();

	while (running && (ev = xcb_wait_for_event(conn))) {
		switch (ev->response_type & ~0x80) {
			case XCB_CLIENT_MESSAGE:     h_client_message((void *)(ev)); break;
			case XCB_EXPOSE:             h_expose((void *)(ev)); break;
			case XCB_KEY_PRESS:          h_key_press((void *)(ev)); break;
			case XCB_BUTTON_PRESS:       h_button_press((void *)(ev)); break;
			case XCB_MOTION_NOTIFY:      h_motion_notify((void *)(ev)); break;
			case XCB_MAPPING_NOTIFY:     h_mapping_notify((void *)(ev)); break;
		}

		free(ev);
	}

	destroy_window();

	return status;
}
