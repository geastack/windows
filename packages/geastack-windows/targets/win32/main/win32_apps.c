/* SPDX-License-Identifier: Apache-2.0
 * The gea_embedded_apps_* C API the framework's generated app code calls; the
 * apps.h header is C++-only, so the prototypes are declared inline here. */
#include <string.h>

int gea_embedded_apps_launch(const char *app_id);
int gea_embedded_apps_return_to_launcher_on_reset(void);
void gea_embedded_apps_start_launcher_button_task(void);
const char *gea_embedded_apps_get_current_id(void);

static char s_current_app_id[64] = "";

int gea_embedded_apps_launch(const char *app_id)
{
	if (!app_id || !app_id[0]) return 0;
	strncpy_s(s_current_app_id, sizeof(s_current_app_id), app_id, sizeof(s_current_app_id) - 1);
	return 1;
}

int gea_embedded_apps_return_to_launcher_on_reset(void)
{
	return 0;
}

void gea_embedded_apps_start_launcher_button_task(void) {}

const char *gea_embedded_apps_get_current_id(void)
{
	return s_current_app_id;
}
