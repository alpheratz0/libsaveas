#include <stdio.h>
#include <string.h>
#include <libsaveas/saveas.h>

int
main(void)
{
	char save_path[255] = { 0 };
	while (saveas_show_popup(save_path, sizeof(save_path)))
		memset(save_path, 0, sizeof(save_path));
	printf("save_path = %s\n", save_path);
	return 0;
}
