#include "xc-common.h"

char *xc_clientname = "sc-hello";

#define METHOD_NEW_NOARG(c, ns, meth, name, doc, ud) \
	xmmsc_sc_method_new (c, ns, meth, name, doc, NULL, NULL, FALSE, FALSE, ud)

static xmmsv_t *
method_Hello (xmmsv_t *pargs, xmmsv_t *nargs, void *udata)
{
	xmmsv_t *v;
	g_message("%s", "Hello, world!");
	v = xmmsv_new_string ("Hello, world!");
	return v;
}
static void
register_Hello (xmmsc_connection_t *conn, void *udata)
{
	METHOD_NEW_NOARG (conn, NULL, method_Hello, "Hello", "Say hello to the world", NULL);
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
	/* xmmsv_t *v; */

	xmmsc_sc_setup (conn);

	/* v = xmmsv_new_string ("Hello, world!"); */
	/* xmmsc_sc_namespace_add_constant (NULL, "hello", v); */

	register_Hello (conn, NULL);

	xmmsc_sc_broadcast_new (conn, NULL, "bcHello", "Keep saying hello to the world");

	g_timeout_add (3000, (GSourceFunc) say_hello_timeout, conn);
}

void
xc_cleanup (GMainLoop *ml, xmmsc_connection_t *conn)
{
	return;
}
