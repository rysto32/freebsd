/*-
 * Copyright (c) 2014-2015 Sandvine Inc.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <atf-c++.hpp>
#include <nv.h>

#include <errno.h>
/*
 * Test that a newly created nvlist has no errors, and is empty.
 */
ATF_TEST_CASE_WITHOUT_HEAD(nvlist_create__is_empty);
ATF_TEST_CASE_BODY(nvlist_create__is_empty)
{
	nvlist_t *nvl;
	int type;
	void *it;

	nvl = nvlist_create(0);

	ATF_REQUIRE(nvl != NULL);

	ATF_REQUIRE_EQ(nvlist_error(nvl), 0);
	ATF_REQUIRE(nvlist_empty(nvl));

	it = NULL;
	ATF_REQUIRE_EQ(nvlist_next(nvl, &type, &it), NULL);

	nvlist_destroy(nvl);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_add_null__single_insert);
ATF_TEST_CASE_BODY(nvlist_add_null__single_insert)
{
	nvlist_t *nvl;
	void *it;
	const char *key;
	int type;

	key = "key";
	nvl = nvlist_create(0);

	ATF_REQUIRE(nvl != NULL);
	ATF_REQUIRE(!nvlist_exists(nvl, key));

	nvlist_add_null(nvl, key);

	ATF_REQUIRE(!nvlist_empty(nvl));
	ATF_REQUIRE(nvlist_exists(nvl, key));
	ATF_REQUIRE(nvlist_existsf(nvl, "%s", key));
	ATF_REQUIRE(nvlist_exists_null(nvl, key));
	ATF_REQUIRE(nvlist_existsf_null(nvl, "key"));

	/* Iterate over the nvlist; ensure that it has only our one key. */
	it = NULL;
	ATF_REQUIRE_EQ(strcmp(nvlist_next(nvl, &type, &it), key), 0);
	ATF_REQUIRE_EQ(type, NV_TYPE_NULL);
	ATF_REQUIRE_EQ(nvlist_next(nvl, &type,&it), NULL);

	nvlist_destroy(nvl);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_add_bool__single_insert);
ATF_TEST_CASE_BODY(nvlist_add_bool__single_insert)
{
	nvlist_t *nvl;
	void *it;
	const char *key;
	int type;

	key = "name";
	nvl = nvlist_create(0);

	ATF_REQUIRE(nvl != NULL);
	ATF_REQUIRE(!nvlist_exists(nvl, key));

	nvlist_add_bool(nvl, key, true);

	ATF_REQUIRE(!nvlist_empty(nvl));
	ATF_REQUIRE(nvlist_exists(nvl, key));
	ATF_REQUIRE(nvlist_existsf(nvl, "%s%s", "na", "me"));
	ATF_REQUIRE(nvlist_exists_bool(nvl, key));
	ATF_REQUIRE(nvlist_existsf_bool(nvl, "%s%c", "nam", 'e'));
	ATF_REQUIRE_EQ(nvlist_get_bool(nvl, key), true);
	ATF_REQUIRE_EQ(nvlist_getf_bool(nvl, "%c%s", 'n', "ame"), true);

	/* Iterate over the nvlist; ensure that it has only our one key. */
	it = NULL;
	ATF_REQUIRE_EQ(strcmp(nvlist_next(nvl, &type, &it), key), 0);
	ATF_REQUIRE_EQ(type, NV_TYPE_BOOL);
	ATF_REQUIRE_EQ(nvlist_next(nvl, &type,&it), NULL);

	nvlist_destroy(nvl);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_add_number__single_insert);
ATF_TEST_CASE_BODY(nvlist_add_number__single_insert)
{
	nvlist_t *nvl;
	void *it;
	const char *key;
	uint64_t value;
	int type;

	key = "foo123";
	value = 71965;
	nvl = nvlist_create(0);

	ATF_REQUIRE(nvl != NULL);
	ATF_REQUIRE(!nvlist_exists(nvl, key));

	nvlist_add_number(nvl, key, value);

	ATF_REQUIRE(!nvlist_empty(nvl));
	ATF_REQUIRE(nvlist_exists(nvl, key));
	ATF_REQUIRE(nvlist_existsf(nvl, "%s%d", "foo", 123));
	ATF_REQUIRE(nvlist_exists_number(nvl, key));
	ATF_REQUIRE(nvlist_existsf_number(nvl, "%s", key));
	ATF_REQUIRE_EQ(nvlist_get_number(nvl, key), value);
	ATF_REQUIRE_EQ(nvlist_getf_number(nvl, "%s", key), value);

	/* Iterate over the nvlist; ensure that it has only our one key. */
	it = NULL;
	ATF_REQUIRE_EQ(strcmp(nvlist_next(nvl, &type, &it), key), 0);
	ATF_REQUIRE_EQ(type, NV_TYPE_NUMBER);
	ATF_REQUIRE_EQ(nvlist_next(nvl, &type,&it), NULL);

	nvlist_destroy(nvl);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_add_string__single_insert);
ATF_TEST_CASE_BODY(nvlist_add_string__single_insert)
{
	nvlist_t *nvl;
	void *it;
	const char *key;
	const char *value;
	int type;

	key = "test";
	value = "fgjdkgjdk";
	nvl = nvlist_create(0);

	ATF_REQUIRE(nvl != NULL);
	ATF_REQUIRE(!nvlist_exists(nvl, key));

	nvlist_add_string(nvl, key, value);

	ATF_REQUIRE(!nvlist_empty(nvl));
	ATF_REQUIRE(nvlist_exists(nvl, key));
	ATF_REQUIRE(nvlist_existsf(nvl, "%s", key));
	ATF_REQUIRE(nvlist_exists_string(nvl, key));
	ATF_REQUIRE(nvlist_existsf_string(nvl, "%s", key));
	ATF_REQUIRE_EQ(strcmp(nvlist_get_string(nvl, key), value), 0);
	ATF_REQUIRE_EQ(strcmp(nvlist_getf_string(nvl, "%s", key), value), 0);

	/* nvlist_add_* is required to clone the value, so check for that. */
	ATF_REQUIRE(nvlist_get_string(nvl, key) != value);

	/* Iterate over the nvlist; ensure that it has only our one key. */
	it = NULL;
	ATF_REQUIRE_EQ(strcmp(nvlist_next(nvl, &type, &it), key), 0);
	ATF_REQUIRE_EQ(type, NV_TYPE_STRING);
	ATF_REQUIRE_EQ(nvlist_next(nvl, &type,&it), NULL);

	nvlist_destroy(nvl);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_add_nvlist__single_insert);
ATF_TEST_CASE_BODY(nvlist_add_nvlist__single_insert)
{
	nvlist_t *nvl;
	void *it;
	const char *key, *subkey;
	nvlist_t *sublist;
	const nvlist_t *value;
	int type;

	key = "test";
	subkey = "subkey";
	sublist = nvlist_create(0);
	nvl = nvlist_create(0);

	ATF_REQUIRE(nvl != NULL);
	ATF_REQUIRE(!nvlist_exists(nvl, key));

	nvlist_add_null(sublist, subkey);
	nvlist_add_nvlist(nvl, key, sublist);

	ATF_REQUIRE(!nvlist_empty(nvl));
	ATF_REQUIRE(nvlist_exists(nvl, key));
	ATF_REQUIRE(nvlist_existsf(nvl, "%s", key));
	ATF_REQUIRE(nvlist_exists_nvlist(nvl, key));
	ATF_REQUIRE(nvlist_existsf_nvlist(nvl, "%s", key));

	value = nvlist_get_nvlist(nvl, key);
	ATF_REQUIRE(nvlist_exists_null(value, subkey));

	/* nvlist_add_* is required to clone the value, so check for that. */
	ATF_REQUIRE(sublist != value);

	value = nvlist_getf_nvlist(nvl, "%s", key);
	ATF_REQUIRE(nvlist_exists_null(value, subkey));
	ATF_REQUIRE(sublist != value);

	/* Iterate over the nvlist; ensure that it has only our one key. */
	it = NULL;
	ATF_REQUIRE_EQ(strcmp(nvlist_next(nvl, &type, &it), key), 0);
	ATF_REQUIRE_EQ(type, NV_TYPE_NVLIST);
	ATF_REQUIRE_EQ(nvlist_next(nvl, &type,&it), NULL);

	nvlist_destroy(sublist);
	nvlist_destroy(nvl);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_add_binary__single_insert);
ATF_TEST_CASE_BODY(nvlist_add_binary__single_insert)
{
	nvlist_t *nvl;
	void *it;
	const char *key;
	void *value;
	const void *ret_value;
	size_t value_size, ret_size;
	int type;

	key = "binary";
	value_size = 13;
	value = malloc(value_size);
	memset(value, 0xa5, value_size);
	nvl = nvlist_create(0);

	ATF_REQUIRE(nvl != NULL);
	ATF_REQUIRE(!nvlist_exists(nvl, key));

	nvlist_add_binary(nvl, key, value, value_size);

	ATF_REQUIRE(!nvlist_empty(nvl));
	ATF_REQUIRE(nvlist_exists(nvl, key));
	ATF_REQUIRE(nvlist_existsf(nvl, "%s", key));
	ATF_REQUIRE(nvlist_exists_binary(nvl, key));
	ATF_REQUIRE(nvlist_existsf_binary(nvl, "%s", key));

	ret_value = nvlist_get_binary(nvl, key, &ret_size);
	ATF_REQUIRE_EQ(value_size, ret_size);
	ATF_REQUIRE_EQ(memcmp(value, ret_value, ret_size), 0);

	/* nvlist_add_* is required to clone the value, so check for that. */
	ATF_REQUIRE(value != ret_value);

	ret_value = nvlist_getf_binary(nvl, &ret_size, "%s", key);
	ATF_REQUIRE_EQ(value_size, ret_size);
	ATF_REQUIRE_EQ(memcmp(value, ret_value, ret_size), 0);
	ATF_REQUIRE(value != ret_value);

	/* Iterate over the nvlist; ensure that it has only our one key. */
	it = NULL;
	ATF_REQUIRE_EQ(strcmp(nvlist_next(nvl, &type, &it), key), 0);
	ATF_REQUIRE_EQ(type, NV_TYPE_BINARY);
	ATF_REQUIRE_EQ(nvlist_next(nvl, &type,&it), NULL);

	nvlist_destroy(nvl);
	free(value);
}

ATF_INIT_TEST_CASES(tp)
{
	ATF_ADD_TEST_CASE(tp, nvlist_create__is_empty);
	ATF_ADD_TEST_CASE(tp, nvlist_add_null__single_insert);
	ATF_ADD_TEST_CASE(tp, nvlist_add_bool__single_insert);
	ATF_ADD_TEST_CASE(tp, nvlist_add_number__single_insert);
	ATF_ADD_TEST_CASE(tp, nvlist_add_string__single_insert);
	ATF_ADD_TEST_CASE(tp, nvlist_add_nvlist__single_insert);
	ATF_ADD_TEST_CASE(tp, nvlist_add_binary__single_insert);
}
