/*-
 * Copyright (c) 2011-2022 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2011-2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
 * Copyright (c) 2013 Matthew Seaman <matthew@FreeBSD.org>
 * Copyright (c) 2013-2016 Vsevolod Stakhov <vsevolod@FreeBSD.org>
 * Copyright (c) 2023 Serenity Cyber Security, LLC
 *                    Author: Gleb Popov <arrowd@FreeBSD.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#include "pkg_config.h"
#endif

#define dbg(x, ...) pkg_dbg(PKG_DBG_JOBS, x, __VA_ARGS__)

#include <bsd_compat.h>

#include <sys/param.h>
#include <sys/mount.h>
#include <sys/types.h>

#include <archive.h>
#include <archive_entry.h>
#include <assert.h>
#include <errno.h>
#ifdef HAVE_LIBUTIL_H
#include <libutil.h>
#endif
#include <search.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <ctype.h>

#ifdef HAVE_SYS_STATFS_H
#include <sys/statfs.h>
#endif
#if defined(HAVE_SYS_STATVFS_H)
#include <sys/statvfs.h>
#endif

#include "pkg.h"
#include "private/event.h"
#include "private/pkg.h"
#include "private/pkgdb.h"
#include "private/pkg_jobs.h"

#define charv_sort(cv, func) do { qsort((cv)->d, (cv)->len, sizeof(char *), func); (cv)->unsorted = false; } while (0)

extern struct pkg_ctx ctx;

static int pkg_jobs_installed_local_pkg(struct pkg_jobs *j, struct pkg *pkg);
static int pkg_jobs_find_upgrade(struct pkg_jobs *j, const char *pattern, match_t m);
static int pkg_jobs_fetch(struct pkg_jobs *j);
static bool new_pkg_version(struct pkg_jobs *j);
static int pkg_jobs_check_conflicts(struct pkg_jobs *j);
struct pkg_jobs_locked {
	int (*locked_pkg_cb)(struct pkg *, void *);
	void *context;
};
static __thread struct pkg_jobs_locked *pkgs_job_lockedpkg;
typedef vec_t(int64_t) candidates_t;

#define IS_DELETE(j) ((j)->type == PKG_JOBS_DEINSTALL || (j)->type == PKG_JOBS_AUTOREMOVE)

int
pkg_jobs_new(struct pkg_jobs **j, pkg_jobs_t t, struct pkgdb *db)
{
	assert(db != NULL);

	*j = xcalloc(1, sizeof(struct pkg_jobs));

	(*j)->universe = pkg_jobs_universe_new(*j);

	if ((*j)->universe == NULL) {
		free(*j);
		return (EPKG_FATAL);
	}

	(*j)->db = db;
	(*j)->type = t;
	(*j)->solved = false;
	(*j)->pinning = true;
	(*j)->flags = PKG_FLAG_NONE;
	(*j)->conservative = pkg_object_bool(pkg_config_get("CONSERVATIVE_UPGRADE"));
	(*j)->triggers.dfd = -1;

	return (EPKG_OK);
}

void
pkg_jobs_set_flags(struct pkg_jobs *j, pkg_flags flags)
{
	j->flags = flags;
}

int
pkg_jobs_set_repository(struct pkg_jobs *j, const char *ident)
{
	c_charv_t idents = vec_init();
	if (ident != NULL)
		vec_push(&idents, ident);
	return (pkg_jobs_set_repositories(j, &idents));
}

int
pkg_jobs_set_repositories(struct pkg_jobs *j, c_charv_t *idents)
{
	int ret = EPKG_OK;
	if (idents == NULL)
		return (EPKG_OK);
	for (size_t i = 0; i < idents->len; i++) {
		if ((pkg_repo_find(idents->d[i])) == NULL) {
			pkg_emit_error("Unknown repository: %s", idents->d[i]);
			ret = EPKG_FATAL;
		}
	}
	if (ret == EPKG_FATAL)
		return (ret);

	j->reponames = idents;

	return (ret);
}

int
pkg_jobs_set_destdir(struct pkg_jobs *j, const char *dir)
{
	if (dir == NULL)
		return (EPKG_FATAL);

	j->destdir = dir;

	return (EPKG_OK);
}

const char*
pkg_jobs_destdir(struct pkg_jobs *j)
{
	return (j->destdir);
}

static void
pkg_jobs_pattern_free(struct job_pattern *jp)
{
	free(jp->pattern);
	free(jp->path);
	free(jp);
}

void
pkg_jobs_request_free(struct pkg_job_request *req)
{
	struct pkg_job_request_item *it, *tmp;

	if (req != NULL) {
		DL_FOREACH_SAFE(req->item, it, tmp) {
			free(it);
		}

		free(req);
	}
}

void
pkg_jobs_free(struct pkg_jobs *j)
{
	pkghash_it it;

	if (j == NULL)
		return;

	it = pkghash_iterator(j->request_add);
	while (pkghash_next(&it))
		pkg_jobs_request_free(it.value);
	pkghash_destroy(j->request_add);
	j->request_add = NULL;

	it = pkghash_iterator(j->request_delete);
	while (pkghash_next(&it))
		pkg_jobs_request_free(it.value);
	pkghash_destroy(j->request_delete);
	j->request_delete = NULL;

	pkg_jobs_universe_free(j->universe);
	vec_free_and_free(&j->jobs, free);
	LL_FREE(j->patterns, pkg_jobs_pattern_free);
	if (j->triggers.cleanup != NULL) {
		vec_free_and_free(j->triggers.cleanup, trigger_free);
		free(j->triggers.cleanup);
	}
	if (j->triggers.dfd != -1)
		close(j->triggers.dfd);
	if (j->triggers.schema != NULL)
		ucl_object_unref(j->triggers.schema);
	pkghash_destroy(j->orphaned);
	pkghash_destroy(j->notorphaned);
	vec_free_and_free(&j->system_shlibs, free);
	free(j);
}

static bool
pkg_jobs_maybe_match_file(struct job_pattern *jp, const char *pattern)
{
	const char *dot_pos;
	char *pkg_path;

	assert(jp != NULL);
	assert(pattern != NULL);

	dot_pos = strrchr(pattern, '.');
	if (dot_pos != NULL) {
		/*
		 * Compare suffix with .txz or .tbz
		 */
		dot_pos ++;
		if (STREQ(dot_pos, "pkg") ||
		    STREQ(dot_pos, "tzst") ||
		    STREQ(dot_pos, "txz") ||
		    STREQ(dot_pos, "tbz") ||
		    STREQ(dot_pos, "tgz") ||
		    STREQ(dot_pos, "tar")) {
			if ((pkg_path = realpath(pattern, NULL)) != NULL) {
				/* Dot pos is one character after the dot */
				int len = dot_pos - pattern;

				dbg(2, "Adding file: %s", pattern);
				jp->flags |= PKG_PATTERN_FLAG_FILE;
				jp->path = pkg_path;
				jp->pattern = xmalloc(len);
				strlcpy(jp->pattern, pattern, len);

				return (true);
			}
		}
	}
	else if (STREQ(pattern, "-")) {
		/*
		 * Read package from stdin
		 */
		jp->flags = PKG_PATTERN_FLAG_FILE;
		jp->path = xstrdup(pattern);
		jp->pattern = xstrdup(pattern);
	}

	return (false);
}

int
pkg_jobs_add(struct pkg_jobs *j, match_t match, char **argv, int argc)
{
	struct job_pattern *jp;
	int i = 0;

	if (j->solved) {
		pkg_emit_error("The job has already been solved. "
		    "Impossible to append new elements");
		return (EPKG_FATAL);
	}

	for (i = 0; i < argc; i++) {
		jp = xcalloc(1, sizeof(struct job_pattern));
		if (j->type == PKG_JOBS_DEINSTALL ||
		    !pkg_jobs_maybe_match_file(jp, argv[i])) {
			jp->pattern = xstrdup(argv[i]);
			jp->match = match;
		}
		LL_APPEND(j->patterns, jp);
	}

	if (argc == 0 && match == MATCH_ALL) {
		jp = xcalloc(1, sizeof(struct job_pattern));
		jp->pattern = NULL;
		jp->match = match;
		LL_APPEND(j->patterns, jp);
	}

	return (EPKG_OK);
}

bool
pkg_jobs_iter(struct pkg_jobs *j, void **iter,
				struct pkg **new, struct pkg **old,
				int *type)
{
	struct pkg_solved *s;
	struct {
		pkg_solved_list *list;
		size_t pos;
	} *t;
	t = *iter;
	if (*iter == NULL) {
		t = xcalloc(1, sizeof(*t));
		*iter = t;
	} else if (t->pos >= t->list->len) {
			free(t);
			return (false);
	}

	if (j->jobs.len == 0)
		return (false);
	if (t->list == NULL) {
		t->list = &j->jobs;
		t->pos = 0;
	}
	s = t->list->d[t->pos++];
	*new = s->items[0]->pkg;
	*old = s->items[1] ? s->items[1]->pkg : NULL;
	*type = s->type;
	return (true);
}

static struct pkg_job_request_item*
pkg_jobs_add_req_from_universe(pkghash **head, struct pkg_job_universe_item *un,
    bool local, bool automatic)
{
	struct pkg_job_request *req;
	struct pkg_job_request_item *nit;
	struct pkg_job_universe_item *uit;
	bool new_req = false;

	assert(un != NULL);
	req = pkghash_get_value(*head, un->pkg->uid);

	if (req == NULL) {
		req = xcalloc(1, sizeof(*req));
		new_req = true;
		req->automatic = automatic;
		dbg(4, "add new uid %s to the request", un->pkg->uid);
	}
	else {
		if (req->item->unit == un) {
			/* We have exactly the same request, skip it */
			return (req->item);
		}
	}

	DL_FOREACH(un, uit) {
		if ((uit->pkg->type == PKG_INSTALLED && local) ||
				(uit->pkg->type != PKG_INSTALLED && !local)) {
			nit = xcalloc(1, sizeof(*nit));
			nit->pkg = uit->pkg;
			nit->unit = uit;
			DL_APPEND(req->item, nit);
		}
	}

	if (new_req) {
		if (req->item != NULL) {
			pkghash_safe_add(*head, un->pkg->uid, req, NULL);
		}
		else {
			free(req);
			return (NULL);
		}
	}

	return (req->item);
}

static struct pkg_job_request_item*
pkg_jobs_add_req(struct pkg_jobs *j, struct pkg *pkg)
{
	pkghash **head;
	struct pkg_job_request *req;
	struct pkg_job_request_item *nit;
	struct pkg_job_universe_item *un;
	int rc;

	assert(pkg != NULL);

	if (!IS_DELETE(j)) {
		head = &j->request_add;
		assert(pkg->type != PKG_INSTALLED);
	}
	else {
		head = &j->request_delete;
		assert(pkg->type == PKG_INSTALLED);
	}

	dbg(4, "add package %s-%s to the request", pkg->name,
			pkg->version);
	rc = pkg_jobs_universe_add_pkg(j->universe, pkg, false, &un);

	if (rc == EPKG_END) {
		/*
		 * This means that we have a package in the universe with the same
		 * digest. In turn, that means that two upgrade candidates are equal,
		 * we thus won't do anything with this item, as it is definitely useless
		 */
		req = pkghash_get_value(*head, pkg->uid);
		if (req != NULL) {
			DL_FOREACH(req->item, nit) {
				if (nit->unit == un)
					return (nit);
			}
		}
		else {
			/*
			 * We need to add request chain from the universe chain
			 */
			return (pkg_jobs_add_req_from_universe(head, un, IS_DELETE(j), false));
		}

		return (NULL);
	}
	else if (rc == EPKG_FATAL) {
		/*
		 * Something bad has happened
		 */
		return (NULL);
	}

	if (pkg->locked) {
		pkg_emit_locked(pkg);
		return (NULL);
	}

	req = pkghash_get_value(*head, pkg->uid);

	nit = xcalloc(1, sizeof(*nit));
	nit->pkg = pkg;
	nit->unit = un;

	if (req == NULL) {
		/* Allocate new unique request item */
		req = xcalloc(1, sizeof(*req));
		pkghash_safe_add(*head, pkg->uid, req, NULL);
	}

	/* Append candidate to the list of candidates */
	DL_APPEND(req->item, nit);

	return (nit);
}

static bool
append_to_del_request(struct pkg_jobs *j, pkgs_t *to_process, const char *uid, const char *reqname)
{
	struct pkg *p;

	p = pkg_jobs_universe_get_local(j->universe, uid, 0);
	if (p == NULL)
		return (true);
	if (p->locked) {
		pkg_emit_error("%s is locked cannot delete %s", p->name,
		   reqname);
		return (false);
	}
	vec_push(to_process, p);
	return (true);
}

bool
delete_process_provides(struct pkg_jobs *j, struct pkg *lp, const char *provide,
    struct pkgdb_it *(*provideq)(struct pkgdb *db, const char *req),
    struct pkgdb_it *(*requireq)(struct pkgdb *db, const char *req),
    pkgs_t *to_process)
{
	struct pkgdb_it *lit, *rit;
	struct pkg *pkg;
	struct pkg_job_request *req;
	bool ret = true;

	/* check for pkgbase shlibs and provides */
	if (charv_search(&j->system_shlibs, provide) != NULL)
		return (ret);
	/* if something else to provide the same thing we can safely delete */
	lit = provideq(j->db, provide);
	if (lit == NULL)
		return (ret);
	pkg = NULL;
	while (pkgdb_it_next(lit, &pkg, PKG_LOAD_BASIC) == EPKG_OK) {
		/* skip myself */
		if (STREQ(pkg->uid, lp->uid))
			continue;
		req = pkghash_get_value(j->request_delete, pkg->uid);
		/*
		 * skip already processed provides
		 * if N packages providing the same "provide"
		 * are in the request delete they needs to be
		 * counted as to be removed and then if no
		 * packages are left providing the provide are
		 * left after the removal of those packages
		 * cascade.
		 */
		if (req != NULL && req->processed)
			continue;

		pkgdb_it_free (lit);
		return (ret);
	}
	pkgdb_it_free(lit);
	rit = requireq(j->db, provide);
	if (rit == NULL)
		return (ret);

	pkg = NULL;
	while (pkgdb_it_next(rit, &pkg, PKG_LOAD_BASIC) == EPKG_OK) {
		if (!append_to_del_request(j, to_process,
		    pkg->uid, lp->name))
			ret = false;
	}
	pkgdb_it_free(rit);
	return (ret);
}

/*
 * For delete request we merely check rdeps and force flag
 */
static int
pkg_jobs_process_delete_request(struct pkg_jobs *j)
{
	bool force = j->flags & PKG_FLAG_FORCE;
	struct pkg_job_request *req;
	struct pkg_dep *d = NULL;
	struct pkg *lp;
	int rc = EPKG_OK;
	pkgs_t to_process = vec_init();
	pkghash_it it;

	if (force)
		return (EPKG_OK);

	/*
	 * Need to add also all reverse deps here
	 */
	it = pkghash_iterator(j->request_delete);
	while (pkghash_next(&it)) {
		req = it.value;
		if (req->processed)
			continue;
		req->processed = true;
		lp = req->item->pkg;
		d = NULL;
		while (pkg_rdeps(lp, &d) == EPKG_OK) {
			if (!append_to_del_request(j, &to_process, d->uid,
			    lp->name))
				rc = EPKG_FATAL;
		}

		vec_foreach(lp->provides, i) {
			if (!delete_process_provides(j, lp, lp->provides.d[i],
			    pkgdb_query_provide, pkgdb_query_require,
			    &to_process))
				rc = EPKG_FATAL;
		}

		vec_foreach(lp->shlibs_provided, i) {
			if (!delete_process_provides(j, lp, lp->shlibs_provided.d[i],
			    pkgdb_query_shlib_provide,
			    pkgdb_query_shlib_require, &to_process))
				rc = EPKG_FATAL;
		}
	}

	if (rc == EPKG_FATAL)
		return (rc);

	vec_foreach(to_process, pit) {
		lp = to_process.d[pit];
		if (pkg_jobs_add_req(j, lp) == NULL) {
			vec_free(&to_process);
			return (EPKG_FATAL);
		}
	}

	if (vec_len(&to_process) > 0)
		rc = pkg_jobs_process_delete_request(j);
	vec_free(&to_process);

	return (rc);
}

static bool pkg_jobs_test_automatic(struct pkg_jobs *j, struct pkg *p);

static bool
_is_orphaned(struct pkg_jobs *j, const char *uid)
{
	struct pkg_job_universe_item *unit;
	struct pkg *npkg;

	if (pkghash_get(j->notorphaned, uid) != NULL)
		return (false);
	unit = pkg_jobs_universe_find(j->universe, uid);
	if (unit != NULL) {
		if (!unit->pkg->automatic || unit->pkg->vital)
			return (false);
		npkg = unit->pkg;
	} else {
		npkg = pkg_jobs_universe_get_local(j->universe, uid,
		    PKG_LOAD_BASIC|PKG_LOAD_RDEPS|PKG_LOAD_ANNOTATIONS|
		    PKG_LOAD_SHLIBS_REQUIRED|PKG_LOAD_REQUIRES);
		if (npkg == NULL)
			return (false);
		if (!npkg->automatic || npkg->vital) {
			pkg_free(npkg);
			return (false);
		}
		if (pkg_jobs_universe_process(j->universe, npkg) != EPKG_OK)
			return (false);
	}

	if (!pkg_jobs_test_automatic(j, npkg))
		return (false);

	return (true);
}

static bool
is_orphaned(struct pkg_jobs *j, const char *uid)
{
	if (pkghash_get(j->orphaned, uid) != NULL)
		return (true);
	if (pkghash_get(j->notorphaned, uid) != NULL)
		return (false);
	if (_is_orphaned(j, uid)) {
		pkghash_safe_add(j->orphaned, uid, NULL, NULL);
		return (true);
	}
	pkghash_safe_add(j->notorphaned, uid, NULL, NULL);
	return (false);
}

/**
 * Test whether package specified is automatic with all its rdeps
 * @param j
 * @param p
 * @return
 */
static bool
pkg_jobs_test_automatic(struct pkg_jobs *j, struct pkg *p)
{
	struct pkg_dep *d = NULL;
	struct pkg *npkg = NULL;
	struct pkgdb_it *it;

	while (pkg_rdeps(p, &d) == EPKG_OK) {
		if (!is_orphaned(j, d->uid))
			return (false);
	}

	vec_foreach(p->provides, i) {
		it = pkgdb_query_require(j->db, p->provides.d[i]);
		if (it == NULL)
			continue;
		npkg = NULL;
		while (pkgdb_it_next(it, &npkg, PKG_LOAD_BASIC) == EPKG_OK) {
			if (!is_orphaned(j, npkg->uid)) {
				pkgdb_it_free(it);
				pkg_free(npkg);
				return (false);
			}
		}
		pkgdb_it_free(it);
	}

	vec_foreach(p->shlibs_provided, i) {
		it = pkgdb_query_shlib_require(j->db, p->shlibs_provided.d[i]);
		if (it == NULL)
			continue;
		npkg = NULL;
		while (pkgdb_it_next(it, &npkg, PKG_LOAD_BASIC) == EPKG_OK) {
			if (!is_orphaned(j, npkg->uid)) {
				pkgdb_it_free(it);
				pkg_free(npkg);
				return (false);
			}
		}
		pkgdb_it_free(it);
	}
	pkg_free(npkg);

	return (true);
}



static bool
new_pkg_version(struct pkg_jobs *j)
{
	struct pkg *p;
	const char *uid = "pkg";
	pkg_flags old_flags;
	bool ret = false;
	struct pkg_job_universe_item *nit, *cit;

	/* Disable -f for pkg self-check, and restore at end. */
	old_flags = j->flags;
	j->flags &= ~(PKG_FLAG_FORCE|PKG_FLAG_RECURSIVE);

	/* determine local pkgng */
	p = pkg_jobs_universe_get_local(j->universe, uid, 0);

	if (p == NULL) {
		uid = "pkg-devel";
		p = pkg_jobs_universe_get_local(j->universe, uid, 0);
	}

	/* you are using git version skip */
	if (p == NULL) {
		ret = false;
		goto end;
	}

	/* Use maximum priority for pkg */
	if (pkg_jobs_find_upgrade(j, uid, MATCH_INTERNAL) == EPKG_OK) {
		/*
		 * Now we can have *potential* upgrades, but we can have a situation,
		 * when our upgrade candidate comes from another repo
		 */
		nit = pkg_jobs_universe_find(j->universe, uid);

		if (nit) {
			DL_FOREACH(nit, cit) {
				if (pkg_version_change_between (cit->pkg, p) == PKG_UPGRADE) {
					/* We really have newer version which is not installed */
					ret = true;
					break;
				}
			}
		}
	}

end:
	j->flags = old_flags;

	return (ret);
}

static int
pkg_jobs_process_remote_pkg(struct pkg_jobs *j, struct pkg *rp,
	struct pkg_job_request_item **req, int with_version)
{
	struct pkg_job_universe_item *nit, *cur;
	struct pkg_job_request_item *nrit = NULL;
	struct pkg *lp = NULL;
	struct pkg_dep *rdep = NULL;

	if (rp->digest == NULL) {
		if (pkg_checksum_calculate(rp, j->db, false, true, false) != EPKG_OK) {
			return (EPKG_FATAL);
		}
	}
	if (j->type != PKG_JOBS_FETCH) {
		lp = pkg_jobs_universe_get_local(j->universe, rp->uid, 0);
		if (lp && lp->locked)
			return (EPKG_LOCKED);
	}

	nit = pkg_jobs_universe_get_upgrade_candidates(j->universe, rp->uid, lp,
		j->flags & PKG_FLAG_FORCE,
		with_version != 0 ? rp->version : NULL);

	if (nit != NULL) {
		nrit = pkg_jobs_add_req_from_universe(&j->request_add, nit, false, false);

		if (req != NULL)
			*req = nrit;

		if (j->flags & PKG_FLAG_UPGRADE_VULNERABLE) {
			/* Set the proper reason */
			DL_FOREACH(nit, cur) {
				if (cur->pkg->type != PKG_INSTALLED) {
					free(cur->pkg->reason);
					xasprintf(&cur->pkg->reason, "vulnerability found");
				}
			}
			/* Also process all rdeps recursively */
			if (nrit != NULL) {
				while (pkg_rdeps(nrit->pkg, &rdep) == EPKG_OK) {
					lp = pkg_jobs_universe_get_local(j->universe, rdep->uid, 0);

					if (lp) {
						(void)pkg_jobs_process_remote_pkg(j, lp, NULL, 0);
					}
				}
			}
		}
	}

	if (nrit == NULL && lp)
		return (EPKG_INSTALLED);

	return (nrit != NULL ? EPKG_OK : EPKG_FATAL);
}

static int
pkg_jobs_try_remote_candidate(struct pkg_jobs *j, const char *cond, const char *pattern, match_t m)
{
	struct pkg *p = NULL;
	struct pkgdb_it *it;
	unsigned flags = PKG_LOAD_BASIC|PKG_LOAD_OPTIONS|PKG_LOAD_DEPS|
				PKG_LOAD_REQUIRES|PKG_LOAD_PROVIDES|
				PKG_LOAD_SHLIBS_REQUIRED|PKG_LOAD_SHLIBS_PROVIDED|
				PKG_LOAD_ANNOTATIONS|PKG_LOAD_CONFLICTS;
	int rc = EPKG_FATAL;
	xstring *qmsg = NULL;

	if ((it = pkgdb_repo_query_cond2(j->db, cond, pattern, m, j->reponames)) == NULL)
		return (EPKG_FATAL);

	while (pkgdb_it_next(it, &p, flags) == EPKG_OK) {
		xstring_renew(qmsg);
	}


	pkg_free(p);

	xstring_free(qmsg);
	pkgdb_it_free(it);

	return (rc);
}

static int
pkg_jobs_guess_upgrade_candidate(struct pkg_jobs *j, const char *pattern)
{

	int rc = EPKG_FATAL;
	const char *pos, *opattern = pattern;
	char *cpy;
	size_t len, olen;

	/* First of all, try to search a package with the same name */
	pos = strchr(pattern, '/');
	if (pos != NULL && pos[1] != '\0') {
		if (pkg_jobs_try_remote_candidate(j, pos + 1, NULL, MATCH_INTERNAL)
						== EPKG_OK)
			return (EPKG_OK);

		pos ++;
	} else {
		pos = pattern;
	}

	/* Figure, if we have any numbers at the end of the package */
	olen = strlen(pos);
	len = olen;
	while (len > 0) {
		if (isdigit(pos[len - 1]) || pos[len - 1] == '.')
			len --;
		else
			break;
	}

	if (olen != len) {
		/* Try exact pattern without numbers */
		cpy = xmalloc(len + 1);
		strlcpy(cpy, pos, len + 1);
		if (pkg_jobs_try_remote_candidate(j, cpy, NULL, MATCH_INTERNAL) != EPKG_OK) {
			free(cpy);
			cpy = sqlite3_mprintf(" WHERE p.name REGEXP ('^' || %.*Q || '[0-9.]*$')",
					len, pos);

			if (pkg_jobs_try_remote_candidate(j, cpy, opattern, MATCH_ALL)
					== EPKG_OK)
				rc = EPKG_OK;
			sqlite3_free(cpy);
		}
		else {
			free(cpy);
			rc = EPKG_OK;
		}
	}

	return (rc);
}

static int
pkg_jobs_find_upgrade(struct pkg_jobs *j, const char *pattern, match_t m)
{
	struct pkg *p = NULL;
	struct pkgdb_it *it;
	bool checklocal, found = false;
	int rc = EPKG_FATAL;
	int with_version;
	struct pkg_dep *rdep = NULL;
	unsigned flags = PKG_LOAD_BASIC|PKG_LOAD_OPTIONS|PKG_LOAD_DEPS|
			PKG_LOAD_REQUIRES|PKG_LOAD_PROVIDES|
			PKG_LOAD_SHLIBS_REQUIRED|PKG_LOAD_SHLIBS_PROVIDED|
			PKG_LOAD_ANNOTATIONS|PKG_LOAD_CONFLICTS;
	struct pkg_job_universe_item *unit = NULL;

	if ((it = pkgdb_repo_query2(j->db, pattern, m, j->reponames)) == NULL)
		return (rc);

	/*
	 * MATCH_EXACT is handled at a higher level, so that we can complain if a
	 * specific upgrade was requested without the package being locally installed.
	 *
	 * MATCH_ALL is a non-issue, because we will not get that from pkg-upgrade
	 * anyways.

	 * Pattern matches are the main target, as the above query may grab packages
	 * that are not installed that we can ignore.
	 */
	checklocal = j->type == PKG_JOBS_UPGRADE && m != MATCH_EXACT && m != MATCH_ALL;
	while (it != NULL && pkgdb_it_next(it, &p, flags) == EPKG_OK) {
		if (checklocal && pkg_jobs_installed_local_pkg(j, p) != EPKG_OK)
			continue;
		if (pattern != NULL && *pattern != '@') {
			with_version = strcmp(p->name, pattern);
		} else {
			with_version = 0;
		}
		rc = pkg_jobs_process_remote_pkg(j, p, NULL, with_version);
		if (rc == EPKG_FATAL) {
			break;
		} else if (rc == EPKG_OK)
			found = true;

		pkg_free(p);
		p = NULL;
	}

	pkgdb_it_free(it);

	if (!found && rc != EPKG_INSTALLED) {
		/*
		 * Here we need to ensure that this package has no
		 * reverse deps installed
		 */
		p = pkg_jobs_universe_get_local(j->universe, pattern,
			PKG_LOAD_BASIC|PKG_LOAD_RDEPS);
		if (p == NULL)
			return (EPKG_FATAL);

		while(pkg_rdeps(p, &rdep) == EPKG_OK) {
			struct pkg *rdep_package;

			rdep_package = pkg_jobs_universe_get_local(j->universe, rdep->uid,
					PKG_LOAD_BASIC);
			if (rdep_package != NULL)
				return (EPKG_END);
		}

		dbg(2, "non-automatic package with pattern %s has not been found in "
				"remote repo", pattern);
		rc = pkg_jobs_universe_add_pkg(j->universe, p, false, &unit);
		if (rc == EPKG_OK) {
			rc = pkg_jobs_guess_upgrade_candidate(j, pattern);
		}
	}

	return (rc);
}

static int
pkg_jobs_check_local_pkg(struct pkg_jobs *j, struct job_pattern *jp)
{
	struct pkgdb_it *it;
	struct pkg *pkg = NULL;
	int rc = EPKG_OK;

	it = pkgdb_query(j->db, jp->pattern, jp->match);
	if (it != NULL) {
		if (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC|PKG_LOAD_ANNOTATIONS) != EPKG_OK)
			rc = EPKG_FATAL;
		else
			pkg_free(pkg);

		pkgdb_it_free(it);
	}
	else {
		rc = EPKG_FATAL;
	}

	return (rc);
}

static int
pkg_jobs_installed_local_pkg(struct pkg_jobs *j, struct pkg *pkg)
{
	struct job_pattern jfp;

	jfp.match = MATCH_INTERNAL;
	jfp.pattern = pkg->name;
	return (pkg_jobs_check_local_pkg(j, &jfp));
}

static int
pkg_jobs_find_remote_pattern(struct pkg_jobs *j, struct job_pattern *jp)
{
	int rc = EPKG_OK;
	struct pkg *pkg = NULL;
	struct pkg_job_request *req;

	if (!(jp->flags & PKG_PATTERN_FLAG_FILE)) {
		if (j->type == PKG_JOBS_UPGRADE && jp->match == MATCH_INTERNAL) {
			/*
			 * For upgrade patterns we must ensure that a local package is
			 * installed as well.  This only works if we're operating on an
			 * exact match, as we otherwise don't know exactly what packages
			 * are in store for us.
			 */
			if (pkg_jobs_check_local_pkg(j, jp) != EPKG_OK) {
				pkg_emit_error("%s is not installed, therefore upgrade is impossible",
						jp->pattern);
				return (EPKG_NOTINSTALLED);
			}
		}
		rc = pkg_jobs_find_upgrade(j, jp->pattern, jp->match);
	}
	else {
		if (pkg_open(&pkg, jp->path, PKG_OPEN_MANIFEST_ONLY) != EPKG_OK) {
			rc = EPKG_FATAL;
		} else if (pkg_validate(pkg, j->db) == EPKG_OK) {
			if (j->type == PKG_JOBS_UPGRADE && pkg_jobs_installed_local_pkg(j, pkg) != EPKG_OK) {
				pkg_emit_error("%s is not installed, therefore upgrade is impossible",
							pkg->name);
				return (EPKG_NOTINSTALLED);
			}
			pkg->type = PKG_FILE;
			pkg_jobs_add_req(j, pkg);

			req = pkghash_get_value(j->request_add, pkg->uid);
			if (req != NULL)
				req->item->jp = jp;
		}
		else {
			pkg_emit_error("cannot load %s: invalid format",
					jp->pattern);
			rc = EPKG_FATAL;
		}
	}

	return (rc);
}

bool
pkg_jobs_need_upgrade(charv_t *system_shlibs, struct pkg *rp, struct pkg *lp)
{
	int ret, ret1, ret2;
	struct pkg_option *lo = NULL, *ro = NULL;
	struct pkg_dep *ld = NULL, *rd = NULL;
	struct pkg_conflict *lc = NULL, *rc = NULL;
	charv_t rsv = vec_init();
	charv_t lsv = vec_init();
	char *rv;

	if (!rp->reason) xasprintf(&rp->reason, "p_j_n_u %s %s no reason?", rp->name, rp->origin);

	/* If no local package, then rp is obviously need to be added */
	if (lp == NULL)
	{
		xasprintf(&rp->reason, "new");
		return true;
	}

	/* Do not upgrade locked packages */
	if (lp->locked) {
		pkg_emit_locked(lp);
		return (false);
	}

	if (lp->digest != NULL && rp->digest != NULL &&
	    STREQ(lp->digest, rp->digest)) {
		/* Remote and local packages has the same digest, hence they are the same */
		return (false);
	}
	/*
	 * XXX: for a remote package we also need to check whether options
	 * are compatible.
	 */
	ret = pkg_version_cmp(lp->version, rp->version);
	if (ret > 0)
		return (false);
	else if (ret < 0)
	{
		xasprintf(&rp->reason, "new version");
		return (true);
	}

	/* Compare archs */
	if (!STREQ(lp->abi, rp->abi)) {
		free(rp->reason);
		xasprintf(&rp->reason, "ABI changed: '%s' -> '%s'",
		    lp->abi, rp->abi);
		assert(rp->reason != NULL);
		return (true);
	}

	/* compare options */
#if 0
	for (;;) {
		if (!pkg_object_bool(pkg_config_get("PKG_REINSTALL_ON_OPTIONS_CHANGE")))
			break;
		ret1 = pkg_options(rp, &ro);
		ret2 = pkg_options(lp, &lo);
		if (ret1 != ret2) {
			free(rp->reason);
			if (ro == NULL)
				xasprintf(&rp->reason, "option removed: %s",
				    lo->key);
			else if (lo == NULL)
				xasprintf(&rp->reason, "option added: %s",
				    ro->key);
			else
				xasprintf(&rp->reason, "option changed: %s",
				    ro->key);
			assert(rp->reason != NULL);
			return (true);
		}
		if (ret1 == EPKG_OK) {
			if (!STREQ(lo->key, ro->key) ||
			    !STREQ(lo->value, ro->value)) {
				free(rp->reason);
				xasprintf(&rp->reason, "options changed");
				return (true);
			}
		}
		else
			break;
	}
#else
	if (pkg_object_bool(pkg_config_get("PKG_REINSTALL_ON_OPTIONS_CHANGE"))) {
		while (pkg_options(rp, &ro) == EPKG_OK) {
			char *str;
			asprintf(&str, "%s:%s", ro->key, ro->value);
			vec_push(&rsv, str);
		}
		while (pkg_options(lp, &lo) == EPKG_OK) {
			char *str;
			asprintf(&str, "%s:%s", lo->key, lo->value);
			vec_push(&lsv, str);
		}

		charv_sort(&lsv, char_cmp);
		charv_sort(&rsv, char_cmp);
		rv = charv_diff(&lsv, &rsv, NULL);
		vec_free_and_free(&lsv, free);
		vec_free_and_free(&rsv, free);

		if (rv) {
			free(rp->reason);
			xasprintf(&rp->reason, "options %s", rv);
			free(rv);
			return (true);
		}
	}
#endif

	/* What about the direct deps */
#if 0
	for (;;) {
		ret1 = pkg_deps(rp, &rd);
		ret2 = pkg_deps(lp, &ld);
		if (ret1 != ret2) {
			free(rp->reason);
			if (rd == NULL)
				xasprintf(&rp->reason, "direct dependency removed: %s",
				    ld->name);
			else if (ld == NULL)
				xasprintf(&rp->reason, "direct dependency added: %s",
				    rd->name);
			else
				xasprintf(&rp->reason, "direct dependency changed: %s",
				    rd->name);
			assert (rp->reason != NULL);
			return (true);
		}
		if (ret1 == EPKG_OK) {
			if (!STREQ(rd->name, ld->name) ||
			    !STREQ(rd->origin, ld->origin)) {
				free(rp->reason);
				xasprintf(&rp->reason, "direct dependency changed: %s",
				    rd->name);
				assert (rp->reason != NULL);
				return (true);
			}
		}
		else
			break;
	}
#else
	while (pkg_deps(rp, &rd) == EPKG_OK) {
		char *str;
		asprintf(&str, "%s:%s", rd->name, rd->origin);
		vec_push(&rsv, str);
	}
	while (pkg_deps(lp, &ld) == EPKG_OK) {
		char *str;
		asprintf(&str, "%s:%s", ld->name, ld->origin);
		vec_push(&lsv, str);
	}

	charv_sort(&lsv, char_cmp);
	charv_sort(&rsv, char_cmp);
	rv = charv_diff(&lsv, &rsv, NULL);
	vec_free_and_free(&lsv, free);
	vec_free_and_free(&rsv, free);

	if (rv) {
		free(rp->reason);
		xasprintf(&rp->reason, "direct dependencies %s", rv);
		free(rv);
		return (true);
	}
#endif

	/* Conflicts */
#if 0
	for (;;) {
		ret1 = pkg_conflicts(rp, &rc);
		ret2 = pkg_conflicts(lp, &lc);
		if (ret1 != ret2) {
			free(rp->reason);
			rp->reason = xstrdup("direct conflict changed");
			return (true);
		}
		if (ret1 == EPKG_OK) {
			if (!STREQ(rc->uid, lc->uid)) {
				free(rp->reason);
				rp->reason = xstrdup("direct conflict changed");
				return (true);
			}
		}
		else
			break;
	}
#else
	while (pkg_conflicts(rp, &rc) == EPKG_OK) 
		vec_push(&rsv, rc->uid);
	while (pkg_conflicts(lp, &lc) == EPKG_OK) 
		vec_push(&lsv, lc->uid);

	charv_sort(&lsv, char_cmp);
	charv_sort(&rsv, char_cmp);
	rv = charv_diff(&lsv, &rsv, NULL);
	vec_free_and_free(&lsv, free);
	vec_free_and_free(&rsv, free);

	if (rv) {
		free(rp->reason);
		xasprintf(&rp->reason, "direct conflicts %s", rv);
		free(rv);
		return (true);
	}
#endif

	/* Provides */
	pkg_lists_sort(lp);
	pkg_lists_sort(rp);
	rv = charv_diff(&lp->provides, &rp->provides, NULL);
	if (rv) {
		free(rp->reason);
		xasprintf(&rp->reason, "provides %s", rv);
		free(rv);
		return (true);
	}

	/* Requires */
	rv = charv_diff(&lp->requires, &rp->requires, NULL);
	if (rv) {
		free(rp->reason);
		xasprintf(&rp->reason, "requires %s", rv);
		free(rv);
		return (true);
	}

	/* Finish by the shlibs */
	rv = charv_diff(&lp->shlibs_provided, &rp->shlibs_provided, NULL);
	if (rv) {
		free(rp->reason);
		xasprintf(&rp->reason, "provided shared libraries %s", rv);
		free(rv);
		return (true);
	}

	rv = charv_diff(&lp->shlibs_required, &rp->shlibs_required, system_shlibs);
	if (rv) {
		free(rp->reason);
		xasprintf(&rp->reason, "required shared libraries %s", rv);
		free(rv);
		return (true);
	}

	return (false);
}

static void
pkg_jobs_propagate_automatic(struct pkg_jobs *j)
{
	struct pkg_job_universe_item *unit, *cur, *local;
	struct pkg_job_request *req;
	bool automatic;
	pkghash_it it;

	it = pkghash_iterator(j->universe->items);
	while (pkghash_next(&it)) {
		unit = (struct pkg_job_universe_item *)it.value;
		if (unit->next == NULL) {
			/*
			 * For packages that are alone in the installation list
			 * we search them in the corresponding request
			 */
			req = pkghash_get_value(j->request_add, unit->pkg->uid);
			if ((req == NULL || req->automatic) &&
			    unit->pkg->type != PKG_INSTALLED) {
				automatic = true;
				dbg(2, "set automatic flag for %s", unit->pkg->uid);
				unit->pkg->automatic = automatic;
			}
			else {
				if (j->type == PKG_JOBS_INSTALL) {
					unit->pkg->automatic = false;
				}
			}
		}
		else {
			/*
			 * For packages that are in the conflict chain we need to inherit
			 * automatic flag from the local package
			 */
			local = NULL;
			automatic = false;
			LL_FOREACH(unit, cur) {
				if (cur->pkg->type == PKG_INSTALLED) {
					local = cur;
					automatic = local->pkg->automatic;
					break;
				}
			}
			if (local != NULL) {
				LL_FOREACH(unit, cur) {
					/*
					 * Propagate automatic from local package
					 */
					if (cur->pkg->type != PKG_INSTALLED) {
						cur->pkg->automatic = automatic;
					}
				}
			}
			else {
				/*
				 * For packages that are not unique, we might still have
				 * a situation when we need to set automatic for all
				 * non-local packages
				 *
				 * See #1374
				 */
				req = pkghash_get_value(j->request_add, unit->pkg->uid);
				if ((req == NULL || req->automatic)) {
					automatic = true;
					dbg(2, "set automatic flag for %s", unit->pkg->uid);
					LL_FOREACH(unit, cur) {
						cur->pkg->automatic = automatic;
					}
				}
			}
		}
	}
}

static struct pkg_job_request *
pkg_jobs_find_deinstall_request(struct pkg_job_universe_item *item,
		struct pkg_jobs *j, int rec_level)
{
	struct pkg_job_request *found;
	struct pkg_job_universe_item *dep_item;
	struct pkg_dep *d = NULL;
	struct pkg *pkg = item->pkg;

	if (rec_level > 128) {
		dbg(2, "cannot find deinstall request after 128 iterations for %s,"
		    "circular dependency maybe", pkg->uid);
		return (NULL);
	}

	found = pkghash_get_value(j->request_delete, pkg->uid);
	if (found == NULL) {
		while (pkg_deps(pkg, &d) == EPKG_OK) {
			dep_item = pkg_jobs_universe_find(j->universe, d->uid);
			if (dep_item) {
				found = pkg_jobs_find_deinstall_request(dep_item, j, rec_level + 1);
				if (found)
					return (found);
			}
		}
	}
	else {
		return (found);
	}

	return (NULL);
}

static void
pkg_jobs_set_deinstall_reasons(struct pkg_jobs *j)
{
	struct pkg_solved *sit;
	struct pkg_job_request *jreq;
	struct pkg *req_pkg, *pkg;

	vec_foreach(j->jobs, i) {
		sit = j->jobs.d[i];
		jreq = pkg_jobs_find_deinstall_request(sit->items[0], j, 0);
		if (jreq != NULL && jreq->item->unit != sit->items[0]) {
			req_pkg = jreq->item->pkg;
			pkg = sit->items[0]->pkg;
			/* Set the reason */
			free(pkg->reason);
			pkg_asprintf(&pkg->reason, "depends on %n-%v", req_pkg, req_pkg);
		}
	}
}

static int
comp(const void *a, const void *b)
{
	const struct pkg *pa = a;
	const struct pkg *pb = b;

	return strcmp(pa->name, pb->name);
}

static int
jobs_solve_deinstall(struct pkg_jobs *j)
{
	struct job_pattern *jp;
	struct pkg *pkg = NULL;
	struct pkgdb_it *it;
	bool force = (j->flags & PKG_FLAG_FORCE);
	LL_FOREACH(j->patterns, jp) {
		if ((it = pkgdb_query(j->db, jp->pattern, jp->match)) == NULL)
			return (EPKG_FATAL);

		if (pkgdb_it_count(it) == 0) {
			pkg_emit_notice("No packages matched for pattern '%s'\n", jp->pattern);
		}

		while (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC|PKG_LOAD_RDEPS|
		    PKG_LOAD_DEPS|PKG_LOAD_ANNOTATIONS|PKG_LOAD_PROVIDES|
		    PKG_LOAD_SHLIBS_PROVIDED) == EPKG_OK) {
			if(pkg->locked || (pkg->vital && !force)) {
				if (tsearch(pkg, &j->lockedpkgs, comp) == NULL) {
					pkgdb_it_free(it);
					return (EPKG_FATAL);
				}
			}
			else {
				pkg_jobs_add_req(j, pkg);
			}
			pkg = NULL;
		}
		pkgdb_it_free(it);
	}

	j->solved = true;

	return (pkg_jobs_process_delete_request(j));
}

static int
jobs_solve_autoremove(struct pkg_jobs *j)
{
	struct pkg *pkg = NULL;
	struct pkgdb_it *it;

	if ((it = pkgdb_query_cond(j->db, " WHERE automatic=1 AND vital=0 AND locked=0", NULL, MATCH_ALL)) == NULL)
		return (EPKG_FATAL);

	while (pkgdb_it_next(it, &pkg,
			PKG_LOAD_BASIC|PKG_LOAD_RDEPS|PKG_LOAD_DEPS|
			PKG_LOAD_ANNOTATIONS|PKG_LOAD_PROVIDES|
			PKG_LOAD_SHLIBS_PROVIDED)
			== EPKG_OK) {
		if (pkg_jobs_test_automatic(j, pkg)) {
			assert(pkg_jobs_add_req(j, pkg));
		}
		pkg = NULL;
	}
	pkgdb_it_free(it);

	j->solved = true;
	pkg_jobs_process_delete_request(j);

	return (EPKG_OK);
}

static bool
pkg_jobs_check_remote_candidate(struct pkg_jobs *j, struct pkg *pkg)
{
	struct pkgdb_it *it;
	struct pkg *p = NULL;

	/* If we have no digest, we need to check this package */
	if (pkg->digest == NULL)
		return (true);

	it = pkgdb_repo_query2(j->db, pkg->uid, MATCH_INTERNAL, j->reponames);
	if (it != NULL) {
		/*
		 * If we have the same package in a remote repo, it is not an
		 * installation candidate
		 */
		int npkg = 0;

		while (pkgdb_it_next(it, &p, PKG_LOAD_BASIC) == EPKG_OK) {
			/*
			 * Check package with the same uid and explore whether digest
			 * has been changed
			 */
			if (!STREQ(p->digest, pkg->digest))
				npkg ++;

			pkg_free(p);
			p = NULL;
		}

		pkgdb_it_free(it);

		if (npkg == 0)
			return (false);
	}

	return (true);
}

static candidates_t *
pkg_jobs_find_install_candidates(struct pkg_jobs *j)
{
	struct pkg *pkg = NULL;
	struct pkgdb_it *it;
	candidates_t *candidates;

	if ((it = pkgdb_query(j->db, NULL, MATCH_ALL)) == NULL)
		return (NULL);

	candidates = xcalloc(1, sizeof(*candidates));

	while (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC) == EPKG_OK) {

		if ((j->flags & PKG_FLAG_FORCE) ||
						pkg_jobs_check_remote_candidate(j, pkg)) {
			vec_push(candidates, pkg->id);
		}
		pkg_free(pkg);
		pkg = NULL;
	}
	pkgdb_it_free(it);

	return (candidates);
}

static int
jobs_solve_full_upgrade(struct pkg_jobs *j)
{
	struct pkg *pkg = NULL;
	size_t jcount = 0;
	size_t elt_num = 0;
	char sqlbuf[256];
	candidates_t *candidates;
	struct pkg_job_request *req;
	struct pkgdb_it *it;
	pkghash_it hit;
	unsigned flags = PKG_LOAD_BASIC|PKG_LOAD_OPTIONS|PKG_LOAD_DEPS|PKG_LOAD_REQUIRES|
			PKG_LOAD_SHLIBS_REQUIRED|PKG_LOAD_ANNOTATIONS|PKG_LOAD_CONFLICTS;

	assert(!j->solved);

	candidates = pkg_jobs_find_install_candidates(j);
	jcount = candidates->len;

	pkg_emit_progress_start("Checking for upgrades (%zd candidates)",
			jcount);

	vec_foreach(*candidates, i) {
		pkg_emit_progress_tick(++elt_num, jcount);
		sqlite3_snprintf(sizeof(sqlbuf), sqlbuf, " WHERE p.id=%" PRId64,
		    candidates->d[i]);
		if ((it = pkgdb_query_cond(j->db, sqlbuf, NULL, MATCH_ALL)) == NULL)
			return (EPKG_FATAL);

		pkg = NULL;
		while (pkgdb_it_next(it, &pkg, flags) == EPKG_OK) {
			/* Do not test we ignore what doesn't exists remotely */
			pkg_jobs_find_upgrade(j, pkg->uid, MATCH_INTERNAL);
		}
		pkg_free(pkg);
		pkgdb_it_free(it);
	}
	vec_free(candidates);
	free(candidates);
	pkg_emit_progress_tick(jcount, jcount);

	pkg_emit_progress_start("Processing candidates (%zd candidates)",
			jcount);
	elt_num = 0;

	hit = pkghash_iterator(j->request_add);
	while (pkghash_next(&hit)) {
		req = hit.value;
		pkg_emit_progress_tick(++elt_num, jcount);
		pkg_jobs_universe_process(j->universe, req->item->pkg);
	}
	pkg_emit_progress_tick(jcount, jcount);

	pkg_jobs_universe_process_upgrade_chains(j);

	return (EPKG_OK);
}

static int
jobs_solve_partial_upgrade(struct pkg_jobs *j)
{
	struct job_pattern *jp;
	struct pkg_job_request *req;
	bool error_found = false;
	int retcode;
	pkghash_it it;

	assert(!j->solved);

	LL_FOREACH(j->patterns, jp) {
		retcode = pkg_jobs_find_remote_pattern(j, jp);
		if (retcode == EPKG_FATAL) {
			pkg_emit_error("No packages available to %s matching '%s' "
					"have been found in the "
					"repositories",
					(j->type == PKG_JOBS_UPGRADE) ? "upgrade" : "install",
					jp->pattern);
			/* delay the return to be sure we print a message for all issues */
			if ((j->flags & PKG_FLAG_UPGRADE_VULNERABLE) == 0)
				error_found = true;
		}
		if (retcode == EPKG_LOCKED) {
			return (retcode);
		}
	}
	if (error_found)
		return (EPKG_FATAL);
	/*
	 * Here we have not selected the proper candidate among all
	 * possible choices.
	 * Hence, we want to perform this procedure now to ensure that
	 * we are processing the correct packages.
	 */
	pkg_jobs_universe_process_upgrade_chains(j);
	/*
	 * Need to iterate request one more time to recurse depends
	 */

	it = pkghash_iterator(j->request_add);
	while (pkghash_next(&it)) {
		req = it.value;
		retcode = pkg_jobs_universe_process(j->universe, req->item->pkg);
		if (retcode != EPKG_OK)
			return (retcode);
	}
	return (EPKG_OK);
}

static int
jobs_solve_install_upgrade(struct pkg_jobs *j)
{
	struct pkg_job_request *req;
	int retcode = 0;
	pkghash_it it;

	/* Check for new pkg. Skip for 'upgrade -F'. */
	if (((j->flags & PKG_FLAG_SKIP_INSTALL) == 0 &&
	    (j->flags & PKG_FLAG_DRY_RUN) == 0) &&
	    (j->flags & PKG_FLAG_PKG_VERSION_TEST) == PKG_FLAG_PKG_VERSION_TEST)
		if (new_pkg_version(j)) {
			j->flags &= ~PKG_FLAG_PKG_VERSION_TEST;
			j->conservative = false;
			j->pinning = false;
			pkg_emit_newpkgversion();
			goto order;
		}

	if (j->patterns == NULL && j->type == PKG_JOBS_INSTALL) {
		pkg_emit_error("no patterns are specified for install job");
		return (EPKG_FATAL);
	}

	if (!j->solved) {
		if (j->patterns == NULL) {
			retcode = jobs_solve_full_upgrade(j);
			if (retcode != EPKG_OK)
				return (retcode);
		} else {
			retcode = jobs_solve_partial_upgrade(j);
			if (retcode != EPKG_OK)
				return (retcode);
		}
	}
	else {
		/*
		 * If we have tried to solve request, then we just want to re-add all
		 * request packages to the universe to find out any potential conflicts
		 */
		it = pkghash_iterator(j->request_add);
		while (pkghash_next(&it)) {
			req = it.value;
			pkg_jobs_universe_process(j->universe, req->item->pkg);
		}
	}

	if (pkg_conflicts_request_resolve(j) != EPKG_OK) {
		pkg_emit_error("Cannot resolve conflicts in a request");
		return (EPKG_FATAL);
	}

	pkg_jobs_propagate_automatic(j);

order:

	j->solved = true;

	return (EPKG_OK);
}

static int
jobs_solve_fetch(struct pkg_jobs *j)
{
	struct job_pattern *jp;
	struct pkg *pkg = NULL;
	struct pkgdb_it *it;
	struct pkg_job_request *req;
	pkghash_it hit;
	pkg_error_t rc;

	assert(!j->solved);

	if ((j->flags & PKG_FLAG_UPGRADES_FOR_INSTALLED) == PKG_FLAG_UPGRADES_FOR_INSTALLED) {
		if ((it = pkgdb_query(j->db, NULL, MATCH_ALL)) == NULL)
			return (EPKG_FATAL);

		while (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC) == EPKG_OK) {
			if(pkg->locked) {
				pkg_emit_locked(pkg);
			}
			else {
				/* Do not test we ignore what doesn't exists remotely */
				pkg_jobs_find_upgrade(j, pkg->uid, MATCH_INTERNAL);
			}
			pkg = NULL;
		}
		pkgdb_it_free(it);
	} else {
		LL_FOREACH(j->patterns, jp) {
			/* TODO: use repository priority here */
			if (pkg_jobs_find_upgrade(j, jp->pattern, jp->match) == EPKG_FATAL)
				pkg_emit_error("No packages matching '%s' have been found in the "
						"repositories", jp->pattern);
		}
		hit = pkghash_iterator(j->request_add);
		while (pkghash_next(&hit)) {
			req = hit.value;
			rc = pkg_jobs_universe_process(j->universe, req->item->pkg);
			if (rc != EPKG_OK && rc != EPKG_END)
				return (rc);
		}
	}

	j->solved = true;

	return (EPKG_OK);
}

static int
solve_with_external_cudf_solver(struct pkg_jobs *j, const char *solver)
{
	int ret, pstatus;
	FILE *spipe[2];
	pid_t pchild;

	pchild = process_spawn_pipe(spipe, solver);
	if (pchild == -1)
		return (EPKG_FATAL);

	ret = pkg_jobs_cudf_emit_file(j, j->type, spipe[1]);
	fclose(spipe[1]);

	if (ret == EPKG_OK)
		ret = pkg_jobs_cudf_parse_output(j, spipe[0]);

	fclose(spipe[0]);
	waitpid(pchild, &pstatus, WNOHANG);

	return (ret);
}

static int
solve_with_external_sat_solver(struct pkg_solve_problem *pb, const char *solver)
{
	int ret, pstatus;
	FILE *spipe[2];
	pid_t pchild;

	pchild = process_spawn_pipe(spipe, solver);
	if (pchild == -1)
		return (EPKG_FATAL);

	ret = pkg_solve_dimacs_export(pb, spipe[1]);
	fclose(spipe[1]);

	if (ret == EPKG_OK)
		ret = pkg_solve_parse_sat_output(spipe[0], pb);

	fclose(spipe[0]);
	waitpid(pchild, &pstatus, WNOHANG);

	return (ret);
}

static int
solve_with_sat_solver(struct pkg_jobs *j)
{
	const char *sat_solver = pkg_object_string(pkg_config_get("SAT_SOLVER"));
	struct pkg_solve_problem *problem;
	const char *dotfile;
	FILE *dot = NULL;
	int ret;

	pkg_jobs_universe_process_upgrade_chains(j);
	problem = pkg_solve_jobs_to_sat(j);
	if (problem == NULL) {
		pkg_emit_error("cannot convert job to SAT problem");
		j->solved = false;
		return (EPKG_FATAL);
	}

	if (sat_solver != NULL)
		return (solve_with_external_sat_solver(problem, sat_solver));

	ret = pkg_solve_sat_problem(problem);
	if (ret == EPKG_AGAIN) {
		pkg_solve_problem_free(problem);
		return (solve_with_sat_solver(j));
	}

	if (ret == EPKG_FATAL) {
		pkg_emit_error("cannot solve job using SAT solver");
		pkg_solve_problem_free(problem);
		j->solved = false;
	} else {
		ret = pkg_solve_sat_to_jobs(problem);
	}

	if ((dotfile = pkg_object_string(pkg_config_get("DOT_FILE")))
		!= NULL) {
		dot = fopen(dotfile, "we");

		if (dot == NULL) {
			pkg_emit_errno("fopen", dotfile);
		} else {
			pkg_solve_dot_export(problem, dot);
			fclose(dot);
		}
	}
	pkg_solve_problem_free(problem);

	return (ret);
}

static int
pkg_jobs_run_solver(struct pkg_jobs *j)
{
	int ret;
	const char *cudf_solver;

	pkgdb_begin_solver(j->db);

	switch (j->type) {
	case PKG_JOBS_AUTOREMOVE:
		ret = jobs_solve_autoremove(j);
		break;
	case PKG_JOBS_DEINSTALL:
		ret = jobs_solve_deinstall(j);
		break;
	case PKG_JOBS_UPGRADE:
	case PKG_JOBS_INSTALL:
		ret = jobs_solve_install_upgrade(j);
		break;
	case PKG_JOBS_FETCH:
		ret = jobs_solve_fetch(j);
		break;
	default:
		pkgdb_end_solver(j->db);
		return (EPKG_FATAL);
	}

	cudf_solver = pkg_object_string(pkg_config_get("CUDF_SOLVER"));

	if (ret == EPKG_OK) {
		if (cudf_solver != NULL) {
			ret = solve_with_external_cudf_solver(j, cudf_solver);
		} else {
			ret = solve_with_sat_solver(j);
		}
	}

	if (j->type == PKG_JOBS_DEINSTALL && j->solved)
		pkg_jobs_set_deinstall_reasons(j);

	pkgdb_end_solver(j->db);

	return (ret);
}

int
pkg_jobs_solve(struct pkg_jobs *j)
{
	int ret;

	if (j->system_shlibs.len == 0) {
		/* If /usr/bin/uname is in the pkg database, we are targeting
		 * a pkgbase system and should rely on the pkgbase packages to
		 * provide system shlibs. */
		if (!pkgdb_file_exists(j->db, "/usr/bin/uname")) {
			ret = scan_system_shlibs(&j->system_shlibs, ctx.pkg_rootdir);
			if (ret == EPKG_NOCOMPAT32) {
				j->ignore_compat32 = true;
			} else if (ret != EPKG_OK) {
				return (ret);
			}
		}
	}

	ret = pkg_jobs_run_solver(j);
	if (ret != EPKG_OK)
		return (ret);

	/*
	 * We can avoid asking the user for confirmation twice in the case of
	 * conflicts if we can check for and solve conflicts without first
	 * needing to fetch.
	 */
	vec_foreach(j->jobs, i) {
		struct pkg *p;

		p = ((struct pkg_solved *)j->jobs.d[i])->items[0]->pkg;
		if (p->type != PKG_REMOTE)
			continue;

		if (pkgdb_ensure_loaded(j->db, p, PKG_LOAD_FILES|PKG_LOAD_DIRS)
				== EPKG_FATAL) {
			j->need_fetch = true;
			break;
		}
	}

	if (j->solved && !j->need_fetch && j->type != PKG_JOBS_FETCH) {
		int rc;
		bool has_conflicts = false;
		do {
			j->conflicts_registered = 0;
			rc = pkg_jobs_check_conflicts(j);
			if (rc == EPKG_CONFLICT) {
				/* Cleanup results */
				vec_free_and_free(&j->jobs, free);
				has_conflicts = true;
				pkg_jobs_solve(j);
			}
			else if (rc == EPKG_OK && !has_conflicts) {
				break;
			}
		} while (j->conflicts_registered > 0);
	}

	return (ret);
}

int
pkg_jobs_count(struct pkg_jobs *j)
{
	assert(j != NULL);

	return (j->jobs.len);
}

int
pkg_jobs_total(struct pkg_jobs *j)
{
	assert(j != NULL);

	return (j->total);
}

pkg_jobs_t
pkg_jobs_type(struct pkg_jobs *j)
{
	assert(j != NULL);

	return (j->type);
}

static int
pkg_jobs_handle_install(struct pkg_solved *ps, struct pkg_jobs *j)
{
	struct pkg *new, *old;
	struct pkg_job_request *req;
	char path[MAXPATHLEN], *target;
	int flags = 0;
	int retcode = EPKG_FATAL;

	dbg(2, "begin %s", __func__);
	/*
	 * For a split upgrade, pass along the old package even though it's
	 * already deleted, since we need it in order to merge configuration
	 * file changes.
	 */
	new = ps->items[0]->pkg;
	old = NULL;
	if (ps->items[1] != NULL)
		old = ps->items[1]->pkg;
	else if (ps->type == PKG_SOLVED_UPGRADE_INSTALL)
		old = ps->xlink->items[0]->pkg;

	req = pkghash_get_value(j->request_add, new->uid);
	if (req != NULL && req->item->jp != NULL &&
			(req->item->jp->flags & PKG_PATTERN_FLAG_FILE)) {
		/*
		 * We have package as a file, set special repository name
		 */
		target = req->item->jp->path;
		free(new->reponame);
		new->reponame = xstrdup("local file");
	}
	else {
		pkg_snprintf(path, sizeof(path), "%R", new);
		if (*path != '/')
			pkg_repo_cached_name(new, path, sizeof(path));
		target = path;
	}

	if (old != NULL)
		new->old_version = xstrdup(old->version);

	if ((j->flags & PKG_FLAG_FORCE) == PKG_FLAG_FORCE)
		flags |= PKG_ADD_FORCE;
	if ((j->flags & PKG_FLAG_NOSCRIPT) == PKG_FLAG_NOSCRIPT)
		flags |= PKG_ADD_NOSCRIPT;
	if ((j->flags & PKG_FLAG_FORCE_MISSING) == PKG_FLAG_FORCE_MISSING)
		flags |= PKG_ADD_FORCE_MISSING;
	if (ps->type != PKG_SOLVED_INSTALL) {
		flags |= PKG_ADD_UPGRADE;
		if (ps->type == PKG_SOLVED_UPGRADE_INSTALL)
			flags |= PKG_ADD_SPLITTED_UPGRADE;
	}
	if (new->automatic || (j->flags & PKG_FLAG_AUTOMATIC) == PKG_FLAG_AUTOMATIC)
		flags |= PKG_ADD_AUTOMATIC;

	if (new->type == PKG_GROUP_REMOTE)
		retcode = pkg_add_group(new);
	else if (old != NULL)
		retcode = pkg_add_upgrade(j->db, target, flags, NULL, new, old, &j->triggers);
	else
		retcode = pkg_add_from_remote(j->db, target, flags, NULL, new, &j->triggers);

	dbg(2, "end %s:", __func__);
	return (retcode);
}

static int
pkg_jobs_handle_delete(struct pkg_solved *ps, struct pkg_jobs *j)
{
	struct pkg *rpkg;
	int flags;

	rpkg = NULL;
	flags = 0;
	if ((j->flags & PKG_FLAG_NOSCRIPT) != 0)
		flags |= PKG_DELETE_NOSCRIPT;
	if (ps->type == PKG_SOLVED_UPGRADE_REMOVE) {
		flags |= PKG_DELETE_UPGRADE;
		rpkg = ps->xlink->items[0]->pkg;
	}
	return (pkg_delete(ps->items[0]->pkg, rpkg, j->db, flags,
	    &j->triggers));
}

static int
pkg_jobs_execute(struct pkg_jobs *j)
{
	dbg(1, "execute");
	struct pkg *p;
	int retcode = EPKG_FATAL;
	pkg_plugin_hook_t pre, post;
	size_t total_actions;
	size_t current_action = 0;

//j->triggers.cleanup = triggers_load(true);
	if (j->type == PKG_JOBS_INSTALL) {
		pre = PKG_PLUGIN_HOOK_PRE_INSTALL;
		post = PKG_PLUGIN_HOOK_POST_INSTALL;
	}
	else if (j->type == PKG_JOBS_UPGRADE) {
		pre = PKG_PLUGIN_HOOK_PRE_UPGRADE;
		post = PKG_PLUGIN_HOOK_POST_UPGRADE;
	}
	else if (j->type == PKG_JOBS_AUTOREMOVE){
		pre = PKG_PLUGIN_HOOK_PRE_AUTOREMOVE;
		post = PKG_PLUGIN_HOOK_POST_AUTOREMOVE;
	}
	else {
		pre = PKG_PLUGIN_HOOK_PRE_DEINSTALL;
		post = PKG_PLUGIN_HOOK_POST_DEINSTALL;
	}

	if (j->flags & PKG_FLAG_SKIP_INSTALL)
		return (EPKG_OK);

	retcode = pkgdb_upgrade_lock(j->db, PKGDB_LOCK_ADVISORY,
			PKGDB_LOCK_EXCLUSIVE);
	if (retcode != EPKG_OK)
		return (retcode);

	pkg_plugins_hook_run(pre, j, j->db);

	retcode = pkg_jobs_schedule(j);
	if (retcode != EPKG_OK)
		return (retcode);

	total_actions = j->jobs.len;
	vec_foreach(j->jobs, i) {
		struct pkg_solved *ps = j->jobs.d[i];

		pkg_emit_new_action(++current_action, total_actions);
		switch (ps->type) {
		case PKG_SOLVED_DELETE:
			if ((j->flags & PKG_FLAG_FORCE) == 0) {
				p = ps->items[0]->pkg;
				if (p->vital) {
					pkg_emit_error(
					    "Cannot delete vital package: %s!", p->name);
					pkg_emit_error(
					    "If you are sure you want to remove %s", p->name);
					pkg_emit_error(
					    "unset the 'vital' flag with: pkg set -v 0 %s", p->name);
					retcode = EPKG_FATAL;
					goto cleanup;
				}
				if (STREQ(p->name, "pkg") ||
				    STREQ(p->name, "pkg-devel")) {
					if (j->patterns->match == MATCH_ALL)
						continue;
					pkg_emit_error(
					    "Cannot delete pkg itself without force flag");
					retcode = EPKG_FATAL;
					goto cleanup;
				}
			}
			/* FALLTHROUGH */
		case PKG_SOLVED_UPGRADE_REMOVE:
			retcode = pkg_jobs_handle_delete(ps, j);
			if (retcode != EPKG_OK)
				goto cleanup;
			break;
		case PKG_SOLVED_INSTALL:
		case PKG_SOLVED_UPGRADE_INSTALL:
		case PKG_SOLVED_UPGRADE:
			retcode = pkg_jobs_handle_install(ps, j);
			if (retcode != EPKG_OK)
				goto cleanup;
			break;
		case PKG_SOLVED_FETCH:
			retcode = EPKG_FATAL;
			pkg_emit_error("internal error: bad job type");
			goto cleanup;
		}

	}

	pkg_plugins_hook_run(post, j, j->db);
	triggers_execute(j->triggers.cleanup);

cleanup:
	pkgdb_release_lock(j->db, PKGDB_LOCK_EXCLUSIVE);
	dbg(1, "execute done");

	return (retcode);
}

static void
pkg_jobs_cancel(struct pkg_jobs *j)
{
	pkgdb_release_lock(j->db, PKGDB_LOCK_ADVISORY);
}

int
pkg_jobs_apply(struct pkg_jobs *j)
{
	int rc;
	bool has_conflicts = false;

	if (!j->solved) {
		pkg_emit_error("The jobs hasn't been solved");
		return (EPKG_FATAL);
	}

	switch (j->type) {
	case PKG_JOBS_INSTALL:
	case PKG_JOBS_UPGRADE:
	case PKG_JOBS_DEINSTALL:
	case PKG_JOBS_AUTOREMOVE:
		if (j->need_fetch) {
			pkg_plugins_hook_run(PKG_PLUGIN_HOOK_PRE_FETCH, j, j->db);
			rc = pkg_jobs_fetch(j);
			pkg_plugins_hook_run(PKG_PLUGIN_HOOK_POST_FETCH, j, j->db);
			if (rc == EPKG_OK) {
				/* Check local conflicts in the first run */
				if (j->solved == 1) {
					do {
						j->conflicts_registered = 0;
						rc = pkg_jobs_check_conflicts(j);
						if (rc == EPKG_CONFLICT) {
							/* Cleanup results */
							vec_free_and_free(&j->jobs, free);
							has_conflicts = true;
							rc = pkg_jobs_solve(j);
						}
						else if (rc == EPKG_OK && !has_conflicts) {
							rc = pkg_jobs_execute(j);
							break;
						}
					} while (j->conflicts_registered > 0);

					if (has_conflicts) {
						return (EPKG_CONFLICT);
					}
				}
				else {
					/* Not the first run, conflicts are resolved already */
					rc = pkg_jobs_execute(j);
				}
			}
			else if (rc == EPKG_CANCEL) {
				pkg_jobs_cancel(j);
			}
		}
		else {
			rc = pkg_jobs_execute(j);
		}

		break;
	case PKG_JOBS_FETCH:
		pkg_plugins_hook_run(PKG_PLUGIN_HOOK_PRE_FETCH, j, j->db);
		rc = pkg_jobs_fetch(j);
		pkg_plugins_hook_run(PKG_PLUGIN_HOOK_POST_FETCH, j, j->db);
		break;
	default:
		rc = EPKG_FATAL;
		pkg_emit_error("bad jobs argument");
		break;
	}

	return (rc);
}


static int
pkg_jobs_fetch(struct pkg_jobs *j)
{
	struct pkg *p = NULL;
	struct stat st;
	int64_t dlsize = 0, fs_avail = -1;
	const char *cachedir = NULL;
	char cachedpath[MAXPATHLEN];
	bool mirror = (j->flags & PKG_FLAG_FETCH_MIRROR) ? true : false;
	int retcode;


	if (j->destdir == NULL || !mirror)
		cachedir = ctx.cachedir;
	else
		cachedir = j->destdir;

	/* check for available size to fetch */
	vec_foreach(j->jobs, i) {
		struct pkg_solved *ps = j->jobs.d[i];
		if (ps->type != PKG_SOLVED_DELETE && ps->type != PKG_SOLVED_UPGRADE_REMOVE) {
			p = ps->items[0]->pkg;
			if (p->type != PKG_REMOTE)
				continue;

			if (mirror) {
				snprintf(cachedpath, sizeof(cachedpath),
				   "%s/%s", cachedir, p->repopath);
			}
			else
				pkg_repo_cached_name(p, cachedpath, sizeof(cachedpath));

			if (stat(cachedpath, &st) == -1)
				dlsize += p->pkgsize;
			else
				dlsize += p->pkgsize - st.st_size;
		}
	}

	if (dlsize == 0)
		return (EPKG_OK);

#ifdef HAVE_FSTATFS
	struct statfs fs;
	while (statfs(cachedir, &fs) == -1) {
		if (errno == ENOENT) {
			if (pkg_mkdirs(cachedir) != EPKG_OK)
				return (EPKG_FATAL);
		} else {
			pkg_emit_errno("statfs", cachedir);
			return (EPKG_FATAL);
		}
	}
	fs_avail = fs.f_bsize * (int64_t)fs.f_bavail;
#elif defined(HAVE_SYS_STATVFS_H)
	struct statvfs fs;
	while (statvfs(cachedir, &fs) == -1) {
		if (errno == ENOENT) {
			if (pkg_mkdirs(cachedir) != EPKG_OK)
				return (EPKG_FATAL);
		} else {
			pkg_emit_errno("statvfs", cachedir);
			return (EPKG_FATAL);
		}
	}
	fs_avail = fs.f_bsize * (int64_t)fs.f_bavail;
#endif

	if (fs_avail != -1 && dlsize > fs_avail) {
		char dlsz[9], fsz[9];

		humanize_number(dlsz, sizeof(dlsz), dlsize, "B",
		    HN_AUTOSCALE, HN_IEC_PREFIXES);
		humanize_number(fsz, sizeof(fsz), fs_avail, "B",
		    HN_AUTOSCALE, HN_IEC_PREFIXES);
		pkg_emit_error("Not enough space in %s, needed %s available %s",
		    cachedir, dlsz, fsz);
		return (EPKG_FATAL);
	}

	if ((j->flags & PKG_FLAG_DRY_RUN) == PKG_FLAG_DRY_RUN)
		return (EPKG_OK); /* don't download anything */

	/* Fetch */
	vec_foreach(j->jobs, i) {
		struct pkg_solved *ps = j->jobs.d[i];
		if (ps->type != PKG_SOLVED_DELETE && ps->type != PKG_SOLVED_UPGRADE_REMOVE) {
			p = ps->items[0]->pkg;
			if (p->type != PKG_REMOTE)
				continue;

			if (mirror) {
				retcode = pkg_repo_mirror_package(p, cachedir);
				if (retcode != EPKG_OK)
					return (retcode);
			}
			else {
				retcode = pkg_repo_fetch_package(p);
				if (pkg_repo_fetch_package(p) != EPKG_OK)
					return (retcode);
			}
		}
	}

	return (EPKG_OK);
}

static int
pkg_jobs_check_conflicts(struct pkg_jobs *j)
{
	struct pkg *p = NULL;
	int ret = EPKG_OK, res, added = 0;

	pkg_emit_integritycheck_begin();
	j->conflicts_registered = 0;

	vec_foreach(j->jobs, i) {
		struct pkg_solved *ps = j->jobs.d[i];
		if (ps->type == PKG_SOLVED_DELETE || ps->type == PKG_SOLVED_UPGRADE_REMOVE) {
			continue;
		}
		else {
			p = ps->items[0]->pkg;

			if (p->type == PKG_REMOTE)
				pkgdb_ensure_loaded(j->db, p, PKG_LOAD_FILES|PKG_LOAD_DIRS);
		}
		if ((res = pkg_conflicts_append_chain(ps->items[0], j)) != EPKG_OK)
			ret = res;
		else
			added ++;
	}

	dbg(1, "check integrity for %d items added", added);

	pkg_emit_integritycheck_finished(j->conflicts_registered);
	if (j->conflicts_registered > 0)
		ret = EPKG_CONFLICT;

	return (ret);
}

bool
pkg_jobs_has_lockedpkgs(struct pkg_jobs *j)
{

	return j->lockedpkgs ? true : false;
}

static void
pkg_jobs_visit_lockedpkgs(const void * node, VISIT v, int i __unused)
{

	if (v == postorder || v == leaf) {
		pkgs_job_lockedpkg->locked_pkg_cb(*(struct pkg **)node,
		    pkgs_job_lockedpkg->context);
	}
}

void
pkg_jobs_iter_lockedpkgs(struct pkg_jobs *j, locked_pkgs_cb cb, void * ctx)
{
	struct pkg_jobs_locked foo;

	foo.locked_pkg_cb = cb;
	foo.context = ctx;
	pkgs_job_lockedpkg = &foo;

	twalk(j->lockedpkgs, pkg_jobs_visit_lockedpkgs);
}
