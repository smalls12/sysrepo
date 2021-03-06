/**
 * @file test_operational.c
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief test for operational datastore behavior
 *
 * @copyright
 * Copyright 2018 Deutsche Telekom AG.
 * Copyright 2018 - 2019 CESNET, z.s.p.o.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#define _GNU_SOURCE

#include <string.h>
#include <unistd.h>
#include <setjmp.h>
#include <stdlib.h>
#include <stdarg.h>
#include <pthread.h>

#include <cmocka.h>
#include <libyang/libyang.h>

#include "tests/config.h"
#include "sysrepo.h"

struct state {
    sr_conn_ctx_t *conn;
    sr_session_ctx_t *sess;
    int cb_called;
    pthread_barrier_t barrier;
};

static int
setup(void **state)
{
    struct state *st;
    uint32_t conn_count;
    const char *act_feat = "advanced-testing";

    st = malloc(sizeof *st);
    if (!st) {
        return 1;
    }
    *state = st;

    sr_connection_count(&conn_count);
    assert_int_equal(conn_count, 0);

    if (sr_connect(0, &st->conn) != SR_ERR_OK) {
        return 1;
    }

    if (sr_install_module(st->conn, TESTS_DIR "/files/test.yang", TESTS_DIR "/files", NULL, 0) != SR_ERR_OK) {
        return 1;
    }
    if (sr_install_module(st->conn, TESTS_DIR "/files/ietf-interfaces.yang", TESTS_DIR "/files", NULL, 0) != SR_ERR_OK) {
        return 1;
    }
    if (sr_install_module(st->conn, TESTS_DIR "/files/iana-if-type.yang", TESTS_DIR "/files", NULL, 0) != SR_ERR_OK) {
        return 1;
    }
    if (sr_install_module(st->conn, TESTS_DIR "/files/mixed-config.yang", TESTS_DIR "/files", NULL, 0) != SR_ERR_OK) {
        return 1;
    }
    if (sr_install_module(st->conn, TESTS_DIR "/files/act.yang", TESTS_DIR "/files", &act_feat, 1) != SR_ERR_OK) {
        return 1;
    }
    if (sr_install_module(st->conn, TESTS_DIR "/files/act2.yang", TESTS_DIR "/files", NULL, 0) != SR_ERR_OK) {
        return 1;
    }
    if (sr_install_module(st->conn, TESTS_DIR "/files/act3.yang", TESTS_DIR "/files", NULL, 0) != SR_ERR_OK) {
        return 1;
    }
    sr_disconnect(st->conn);

    if (sr_connect(0, &(st->conn)) != SR_ERR_OK) {
        return 1;
    }

    if (sr_session_start(st->conn, SR_DS_RUNNING, &st->sess) != SR_ERR_OK) {
        return 1;
    }

    sr_session_set_nc_id(st->sess, 64);

    st->cb_called = 0;

    pthread_barrier_init(&st->barrier, NULL, 2);

    return 0;
}

static int
teardown(void **state)
{
    struct state *st = (struct state *)*state;

    sr_remove_module(st->conn, "act3");
    sr_remove_module(st->conn, "act2");
    sr_remove_module(st->conn, "act");
    sr_remove_module(st->conn, "mixed-config");
    sr_remove_module(st->conn, "ietf-interfaces");
    sr_remove_module(st->conn, "iana-if-type");
    sr_remove_module(st->conn, "test");

    sr_disconnect(st->conn);
    pthread_barrier_destroy(&st->barrier);
    free(st);
    return 0;
}

static int
clear_up(void **state)
{
    struct state *st = (struct state *)*state;

    sr_session_switch_ds(st->sess, SR_DS_OPERATIONAL);

    sr_delete_item(st->sess, "/ietf-interfaces:interfaces", 0);
    sr_delete_item(st->sess, "/ietf-interfaces:interfaces-state", 0);
    sr_delete_item(st->sess, "/test:cont", 0);
    sr_apply_changes(st->sess, 0, 0);

    sr_session_switch_ds(st->sess, SR_DS_STARTUP);

    sr_delete_item(st->sess, "/ietf-interfaces:interfaces", 0);
    sr_delete_item(st->sess, "/test:cont", 0);
    sr_apply_changes(st->sess, 0, 0);

    sr_session_switch_ds(st->sess, SR_DS_RUNNING);

    sr_delete_item(st->sess, "/ietf-interfaces:interfaces", 0);
    sr_delete_item(st->sess, "/test:cont", 0);
    sr_apply_changes(st->sess, 0, 0);

    return 0;
}

/* TEST 1 (no threads) */
static int
enabled_change_cb(sr_session_ctx_t *session, const char *module_name, const char *xpath, sr_event_t event,
        uint32_t request_id, void *private_data)
{
    sr_change_oper_t op;
    sr_change_iter_t *iter;
    sr_val_t *old_val, *new_val;
    int ret, *called = (int *)private_data;

    assert_int_equal(request_id, 0);

    if (!strcmp(xpath, "/ietf-interfaces:interfaces/interface[name='eth128']")) {
        assert_string_equal(module_name, "ietf-interfaces");

        if (*called == 0) {
            assert_int_equal(event, SR_EV_ENABLED);
        } else if (*called == 1) {
            assert_int_equal(event, SR_EV_DONE);
        } else {
            fail();
        }

        /* get changes iter */
        ret = sr_get_changes_iter(session, "/ietf-interfaces:*//.", &iter);
        assert_int_equal(ret, SR_ERR_OK);

        /* 1st change */
        ret = sr_get_change_next(session, iter, &op, &old_val, &new_val);
        assert_int_equal(ret, SR_ERR_OK);

        assert_int_equal(op, SR_OP_CREATED);
        assert_null(old_val);
        assert_non_null(new_val);
        assert_string_equal(new_val->xpath, "/ietf-interfaces:interfaces");

        sr_free_val(new_val);

        /* 2nd change */
        ret = sr_get_change_next(session, iter, &op, &old_val, &new_val);
        assert_int_equal(ret, SR_ERR_OK);

        assert_int_equal(op, SR_OP_CREATED);
        assert_null(old_val);
        assert_non_null(new_val);
        assert_string_equal(new_val->xpath, "/ietf-interfaces:interfaces/interface[name='eth128']");

        sr_free_val(new_val);

        /* 3rd change */
        ret = sr_get_change_next(session, iter, &op, &old_val, &new_val);
        assert_int_equal(ret, SR_ERR_OK);

        assert_int_equal(op, SR_OP_CREATED);
        assert_null(old_val);
        assert_non_null(new_val);
        assert_string_equal(new_val->xpath, "/ietf-interfaces:interfaces/interface[name='eth128']/name");

        sr_free_val(new_val);

        /* 4th change */
        ret = sr_get_change_next(session, iter, &op, &old_val, &new_val);
        assert_int_equal(ret, SR_ERR_OK);

        assert_int_equal(op, SR_OP_CREATED);
        assert_null(old_val);
        assert_non_null(new_val);
        assert_string_equal(new_val->xpath, "/ietf-interfaces:interfaces/interface[name='eth128']/type");

        sr_free_val(new_val);

        /* 5th change */
        ret = sr_get_change_next(session, iter, &op, &old_val, &new_val);
        assert_int_equal(ret, SR_ERR_OK);

        assert_int_equal(op, SR_OP_CREATED);
        assert_null(old_val);
        assert_non_null(new_val);
        assert_string_equal(new_val->xpath, "/ietf-interfaces:interfaces/interface[name='eth128']/enabled");
        assert_int_equal(new_val->dflt, 1);

        sr_free_val(new_val);

        /* no more changes */
        ret = sr_get_change_next(session, iter, &op, &old_val, &new_val);
        assert_int_equal(ret, SR_ERR_NOT_FOUND);

        sr_free_change_iter(iter);
    } else if (!strcmp(xpath, "/ietf-interfaces:interfaces/interface[name='eth256']")) {
        assert_string_equal(module_name, "ietf-interfaces");

        if (*called == 0) {
            assert_int_equal(event, SR_EV_ENABLED);
        } else if (*called == 1) {
            assert_int_equal(event, SR_EV_DONE);
        } else {
            fail();
        }

        /* get changes iter */
        ret = sr_get_changes_iter(session, "/ietf-interfaces:*//.", &iter);
        assert_int_equal(ret, SR_ERR_OK);

        /* 1st change */
        ret = sr_get_change_next(session, iter, &op, &old_val, &new_val);
        assert_int_equal(ret, SR_ERR_OK);

        assert_int_equal(op, SR_OP_CREATED);
        assert_null(old_val);
        assert_non_null(new_val);
        assert_string_equal(new_val->xpath, "/ietf-interfaces:interfaces");

        sr_free_val(new_val);

        /* no more changes */
        ret = sr_get_change_next(session, iter, &op, &old_val, &new_val);
        assert_int_equal(ret, SR_ERR_NOT_FOUND);

        sr_free_change_iter(iter);
    } else {
        fail();
    }

    ++(*called);
    return SR_ERR_OK;
}

static void
test_enabled_partial(void **state)
{
    struct state *st = (struct state *)*state;
    sr_subscription_ctx_t *subscr;
    struct lyd_node *data;
    char *str;
    const char *str2;
    int ret, called;

    /* create some configuration data */
    ret = sr_set_item_str(st->sess, "/ietf-interfaces:interfaces/interface[name='eth64']/type",
            "iana-if-type:ethernetCsmacd", NULL, SR_EDIT_STRICT);
    assert_int_equal(ret, SR_ERR_OK);
    ret = sr_set_item_str(st->sess, "/ietf-interfaces:interfaces/interface[name='eth128']/type",
            "iana-if-type:ethernetCsmacd", NULL, SR_EDIT_STRICT);
    assert_int_equal(ret, SR_ERR_OK);
    ret = sr_apply_changes(st->sess, 0, 0);
    assert_int_equal(ret, SR_ERR_OK);

    /* nothing should be in "operational" because there is no subscription */
    ret = sr_session_switch_ds(st->sess, SR_DS_OPERATIONAL);
    assert_int_equal(ret, SR_ERR_OK);

    ret = sr_get_data(st->sess, "/ietf-interfaces:interfaces", 0, 0, SR_OPER_WITH_ORIGIN, &data);
    assert_int_equal(ret, SR_ERR_OK);

    assert_non_null(data);
    assert_int_equal(data->dflt, 1);
    lyd_free_withsiblings(data);

    /* subscribe to one specific interface and also expect to be notified */
    ret = sr_session_switch_ds(st->sess, SR_DS_RUNNING);
    assert_int_equal(ret, SR_ERR_OK);

    called = 0;
    ret = sr_module_change_subscribe(st->sess, "ietf-interfaces", "/ietf-interfaces:interfaces/interface[name='eth128']",
            enabled_change_cb, &called, 0, SR_SUBSCR_ENABLED, &subscr);
    assert_int_equal(ret, SR_ERR_OK);
    assert_int_equal(called, 2);

    /* that is the only interface that should now be in "operational" */
    ret = sr_session_switch_ds(st->sess, SR_DS_OPERATIONAL);
    assert_int_equal(ret, SR_ERR_OK);

    ret = sr_get_data(st->sess, "/ietf-interfaces:interfaces", 0, 0, SR_OPER_WITH_ORIGIN, &data);
    assert_int_equal(ret, SR_ERR_OK);

    ret = sr_session_switch_ds(st->sess, SR_DS_RUNNING);
    assert_int_equal(ret, SR_ERR_OK);

    lyd_print_mem(&str, data, LYD_XML, LYP_WITHSIBLINGS);
    lyd_free(data);

    str2 =
    "<interfaces xmlns=\"urn:ietf:params:xml:ns:yang:ietf-interfaces\""
        " xmlns:or=\"urn:ietf:params:xml:ns:yang:ietf-origin\" or:origin=\"intended\">"
        "<interface>"
            "<name>eth128</name>"
            "<type xmlns:ianaift=\"urn:ietf:params:xml:ns:yang:iana-if-type\">ianaift:ethernetCsmacd</type>"
        "</interface>"
    "</interfaces>";

    assert_string_equal(str, str2);
    free(str);

    /* unsusbcribe */
    sr_unsubscribe(subscr);

    /* subscribe to a not-present interface */
    called = 0;
    ret = sr_module_change_subscribe(st->sess, "ietf-interfaces", "/ietf-interfaces:interfaces/interface[name='eth256']",
            enabled_change_cb, &called, 0, SR_SUBSCR_ENABLED, &subscr);
    assert_int_equal(ret, SR_ERR_OK);
    assert_int_equal(called, 2);

    /* "operational" should be empty again */
    ret = sr_session_switch_ds(st->sess, SR_DS_OPERATIONAL);
    assert_int_equal(ret, SR_ERR_OK);

    ret = sr_get_data(st->sess, "/ietf-interfaces:interfaces", 0, 0, SR_OPER_WITH_ORIGIN, &data);
    assert_int_equal(ret, SR_ERR_OK);

    assert_non_null(data);
    assert_int_equal(data->dflt, 1);
    lyd_free_withsiblings(data);

    /* unsusbcribe */
    sr_unsubscribe(subscr);
}

/* TEST 2 */
static int
simple_oper_cb(sr_session_ctx_t *session, const char *module_name, const char *xpath, const char *request_xpath,
        uint32_t request_id, struct lyd_node **parent, void *private_data)
{
    const struct ly_ctx *ly_ctx;
    struct lyd_node *node;

    assert_string_equal(request_xpath, "/ietf-interfaces:*");
    assert_int_equal(sr_session_get_nc_id(session), 64);
    (void)request_id;
    (void)private_data;

    ly_ctx = sr_get_context(sr_session_get_connection(session));

    assert_string_equal(module_name, "ietf-interfaces");
    assert_string_equal(xpath, "/ietf-interfaces:interfaces-state");
    assert_non_null(parent);
    assert_null(*parent);

    node = lyd_new_path(NULL, ly_ctx, "/ietf-interfaces:interfaces-state/interface[name='eth5']/type",
            "iana-if-type:ethernetCsmacd", 0, 0);
    assert_non_null(node);
    *parent = node;

    node = lyd_new_path(*parent, NULL, "/ietf-interfaces:interfaces-state/interface[name='eth5']/oper-status",
            "testing", 0, 0);
    assert_non_null(node);

    node = lyd_new_path(*parent, NULL, "/ietf-interfaces:interfaces-state/interface[name='eth5']/statistics/discontinuity-time",
            "2000-01-01T00:00:00Z", 0, 0);
    assert_non_null(node);

    return SR_ERR_OK;
}

static void
test_simple(void **state)
{
    struct state *st = (struct state *)*state;
    const struct ly_ctx *ly_ctx;
    struct lyd_node *data;
    sr_subscription_ctx_t *subscr;
    char *str1;
    const char *str2;
    int ret;

    ly_ctx = sr_get_context(st->conn);
    assert_non_null(ly_ctx);

    /* set some configuration data */
    ret = sr_set_item_str(st->sess, "/ietf-interfaces:interfaces/interface[name='eth1']/type",
            "iana-if-type:ethernetCsmacd", NULL, SR_EDIT_STRICT);
    assert_int_equal(ret, SR_ERR_OK);
    ret = sr_apply_changes(st->sess, 0, 0);
    assert_int_equal(ret, SR_ERR_OK);

    /* subscribe to all configuration data just to enable them */
    ret = sr_module_change_subscribe(st->sess, "ietf-interfaces", "/ietf-interfaces:interfaces", enabled_change_cb, NULL, 0, 0, &subscr);
    assert_int_equal(ret, SR_ERR_OK);

    /* try to read them back from operational DS */
    ret = sr_session_switch_ds(st->sess, SR_DS_OPERATIONAL);
    assert_int_equal(ret, SR_ERR_OK);

    ret = sr_get_data(st->sess, "/ietf-interfaces:*", 0, 0, SR_OPER_WITH_ORIGIN, &data);
    assert_int_equal(ret, SR_ERR_OK);

    ret = lyd_print_mem(&str1, data, LYD_XML, LYP_WITHSIBLINGS | LYP_WD_ALL);
    assert_int_equal(ret, 0);

    lyd_free_withsiblings(data);

    str2 =
    "<interfaces xmlns=\"urn:ietf:params:xml:ns:yang:ietf-interfaces\""
        " xmlns:or=\"urn:ietf:params:xml:ns:yang:ietf-origin\" or:origin=\"intended\">"
        "<interface>"
            "<name>eth1</name>"
            "<type xmlns:ianaift=\"urn:ietf:params:xml:ns:yang:iana-if-type\">ianaift:ethernetCsmacd</type>"
            "<enabled or:origin=\"default\">true</enabled>"
        "</interface>"
    "</interfaces>";

    assert_string_equal(str1, str2);
    free(str1);

    /* subscribe as state data provider */
    ret = sr_oper_get_items_subscribe(st->sess, "ietf-interfaces", "/ietf-interfaces:interfaces-state", simple_oper_cb,
            NULL, SR_SUBSCR_CTX_REUSE, &subscr);
    assert_int_equal(ret, SR_ERR_OK);

    /* read all data from operational again */
    ret = sr_get_data(st->sess, "/ietf-interfaces:*", 0, 0, SR_OPER_WITH_ORIGIN, &data);
    assert_int_equal(ret, SR_ERR_OK);

    ret = lyd_print_mem(&str1, data, LYD_XML, LYP_WITHSIBLINGS | LYP_WD_ALL);
    assert_int_equal(ret, 0);

    lyd_free_withsiblings(data);

    str2 =
    "<interfaces xmlns=\"urn:ietf:params:xml:ns:yang:ietf-interfaces\""
        " xmlns:or=\"urn:ietf:params:xml:ns:yang:ietf-origin\" or:origin=\"intended\">"
        "<interface>"
            "<name>eth1</name>"
            "<type xmlns:ianaift=\"urn:ietf:params:xml:ns:yang:iana-if-type\">ianaift:ethernetCsmacd</type>"
            "<enabled or:origin=\"default\">true</enabled>"
        "</interface>"
    "</interfaces>"
    "<interfaces-state xmlns=\"urn:ietf:params:xml:ns:yang:ietf-interfaces\""
        " xmlns:or=\"urn:ietf:params:xml:ns:yang:ietf-origin\" or:origin=\"unknown\">"
        "<interface>"
            "<name>eth5</name>"
            "<type xmlns:ianaift=\"urn:ietf:params:xml:ns:yang:iana-if-type\">ianaift:ethernetCsmacd</type>"
            "<oper-status>testing</oper-status>"
            "<statistics>"
                "<discontinuity-time>2000-01-01T00:00:00Z</discontinuity-time>"
            "</statistics>"
        "</interface>"
    "</interfaces-state>";

    assert_string_equal(str1, str2);
    free(str1);

    sr_unsubscribe(subscr);
}

/* TEST 3 */
static int
fail_oper_cb(sr_session_ctx_t *session, const char *module_name, const char *xpath, const char *request_xpath,
        uint32_t request_id, struct lyd_node **parent, void *private_data)
{
    (void)session;
    (void)request_id;
    (void)private_data;

    assert_string_equal(request_xpath, "/ietf-interfaces:*");

    assert_string_equal(module_name, "ietf-interfaces");
    assert_string_equal(xpath, "/ietf-interfaces:interfaces-state");
    assert_non_null(parent);
    assert_null(*parent);

    sr_set_error(session, "/no/special/xpath", "Callback failed with an error.");
    return SR_ERR_UNAUTHORIZED;
}

static void
test_fail(void **state)
{
    struct state *st = (struct state *)*state;
    struct lyd_node *data;
    sr_subscription_ctx_t *subscr;
    int ret;

    /* set some configuration data */
    ret = sr_set_item_str(st->sess, "/ietf-interfaces:interfaces/interface[name='eth1']/type",
            "iana-if-type:ethernetCsmacd", NULL, SR_EDIT_STRICT);
    assert_int_equal(ret, SR_ERR_OK);
    ret = sr_apply_changes(st->sess, 0, 0);
    assert_int_equal(ret, SR_ERR_OK);

    /* subscribe as state data provider*/
    ret = sr_oper_get_items_subscribe(st->sess, "ietf-interfaces", "/ietf-interfaces:interfaces-state", fail_oper_cb,
            NULL, 0, &subscr);
    assert_int_equal(ret, SR_ERR_OK);

    /* read all data from operational */
    ret = sr_session_switch_ds(st->sess, SR_DS_OPERATIONAL);
    assert_int_equal(ret, SR_ERR_OK);

    ret = sr_get_data(st->sess, "/ietf-interfaces:*", 0, 0, SR_OPER_WITH_ORIGIN, &data);
    assert_int_equal(ret, SR_ERR_CALLBACK_FAILED);

    sr_unsubscribe(subscr);
}

/* TEST 4 */
static int
config_oper_cb(sr_session_ctx_t *session, const char *module_name, const char *xpath, const char *request_xpath,
        uint32_t request_id, struct lyd_node **parent, void *private_data)
{
    const struct ly_ctx *ly_ctx;

    (void)request_id;
    (void)private_data;

    assert_string_equal(request_xpath, "/ietf-interfaces:*");

    ly_ctx = sr_get_context(sr_session_get_connection(session));

    assert_string_equal(module_name, "ietf-interfaces");
    assert_string_equal(xpath, "/ietf-interfaces:interfaces");
    assert_non_null(parent);
    assert_null(*parent);

    *parent = lyd_new_path(NULL, ly_ctx, "/ietf-interfaces:interfaces/interface[name='eth5']/type",
            "iana-if-type:ethernetCsmacd", 0, 0);
    assert_non_null(*parent);

    return SR_ERR_OK;
}

static void
test_config(void **state)
{
    struct state *st = (struct state *)*state;
    struct lyd_node *data;
    sr_subscription_ctx_t *subscr;
    char *str1;
    const char *str2;
    int ret;

    /* set some configuration data */
    ret = sr_set_item_str(st->sess, "/ietf-interfaces:interfaces/interface[name='eth1']/type",
            "iana-if-type:ethernetCsmacd", NULL, SR_EDIT_STRICT);
    assert_int_equal(ret, SR_ERR_OK);
    ret = sr_set_item_str(st->sess, "/ietf-interfaces:interfaces/interface[name='eth2']/type",
            "iana-if-type:ethernetCsmacd", NULL, SR_EDIT_STRICT);
    assert_int_equal(ret, SR_ERR_OK);
    ret = sr_apply_changes(st->sess, 0, 0);
    assert_int_equal(ret, SR_ERR_OK);

    /* subscribe to all configuration data just to enable them */
    ret = sr_module_change_subscribe(st->sess, "ietf-interfaces", "/ietf-interfaces:interfaces", enabled_change_cb, NULL, 0, 0, &subscr);
    assert_int_equal(ret, SR_ERR_OK);

    /* subscribe as config data provider and listen */
    ret = sr_oper_get_items_subscribe(st->sess, "ietf-interfaces", "/ietf-interfaces:interfaces", config_oper_cb,
            NULL, SR_SUBSCR_CTX_REUSE, &subscr);
    assert_int_equal(ret, SR_ERR_OK);

    /* read all data from operational */
    ret = sr_session_switch_ds(st->sess, SR_DS_OPERATIONAL);
    assert_int_equal(ret, SR_ERR_OK);

    ret = sr_get_data(st->sess, "/ietf-interfaces:*", 0, 0, SR_OPER_WITH_ORIGIN, &data);
    assert_int_equal(data->dflt, 1);

    ret = lyd_print_mem(&str1, data->next, LYD_XML, LYP_WITHSIBLINGS);
    assert_int_equal(ret, 0);

    lyd_free_withsiblings(data);

    str2 =
    "<interfaces xmlns=\"urn:ietf:params:xml:ns:yang:ietf-interfaces\""
        " xmlns:or=\"urn:ietf:params:xml:ns:yang:ietf-origin\" or:origin=\"unknown\">"
        "<interface>"
            "<name>eth5</name>"
            "<type xmlns:ianaift=\"urn:ietf:params:xml:ns:yang:iana-if-type\">ianaift:ethernetCsmacd</type>"
        "</interface>"
    "</interfaces>";

    assert_string_equal(str1, str2);
    free(str1);

    sr_unsubscribe(subscr);
}

/* TEST 5 */
static int
list_oper_cb(sr_session_ctx_t *session, const char *module_name, const char *xpath, const char *request_xpath,
        uint32_t request_id, struct lyd_node **parent, void *private_data)
{
    struct lyd_node *node;

    (void)session;
    (void)request_id;
    (void)private_data;

    assert_string_equal(request_xpath, "/ietf-interfaces:*");
    assert_string_equal(module_name, "ietf-interfaces");
    assert_non_null(parent);
    assert_non_null(*parent);

    if (!strcmp(xpath, "/ietf-interfaces:interfaces/interface[name='eth2']")) {
        node = lyd_new_path(*parent, NULL, "/ietf-interfaces:interfaces/interface[name='eth2']/type",
                "iana-if-type:ethernetCsmacd", 0, 0);
        assert_non_null(node);
    } else if (!strcmp(xpath, "/ietf-interfaces:interfaces/interface[name='eth3']")) {
        node = lyd_new_path(*parent, NULL, "/ietf-interfaces:interfaces/interface[name='eth3']/type",
                "iana-if-type:ethernetCsmacd", 0, 0);
        assert_non_null(node);
    } else {
        fail();
    }

    return SR_ERR_OK;
}

static void
test_list(void **state)
{
    struct state *st = (struct state *)*state;
    struct lyd_node *data;
    sr_subscription_ctx_t *subscr;
    char *str1;
    const char *str2;
    int ret;

    /* set some configuration data */
    ret = sr_set_item_str(st->sess, "/ietf-interfaces:interfaces/interface[name='eth1']/type",
            "iana-if-type:ethernetCsmacd", NULL, SR_EDIT_STRICT);
    assert_int_equal(ret, SR_ERR_OK);
    ret = sr_apply_changes(st->sess, 0, 0);
    assert_int_equal(ret, SR_ERR_OK);

    /* subscribe to all configuration data just to enable them */
    ret = sr_module_change_subscribe(st->sess, "ietf-interfaces", "/ietf-interfaces:interfaces", enabled_change_cb, NULL, 0, 0, &subscr);
    assert_int_equal(ret, SR_ERR_OK);

    /* subscribe as 2 list instances data provider and listen */
    ret = sr_oper_get_items_subscribe(st->sess, "ietf-interfaces", "/ietf-interfaces:interfaces/interface[name='eth2']",
            list_oper_cb, NULL, SR_SUBSCR_CTX_REUSE, &subscr);
    assert_int_equal(ret, SR_ERR_OK);
    ret = sr_oper_get_items_subscribe(st->sess, "ietf-interfaces", "/ietf-interfaces:interfaces/interface[name='eth3']",
            list_oper_cb, NULL, SR_SUBSCR_CTX_REUSE, &subscr);
    assert_int_equal(ret, SR_ERR_OK);

    /* read all data from operational */
    ret = sr_session_switch_ds(st->sess, SR_DS_OPERATIONAL);
    assert_int_equal(ret, SR_ERR_OK);

    ret = sr_get_data(st->sess, "/ietf-interfaces:*", 0, 0, SR_OPER_WITH_ORIGIN, &data);
    assert_int_equal(data->next->dflt, 1);

    ret = lyd_print_mem(&str1, data, LYD_XML, LYP_WITHSIBLINGS);
    assert_int_equal(ret, 0);

    lyd_free_withsiblings(data);

    str2 =
    "<interfaces xmlns=\"urn:ietf:params:xml:ns:yang:ietf-interfaces\""
        " xmlns:or=\"urn:ietf:params:xml:ns:yang:ietf-origin\" or:origin=\"intended\">"
        "<interface>"
            "<name>eth1</name>"
            "<type xmlns:ianaift=\"urn:ietf:params:xml:ns:yang:iana-if-type\">ianaift:ethernetCsmacd</type>"
        "</interface>"
        "<interface or:origin=\"unknown\">"
            "<name>eth2</name>"
            "<type xmlns:ianaift=\"urn:ietf:params:xml:ns:yang:iana-if-type\">ianaift:ethernetCsmacd</type>"
        "</interface>"
        "<interface or:origin=\"unknown\">"
            "<name>eth3</name>"
            "<type xmlns:ianaift=\"urn:ietf:params:xml:ns:yang:iana-if-type\">ianaift:ethernetCsmacd</type>"
        "</interface>"
    "</interfaces>";

    assert_string_equal(str1, str2);
    free(str1);

    sr_unsubscribe(subscr);
}

/* TEST 6 */
static int
nested_oper_cb(sr_session_ctx_t *session, const char *module_name, const char *xpath, const char *request_xpath,
        uint32_t request_id, struct lyd_node **parent, void *private_data)
{
    const struct ly_ctx *ly_ctx;
    struct lyd_node *node;

    (void)request_id;
    (void)private_data;

    ly_ctx = sr_get_context(sr_session_get_connection(session));

    assert_string_equal(request_xpath, "/ietf-interfaces:*");
    assert_string_equal(module_name, "ietf-interfaces");
    assert_non_null(parent);

    if (!strcmp(xpath, "/ietf-interfaces:interfaces-state/interface[name='eth2']/phys-address")) {
        assert_non_null(*parent);

        node = lyd_new_path(*parent, NULL, "phys-address",
                "01:23:45:67:89:ab", 0, 0);
        assert_non_null(node);
    } else if (!strcmp(xpath, "/ietf-interfaces:interfaces-state")) {
        assert_null(*parent);

        node = lyd_new_path(NULL, ly_ctx, "/ietf-interfaces:interfaces-state/interface[name='eth2']/type",
                "iana-if-type:ethernetCsmacd", 0, 0);
        assert_non_null(node);
        *parent = node;

        node = lyd_new_path(*parent, NULL, "/ietf-interfaces:interfaces-state/interface[name='eth2']/oper-status",
                "testing", 0, 0);
        assert_non_null(node);

        node = lyd_new_path(*parent, NULL, "/ietf-interfaces:interfaces-state/interface[name='eth2']/statistics/discontinuity-time",
                "2000-01-01T00:00:00Z", 0, 0);
        assert_non_null(node);

        node = lyd_new_path(*parent, NULL, "/ietf-interfaces:interfaces-state/interface[name='eth3']/type",
                "iana-if-type:ethernetCsmacd", 0, 0);
        assert_non_null(node);

        node = lyd_new_path(*parent, NULL, "/ietf-interfaces:interfaces-state/interface[name='eth3']/oper-status",
                "dormant", 0, 0);
        assert_non_null(node);

        node = lyd_new_path(*parent, NULL, "/ietf-interfaces:interfaces-state/interface[name='eth3']/statistics/discontinuity-time",
                "2005-01-01T00:00:00Z", 0, 0);
        assert_non_null(node);
    } else {
        fail();
    }

    return SR_ERR_OK;
}

static void
test_nested(void **state)
{
    struct state *st = (struct state *)*state;
    struct lyd_node *data;
    sr_subscription_ctx_t *subscr;
    char *str1;
    const char *str2;
    int ret;

    /* set some configuration data */
    ret = sr_set_item_str(st->sess, "/ietf-interfaces:interfaces/interface[name='eth1']/type",
            "iana-if-type:ethernetCsmacd", NULL, SR_EDIT_STRICT);
    assert_int_equal(ret, SR_ERR_OK);
    ret = sr_apply_changes(st->sess, 0, 0);
    assert_int_equal(ret, SR_ERR_OK);

    /* subscribe to all configuration data just to enable them */
    ret = sr_module_change_subscribe(st->sess, "ietf-interfaces", "/ietf-interfaces:interfaces", enabled_change_cb, NULL, 0, 0, &subscr);
    assert_int_equal(ret, SR_ERR_OK);

    /* subscribe as state data provider and listen, it should be called only 2x */
    ret = sr_oper_get_items_subscribe(st->sess, "ietf-interfaces", "/ietf-interfaces:interfaces-state/interface[name='eth4']/phys-address",
            nested_oper_cb, NULL, SR_SUBSCR_CTX_REUSE, &subscr);
    assert_int_equal(ret, SR_ERR_OK);
    ret = sr_oper_get_items_subscribe(st->sess, "ietf-interfaces", "/ietf-interfaces:interfaces-state",
            nested_oper_cb, NULL, SR_SUBSCR_CTX_REUSE, &subscr);
    assert_int_equal(ret, SR_ERR_OK);
    ret = sr_oper_get_items_subscribe(st->sess, "ietf-interfaces", "/ietf-interfaces:interfaces-state/interface[name='eth2']/phys-address",
            nested_oper_cb, NULL, SR_SUBSCR_CTX_REUSE, &subscr);
    assert_int_equal(ret, SR_ERR_OK);

    /* read all data from operational */
    ret = sr_session_switch_ds(st->sess, SR_DS_OPERATIONAL);
    assert_int_equal(ret, SR_ERR_OK);

    ret = sr_get_data(st->sess, "/ietf-interfaces:*", 0, 0, SR_OPER_WITH_ORIGIN, &data);
    assert_int_equal(ret, SR_ERR_OK);

    ret = lyd_print_mem(&str1, data, LYD_XML, LYP_WITHSIBLINGS);
    assert_int_equal(ret, 0);

    lyd_free_withsiblings(data);

    str2 =
    "<interfaces xmlns=\"urn:ietf:params:xml:ns:yang:ietf-interfaces\""
        " xmlns:or=\"urn:ietf:params:xml:ns:yang:ietf-origin\" or:origin=\"intended\">"
        "<interface>"
            "<name>eth1</name>"
            "<type xmlns:ianaift=\"urn:ietf:params:xml:ns:yang:iana-if-type\">ianaift:ethernetCsmacd</type>"
        "</interface>"
    "</interfaces>"
    "<interfaces-state xmlns=\"urn:ietf:params:xml:ns:yang:ietf-interfaces\""
        " xmlns:or=\"urn:ietf:params:xml:ns:yang:ietf-origin\" or:origin=\"unknown\">"
        "<interface>"
            "<name>eth2</name>"
            "<type xmlns:ianaift=\"urn:ietf:params:xml:ns:yang:iana-if-type\">ianaift:ethernetCsmacd</type>"
            "<oper-status>testing</oper-status>"
            "<statistics>"
                "<discontinuity-time>2000-01-01T00:00:00Z</discontinuity-time>"
            "</statistics>"
            "<phys-address>01:23:45:67:89:ab</phys-address>"
        "</interface>"
        "<interface>"
            "<name>eth3</name>"
            "<type xmlns:ianaift=\"urn:ietf:params:xml:ns:yang:iana-if-type\">ianaift:ethernetCsmacd</type>"
            "<oper-status>dormant</oper-status>"
            "<statistics>"
                "<discontinuity-time>2005-01-01T00:00:00Z</discontinuity-time>"
            "</statistics>"
        "</interface>"
    "</interfaces-state>";

    assert_string_equal(str1, str2);
    free(str1);

    sr_unsubscribe(subscr);
}

/* TEST 7 */
static int
mixed_oper_cb(sr_session_ctx_t *session, const char *module_name, const char *xpath, const char *request_xpath,
        uint32_t request_id, struct lyd_node **parent, void *private_data)
{
    const struct ly_ctx *ly_ctx;
    struct lyd_node *node;

    (void)request_id;
    (void)private_data;

    ly_ctx = sr_get_context(sr_session_get_connection(session));

    assert_string_equal(request_xpath, "/ietf-interfaces:*");
    assert_string_equal(module_name, "ietf-interfaces");
    assert_string_equal(xpath, "/ietf-interfaces:*");
    assert_non_null(parent);
    assert_null(*parent);

    /* config */
    *parent = lyd_new_path(NULL, ly_ctx, "/ietf-interfaces:interfaces/interface[name='eth10']/type",
            "iana-if-type:ethernetCsmacd", 0, 0);
    assert_non_null(*parent);

    /* state */
    node = lyd_new_path(*parent, NULL, "/ietf-interfaces:interfaces-state/interface[name='eth11']/type",
            "iana-if-type:ethernetCsmacd", 0, 0);
    assert_non_null(node);

    node = lyd_new_path(*parent, NULL, "/ietf-interfaces:interfaces-state/interface[name='eth11']/oper-status",
            "down", 0, 0);
    assert_non_null(node);

    node = lyd_new_path(*parent, NULL, "/ietf-interfaces:interfaces-state/interface[name='eth11']/statistics/discontinuity-time",
            "2000-01-01T00:00:00Z", 0, 0);
    assert_non_null(node);

    return SR_ERR_OK;
}

static void
test_mixed(void **state)
{
    struct state *st = (struct state *)*state;
    struct lyd_node *data;
    sr_subscription_ctx_t *subscr;
    char *str1;
    const char *str2;
    int ret;

    /* set some configuration data */
    ret = sr_set_item_str(st->sess, "/ietf-interfaces:interfaces/interface[name='eth1']/type",
            "iana-if-type:ethernetCsmacd", NULL, SR_EDIT_STRICT);
    assert_int_equal(ret, SR_ERR_OK);
    ret = sr_apply_changes(st->sess, 0, 0);
    assert_int_equal(ret, SR_ERR_OK);

    /* subscribe to all configuration data just to enable them */
    ret = sr_module_change_subscribe(st->sess, "ietf-interfaces", "/ietf-interfaces:interfaces", enabled_change_cb, NULL, 0, 0, &subscr);
    assert_int_equal(ret, SR_ERR_OK);

    /* subscribe as config data provider and listen */
    ret = sr_oper_get_items_subscribe(st->sess, "ietf-interfaces", "/ietf-interfaces:*", mixed_oper_cb,
            NULL, SR_SUBSCR_CTX_REUSE, &subscr);
    assert_int_equal(ret, SR_ERR_OK);

    /* read all data from operational */
    ret = sr_session_switch_ds(st->sess, SR_DS_OPERATIONAL);
    assert_int_equal(ret, SR_ERR_OK);

    ret = sr_get_data(st->sess, "/ietf-interfaces:*", 0, 0, SR_OPER_WITH_ORIGIN, &data);
    assert_int_equal(ret, SR_ERR_OK);

    ret = lyd_print_mem(&str1, data, LYD_XML, LYP_WITHSIBLINGS);
    assert_int_equal(ret, 0);

    lyd_free_withsiblings(data);

    str2 =
    "<interfaces xmlns=\"urn:ietf:params:xml:ns:yang:ietf-interfaces\""
        " xmlns:or=\"urn:ietf:params:xml:ns:yang:ietf-origin\" or:origin=\"unknown\">"
        "<interface>"
            "<name>eth10</name>"
            "<type xmlns:ianaift=\"urn:ietf:params:xml:ns:yang:iana-if-type\">ianaift:ethernetCsmacd</type>"
        "</interface>"
    "</interfaces>"
    "<interfaces-state xmlns=\"urn:ietf:params:xml:ns:yang:ietf-interfaces\""
        " xmlns:or=\"urn:ietf:params:xml:ns:yang:ietf-origin\" or:origin=\"unknown\">"
        "<interface>"
            "<name>eth11</name>"
            "<type xmlns:ianaift=\"urn:ietf:params:xml:ns:yang:iana-if-type\">ianaift:ethernetCsmacd</type>"
            "<oper-status>down</oper-status>"
            "<statistics>"
                "<discontinuity-time>2000-01-01T00:00:00Z</discontinuity-time>"
            "</statistics>"
        "</interface>"
    "</interfaces-state>";

    assert_string_equal(str1, str2);
    free(str1);

    sr_unsubscribe(subscr);
}

/* TEST 8 */
static int
xpath_check_oper_cb(sr_session_ctx_t *session, const char *module_name, const char *xpath, const char *request_xpath,
        uint32_t request_id, struct lyd_node **parent, void *private_data)
{
    struct state *st = (struct state *)private_data;

    (void)session;
    (void)module_name;
    (void)xpath;
    (void)request_xpath;
    (void)request_id;
    (void)parent;

    ++st->cb_called;
    return SR_ERR_OK;
}

static void
test_xpath_check(void **state)
{
    struct state *st = (struct state *)*state;
    struct lyd_node *data;
    sr_subscription_ctx_t *subscr;
    int ret;

    ret = sr_session_switch_ds(st->sess, SR_DS_OPERATIONAL);
    assert_int_equal(ret, SR_ERR_OK);

    /* subscribe as state data provider */
    ret = sr_oper_get_items_subscribe(st->sess, "ietf-interfaces", "/ietf-interfaces:interfaces-state", xpath_check_oper_cb,
            st, 0, &subscr);
    assert_int_equal(ret, SR_ERR_OK);

    /* read interfaces from operational, callback not called */
    st->cb_called = 0;
    ret = sr_get_data(st->sess, "/ietf-interfaces:interfaces", 0, 0, 0, &data);
    assert_int_equal(ret, SR_ERR_OK);
    lyd_free_withsiblings(data);
    assert_int_equal(st->cb_called, 0);

    /* read all from operational, callback called */
    st->cb_called = 0;
    ret = sr_get_data(st->sess, "/ietf-interfaces:*", 0, 0, 0, &data);
    assert_int_equal(ret, SR_ERR_OK);
    lyd_free_withsiblings(data);
    assert_int_equal(st->cb_called, 1);

    sr_unsubscribe(subscr);
    subscr = NULL;

    /* subscribe as state data provider */
    ret = sr_oper_get_items_subscribe(st->sess, "ietf-interfaces", "/ietf-interfaces:interfaces-state/interface[name='eth0']",
            xpath_check_oper_cb, st, SR_SUBSCR_CTX_REUSE, &subscr);
    assert_int_equal(ret, SR_ERR_OK);

    /* read interfaces from operational, callback not called */
    st->cb_called = 0;
    ret = sr_get_data(st->sess, "/ietf-interfaces:interfaces-state/interface[name='eth1']", 0, 0, 0, &data);
    assert_int_equal(ret, SR_ERR_OK);
    lyd_free_withsiblings(data);
    assert_int_equal(st->cb_called, 0);

    /* read all from operational, callback called */
    st->cb_called = 0;
    ret = sr_get_data(st->sess, "/ietf-interfaces:interfaces-state/interface[name='eth0']/type", 0, 0, 0, &data);
    assert_int_equal(ret, SR_ERR_OK);
    lyd_free_withsiblings(data);
    assert_int_equal(st->cb_called, 1);

    sr_unsubscribe(subscr);
}

/* TEST 9 */
static int
state_only_oper_cb(sr_session_ctx_t *session, const char *module_name, const char *xpath, const char *request_xpath,
        uint32_t request_id, struct lyd_node **parent, void *private_data)
{
    struct state *st = (struct state *)private_data;
    const struct ly_ctx *ly_ctx;
    struct lyd_node *node;

    (void)request_xpath;
    (void)request_id;

    assert_string_equal(module_name, "mixed-config");

    ly_ctx = sr_get_context(sr_session_get_connection(session));

    if (!strcmp(xpath, "/mixed-config:test-state")) {
        assert_non_null(parent);
        assert_null(*parent);

        *parent = lyd_new_path(NULL, ly_ctx, "/mixed-config:test-state/test-case[name='one']/result", "101", 0, 0);
        assert_non_null(*parent);
        node = lyd_new_path(*parent, NULL, "/mixed-config:test-state/test-case[name='one']/x", "0.5000", 0, 0);
        assert_non_null(node);
        node = lyd_new_path(*parent, NULL, "/mixed-config:test-state/test-case[name='one']/y", "-0.5000", 0, 0);
        assert_non_null(node);
        node = lyd_new_path(*parent, NULL, "/mixed-config:test-state/test-case[name='one']/z", "-0.2500", 0, 0);
        assert_non_null(node);

        node = lyd_new_path(*parent, NULL, "/mixed-config:test-state/test-case[name='two']", NULL, 0, 0);
        assert_non_null(node);
    } else if (!strcmp(xpath, "/mixed-config:test-state/test-case/result")) {
        assert_non_null(parent);
        assert_non_null(*parent);

        node = lyd_new_path(*parent, NULL, "result", "100", 0, 0);
        assert_non_null(node);
    } else {
        fail();
    }

    ++st->cb_called;
    return SR_ERR_OK;
}

static int
dummy_change_cb(sr_session_ctx_t *session, const char *module_name, const char *xpath, sr_event_t event,
        uint32_t request_id, void *private_data)
{
    (void)session;
    (void)module_name;
    (void)xpath;
    (void)event;
    (void)request_id;
    (void)private_data;

    return SR_ERR_OK;
}

static void
test_state_only(void **state)
{
    struct state *st = (struct state *)*state;
    struct lyd_node *data;
    sr_subscription_ctx_t *subscr;
    char *str1;
    const char *str2;
    int ret;

    /* subscribe as mixed data provider and listen */
    ret = sr_oper_get_items_subscribe(st->sess, "mixed-config", "/mixed-config:test-state", state_only_oper_cb,
            st, 0, &subscr);
    assert_int_equal(ret, SR_ERR_OK);

    /* read all state-only data */
    ret = sr_session_switch_ds(st->sess, SR_DS_OPERATIONAL);
    assert_int_equal(ret, SR_ERR_OK);

    st->cb_called = 0;
    ret = sr_get_data(st->sess, "/*", 0, 0, SR_OPER_NO_CONFIG | SR_OPER_WITH_ORIGIN, &data);
    assert_int_equal(ret, SR_ERR_OK);
    assert_int_equal(st->cb_called, 1);

    ret = lyd_print_mem(&str1, data, LYD_XML, LYP_WITHSIBLINGS);
    assert_int_equal(ret, 0);

    lyd_free_withsiblings(data);

    str2 =
    "<test-state xmlns=\"urn:sysrepo:mixed-config\" xmlns:or=\"urn:ietf:params:xml:ns:yang:ietf-origin\" or:origin=\"unknown\">"
        "<test-case>"
            "<name>one</name>"
            "<result>101</result>"
            "<x>0.5</x>"
            "<y>-0.5</y>"
            "<z>-0.25</z>"
        "</test-case>"
    "</test-state>";

    assert_string_equal(str1, str2);
    free(str1);

    sr_unsubscribe(subscr);

    /* set some configuration data */
    ret = sr_session_switch_ds(st->sess, SR_DS_RUNNING);
    assert_int_equal(ret, SR_ERR_OK);
    ret = sr_set_item_str(st->sess, "/mixed-config:test-state/test-case[name='three']", NULL, NULL, 0);
    assert_int_equal(ret, SR_ERR_OK);
    ret = sr_apply_changes(st->sess, 0, 0);
    assert_int_equal(ret, SR_ERR_OK);

    /* subscribe to all configuration data just to enable them */
    ret = sr_module_change_subscribe(st->sess, "mixed-config", "/mixed-config:test-state", dummy_change_cb, NULL,
            0, 0, &subscr);
    assert_int_equal(ret, SR_ERR_OK);

    /* subscribe as nested state data provider and listen */
    ret = sr_oper_get_items_subscribe(st->sess, "mixed-config", "/mixed-config:test-state/test-case/result", state_only_oper_cb,
            st, SR_SUBSCR_CTX_REUSE, &subscr);
    assert_int_equal(ret, SR_ERR_OK);

    /* read all state-only data */
    ret = sr_session_switch_ds(st->sess, SR_DS_OPERATIONAL);
    assert_int_equal(ret, SR_ERR_OK);

    st->cb_called = 0;
    ret = sr_get_data(st->sess, "/*", 0, 0, SR_OPER_NO_CONFIG | SR_OPER_WITH_ORIGIN, &data);
    assert_int_equal(ret, SR_ERR_OK);
    assert_int_equal(st->cb_called, 1);

    ret = lyd_print_mem(&str1, data, LYD_XML, LYP_WITHSIBLINGS);
    assert_int_equal(ret, 0);

    lyd_free_withsiblings(data);

    str2 =
    "<test-state xmlns=\"urn:sysrepo:mixed-config\" xmlns:or=\"urn:ietf:params:xml:ns:yang:ietf-origin\" or:origin=\"intended\">"
        "<test-case>"
            "<name>three</name>"
            "<result or:origin=\"unknown\">100</result>"
        "</test-case>"
    "</test-state>";

    assert_string_equal(str1, str2);
    free(str1);

    /* set some more configuration data */
    ret = sr_session_switch_ds(st->sess, SR_DS_RUNNING);
    assert_int_equal(ret, SR_ERR_OK);
    ret = sr_set_item_str(st->sess, "/mixed-config:test-state/test-case[name='four']", NULL, NULL, 0);
    assert_int_equal(ret, SR_ERR_OK);
    ret = sr_set_item_str(st->sess, "/mixed-config:test-state/test-case[name='five']", NULL, NULL, 0);
    assert_int_equal(ret, SR_ERR_OK);
    ret = sr_apply_changes(st->sess, 0, 0);
    assert_int_equal(ret, SR_ERR_OK);

    /* read some state data (callback should not be called for a filtered-out parent) */
    ret = sr_session_switch_ds(st->sess, SR_DS_OPERATIONAL);
    assert_int_equal(ret, SR_ERR_OK);

    st->cb_called = 0;
    ret = sr_get_data(st->sess, "/mixed-config:test-state/test-case[name='four']", 0, 0,
            SR_OPER_NO_CONFIG | SR_OPER_WITH_ORIGIN, &data);
    assert_int_equal(ret, SR_ERR_OK);
    assert_int_equal(st->cb_called, 1);

    ret = lyd_print_mem(&str1, data, LYD_XML, LYP_WITHSIBLINGS);
    assert_int_equal(ret, 0);

    lyd_free_withsiblings(data);

    str2 =
    "<test-state xmlns=\"urn:sysrepo:mixed-config\" xmlns:or=\"urn:ietf:params:xml:ns:yang:ietf-origin\" or:origin=\"intended\">"
        "<test-case>"
            "<name>four</name>"
            "<result or:origin=\"unknown\">100</result>"
        "</test-case>"
    "</test-state>";

    assert_string_equal(str1, str2);
    free(str1);

    sr_unsubscribe(subscr);
}

/* TEST 10 */
static void
test_config_only(void **state)
{
    struct state *st = (struct state *)*state;
    struct lyd_node *data;
    sr_subscription_ctx_t *subscr;
    char *str1;
    const char *str2;
    int ret;

    /* set some configuration data */
    ret = sr_set_item_str(st->sess, "/ietf-interfaces:interfaces/interface[name='eth1']/type",
            "iana-if-type:ethernetCsmacd", NULL, SR_EDIT_STRICT);
    assert_int_equal(ret, SR_ERR_OK);
    ret = sr_apply_changes(st->sess, 0, 0);
    assert_int_equal(ret, SR_ERR_OK);

    /* subscribe to all configuration data just to enable them */
    ret = sr_module_change_subscribe(st->sess, "ietf-interfaces", "/ietf-interfaces:interfaces", enabled_change_cb, NULL, 0, 0, &subscr);
    assert_int_equal(ret, SR_ERR_OK);

    /* subscribe as config data provider and listen */
    ret = sr_oper_get_items_subscribe(st->sess, "ietf-interfaces", "/ietf-interfaces:*", mixed_oper_cb,
            NULL, SR_SUBSCR_CTX_REUSE, &subscr);
    assert_int_equal(ret, SR_ERR_OK);

    /* read all state-only data */
    ret = sr_session_switch_ds(st->sess, SR_DS_OPERATIONAL);
    assert_int_equal(ret, SR_ERR_OK);

    ret = sr_get_data(st->sess, "/ietf-interfaces:*", 0, 0, SR_OPER_NO_STATE | SR_OPER_WITH_ORIGIN, &data);
    assert_int_equal(ret, SR_ERR_OK);

    ret = lyd_print_mem(&str1, data, LYD_XML, LYP_WITHSIBLINGS);
    assert_int_equal(ret, 0);

    lyd_free_withsiblings(data);

    str2 =
    "<interfaces xmlns=\"urn:ietf:params:xml:ns:yang:ietf-interfaces\""
        " xmlns:or=\"urn:ietf:params:xml:ns:yang:ietf-origin\" or:origin=\"unknown\">"
        "<interface>"
            "<name>eth10</name>"
            "<type xmlns:ianaift=\"urn:ietf:params:xml:ns:yang:iana-if-type\">ianaift:ethernetCsmacd</type>"
        "</interface>"
    "</interfaces>";

    assert_string_equal(str1, str2);
    free(str1);

    sr_unsubscribe(subscr);
}

/* TEST 11 */
static void
test_conn_owner1(void **state)
{
    struct state *st = (struct state *)*state;
    struct lyd_node *data;
    sr_conn_ctx_t *conn;
    sr_session_ctx_t *sess;
    char *str1;
    const char *str2;
    int ret;

    /* create another connection and session */
    ret = sr_connect(0, &conn);
    assert_int_equal(ret, SR_ERR_OK);
    ret = sr_session_start(conn, SR_DS_OPERATIONAL, &sess);
    assert_int_equal(ret, SR_ERR_OK);

    /* set some operational data */
    ret = sr_set_item_str(sess, "/ietf-interfaces:interfaces-state/interface[name='eth1']/type",
            "iana-if-type:ethernetCsmacd", NULL, SR_EDIT_STRICT);
    assert_int_equal(ret, SR_ERR_OK);
    ret = sr_apply_changes(sess, 0, 0);
    assert_int_equal(ret, SR_ERR_OK);

    /* read the data */
    ret = sr_get_data(sess, "/ietf-interfaces:interfaces-state", 0, 0, SR_OPER_WITH_ORIGIN, &data);
    assert_int_equal(ret, SR_ERR_OK);

    ret = lyd_print_mem(&str1, data, LYD_XML, LYP_WITHSIBLINGS);
    assert_int_equal(ret, 0);

    lyd_free_withsiblings(data);

    str2 =
    "<interfaces-state xmlns=\"urn:ietf:params:xml:ns:yang:ietf-interfaces\""
        " xmlns:or=\"urn:ietf:params:xml:ns:yang:ietf-origin\" or:origin=\"unknown\">"
        "<interface>"
            "<name>eth1</name>"
            "<type xmlns:ianaift=\"urn:ietf:params:xml:ns:yang:iana-if-type\">ianaift:ethernetCsmacd</type>"
        "</interface>"
    "</interfaces-state>";

    assert_string_equal(str1, str2);
    free(str1);

    /* disconnect, operational data should be removed */
    sr_disconnect(conn);

    /* resd the data again */
    ret = sr_session_switch_ds(st->sess, SR_DS_OPERATIONAL);
    assert_int_equal(ret, SR_ERR_OK);

    ret = sr_get_data(st->sess, "/ietf-interfaces:interfaces-state", 0, 0, SR_OPER_WITH_ORIGIN, &data);
    assert_int_equal(ret, SR_ERR_OK);
    assert_int_equal(data->dflt, 1);

    lyd_free_withsiblings(data);
}

/* TEST 12 */
static void
test_conn_owner2(void **state)
{
    struct state *st = (struct state *)*state;
    struct lyd_node *data;
    sr_conn_ctx_t *conn;
    sr_session_ctx_t *sess;
    char *str1;
    const char *str2;
    int ret;

    /* create another connection and session */
    ret = sr_connect(0, &conn);
    assert_int_equal(ret, SR_ERR_OK);
    ret = sr_session_start(conn, SR_DS_OPERATIONAL, &sess);
    assert_int_equal(ret, SR_ERR_OK);

    /* set some operational data */
    ret = sr_set_item_str(sess, "/ietf-interfaces:interfaces-state/interface[name='eth1']/type",
            "iana-if-type:ethernetCsmacd", NULL, SR_EDIT_STRICT);
    assert_int_equal(ret, SR_ERR_OK);
    ret = sr_set_item_str(sess, "/ietf-interfaces:interfaces-state/interface[name='eth1']/oper-status",
            "up", NULL, SR_EDIT_STRICT);
    assert_int_equal(ret, SR_ERR_OK);
    ret = sr_set_item_str(sess, "/ietf-interfaces:interfaces-state/interface[name='eth1']/speed",
            "1024", NULL, SR_EDIT_STRICT);
    assert_int_equal(ret, SR_ERR_OK);
    ret = sr_apply_changes(sess, 0, 0);
    assert_int_equal(ret, SR_ERR_OK);

    /* read the data */
    ret = sr_get_data(sess, "/ietf-interfaces:interfaces-state", 0, 0, SR_OPER_WITH_ORIGIN, &data);
    assert_int_equal(ret, SR_ERR_OK);

    ret = lyd_print_mem(&str1, data, LYD_XML, LYP_WITHSIBLINGS);
    assert_int_equal(ret, 0);

    lyd_free_withsiblings(data);

    str2 =
    "<interfaces-state xmlns=\"urn:ietf:params:xml:ns:yang:ietf-interfaces\""
        " xmlns:or=\"urn:ietf:params:xml:ns:yang:ietf-origin\" or:origin=\"unknown\">"
        "<interface>"
            "<name>eth1</name>"
            "<type xmlns:ianaift=\"urn:ietf:params:xml:ns:yang:iana-if-type\">ianaift:ethernetCsmacd</type>"
            "<oper-status>up</oper-status>"
            "<speed>1024</speed>"
        "</interface>"
    "</interfaces-state>";

    assert_string_equal(str1, str2);
    free(str1);

    /* set nested oper data owned by another connection */
    ret = sr_session_switch_ds(st->sess, SR_DS_OPERATIONAL);
    assert_int_equal(ret, SR_ERR_OK);

    ret = sr_set_item_str(st->sess, "/ietf-interfaces:interfaces-state/interface[name='eth1']/statistics/discontinuity-time",
            "2019-10-29T09:43:12Z", NULL, SR_EDIT_STRICT);
    assert_int_equal(ret, SR_ERR_OK);
    ret = sr_apply_changes(st->sess, 0, 0);
    assert_int_equal(ret, SR_ERR_OK);

    /* read the data */
    ret = sr_get_data(st->sess, "/ietf-interfaces:interfaces-state", 0, 0, 0, &data);
    assert_int_equal(ret, SR_ERR_OK);

    ret = lyd_print_mem(&str1, data, LYD_XML, LYP_WITHSIBLINGS);
    assert_int_equal(ret, 0);

    lyd_free_withsiblings(data);

    str2 =
    "<interfaces-state xmlns=\"urn:ietf:params:xml:ns:yang:ietf-interfaces\">"
        "<interface>"
            "<name>eth1</name>"
            "<type xmlns:ianaift=\"urn:ietf:params:xml:ns:yang:iana-if-type\">ianaift:ethernetCsmacd</type>"
            "<oper-status>up</oper-status>"
            "<speed>1024</speed>"
            "<statistics>"
                "<discontinuity-time>2019-10-29T09:43:12Z</discontinuity-time>"
            "</statistics>"
        "</interface>"
    "</interfaces-state>";

    assert_string_equal(str1, str2);
    free(str1);

    /* disconnect, some operational data should be removed */
    sr_disconnect(conn);

    /* read the data again */
    ret = sr_get_data(st->sess, "/ietf-interfaces:interfaces-state", 0, 0, SR_OPER_WITH_ORIGIN, &data);
    assert_int_equal(ret, SR_ERR_OK);

    ret = lyd_print_mem(&str1, data, LYD_XML, LYP_WITHSIBLINGS);
    assert_int_equal(ret, 0);

    lyd_free_withsiblings(data);

    str2 =
    "<interfaces-state xmlns=\"urn:ietf:params:xml:ns:yang:ietf-interfaces\""
        " xmlns:or=\"urn:ietf:params:xml:ns:yang:ietf-origin\" or:origin=\"unknown\">"
        "<interface>"
            "<name>eth1</name>"
            "<statistics>"
                "<discontinuity-time>2019-10-29T09:43:12Z</discontinuity-time>"
            "</statistics>"
        "</interface>"
    "</interfaces-state>";

    assert_string_equal(str1, str2);
    free(str1);
}

/* TEST 13 */
static int
oper_change_cb(sr_session_ctx_t *session, const char *module_name, const char *xpath, sr_event_t event,
        uint32_t request_id, void *private_data)
{
    struct state *st = (struct state *)private_data;
    sr_change_oper_t op;
    sr_change_iter_t *iter;
    sr_val_t *old_val, *new_val;
    int ret;

    (void)request_id;

    if (!strcmp(xpath, "/ietf-interfaces:interfaces-state")) {
        assert_string_equal(module_name, "ietf-interfaces");

        if (st->cb_called == 0) {
            assert_int_equal(event, SR_EV_CHANGE);
        } else if (st->cb_called == 1) {
            assert_int_equal(event, SR_EV_DONE);
        } else {
            fail();
        }

        /* get changes iter */
        ret = sr_get_changes_iter(session, "/ietf-interfaces:*//.", &iter);
        assert_int_equal(ret, SR_ERR_OK);

        /* 1st change */
        ret = sr_get_change_next(session, iter, &op, &old_val, &new_val);
        assert_int_equal(ret, SR_ERR_OK);

        assert_int_equal(op, SR_OP_CREATED);
        assert_null(old_val);
        assert_non_null(new_val);
        assert_string_equal(new_val->xpath, "/ietf-interfaces:interfaces-state/interface[name='eth1']");

        sr_free_val(new_val);

        /* 2nd change */
        ret = sr_get_change_next(session, iter, &op, &old_val, &new_val);
        assert_int_equal(ret, SR_ERR_OK);

        assert_int_equal(op, SR_OP_CREATED);
        assert_null(old_val);
        assert_non_null(new_val);
        assert_string_equal(new_val->xpath, "/ietf-interfaces:interfaces-state/interface[name='eth1']/name");

        sr_free_val(new_val);

        /* 3rd change */
        ret = sr_get_change_next(session, iter, &op, &old_val, &new_val);
        assert_int_equal(ret, SR_ERR_OK);

        assert_int_equal(op, SR_OP_CREATED);
        assert_null(old_val);
        assert_non_null(new_val);
        assert_string_equal(new_val->xpath, "/ietf-interfaces:interfaces-state/interface[name='eth1']/type");

        sr_free_val(new_val);

        /* 4th change */
        ret = sr_get_change_next(session, iter, &op, &old_val, &new_val);
        assert_int_equal(ret, SR_ERR_OK);

        assert_int_equal(op, SR_OP_CREATED);
        assert_null(old_val);
        assert_non_null(new_val);
        assert_string_equal(new_val->xpath, "/ietf-interfaces:interfaces-state/interface[name='eth1']/statistics");

        sr_free_val(new_val);

        /* no more changes */
        ret = sr_get_change_next(session, iter, &op, &old_val, &new_val);
        assert_int_equal(ret, SR_ERR_NOT_FOUND);

        sr_free_change_iter(iter);
    } else {
        fail();
    }

    ++st->cb_called;
    if (event == SR_EV_DONE) {
        pthread_barrier_wait(&st->barrier);
    }
    return SR_ERR_OK;
}

static void
test_stored_state(void **state)
{
    struct state *st = (struct state *)*state;
    struct lyd_node *data;
    sr_subscription_ctx_t *subscr;
    char *str1;
    const char *str2;
    int ret;

    /* switch to operational DS */
    ret = sr_session_switch_ds(st->sess, SR_DS_OPERATIONAL);
    assert_int_equal(ret, SR_ERR_OK);

    /* subscribe to operational data changes */
    st->cb_called = 0;
    ret = sr_module_change_subscribe(st->sess, "ietf-interfaces", "/ietf-interfaces:interfaces-state", oper_change_cb,
            st, 0, 0, &subscr);
    assert_int_equal(ret, SR_ERR_OK);

    /* set some operational data */
    ret = sr_set_item_str(st->sess, "/ietf-interfaces:interfaces-state/interface[name='eth1']/type",
            "iana-if-type:ethernetCsmacd", NULL, SR_EDIT_STRICT);
    assert_int_equal(ret, SR_ERR_OK);
    ret = sr_apply_changes(st->sess, 0, 0);
    assert_int_equal(ret, SR_ERR_OK);

    /* callback was called now */
    pthread_barrier_wait(&st->barrier);
    assert_int_equal(st->cb_called, 2);

    /* read the data */
    ret = sr_get_data(st->sess, "/ietf-interfaces:interfaces-state", 0, 0, SR_OPER_WITH_ORIGIN, &data);
    assert_int_equal(ret, SR_ERR_OK);

    ret = lyd_print_mem(&str1, data, LYD_XML, LYP_WITHSIBLINGS);
    assert_int_equal(ret, 0);

    lyd_free_withsiblings(data);

    str2 =
    "<interfaces-state xmlns=\"urn:ietf:params:xml:ns:yang:ietf-interfaces\""
        " xmlns:or=\"urn:ietf:params:xml:ns:yang:ietf-origin\" or:origin=\"unknown\">"
        "<interface>"
            "<name>eth1</name>"
            "<type xmlns:ianaift=\"urn:ietf:params:xml:ns:yang:iana-if-type\">ianaift:ethernetCsmacd</type>"
        "</interface>"
    "</interfaces-state>";

    assert_string_equal(str1, str2);
    free(str1);

    sr_unsubscribe(subscr);
}

/* TEST 14 */
static void
test_stored_config(void **state)
{
    struct state *st = (struct state *)*state;
    struct lyd_node *data;
    sr_subscription_ctx_t *subscr;
    char *str1;
    const char *str2;
    int ret;

    /* set some configuration data */
    ret = sr_set_item_str(st->sess, "/ietf-interfaces:interfaces/interface[name='eth1']/type",
            "iana-if-type:ethernetCsmacd", NULL, SR_EDIT_STRICT);
    assert_int_equal(ret, SR_ERR_OK);
    ret = sr_set_item_str(st->sess, "/ietf-interfaces:interfaces/interface[name='eth1']/description",
            "config-description", NULL, SR_EDIT_STRICT);
    assert_int_equal(ret, SR_ERR_OK);
    ret = sr_set_item_str(st->sess, "/ietf-interfaces:interfaces/interface[name='eth1']/enabled",
            "false", NULL, SR_EDIT_STRICT);
    assert_int_equal(ret, SR_ERR_OK);
    ret = sr_apply_changes(st->sess, 0, 0);
    assert_int_equal(ret, SR_ERR_OK);

    /* subscribe to all configuration data just to enable them */
    ret = sr_module_change_subscribe(st->sess, "ietf-interfaces", "/ietf-interfaces:interfaces", dummy_change_cb, NULL,
            0, 0, &subscr);
    assert_int_equal(ret, SR_ERR_OK);

    /* switch to operational DS */
    ret = sr_session_switch_ds(st->sess, SR_DS_OPERATIONAL);
    assert_int_equal(ret, SR_ERR_OK);

    /* overwrite running data by some operational config data */
    ret = sr_set_item_str(st->sess, "/ietf-interfaces:interfaces/interface[name='eth1']/description",
            "oper-description", NULL, 0);
    assert_int_equal(ret, SR_ERR_OK);
    ret = sr_apply_changes(st->sess, 0, 0);
    assert_int_equal(ret, SR_ERR_OK);

    /* read the operational data */
    ret = sr_get_data(st->sess, "/ietf-interfaces:interfaces", 0, 0, SR_OPER_WITH_ORIGIN, &data);
    assert_int_equal(ret, SR_ERR_OK);

    ret = lyd_print_mem(&str1, data, LYD_XML, LYP_WITHSIBLINGS);
    assert_int_equal(ret, 0);

    lyd_free_withsiblings(data);

    str2 =
    "<interfaces xmlns=\"urn:ietf:params:xml:ns:yang:ietf-interfaces\""
        " xmlns:or=\"urn:ietf:params:xml:ns:yang:ietf-origin\" or:origin=\"intended\">"
        "<interface>"
            "<name>eth1</name>"
            "<type xmlns:ianaift=\"urn:ietf:params:xml:ns:yang:iana-if-type\">ianaift:ethernetCsmacd</type>"
            "<description or:origin=\"unknown\">oper-description</description>"
            "<enabled>false</enabled>"
        "</interface>"
    "</interfaces>";

    assert_string_equal(str1, str2);
    free(str1);

    /* delete the interface */
    ret = sr_session_switch_ds(st->sess, SR_DS_RUNNING);
    assert_int_equal(ret, SR_ERR_OK);
    ret = sr_delete_item(st->sess, "/ietf-interfaces:interfaces/interface[name='eth1']", SR_EDIT_STRICT);
    assert_int_equal(ret, SR_ERR_OK);
    ret = sr_apply_changes(st->sess, 0, 0);
    assert_int_equal(ret, SR_ERR_OK);

    /* there should be no operational data then */
    ret = sr_session_switch_ds(st->sess, SR_DS_OPERATIONAL);
    assert_int_equal(ret, SR_ERR_OK);
    ret = sr_get_data(st->sess, "/ietf-interfaces:interfaces", 0, 0, SR_OPER_WITH_ORIGIN, &data);
    assert_int_equal(ret, SR_ERR_OK);
    assert_int_equal(data->dflt, 1);
    lyd_free_withsiblings(data);

    /* it should not be possible to delete a non-existing node just like in conventional datastores */
    ret = sr_delete_item(st->sess, "/ietf-interfaces:interfaces/interface[name='eth1']", SR_EDIT_STRICT);
    assert_int_equal(ret, SR_ERR_OK);
    ret = sr_apply_changes(st->sess, 0, 0);
    assert_int_equal(ret, SR_ERR_NOT_FOUND);
    ret = sr_discard_changes(st->sess);
    assert_int_equal(ret, SR_ERR_OK);

    sr_unsubscribe(subscr);
}

/* TEST 15 */
static void
test_stored_diff_merge_leaf(void **state)
{
    struct state *st = (struct state *)*state;
    struct lyd_node *data;
    char *str1;
    const char *str2;
    int ret;

    /* switch to operational DS */
    ret = sr_session_switch_ds(st->sess, SR_DS_OPERATIONAL);
    assert_int_equal(ret, SR_ERR_OK);

    /* set some operational data */
    ret = sr_set_item_str(st->sess, "/ietf-interfaces:interfaces/interface[name='eth1']/type",
            "iana-if-type:ethernetCsmacd", NULL, SR_EDIT_STRICT);
    assert_int_equal(ret, SR_ERR_OK);
    ret = sr_set_item_str(st->sess, "/ietf-interfaces:interfaces/interface[name='eth1']/description",
            "oper-description", NULL, SR_EDIT_STRICT);
    assert_int_equal(ret, SR_ERR_OK);
    ret = sr_set_item_str(st->sess, "/ietf-interfaces:interfaces/interface[name='eth1']/enabled",
            "false", NULL, SR_EDIT_STRICT);
    assert_int_equal(ret, SR_ERR_OK);
    ret = sr_apply_changes(st->sess, 0, 0);
    assert_int_equal(ret, SR_ERR_OK);

    /* read the data */
    ret = sr_get_data(st->sess, "/ietf-interfaces:interfaces", 0, 0, SR_OPER_WITH_ORIGIN, &data);
    assert_int_equal(ret, SR_ERR_OK);
    ret = lyd_print_mem(&str1, data, LYD_XML, LYP_WITHSIBLINGS);
    assert_int_equal(ret, 0);
    lyd_free_withsiblings(data);

    str2 =
    "<interfaces xmlns=\"urn:ietf:params:xml:ns:yang:ietf-interfaces\""
        " xmlns:or=\"urn:ietf:params:xml:ns:yang:ietf-origin\" or:origin=\"intended\">"
        "<interface>"
            "<name>eth1</name>"
            "<type or:origin=\"unknown\" xmlns:ianaift=\"urn:ietf:params:xml:ns:yang:iana-if-type\">ianaift:ethernetCsmacd</type>"
            "<description or:origin=\"unknown\">oper-description</description>"
            "<enabled or:origin=\"unknown\">false</enabled>"
        "</interface>"
    "</interfaces>";
    assert_string_equal(str1, str2);
    free(str1);

    /* set some other operational data, should be merged with the previous data */
    ret = sr_set_item_str(st->sess, "/ietf-interfaces:interfaces/interface[name='eth1']/description",
            "oper-description2", NULL, 0);
    assert_int_equal(ret, SR_ERR_OK);
    ret = sr_apply_changes(st->sess, 0, 0);
    assert_int_equal(ret, SR_ERR_OK);

    /* read the data */
    ret = sr_get_data(st->sess, "/ietf-interfaces:interfaces", 0, 0, 0, &data);
    assert_int_equal(ret, SR_ERR_OK);
    ret = lyd_print_mem(&str1, data, LYD_XML, LYP_WITHSIBLINGS);
    assert_int_equal(ret, 0);
    lyd_free_withsiblings(data);

    str2 =
    "<interfaces xmlns=\"urn:ietf:params:xml:ns:yang:ietf-interfaces\">"
        "<interface>"
            "<name>eth1</name>"
            "<type xmlns:ianaift=\"urn:ietf:params:xml:ns:yang:iana-if-type\">ianaift:ethernetCsmacd</type>"
            "<description>oper-description2</description>"
            "<enabled>false</enabled>"
        "</interface>"
    "</interfaces>";
    assert_string_equal(str1, str2);
    free(str1);

    /* set some other operational data, should be merged with the previous data */
    ret = sr_delete_item(st->sess, "/ietf-interfaces:interfaces/interface[name='eth1']/enabled", 0);
    assert_int_equal(ret, SR_ERR_OK);
    ret = sr_apply_changes(st->sess, 0, 0);
    assert_int_equal(ret, SR_ERR_OK);

    /* read the data */
    ret = sr_get_data(st->sess, "/ietf-interfaces:interfaces", 0, 0, SR_OPER_WITH_ORIGIN, &data);
    assert_int_equal(ret, SR_ERR_OK);
    ret = lyd_print_mem(&str1, data, LYD_XML, LYP_WITHSIBLINGS);
    assert_int_equal(ret, 0);
    lyd_free_withsiblings(data);

    str2 =
    "<interfaces xmlns=\"urn:ietf:params:xml:ns:yang:ietf-interfaces\""
        " xmlns:or=\"urn:ietf:params:xml:ns:yang:ietf-origin\" or:origin=\"intended\">"
        "<interface>"
            "<name>eth1</name>"
            "<type or:origin=\"unknown\" xmlns:ianaift=\"urn:ietf:params:xml:ns:yang:iana-if-type\">ianaift:ethernetCsmacd</type>"
            "<description or:origin=\"unknown\">oper-description2</description>"
        "</interface>"
    "</interfaces>";
    assert_string_equal(str1, str2);
    free(str1);
}

/* TEST 16 */
static void
test_stored_diff_merge_replace(void **state)
{
    struct state *st = (struct state *)*state;
    struct lyd_node *data;
    sr_subscription_ctx_t *subscr;
    char *str1;
    const char *str2;
    int ret;

    /* set some running data */
    ret = sr_set_item_str(st->sess, "/ietf-interfaces:interfaces/interface[name='eth1']/type",
            "iana-if-type:ethernetCsmacd", NULL, SR_EDIT_STRICT);
    assert_int_equal(ret, SR_ERR_OK);
    ret = sr_apply_changes(st->sess, 0, 0);
    assert_int_equal(ret, SR_ERR_OK);

    /* subscribe to all configuration data just to enable them */
    ret = sr_module_change_subscribe(st->sess, "ietf-interfaces", "/ietf-interfaces:interfaces", dummy_change_cb, NULL,
            0, 0, &subscr);
    assert_int_equal(ret, SR_ERR_OK);

    /* switch to operational DS */
    ret = sr_session_switch_ds(st->sess, SR_DS_OPERATIONAL);
    assert_int_equal(ret, SR_ERR_OK);

    /* set some operational data */
    ret = sr_set_item_str(st->sess, "/ietf-interfaces:interfaces/interface[name='eth1']/enabled", "false", NULL, 0);
    assert_int_equal(ret, SR_ERR_OK);
    ret = sr_apply_changes(st->sess, 0, 0);
    assert_int_equal(ret, SR_ERR_OK);

    /* read the data */
    ret = sr_get_data(st->sess, "/ietf-interfaces:interfaces", 0, 0, SR_OPER_WITH_ORIGIN, &data);
    assert_int_equal(ret, SR_ERR_OK);
    ret = lyd_print_mem(&str1, data, LYD_XML, LYP_WITHSIBLINGS);
    assert_int_equal(ret, 0);
    lyd_free_withsiblings(data);

    str2 =
    "<interfaces xmlns=\"urn:ietf:params:xml:ns:yang:ietf-interfaces\""
        " xmlns:or=\"urn:ietf:params:xml:ns:yang:ietf-origin\" or:origin=\"intended\">"
        "<interface>"
            "<name>eth1</name>"
            "<type xmlns:ianaift=\"urn:ietf:params:xml:ns:yang:iana-if-type\">ianaift:ethernetCsmacd</type>"
            "<enabled or:origin=\"unknown\">false</enabled>"
        "</interface>"
    "</interfaces>";
    assert_string_equal(str1, str2);
    free(str1);

    /* set some other operational data to be merged */
    data = lyd_new_path(NULL, sr_get_context(st->conn), "/ietf-interfaces:interfaces/interface[name='eth5']/type",
            "iana-if-type:ethernetCsmacd", 0, 0);
    assert_non_null(data);
    ret = sr_edit_batch(st->sess, data, "replace");
    lyd_free_withsiblings(data);
    assert_int_equal(ret, SR_ERR_OK);
    ret = sr_apply_changes(st->sess, 0, 0);
    assert_int_equal(ret, SR_ERR_OK);

    /* read the data */
    ret = sr_get_data(st->sess, "/ietf-interfaces:interfaces", 0, 0, 0, &data);
    assert_int_equal(ret, SR_ERR_OK);
    ret = lyd_print_mem(&str1, data, LYD_XML, LYP_WITHSIBLINGS);
    assert_int_equal(ret, 0);
    lyd_free_withsiblings(data);

    str2 =
    "<interfaces xmlns=\"urn:ietf:params:xml:ns:yang:ietf-interfaces\">"
        "<interface>"
            "<name>eth5</name>"
            "<type xmlns:ianaift=\"urn:ietf:params:xml:ns:yang:iana-if-type\">ianaift:ethernetCsmacd</type>"
        "</interface>"
    "</interfaces>";
    assert_string_equal(str1, str2);
    free(str1);

    /* set some other operational data to be merged */
    ret = sr_set_item_str(st->sess, "/ietf-interfaces:interfaces/interface[name='eth1']/enabled", "true", NULL, 0);
    assert_int_equal(ret, SR_ERR_OK);
    ret = sr_apply_changes(st->sess, 0, 0);
    assert_int_equal(ret, SR_ERR_OK);

    /* read the data */
    ret = sr_get_data(st->sess, "/ietf-interfaces:interfaces", 0, 0, SR_OPER_WITH_ORIGIN, &data);
    assert_int_equal(ret, SR_ERR_OK);
    ret = lyd_print_mem(&str1, data, LYD_XML, LYP_WITHSIBLINGS);
    assert_int_equal(ret, 0);
    lyd_free_withsiblings(data);

    str2 =
    "<interfaces xmlns=\"urn:ietf:params:xml:ns:yang:ietf-interfaces\""
        " xmlns:or=\"urn:ietf:params:xml:ns:yang:ietf-origin\" or:origin=\"intended\">"
        "<interface>"
            "<name>eth1</name>"
            "<enabled or:origin=\"unknown\">true</enabled>"
        "</interface>"
        "<interface or:origin=\"unknown\">"
            "<name>eth5</name>"
            "<type xmlns:ianaift=\"urn:ietf:params:xml:ns:yang:iana-if-type\">ianaift:ethernetCsmacd</type>"
        "</interface>"
    "</interfaces>";
    assert_string_equal(str1, str2);
    free(str1);

    sr_unsubscribe(subscr);
}

/* TEST 17 */
static void
test_stored_diff_merge_userord(void **state)
{
    struct state *st = (struct state *)*state;
    struct lyd_node *data;
    sr_subscription_ctx_t *subscr;
    char *str1;
    const char *str2;
    int ret;

    /* set some running data */
    ret = sr_set_item_str(st->sess, "/test:cont/l2[k='key1']/v", "25", NULL, SR_EDIT_STRICT);
    assert_int_equal(ret, SR_ERR_OK);
    ret = sr_set_item_str(st->sess, "/test:cont/l2[k='key2']/v", "26", NULL, SR_EDIT_STRICT);
    assert_int_equal(ret, SR_ERR_OK);
    ret = sr_apply_changes(st->sess, 0, 0);
    assert_int_equal(ret, SR_ERR_OK);

    /* subscribe to all configuration data just to enable them */
    ret = sr_module_change_subscribe(st->sess, "test", "/test:cont", dummy_change_cb, NULL, 0, 0, &subscr);
    assert_int_equal(ret, SR_ERR_OK);

    /* switch to operational DS */
    ret = sr_session_switch_ds(st->sess, SR_DS_OPERATIONAL);
    assert_int_equal(ret, SR_ERR_OK);

    /* set some operational data (move list and create list) */
    ret = sr_move_item(st->sess, "/test:cont/l2[k='key2']", SR_MOVE_BEFORE, "[k='key1']", NULL, NULL, 0);
    assert_int_equal(ret, SR_ERR_OK);
    ret = sr_set_item_str(st->sess, "/test:cont/l2[k='key3']/v", "27", NULL, SR_EDIT_STRICT);
    assert_int_equal(ret, SR_ERR_OK);
    ret = sr_apply_changes(st->sess, 0, 0);
    assert_int_equal(ret, SR_ERR_OK);

    /* read the data */
    ret = sr_get_data(st->sess, "/test:cont", 0, 0, SR_OPER_WITH_ORIGIN, &data);
    assert_int_equal(ret, SR_ERR_OK);
    ret = lyd_print_mem(&str1, data, LYD_XML, LYP_WITHSIBLINGS);
    assert_int_equal(ret, 0);
    lyd_free_withsiblings(data);

    str2 =
    "<cont xmlns=\"urn:test\" xmlns:or=\"urn:ietf:params:xml:ns:yang:ietf-origin\" or:origin=\"intended\">"
        "<l2 or:origin=\"unknown\">"
            "<k>key2</k>"
            "<v>26</v>"
        "</l2>"
        "<l2>"
            "<k>key1</k>"
            "<v>25</v>"
        "</l2>"
        "<l2 or:origin=\"unknown\">"
            "<k>key3</k>"
            "<v>27</v>"
        "</l2>"
    "</cont>";
    assert_string_equal(str1, str2);
    free(str1);

    /* merge some operational data (merge move into move) */
    ret = sr_move_item(st->sess, "/test:cont/l2[k='key2']", SR_MOVE_AFTER, "[k='key1']", NULL, "learned", 0);
    assert_int_equal(ret, SR_ERR_OK);
    ret = sr_set_item_str(st->sess, "/test:cont/l2[k='key2']/v", "20", NULL, 0);
    assert_int_equal(ret, SR_ERR_OK);
    ret = sr_apply_changes(st->sess, 0, 0);
    assert_int_equal(ret, SR_ERR_OK);

    /* read the data */
    ret = sr_get_data(st->sess, "/test:cont", 0, 0, SR_OPER_WITH_ORIGIN, &data);
    assert_int_equal(ret, SR_ERR_OK);
    ret = lyd_print_mem(&str1, data, LYD_XML, LYP_WITHSIBLINGS);
    assert_int_equal(ret, 0);
    lyd_free_withsiblings(data);

    str2 =
    "<cont xmlns=\"urn:test\" xmlns:or=\"urn:ietf:params:xml:ns:yang:ietf-origin\" or:origin=\"intended\">"
        "<l2>"
            "<k>key1</k>"
            "<v>25</v>"
        "</l2>"
        "<l2 or:origin=\"unknown\">"
            "<k>key3</k>"
            "<v>27</v>"
        "</l2>"
        "<l2 or:origin=\"learned\">"
            "<k>key2</k>"
            "<v or:origin=\"unknown\">20</v>"
        "</l2>"
    "</cont>";
    assert_string_equal(str1, str2);
    free(str1);

    /* merge some operational data (merge move into none) */
    ret = sr_move_item(st->sess, "/test:cont/l2[k='key2']", SR_MOVE_BEFORE, "[k='key1']", NULL, NULL, 0);
    assert_int_equal(ret, SR_ERR_OK);
    ret = sr_apply_changes(st->sess, 0, 0);
    assert_int_equal(ret, SR_ERR_OK);

    /* read the data */
    ret = sr_get_data(st->sess, "/test:cont", 0, 0, 0, &data);
    assert_int_equal(ret, SR_ERR_OK);
    ret = lyd_print_mem(&str1, data, LYD_XML, LYP_WITHSIBLINGS);
    assert_int_equal(ret, 0);
    lyd_free_withsiblings(data);

    str2 =
    "<cont xmlns=\"urn:test\">"
        "<l2>"
            "<k>key2</k>"
            "<v>20</v>"
        "</l2>"
        "<l2>"
            "<k>key1</k>"
            "<v>25</v>"
        "</l2>"
        "<l2>"
            "<k>key3</k>"
            "<v>27</v>"
        "</l2>"
    "</cont>";
    assert_string_equal(str1, str2);
    free(str1);

    /* merge some operational data (merge move into create) */
    ret = sr_move_item(st->sess, "/test:cont/l2[k='key3']", SR_MOVE_BEFORE, "[k='key2']", NULL, NULL, 0);
    assert_int_equal(ret, SR_ERR_OK);
    ret = sr_apply_changes(st->sess, 0, 0);
    assert_int_equal(ret, SR_ERR_OK);

    /* read the data */
    ret = sr_get_data(st->sess, "/test:cont", 0, 0, SR_OPER_WITH_ORIGIN, &data);
    assert_int_equal(ret, SR_ERR_OK);
    ret = lyd_print_mem(&str1, data, LYD_XML, LYP_WITHSIBLINGS);
    assert_int_equal(ret, 0);
    lyd_free_withsiblings(data);

    str2 =
    "<cont xmlns=\"urn:test\" xmlns:or=\"urn:ietf:params:xml:ns:yang:ietf-origin\" or:origin=\"intended\">"
        "<l2 or:origin=\"unknown\">"
            "<k>key3</k>"
            "<v>27</v>"
        "</l2>"
        "<l2 or:origin=\"unknown\">"
            "<k>key2</k>"
            "<v>20</v>"
        "</l2>"
        "<l2>"
            "<k>key1</k>"
            "<v>25</v>"
        "</l2>"
    "</cont>";
    assert_string_equal(str1, str2);
    free(str1);

    sr_unsubscribe(subscr);
}

/* TEST 18 */
static void
test_default_when(void **state)
{
    struct state *st = (struct state *)*state;
    struct lyd_node *data;
    char *str1;
    const char *str2;
    int ret;

    /* switch to operational DS */
    ret = sr_session_switch_ds(st->sess, SR_DS_OPERATIONAL);
    assert_int_equal(ret, SR_ERR_OK);

    /* read the operational data */
    ret = sr_get_data(st->sess, "/act:*", 0, 0, 0, &data);
    assert_int_equal(ret, SR_ERR_OK);
    ret = lyd_print_mem(&str1, data, LYD_XML, LYP_WITHSIBLINGS | LYP_KEEPEMPTYCONT | LYP_WD_ALL);
    assert_int_equal(ret, 0);
    lyd_free_withsiblings(data);

    str2 =
    "<basics xmlns=\"urn:act\">"
        "<subbasics>"
            "<complex_number xmlns=\"urn:act2\"/>"
        "</subbasics>"
    "</basics>"
    "<advanced xmlns=\"urn:act\"/>";
    assert_string_equal(str1, str2);
    free(str1);
}

int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_teardown(test_enabled_partial, clear_up),
        cmocka_unit_test_teardown(test_simple, clear_up),
        cmocka_unit_test_teardown(test_fail, clear_up),
        cmocka_unit_test_teardown(test_config, clear_up),
        cmocka_unit_test_teardown(test_list, clear_up),
        cmocka_unit_test_teardown(test_nested, clear_up),
        cmocka_unit_test_teardown(test_mixed, clear_up),
        cmocka_unit_test_teardown(test_xpath_check, clear_up),
        cmocka_unit_test_teardown(test_state_only, clear_up),
        cmocka_unit_test_teardown(test_config_only, clear_up),
        cmocka_unit_test_teardown(test_conn_owner1, clear_up),
        cmocka_unit_test_teardown(test_conn_owner2, clear_up),
        cmocka_unit_test_teardown(test_stored_state, clear_up),
        cmocka_unit_test_teardown(test_stored_config, clear_up),
        cmocka_unit_test_teardown(test_stored_diff_merge_leaf, clear_up),
        cmocka_unit_test_teardown(test_stored_diff_merge_replace, clear_up),
        cmocka_unit_test_teardown(test_stored_diff_merge_userord, clear_up),
        cmocka_unit_test(test_default_when),
    };

    setenv("CMOCKA_TEST_ABORT", "1", 1);
    sr_log_stderr(SR_LL_INF);
    return cmocka_run_group_tests(tests, setup, teardown);
}
