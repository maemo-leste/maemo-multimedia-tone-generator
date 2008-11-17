#define _GNU_SOURCE

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <log/log.h>
#include <trace/trace.h>

#include "tonegend.h"
#include "dbusif.h"

#define LOG_ERROR(f, args...) log_error(logctx, f, ##args)
#define LOG_INFO(f, args...) log_error(logctx, f, ##args)
#define LOG_WARNING(f, args...) log_error(logctx, f, ##args)

#define TRACE(f, args...) trace_write(trctx, trflags, trkeys, f, ##args)

static DBusHandlerResult handle_message(DBusConnection *,DBusMessage *,void *);
static gchar *create_key(gchar *, gchar *, gchar *);
static void destroy_key(gpointer);


static char *path    = "/com/Nokia/Telephony/Tones";
static char *service = "com.Nokia.Telephony.Tones";

int dbusif_init(int argc, char **argv)
{
    return 0;
}

struct dbusif *dbusif_create(struct tonegend *tonegend)
{
    static struct DBusObjectPathVTable method = {
        .message_function = &handle_message
    };

    struct dbusif   *dbusif = NULL;
    DBusConnection  *conn   = NULL;
    GHashTable      *hash   = NULL;
    DBusError        err;
    int              ret;

    if ((dbusif = (struct dbusif *)malloc(sizeof(*dbusif))) == NULL) {
        LOG_ERROR("%s(): Can't allocate memory", __FUNCTION__);
        goto failed;
    }
    memset(dbusif, 0, sizeof(*dbusif));

    dbus_error_init(&err);

    if ((conn = dbus_bus_get(DBUS_BUS_SYSTEM, &err)) == NULL) {
        LOG_ERROR("%s(): Can't connect to D-Bus daemon: %s",
                  __FUNCTION__, err.message);
        dbus_error_free(&err);
        goto failed;
    }

    dbus_connection_setup_with_g_main(conn, NULL);

    if (!dbus_connection_register_object_path(conn, path, &method, dbusif)) {
        LOG_ERROR("%s(): failed to register object path", __FUNCTION__);
        goto failed;
    }

    ret = dbus_bus_request_name(conn, service, DBUS_NAME_FLAG_REPLACE_EXISTING,
                                &err);
    if (ret < 0) {
        LOG_ERROR("%s(): request name failed: %s", __FUNCTION__, err.message);
        dbus_error_free(&err);
        goto failed;
    }
    if (ret != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        LOG_ERROR("%s(): not a primary owner", __FUNCTION__);
        goto failed;
    }

    dbusif->tonegend = tonegend;
    dbusif->conn   = conn;
    dbusif->hash   = g_hash_table_new_full(g_str_hash, g_str_equal,
                                           destroy_key, NULL);
    LOG_INFO("D-Bus setup OK");

    return dbusif;

 failed:
    if (dbusif != NULL)
        free(dbusif);

    return NULL;
}


int dbusif_register_method(struct tonegend *tonegend,
                           char *intf, char *memb, char *sig,
                           int (*method)(DBusMessage *, struct tonegend *))
{
    struct dbusif *dbusif = tonegend->dbus_ctx;
    gchar         *key;

    if (!memb || !sig || !method) {
        LOG_ERROR("%s(): Called with invalid argument(s)", __FUNCTION__);
        errno = EINVAL;
        return -1;
    }

    if (intf == NULL)
        intf = service;

    key = create_key((gchar *)memb, (gchar *)sig, (gchar *)intf);
    g_hash_table_insert(dbusif->hash, key, (gpointer)method);

    return 0;
}


int dbusif_unregister_method(struct tonegend *tonegend, char *intf,
                             char *memb, char *sign)
{
    return 0;
}


static DBusHandlerResult handle_message(DBusConnection *conn,
                                        DBusMessage    *msg,
                                        void           *user_data)
{
    struct dbusif   *dbusif = (struct dbusif *)user_data;
    struct tonegend *tonegend = dbusif->tonegend;
    DBusMessage     *reply  = NULL;
    int              ret;
    int            (*method)(DBusMessage *, struct tonegend *);
    const char      *intf;
    const char      *memb;
    const char      *sig;
    gchar           *key;
    const char      *errname;
    char             errdesc[256];
    int              success;

    if (dbus_message_get_type(msg) != DBUS_MESSAGE_TYPE_METHOD_CALL)
        TRACE("%s(): ignoring non method_call's", __FUNCTION__);
    else {
        intf = dbus_message_get_interface(msg);
        memb = dbus_message_get_member(msg);
        sig  = dbus_message_get_signature(msg);

        TRACE("%s(): message received: '%s', '%s', '%s'",
              __FUNCTION__, intf, memb, sig);
        
        key = create_key((gchar *)memb, (gchar *)sig, (gchar *)intf);
        method = g_hash_table_lookup(dbusif->hash, key);
        g_free(key);

        success = method ? method(msg, tonegend) : FALSE;

        if (success)
            reply = dbus_message_new_method_return(msg);
        else {
            if (method) {
                errname = DBUS_ERROR_FAILED;
                snprintf(errdesc, sizeof(errdesc), "Internal error");
            }
            else {
                errname = DBUS_ERROR_NOT_SUPPORTED;
                snprintf(errdesc, sizeof(errdesc), "Method '%s(%s)' "
                         "not supported", memb, sig);
            }
            reply = dbus_message_new_error(msg, errname, errdesc);
        }
        
        if (!dbus_connection_send(dbusif->conn, reply, NULL))
            LOG_ERROR("%s(): D-Bus message reply failure", __FUNCTION__);

        dbus_message_unref(reply);
    }
    
    return DBUS_HANDLER_RESULT_HANDLED;
}


static gchar *create_key(gchar *memb, gchar *sign, gchar *intf)
{
    gchar *key = NULL;

    if (memb == NULL) memb = "";
    if (sign == NULL) sign = "";
    if (intf == NULL) intf = "";

    key = g_strconcat(memb, "__", sign, "__", intf, NULL);

    return key;
}


static void destroy_key(gpointer key)
{
    g_free(key);
}


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

