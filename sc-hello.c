#include "xc-common.h"

char *xc_clientname = "sc-hello";

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

	xmmsv_unref (v);
	xmmsv_unref (bc);

	return TRUE;
}

void
xc_setup (GMainLoop *ml, xmmsc_connection_t *conn)
{
	xmmsc_sc_namespace_t *rootns;
	xmmsv_t *v;

	rootns = xmmsc_sc_init (conn);

	/* rootns = xmmsc_sc_namespace_root (conn); */

	v = xmmsv_new_string ("Hello, world!");
	xmmsc_sc_namespace_add_constant (rootns, "constHello", v);
	xmmsv_unref (v);

	METHOD_NEW_NOARG (rootns, method_Hello, "Hello", "Say hello to the world", NULL);

	xmmsc_sc_namespace_add_broadcast (rootns, "bcHello", "Keep saying hello to the world");

	g_timeout_add (3000, (GSourceFunc) say_hello_timeout, conn);
}

void
xc_cleanup (GMainLoop *ml, xmmsc_connection_t *conn)
{
	return;
}
