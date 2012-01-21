#include "xc-common.h"

char *xc_clientname = "sc-recurs";

#define METHOD_NEW_NOARG(ns, meth, name, doc, ud) \
	xmmsc_sc_namespace_add_method (ns, meth, name, doc, NULL, NULL, FALSE, FALSE, ud)

static xmmsv_t *
method_Hello (xmmsv_t *pargs, xmmsv_t *nargs, void *udata)
{
	xmmsv_t *v;
	g_message("%s", "Hello, world!");
	v = xmmsv_new_string ("Hello, world!");
	return v;
}

static gboolean
say_hello_timeout (xmmsc_connection_t *conn) {
	xmmsv_t *bc;
	xmmsv_t *v;
	bc = xmmsv_build_list (XMMSV_LIST_ENTRY_STR("bcHello"), XMMSV_LIST_END);
	v = xmmsv_new_string ("Hello, world!");
	xmmsc_sc_broadcast_emit (conn, bc, v);
	xmmsv_unref (bc);
	xmmsv_unref (v);
	return TRUE;
}

static gboolean
summon_timeout (xmmsc_connection_t *conn) {
	static int step = 0;
	xmmsv_t *bc = NULL;
	xmmsv_t *v = NULL;

	step ++;

	if (step == -3) {
		v = xmmsv_new_string ("“Mephisto is unreachable right now. Try again later.”");
	} else if (step > 0) {
		v = xmmsv_new_int (6);
		if (step == 3) step = -5;
	}

	if (v) {
		bc = xmmsv_build_list (XMMSV_LIST_ENTRY_STR("evil"),
		                       XMMSV_LIST_ENTRY_STR("bcSummon"),
		                       XMMSV_LIST_END);

		xmmsc_sc_broadcast_emit (conn, bc, v);

		xmmsv_unref (v);
		xmmsv_unref (bc);
	}

	return TRUE;
}

void
xc_setup (GMainLoop *ml, xmmsc_connection_t *conn)
{
	xmmsc_sc_namespace_t *rootns;
	xmmsc_sc_namespace_t *ns;
	xmmsv_t *v;

	xmmsc_sc_setup (conn);

	rootns = xmmsc_sc_namespace_root (conn);

	v = xmmsv_new_string ("Hello, world!");
	xmmsc_sc_namespace_add_constant (rootns, "constHello", v);
	xmmsv_unref (v);

	METHOD_NEW_NOARG (rootns, method_Hello, "Hello", "Say hello to the world", NULL);

	xmmsc_sc_namespace_add_broadcast (rootns, "bcHello", "Keep saying hello to the world");

	ns = xmmsc_sc_namespace_new (rootns, "evil", "Hell's gate");
	v = xmmsv_new_int (666);
	xmmsc_sc_namespace_add_constant (ns, "number", v);
	xmmsv_unref (v);
	xmmsc_sc_namespace_add_broadcast (ns, "bcSummon", "Hell's summon ritual");

	g_timeout_add (3000, (GSourceFunc) say_hello_timeout, conn);
	g_timeout_add (0x666, (GSourceFunc) summon_timeout, conn);
}

void
xc_cleanup (GMainLoop *ml, xmmsc_connection_t *conn)
{
	return;
}
