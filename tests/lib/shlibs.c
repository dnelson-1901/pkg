/*-
 * Copyright(c) 2024 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <atf-c.h>
#include <private/pkg.h>
#include <tllist.h>

ATF_TC_WITHOUT_HEAD(cleanup_shlibs_required);

ATF_TC_BODY(cleanup_shlibs_required, tc)
{
	struct pkg *p;
	charv_t internal_provided;
	vec_init(&internal_provided);

	ATF_REQUIRE_EQ(pkg_new(&p, PKG_FILE), EPKG_OK);
	ATF_REQUIRE(p != NULL);
	vec_push(&p->shlibs_required, xstrdup("lib1.so:32"));
	vec_push(&p->shlibs_required, xstrdup("lib1.so"));
	vec_push(&p->shlibs_required, xstrdup("libA.so"));
	vec_push(&p->shlibs_required, xstrdup("libA.so:32"));
	pkg_cleanup_shlibs_required(p, &internal_provided);
	ATF_REQUIRE_EQ(vec_len(&p->shlibs_required), 4);
	vec_push(&p->shlibs_provided, "lib1.so");
	pkg_cleanup_shlibs_required(p, &internal_provided);
	ATF_REQUIRE_EQ(vec_len(&p->shlibs_required), 3);
	vec_push(&internal_provided, "lib1.so:32");
	pkg_cleanup_shlibs_required(p, &internal_provided);
	ATF_REQUIRE_EQ_MSG(vec_len(&p->shlibs_required), 2, "expecint 2 got %zu", vec_len(&p->shlibs_required));
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, cleanup_shlibs_required);

	return (atf_no_error());
}
