/**
 * collectd - src/plugin.c
 * Copyright (C) 2005,2006  Florian octo Forster
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; only version 2 of the License is applicable.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * Authors:
 *   Florian octo Forster <octo at verplant.org>
 **/

#include "collectd.h"

#include <ltdl.h>

#include "plugin.h"
#include "configfile.h"
#include "utils_llist.h"
#include "utils_debug.h"

/*
 * Private variables
 */
static llist_t *list_init;
static llist_t *list_read;
static llist_t *list_write;
static llist_t *list_shutdown;
static llist_t *list_data_set;

static char *plugindir = NULL;

/*
 * Static functions
 */
static const char *plugin_get_dir (void)
{
	if (plugindir == NULL)
		return (PLUGINDIR);
	else
		return (plugindir);
}

static int register_callback (llist_t **list, const char *name, void *callback)
{
	llentry_t *le;

	if ((*list == NULL)
			&& ((*list = llist_create ()) == NULL))
		return (-1);

	le = llist_search (*list, name);
	if (le == NULL)
	{
		le = llentry_create (name, callback);
		if (le == NULL)
			return (-1);

		llist_append (*list, le);
	}
	else
	{
		le->value = callback;
	}

	return (0);
} /* int register_callback */

/*
 * (Try to) load the shared object `file'. Won't complain if it isn't a shared
 * object, but it will bitch about a shared object not having a
 * ``module_register'' symbol..
 */
static int plugin_load_file (char *file)
{
	lt_dlhandle dlh;
	void (*reg_handle) (void);

	DBG ("file = %s", file);

	lt_dlinit ();
	lt_dlerror (); /* clear errors */

	if ((dlh = lt_dlopen (file)) == NULL)
	{
		const char *error = lt_dlerror ();

		syslog (LOG_ERR, "lt_dlopen failed: %s", error);
		DBG ("lt_dlopen failed: %s", error);
		return (1);
	}

	if ((reg_handle = (void (*) (void)) lt_dlsym (dlh, "module_register")) == NULL)
	{
		syslog (LOG_WARNING, "Couldn't find symbol ``module_register'' in ``%s'': %s\n",
				file, lt_dlerror ());
		lt_dlclose (dlh);
		return (-1);
	}

	(*reg_handle) ();

	return (0);
}

/*
 * Public functions
 */
void plugin_set_dir (const char *dir)
{
	if (plugindir != NULL)
		free (plugindir);

	if (dir == NULL)
		plugindir = NULL;
	else if ((plugindir = strdup (dir)) == NULL)
		syslog (LOG_ERR, "strdup failed: %s", strerror (errno));
}

#define BUFSIZE 512
int plugin_load (const char *type)
{
	DIR  *dh;
	const char *dir;
	char  filename[BUFSIZE];
	char  typename[BUFSIZE];
	int   typename_len;
	int   ret;
	struct stat    statbuf;
	struct dirent *de;

	DBG ("type = %s", type);

	dir = plugin_get_dir ();
	ret = 1;

	/* `cpu' should not match `cpufreq'. To solve this we add `.so' to the
	 * type when matching the filename */
	if (snprintf (typename, BUFSIZE, "%s.so", type) >= BUFSIZE)
	{
		syslog (LOG_WARNING, "snprintf: truncated: `%s.so'", type);
		return (-1);
	}
	typename_len = strlen (typename);

	if ((dh = opendir (dir)) == NULL)
	{
		syslog (LOG_ERR, "opendir (%s): %s", dir, strerror (errno));
		return (-1);
	}

	while ((de = readdir (dh)) != NULL)
	{
		if (strncasecmp (de->d_name, typename, typename_len))
			continue;

		if (snprintf (filename, BUFSIZE, "%s/%s", dir, de->d_name) >= BUFSIZE)
		{
			syslog (LOG_WARNING, "snprintf: truncated: `%s/%s'", dir, de->d_name);
			continue;
		}

		if (lstat (filename, &statbuf) == -1)
		{
			syslog (LOG_WARNING, "stat %s: %s", filename, strerror (errno));
			continue;
		}
		else if (!S_ISREG (statbuf.st_mode))
		{
			/* don't follow symlinks */
			continue;
		}

		if (plugin_load_file (filename) == 0)
		{
			/* success */
			ret = 0;
			break;
		}
	}

	closedir (dh);

	return (ret);
}

/*
 * The `register_*' functions follow
 */
int plugin_register_config (const char *name,
		int (*callback) (const char *key, const char *val),
		const char **keys, int keys_num)
{
	cf_register (name, callback, keys, keys_num);
	return (0);
} /* int plugin_register_config */

int plugin_register_init (const char *name,
		int (*callback) (void))
{
	return (register_callback (&list_init, name, (void *) callback));
} /* plugin_register_init */

int plugin_register_read (const char *name,
		int (*callback) (void))
{
	return (register_callback (&list_read, name, (void *) callback));
} /* int plugin_register_read */

int plugin_register_write (const char *name,
		int (*callback) (const data_set_t *ds, const value_list_t *vl))
{
	return (register_callback (&list_write, name, (void *) callback));
} /* int plugin_register_write */

int plugin_register_shutdown (char *name,
		int (*callback) (void))
{
	return (register_callback (&list_shutdown, name, (void *) callback));
} /* int plugin_register_shutdown */

int plugin_register_data_set (const data_set_t *ds)
{
	return (register_callback (&list_data_set, ds->type, (void *) ds));
} /* int plugin_register_data_set */

void plugin_init_all (void)
{
	int (*callback) (void);
	llentry_t *le;

	if (list_init == NULL)
		return;

	le = llist_head (list_init);
	while (le != NULL)
	{
		callback = le->value;
		(*callback) ();

		le = le->next;
	}
} /* void plugin_init_all */

void plugin_read_all (const int *loop)
{
	int (*callback) (void);
	llentry_t *le;

	if (list_read == NULL)
		return;

	le = llist_head (list_read);
	while ((*loop == 0) && (le != NULL))
	{
		callback = le->value;
		(*callback) ();

		le = le->next;
	}
} /* void plugin_read_all */

void plugin_shutdown_all (void)
{
	int (*callback) (void);
	llentry_t *le;

	if (list_shutdown == NULL)
		return;

	le = llist_head (list_shutdown);
	while (le != NULL)
	{
		callback = le->value;
		(*callback) ();

		le = le->next;
	}
} /* void plugin_shutdown_all */

int plugin_dispatch_values (const char *name, const value_list_t *vl)
{
	int (*callback) (const data_set_t *, const value_list_t *);
	data_set_t *ds;
	llentry_t *le;

	if (list_write == NULL)
		return (-1);

	le = llist_search (list_data_set, name);
	if (le == NULL)
		return (-1);

	ds = (data_set_t *) le->value;

	le = llist_head (list_write);
	while (le != NULL)
	{
		callback = le->value;
		(*callback) (ds, vl);

		le = le->next;
	}

	return (0);
}

void plugin_complain (int level, complain_t *c, const char *format, ...)
{
	char message[512];
	va_list ap;
	int step;

	if (c->delay > 0)
	{
		c->delay--;
		return;
	}

	step = atoi (COLLECTD_STEP);
	assert (step > 0);

	if (c->interval < step)
		c->interval = step;
	else
		c->interval *= 2;

	if (c->interval > 86400)
		c->interval = 86400;

	c->delay = c->interval / step;

	va_start (ap, format);
	vsnprintf (message, 512, format, ap);
	message[511] = '\0';
	va_end (ap);

	syslog (level, message);
}

void plugin_relief (int level, complain_t *c, const char *format, ...)
{
	char message[512];
	va_list ap;

	if (c->interval == 0)
		return;

	c->interval = 0;

	va_start (ap, format);
	vsnprintf (message, 512, format, ap);
	message[511] = '\0';
	va_end (ap);

	syslog (level, message);
}
