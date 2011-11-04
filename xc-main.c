#include "xc-common.h"

static void
disconnect_cb (GMainLoop *ml)
{
	g_main_loop_quit (ml);
}

int main ()
{
	xmmsc_connection_t *conn;
	GMainLoop *ml;
	void *glib_conn;

	conn = xmmsc_init (xc_clientname);

	if (!xmmsc_connect (conn, NULL)) {
		xmmsc_unref (conn);
		g_error ("Failed to connect to xmms2 daemon.");
	}

	ml = g_main_loop_new(NULL, FALSE);
	xmmsc_disconnect_callback_set (conn, (xmmsc_disconnect_func_t) disconnect_cb, ml);

	glib_conn = xmmsc_mainloop_gmain_init (conn);

	xc_setup (ml, conn);

	g_main_loop_run (ml);

	xc_cleanup (ml, conn);

	xmmsc_mainloop_gmain_shutdown (conn, glib_conn);
	xmmsc_unref (conn);
	g_main_loop_unref (ml);

	return 0;
}
