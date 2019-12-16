/* (C) 2019 by Harald Welte <laforge@gnumonks.org>
 *
 * All Rights Reserved
 *
 * SPDX-License-Identifier: GPL-2.0+
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/types.h>

#include <osmocom/core/logging.h>
#include <osmocom/core/utils.h>
#include <osmocom/core/exec.h>

/* list of environment variables that we pass (if they exist) to the sub-process/script */
static const char *environment_whitelist[] = {
	"USER", "LOGNAME", "HOME",
	"LANG", "LC_ALL", "LC_COLLATE", "LC_CTYPE", "LC_MESSAGES", "LC_MONETARY", "LC_NUMERIC", "LC_TIME",
	"PATH",
	"PWD",
	"SHELL",
	"TERM",
	"TMPDIR",
	"LD_LIBRARY_PATH",
	"LD_PRELOAD",
	"POSIXLY_CORRECT",
	"HOSTALIASES",
	"TZ", "TZDIR",
	"TERMCAP",
	"COLUMNS", "LINES",
	NULL
};

static bool str_in_list(const char **list, const char *key)
{
	const char **ent;

	for (ent = list; *ent; ent++) {
		if (!strcmp(*ent, key))
			return true;
	}
	return false;
}

/*! generate a filtered version of the process environment containing only entries of whitelisted keys.
 *  \oaram[out] out caller-allocated array of pointers for the generated output
 *  \param[in] out_len size of out (number of pointers)
 *  \param[in] in input environment (NULL-terminated list of pointers like **environment)
 *  \param[in] whitelist whitelist of permitted keys in environment (like **environment)
 *  \returns number of entries filled in 'out'; negtive on error */
int osmo_environment_filter(char **out, size_t out_len, char **in, const char **whitelist)
{
	char tmp[256];
	char **ent;
	size_t out_used = 0;

	/* invalid calls */
	if (!out || out_len == 0 || !whitelist)
		return -EINVAL;

	/* legal, but unusual: no input to filter should generate empty, terminated out */
	if (!in) {
		out[0] = NULL;
		return 1;
	}

	/* iterate over input entries */
	for (ent = in; *ent; ent++) {
		char *eq = strchr(*ent, '=');
		unsigned long eq_pos;
		if (!eq) {
			/* no '=' in string, skip it */
			continue;
		}
		eq_pos = eq - *ent;
		if (eq_pos >= ARRAY_SIZE(tmp))
			continue;
		strncpy(tmp, *ent, eq_pos);
		tmp[eq_pos] = '\0';
		if (str_in_list(whitelist, tmp)) {
			if (out_used == out_len-1)
				break;
			/* append to output */
			out[out_used++] = *ent;
		}
	}
	OSMO_ASSERT(out_used < out_len);
	out[out_used++] = NULL;
	return out_used;
}

/*! append one environment to another; only copying pointers, not actual strings.
 *  \oaram[out] out caller-allocated array of pointers for the generated output
 *  \param[in] out_len size of out (number of pointers)
 *  \param[in] in input environment (NULL-terminated list of pointers like **environment)
 *  \returns number of entries filled in 'out'; negative on error */
int osmo_environment_append(char **out, size_t out_len, char **in)
{
	size_t out_used = 0;

	if (!out || out_len == 0)
		return -EINVAL;

	/* seek to end of existing output */
	for (out_used = 0; out[out_used]; out_used++) {}

	if (!in) {
		if (out_used == 0)
			out[out_used++] = NULL;
		return out_used;
	}

	for (; *in && out_used < out_len-1; in++)
		out[out_used++] = *in;

	OSMO_ASSERT(out_used < out_len);
	out[out_used++] = NULL;

	return out_used;
}

/* Iterate over files in /proc/self/fd and close all above lst_fd_to_keep */
int osmo_close_all_fds_above(int last_fd_to_keep)
{
	struct dirent *ent;
	DIR *dir;
	int rc;

	dir = opendir("/proc/self/fd");
	if (!dir) {
		LOGP(DLGLOBAL, LOGL_ERROR, "Cannot open /proc/self/fd: %s\n", strerror(errno));
		return -ENODEV;
	}

	while ((ent = readdir(dir))) {
		int fd = atoi(ent->d_name);
		if (fd <= last_fd_to_keep)
			continue;
		if (fd == dirfd(dir))
			continue;
		rc = close(fd);
		if (rc)
			LOGP(DLGLOBAL, LOGL_ERROR, "Error closing fd=%d: %s\n", fd, strerror(errno));
	}
	closedir(dir);
	return 0;
}

/* Seems like POSIX has no header file for this? */
extern char **environ;

/* mimic the behavior of system(3), but don't wait for completion */
int osmo_system_nowait(const char *command, char **addl_env)
{
	int rc;

	rc = fork();
	if (rc == 0) {
		/* we are in the child */
		char *new_env[1024];

		/* close all file descriptors above stdio */
		osmo_close_all_fds_above(2);

		/* build the new environment */
		osmo_environment_filter(new_env, ARRAY_SIZE(new_env), environ, environment_whitelist);
		osmo_environment_append(new_env, ARRAY_SIZE(new_env), addl_env);

		/* if we want to behave like system(3), we must go via the shell */
		execle("/bin/sh", "sh", "-c", command, (char *) NULL, new_env);
		/* only reached in case of error */
		LOGP(DLGLOBAL, LOGL_ERROR, "Error executing command '%s' after fork: %s\n",
			command, strerror(errno));
		return -EIO;
	} else {
		/* we are in the parent */
		return rc;
	}
}
