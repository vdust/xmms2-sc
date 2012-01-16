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

typedef struct {
	xmmsc_connection_t *conn;
	gint cid;
	xmmsv_t *ns;
} introspect_data_t;
static introspect_data_t *introspect_data_new (xmmsc_connection_t *conn, gint cid, xmmsv_t *parent_ns, const char *ns);
static void introspect_data_free (introspect_data_t *id);

static int c2c_connected_cb (xmmsv_t *val, xmmsc_connection_t *conn);
static int c2c_disconnected_cb (xmmsv_t *val, xmmsc_connection_t *conn);
static int c2c_message_cb (xmmsv_t *val, xmmsc_connection_t *conn);
static int get_connected_cb (xmmsv_t *val, xmmsc_connection_t *conn);

static int namespace_introspect_cb (xmmsv_t *val, introspect_data_t *id);

static void query_introspection (xmmsc_connection_t *conn, gint cid, xmmsv_t *parent_ns, const char *ns);
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

static xmmsv_t *
string_list_copy (xmmsv_t *list)
{
	xmmsv_t *newlist = xmmsv_new_list();
	xmmsv_list_iter_t *it;
	const char *str;

	if (list && xmmsv_get_list_iter (list, &it)) {
		while (xmmsv_list_iter_valid (it)) {
			xmmsv_list_iter_entry_string (it, &str);
			xmmsv_list_append_string (newlist, str);
			xmmsv_list_iter_next (it);
		}
	}

	return newlist;
}

static introspect_data_t *
introspect_data_new (xmmsc_connection_t *conn, gint cid, xmmsv_t *parent_ns, const char *ns)
{
	introspect_data_t *id;

	id = g_slice_new (introspect_data_t);
	id->conn = conn;
	id->cid = cid;
	id->ns = string_list_copy (parent_ns); /* create a new list even if parent_ns == NULL */

	if (ns) {
		xmmsv_list_append_string (id->ns, ns);
	}

	return id;
}

static void
introspect_data_free (introspect_data_t *id)
{
	if (id) {
		xmmsv_unref (id->ns);
		id->conn = NULL;
		id->cid = 0;
		id->ns = NULL;
		g_slice_free (introspect_data_t, id);
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
	return !xmmsv_is_type(val, XMMSV_TYPE_ERROR);
}

static void
query_introspection (xmmsc_connection_t *conn, gint cid, xmmsv_t *parent_ns, const char *ns)
{
	introspect_data_t *id = introspect_data_new (conn, cid, parent_ns, ns);
	if (id) {
		CALLBACK_SET_ARGS (conn, xmmsc_sc_namespace_introspect, namespace_introspect_cb, id, cid, id->ns);
	}
}

static int
namespace_introspect_cb (xmmsv_t *val, introspect_data_t *id)
{
	gint sender;
	xmmsv_t *payload, *bcs, *bc, *path, *nss;
	const char *bcName, *ns;
	gint i;

	g_print ("@ ");
	xmmsv_dump (val);

	if (!xmmsv_dict_entry_get_int (val, "sender", &sender)) goto cleanup;
	if (!xmmsv_dict_get (val, "payload", &payload)) goto cleanup;
	if (!xmmsv_dict_get (payload, "broadcasts", &bcs)) goto cleanup;
	i = xmmsv_list_get_size (bcs);
	for (; i > 0; --i) {
		if (!xmmsv_list_get (bcs, i - 1, &bc)) continue;
		if (!xmmsv_dict_entry_get_string (bc, "name", &bcName)) continue;
		if (!bcName || bcName[0] == '\0') continue;
		path = string_list_copy (id->ns);
		xmmsv_list_append_string (path, bcName);
		CALLBACK_SET_ARGS (id->conn, xmmsc_sc_broadcast_subscribe, sc_broadcast_cb, id->conn, sender, path);
		xmmsv_unref (path);
	}
	if (xmmsv_dict_get (payload, "namespaces", &nss)) {
		i = xmmsv_list_get_size (nss);
		for (; i > 0; --i) {
			if (!xmmsv_list_get_string (nss, i - 1, &ns)) continue;
			query_introspection (id->conn, id->cid, id->ns, ns);
		}
	}
	
cleanup:
	introspect_data_free (id);

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
	while ((cid = pop_id()) > 0) {
		query_introspection (conn, cid, NULL, NULL);
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
