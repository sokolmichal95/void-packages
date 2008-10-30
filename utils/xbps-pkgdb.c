/*-
 * Copyright (c) 2008 Juan Romero Pardines.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#include <prop/proplib.h>

#define _XBPS_PKGDB_DEFPATH	"/var/xbps/.xbps-pkgdb.plist"

typedef struct pkg_data {
	const char *pkgname;
	const char *version;
	const char *short_desc;
} pkg_data_t;

static void usage(void);
static void add_array_to_dict(prop_dictionary_t, prop_array_t, const char *);
static void add_obj_to_array(prop_array_t, prop_object_t);
static prop_dictionary_t make_dict_from_pkg(pkg_data_t *);
static prop_dictionary_t find_pkg_in_dict(prop_dictionary_t, const char *);
static void register_pkg(prop_dictionary_t, pkg_data_t *, const char *);
static void unregister_pkg(prop_dictionary_t, const char *, const char *);
static void write_plist_file(prop_dictionary_t, const char *);
static void list_pkgs_in_dict(prop_dictionary_t);
static prop_dictionary_t get_dict_from_dbfile(const char *);

static void
usage(void)
{
	printf("usage: xbps-pkgdb <action> [args]\n");
	printf("\n");
	printf("  Available actions:\n");
	printf("    list, register, unregister, version\n");
	printf("  Action arguments:\n");
	printf("    list\t[none]\n");
	printf("    register\t[<pkgname> <version> <shortdesc>]\n");
	printf("    unregister\t[<pkgname> <version>]\n");
	printf("    version\t[<pkgname>]\n");
	printf("  Environment:\n");
	printf("    XBPS_PKGDB_FPATH\tPath to xbps pkgdb plist file\n");
	printf("\n");
	printf("  Examples:\n");
	printf("    $ xbps-pkgdb list\n");
	printf("    $ xbps-pkgdb register pkgname 2.0 \"A short description\"\n");
	printf("    $ xbps-pkgdb unregister pkgname 2.0\n");
	printf("    $ xbps-pkgdb version pkgname\n");
	exit(1);
}

static prop_dictionary_t
find_pkg_in_dict(prop_dictionary_t dict, const char *pkgname)
{
	prop_array_t array;
	prop_object_iterator_t iter;
	prop_object_t obj;
	const char *dpkgn;

	if (dict == NULL || pkgname == NULL)
		return NULL;

	array = prop_dictionary_get(dict, "packages_installed");
	if (array == NULL || prop_object_type(array) != PROP_TYPE_ARRAY)
		return NULL;

	iter = prop_array_iterator(array);
	if (iter == NULL)
		return NULL;

	while ((obj = prop_object_iterator_next(iter))) {
		prop_dictionary_get_cstring_nocopy(obj, "pkgname", &dpkgn);
		if (strcmp(dpkgn, pkgname) == 0)
			break;
	}
	prop_object_iterator_release(iter);

	return obj;
}

static void
add_obj_to_array(prop_array_t array, prop_object_t obj)
{
	if (array == NULL || obj == NULL) {
		printf("%s: NULL array/obj\n", __func__);
		exit(1);
	}

	if (!prop_array_add(array, obj)) {
		prop_object_release(array);
		printf("%s: couldn't add obj into array: %s\n", __func__,
		    strerror(errno));
		exit(1);
	}
	prop_object_release(obj);
}

static void
add_array_to_dict(prop_dictionary_t dict, prop_array_t array, const char *key)
{
	if (dict == NULL || array == NULL || key == NULL) {
		printf("%s: NULL dict/array/key\n", __func__);
		exit(1);
	}

	if (!prop_dictionary_set(dict, key, array)) {
		printf("%s: couldn't add array (%s): %s\n",
		    __func__, key, strerror(errno));
		exit(1);
	}
	prop_object_release(array);
}

static prop_dictionary_t
make_dict_from_pkg(pkg_data_t *pkg)
{
	prop_dictionary_t dict;

	if (pkg == NULL || pkg->pkgname == NULL || pkg->version == NULL ||
	    pkg->short_desc == NULL)
		return NULL;

	dict = prop_dictionary_create();
	if (dict == NULL)
		return NULL;

	prop_dictionary_set_cstring_nocopy(dict, "pkgname", pkg->pkgname);
	prop_dictionary_set_cstring_nocopy(dict, "version", pkg->version);
	prop_dictionary_set_cstring_nocopy(dict, "short_desc", pkg->short_desc);

	return dict;
}

static void
register_pkg(prop_dictionary_t dict, pkg_data_t *pkg, const char *dbfile)
{
	prop_dictionary_t pkgdict;
	prop_array_t array;

	if (dict == NULL || pkg == NULL || dbfile == NULL) {
		printf("%s: NULL dict/pkg/dbfile\n", __func__);
		exit(1);
	}

	pkgdict = make_dict_from_pkg(pkg);
	if (pkgdict == NULL) {
		printf("%s: NULL pkgdict\n", __func__);
		exit(1);
	}

	array = prop_dictionary_get(dict, "packages_installed");
	if (array == NULL || prop_object_type(array) != PROP_TYPE_ARRAY) {
		printf("%s: NULL or incorrect array type\n", __func__);
		exit(1);
	}

	add_obj_to_array(array, pkgdict);
	write_plist_file(dict, dbfile);
}

static void
unregister_pkg(prop_dictionary_t dict, const char *pkgname, const char *dbfile)
{
	prop_array_t array;
	prop_object_t obj;
	prop_object_iterator_t iter;
	const char *curpkgn;
	int i = 0;
	bool found = false;

	if (dict == NULL || pkgname == NULL) {
		printf("%s: NULL dict/pkgname\n", __func__);
		exit(1);
	}

	array = prop_dictionary_get(dict, "packages_installed");
	if (array == NULL || prop_object_type(array) != PROP_TYPE_ARRAY) {
		printf("%s: NULL or incorrect array type\n", __func__);
		exit(1);
	}

	iter = prop_array_iterator(array);
	if (iter == NULL) {
		printf("%s: NULL iter\n", __func__);
		exit(1);
	}

	/* Iterate over the array of dictionaries to find its index. */
	while ((obj = prop_object_iterator_next(iter))) {
		prop_dictionary_get_cstring_nocopy(obj, "pkgname", &curpkgn);
		if (strcmp(curpkgn, pkgname) == 0) {
			found = true;
			break;
		}
		i++;
	}

	if (found == false) {
		printf("=> ERROR: %s not registered in database.\n", pkgname);
		exit(1);
	}

	prop_array_remove(array, i);
	add_array_to_dict(dict, array, "packages_installed");
	write_plist_file(dict, dbfile);
}

static void
write_plist_file(prop_dictionary_t dict, const char *file)
{
	if (dict == NULL || file == NULL) {
		printf("=> ERROR: couldn't write to database file.\n");
		exit(1);
	}

	if (!prop_dictionary_externalize_to_file(dict, file)) {
		prop_object_release(dict);
		perror("=> ERROR: couldn't write to database file");
		exit(1);
	}
}

static void
list_pkgs_in_dict(prop_dictionary_t dict)
{
	prop_array_t array;
	prop_object_t obj;
	prop_object_iterator_t iter;
	const char *pkgname, *version, *short_desc;

	if (dict == NULL)
		exit(1);

	array = prop_dictionary_get(dict, "packages_installed");
	if (array == NULL || prop_object_type(array) != PROP_TYPE_ARRAY) {
		printf("%s: NULL or incorrect array type\n", __func__);
		exit(1);
	}

	iter = prop_array_iterator(array);
	if (iter == NULL) {
		printf("%s: NULL iter\n", __func__);
		exit(1);
	}

	while ((obj = prop_object_iterator_next(iter))) {
		prop_dictionary_get_cstring_nocopy(obj, "pkgname", &pkgname);
		prop_dictionary_get_cstring_nocopy(obj, "version", &version);
		prop_dictionary_get_cstring_nocopy(obj, "short_desc", &short_desc);
		if (pkgname && version && short_desc)
			printf("%s-%s\t%s\n", pkgname, version, short_desc);
	}
	prop_object_iterator_release(iter);
}

static prop_dictionary_t
get_dict_from_dbfile(const char *file)
{
	prop_dictionary_t dict;

	dict =  prop_dictionary_internalize_from_file(file);
	if (dict == NULL) {
		perror("=> ERROR: couldn't find database file");
		exit(1);
	}
	return dict;
}

int
main(int argc, char **argv)
{
	prop_dictionary_t dbdict = NULL, pkgdict;
	prop_array_t dbarray = NULL;
	pkg_data_t pkg;
	const char *version;
	char dbfile[PATH_MAX], *dbfileenv, *tmppath, *in_chroot_env;
	bool in_chroot = false;

	if (argc < 2)
		usage();

	if ((dbfileenv = getenv("XBPS_PKGDB_FPATH")) != NULL) {
		/* Use path as defined by XBPS_PKGDB_FPATH env var */
		tmppath = strncpy(dbfile, dbfileenv, sizeof(dbfile));
		if (sizeof(*tmppath) >= sizeof(dbfile))
			exit(1);
	} else {
		/* Use default path */
		tmppath =
		    strncpy(dbfile, _XBPS_PKGDB_DEFPATH, sizeof(dbfile));
		if (sizeof(*tmppath) >= sizeof(dbfile))
			exit(1);
	}
	/* nul terminate string */
	dbfile[sizeof(dbfile) - 1] = '\0';

	in_chroot_env = getenv("in_chroot");
	if (in_chroot_env != NULL)
		in_chroot = true;

	if (strcmp(argv[1], "register") == 0) {
		/* Registers a package into the database */
		if (argc != 5)
			usage();

		dbdict = prop_dictionary_internalize_from_file(dbfile);
		if (dbdict == NULL) {
			/* Create package dictionary and add its objects. */
			pkg.pkgname = argv[2];
			pkg.version = argv[3];
			pkg.short_desc = argv[4];
			pkgdict = make_dict_from_pkg(&pkg);
			if (pkgdict == NULL) {
				printf("=> ERROR: couldn't register pkg\n");
				exit(1);
			}

			/* Add pkg dictionary into array. */
			dbarray = prop_array_create();
			add_obj_to_array(dbarray, pkgdict);

			/* Add array into main dictionary. */
			dbdict = prop_dictionary_create();
			add_array_to_dict(dbdict, dbarray, "packages_installed");

			/* Write main dictionary to file. */
			write_plist_file(dbdict, dbfile);

			printf("%s==> Package database file not found, "
			    "creating it.\n", in_chroot ? "[chroot] " : "");

			prop_object_release(dbdict);
		} else {
			/* Check if pkg is already registered. */
			pkgdict = find_pkg_in_dict(dbdict, argv[2]);
			if (pkgdict != NULL) {
				printf("=> Package %s-%s already registered.\n",
				    argv[2], argv[3]);
				exit(0);
			}
			pkg.pkgname = argv[2];
			pkg.version = argv[3];
			pkg.short_desc = argv[4];

			register_pkg(dbdict, &pkg, dbfile);
		}

		printf("%s=> %s-%s registered successfully.\n",
		    in_chroot ? "[chroot] " : "", argv[2], argv[3]);

	} else if (strcmp(argv[1], "unregister") == 0) {
		/* Unregisters a package from the database */
		if (argc != 4)
			usage();

		unregister_pkg(get_dict_from_dbfile(dbfile), argv[2], dbfile);

		printf("%s=> %s-%s unregistered successfully.\n",
		    in_chroot ? "[chroot] " : "", argv[2], argv[3]);

	} else if (strcmp(argv[1], "list") == 0) {
		/* Lists packages currently registered in database */
		if (argc != 2)
			usage();

		list_pkgs_in_dict(get_dict_from_dbfile(dbfile));

	} else if (strcmp(argv[1], "version") == 0) {
		/* Prints version of an installed package */
		if (argc != 3)
			usage();

		pkgdict = find_pkg_in_dict(get_dict_from_dbfile(dbfile), argv[2]);
		if (pkgdict == NULL)
			exit(1);
		if (!prop_dictionary_get_cstring_nocopy(pkgdict, "version", &version))
			exit(1);
		printf("%s\n", version);

	} else {
		usage();
	}

	exit(0);
}
