#ifndef __LIBSAVEAS_SAVEAS_H__
#define __LIBSAVEAS_SAVEAS_H__

enum {
	SAVEAS_STATUS_CANCEL,
	SAVEAS_STATUS_OK
};

extern int
saveas_show_popup(const char **path);

#endif
