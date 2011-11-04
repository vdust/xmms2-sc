#include <glib.h>

#include <xmmsclient/xmmsclient.h>
#include <xmmsclient/xmmsclient-glib.h>

extern char *xc_clientname;

void xc_setup_preconnect (xmmsc_connection_t *conn);
void xc_setup (GMainLoop *ml, xmmsc_connection_t *conn);
void xc_cleanup (GMainLoop *ml, xmmsc_connection_t *conn);
