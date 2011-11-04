#include "xc-common.h"
#include "utils/value_utils.h"
#include "utils/coll_utils.h"

char *xc_clientname = "sc-watch";

#define CALLBACK_SET(c, m, cb, u) do { \
	xmmsc_result_t *res = m (c); \
	xmmsc_result_notifier_set_raw (res, (xmmsc_result_notifier_t) cb, u); \
	xmmsc_result_unref (res); \
} while (0)
#define CALLBACK_SET_ARGS(c, m, cb, u, ...) do { \
	xmmsc_result_t *res = m (c, ##__VA_ARGS__ ); \
	xmmsc_result_notifier_set_raw (res, (xmmsc_result_notifier_t) cb, u); \
	xmmsc_result_unref (res); \
} while (0)

static int c2c_connected_cb (xmmsv_t *val, xmmsc_connection_t *conn);
static int c2c_disconnected_cb (xmmsv_t *val, xmmsc_connection_t *conn);
static int c2c_message_cb (xmmsv_t *val, xmmsc_connection_t *conn);
static int get_connected_cb (xmmsv_t *val, xmmsc_connection_t *conn);

static int namespace_introspect_cb (xmmsv_t *val, xmmsc_connection_t *conn);

static gboolean query_clients (xmmsc_connection_t *conn);
static gboolean do_introspection (xmmsc_connection_t *conn);

static GList *cid_queue = NULL;
static guint introspection_tm = 0;

static gint
pop_id()
{
	gint i = 0;
	if (cid_queue) {
		i = GPOINTER_TO_INT (cid_queue->data);
		cid_queue = g_list_delete_link (cid_queue, cid_queue);
	}
	return i;
}

static void
push_cids (xmmsv_t *cids, xmmsc_connection_t *conn)
{
	gint i, cid;
	gboolean added = FALSE;
	if (xmmsv_is_type (cids, XMMSV_TYPE_INT32)) {
		if (xmmsv_get_int (cids, &cid) &&
		    !g_list_find (cid_queue, GINT_TO_POINTER(cid))) {
			cid_queue = g_list_prepend (cid_queue, GINT_TO_POINTER(cid));
			added = TRUE;
		}
	} else if (xmmsv_is_type (cids, XMMSV_TYPE_LIST)) {
		i = xmmsv_list_get_size(cids);
		for (; i > 0; --i) {
			if (xmmsv_list_get_int (cids, i - 1, &cid) &&
			    !g_list_find (cid_queue, GINT_TO_POINTER(cid))) {
				cid_queue = g_list_prepend (cid_queue, GINT_TO_POINTER(cid));
				added = TRUE;
			}
		}
	}
	if (added) {
		if (introspection_tm) {
			g_source_remove (introspection_tm);
		}
		introspection_tm = g_timeout_add (1000, (GSourceFunc) do_introspection, conn);
	}
}

static void
pop_cids (xmmsv_t *cids, xmmsc_connection_t *conn)
{
	gint i, cid;
	if (xmmsv_is_type (cids, XMMSV_TYPE_INT32)) {
		if (xmmsv_get_int (cids, &cid)) {
			cid_queue = g_list_remove (cid_queue, GINT_TO_POINTER(cid));
		}
	} else if (xmmsv_is_type (cids, XMMSV_TYPE_LIST)) {
		i = xmmsv_list_get_size(cids);
		for (; i > 0; --i) {
			if (xmmsv_list_get_int (cids, i - 1, &cid)) {
				cid_queue = g_list_remove (cid_queue, GINT_TO_POINTER(cid));
			}
		}
	}
	if (!cid_queue && introspection_tm) {
		g_source_remove (introspection_tm);
		introspection_tm = 0;
	}
}

static int
c2c_connected_cb (xmmsv_t *val, xmmsc_connection_t *conn)
{
	g_print ("+ Client connected: ");
	xmmsv_dump (val);
	push_cids (val, conn);
	return 1;
}

static int
c2c_disconnected_cb (xmmsv_t *val, xmmsc_connection_t *conn)
{
	g_print ("- Client disconnected: ");
	xmmsv_dump (val);
	pop_cids (val, conn);
	return 1;
}

static int
c2c_message_cb (xmmsv_t *val, xmmsc_connection_t *conn)
{
	g_print ("> Received message: ");
	xmmsv_dump (val);
	return 1;
}

static int
get_connected_cb (xmmsv_t *val, xmmsc_connection_t *conn)
{
	g_print ("# Connected clients: ");
	xmmsv_dump (val);
	push_cids (val, conn);
	return 0;
}

static int
sc_broadcast_cb (xmmsv_t *val, xmmsc_connection_t *conn)
{
	g_print ("> Received SC broadcast: ");
	xmmsv_dump (val);
	return 1;
}

static int
namespace_introspect_cb (xmmsv_t *val, xmmsc_connection_t *conn)
{
	gint sender;
	xmmsv_t *payload, *bcs, *bc, *path;
	const char *bcName;
	gint i;

	g_print ("@ ");
	xmmsv_dump (val);

	if (!xmmsv_dict_entry_get_int (val, "sender", &sender)) return 0;
	if (!xmmsv_dict_get (val, "payload", &payload)) return 0;
	if (!xmmsv_dict_get (payload, "broadcasts", &bcs)) return 0;
	i = xmmsv_list_get_size (bcs);
	for (; i > 0; --i) {
		if (!xmmsv_list_get (bcs, i - 1, &bc)) continue;
		if (!xmmsv_dict_entry_get_string (bc, "name", &bcName)) continue;
		if (!bcName || bcName[0] == '\0') continue;
		path = xmmsv_build_list (XMMSV_LIST_ENTRY_STR(bcName), XMMSV_LIST_END);
		CALLBACK_SET_ARGS (conn, xmmsc_sc_broadcast_subscribe, sc_broadcast_cb, conn, sender, path);
		xmmsv_unref (path);
	}
	return 0;
}

static gboolean
query_clients (xmmsc_connection_t *conn)
{
	CALLBACK_SET (conn, xmmsc_c2c_get_connected_clients, get_connected_cb, conn);
	return FALSE;
}

static gboolean
do_introspection (xmmsc_connection_t *conn)
{
	gint cid;
	xmmsv_t *ns;
	while ((cid = pop_id()) > 0) {
		ns = xmmsv_new_list();
		CALLBACK_SET_ARGS (conn, xmmsc_sc_namespace_introspect, namespace_introspect_cb, conn, cid, ns);
		xmmsv_unref (ns);
	}
	introspection_tm = 0;
	return FALSE;
}



void
xc_setup (GMainLoop *ml, xmmsc_connection_t *conn)
{
	CALLBACK_SET (conn, xmmsc_broadcast_c2c_message, c2c_message_cb, conn);
	CALLBACK_SET (conn, xmmsc_broadcast_c2c_client_connected, c2c_connected_cb, conn);
	CALLBACK_SET (conn, xmmsc_broadcast_c2c_client_disconnected, c2c_disconnected_cb, conn);

	g_idle_add ((GSourceFunc) query_clients, conn);
}

void
xc_cleanup (GMainLoop *ml, xmmsc_connection_t *conn) {
	g_list_free (cid_queue);
	cid_queue = NULL;
	g_source_remove (introspection_tm);
	introspection_tm = 0;
	return;
}
