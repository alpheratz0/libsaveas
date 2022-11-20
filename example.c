#include <stdio.h>
#include <string.h>
#include <libsaveas/saveas.h>

int
main(void)
{
	const char *save_path;

	while (saveas_show_popup(&save_path) != SAVEAS_STATUS_OK);
	printf("save_path = %s\n", save_path);
	return 0;
}
