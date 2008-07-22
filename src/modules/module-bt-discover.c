/***
    This file is part of PulseAudio.

    Copyright 2008 Joao Paulo Rechi Vita

    PulseAudio is free software; you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published
    by the Free Software Foundation; either version 2 of the License,
    or (at your option) any later version.

    PulseAudio is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
    General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with PulseAudio; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
    USA.
***/

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pulse/xmalloc.h>
#include <pulsecore/module.h>
#include <pulsecore/modargs.h>
#include <pulsecore/macro.h>

#include "dbus-util.h"
#include "module-bt-discover-symdef.h"

PA_MODULE_AUTHOR("Joao Paulo Rechi Vita");
PA_MODULE_DESCRIPTION("Detect available bluetooth audio devices and load bluetooth audio drivers");
PA_MODULE_VERSION(PACKAGE_VERSION);
PA_MODULE_USAGE("");

#define HSP_HS_UUID             "00001108-0000-1000-8000-00805F9B34FB"
#define HFP_HS_UUID             "0000111E-0000-1000-8000-00805F9B34FB"
#define A2DP_SOURCE_UUID        "0000110A-0000-1000-8000-00805F9B34FB"
#define A2DP_SINK_UUID          "0000110B-0000-1000-8000-00805F9B34FB"

typedef struct adapter adapter_t;
typedef struct device device_t;
typedef struct uuid uuid_t;

struct uuid {
    char *uuid;
    uuid_t *next;
};

struct device {
    char *name;
    char *object_path;
    int paired;
    adapter_t *adapter;
    char *alias;
    int connected;
    uuid_t *uuid_list;
    char *address;
    int class;
    int trusted;
    device_t *next;
};

struct adapter {
    char *object_path;
    char *mode;
    char *address;
    device_t *device_list;
    adapter_t *next;
};

struct userdata {
    pa_module *module;
    pa_dbus_connection *conn;
    adapter_t *adapter_list;
};

static uuid_t *uuid_new(const char *uuid) {
    uuid_t *node = pa_xnew0(uuid_t, 1);
    node->uuid = pa_xstrdup(uuid);
    node->next = NULL;
    return node;
}

static void uuid_list_append(uuid_t *node, const char *uuid) {
    while (node->next != NULL) node = node->next;
    node->next = uuid_new(uuid);
}

static device_t *device_new(const char *device, adapter_t *adapter) {
    device_t *node = pa_xnew0(device_t, 1);
    node->name = NULL;
    node->object_path = pa_xstrdup(device);
    node->paired = -1;
    node->adapter = adapter;
    node->alias = NULL;
    node->connected = -1;
    node->uuid_list = uuid_new("UUID_HEAD");
    node->address = NULL;
    node->class = -1;
    node->trusted = -1;
    node->next = NULL;
    return node;
}

static void device_list_append(device_t *node, const char *device, adapter_t *adapter) {
    while (node->next != NULL) node = node->next;
    node->next = device_new(device, adapter);
}

static adapter_t *adapter_new(const char *adapter) {
    adapter_t *node = pa_xnew0(adapter_t, 1);
    node->object_path = pa_xstrdup(adapter);
    node->mode = NULL;
    node->address = NULL;
    node->device_list = device_new("/DEVICE_HEAD", NULL);
    node->next = NULL;
    return node;
}

static void adapter_list_append(adapter_t *node, const char *adapter) {
    while (node->next != NULL) node = node->next;
    node->next = adapter_new(adapter);
}

static void print_devices(device_t *device_list) {
    device_t *device_list_i = device_list;
    while (device_list_i != NULL) {
        uuid_t *uuid_list_i = device_list_i->uuid_list;
        if (strcmp(device_list_i->object_path, "/DEVICE_HEAD") != 0) {
            pa_log("    [ %s ]", device_list_i->object_path);
            pa_log("        Name = %s", device_list_i->name);
            pa_log("        Paired = %d", device_list_i->paired);
            pa_log("        Adapter = %s", device_list_i->adapter->object_path);
            pa_log("        Alias = %s", device_list_i->alias);
            pa_log("        Connected = %d", device_list_i->connected);
            pa_log("        UUIDs = ");
            while (uuid_list_i != NULL) {
                if (strcmp(uuid_list_i->uuid, "UUID_HEAD") != 0) {
                    pa_log("            %s", uuid_list_i->uuid);
                }
                uuid_list_i = uuid_list_i->next;
            }
            pa_log("        Address = %s", device_list_i->address);
            pa_log("        Class = 0x%x", device_list_i->class);
            pa_log("        Trusted = %d", device_list_i->trusted);
        }
        device_list_i = device_list_i->next;
    }
}

static void print_adapters(adapter_t *adapter_list) {
    adapter_t *adapter_list_i = adapter_list;
    while (adapter_list_i != NULL) {
        if (strcmp(adapter_list_i->object_path, "/ADAPTER_HEAD") != 0) {
            pa_log("[ %s ]", adapter_list_i->object_path);
            pa_log("    Mode = %s", adapter_list_i->mode);
            pa_log("    Address = %s", adapter_list_i->address);
            print_devices(adapter_list_i->device_list);
        }
        adapter_list_i = adapter_list_i->next;
    }
}

static void detect_adapters(struct userdata *u) {
    DBusError e;
    DBusMessage *m = NULL, *r = NULL;
    DBusMessageIter arg_i, element_i, dict_i, variant_i;
    adapter_t *adapter_list_i;
    const char *key, *value;

    pa_assert(u);
    dbus_error_init(&e);

    /* get adapters */
    pa_assert_se(m = dbus_message_new_method_call("org.bluez", "/", "org.bluez.Manager", "ListAdapters"));
    r = dbus_connection_send_with_reply_and_block(pa_dbus_connection_get(u->conn), m, -1, &e);
    if (!r) {
        pa_log("org.bluez.Manager.ListAdapters failed: %s", e.message);
        goto fail;
    }
    if (!dbus_message_iter_init(r, &arg_i)) {
        pa_log("org.bluez.Manager.ListAdapters reply has no arguments");
        goto fail;
    }
    if (dbus_message_iter_get_arg_type(&arg_i) != DBUS_TYPE_ARRAY) {
        pa_log("org.bluez.Manager.ListAdapters argument is not an array");
        goto fail;
    }
    dbus_message_iter_recurse(&arg_i, &element_i);
    /* TODO: Review error checking
     * should this be changed to while (dbus_message_iter_get_arg_type(&element_i) == DBUS_TYPE_OBJECT_PATH) ? */
    while (dbus_message_iter_get_arg_type(&element_i) != DBUS_TYPE_INVALID) {
        if (dbus_message_iter_get_arg_type(&element_i) == DBUS_TYPE_OBJECT_PATH) {
            dbus_message_iter_get_basic(&element_i, &value);
            adapter_list_append(u->adapter_list, value);
        }
        dbus_message_iter_next(&element_i);
    }

    /* get adapter properties */
    adapter_list_i = u->adapter_list->next;
    while (adapter_list_i != NULL) {
        pa_assert_se(m = dbus_message_new_method_call("org.bluez", adapter_list_i->object_path, "org.bluez.Adapter", "GetProperties"));
        r = dbus_connection_send_with_reply_and_block(pa_dbus_connection_get(u->conn), m, -1, &e);
        if (!r) {
            pa_log("org.bluez.Adapter.GetProperties failed: %s", e.message);
            goto fail;
        }
        if (!dbus_message_iter_init(r, &arg_i)) {
            pa_log("org.bluez.Adapter.GetProperties reply has no arguments");
            goto fail;
        }
        if (dbus_message_iter_get_arg_type(&arg_i) != DBUS_TYPE_ARRAY) {
            pa_log("org.bluez.Adapter.GetProperties argument is not an array");
            goto fail;
        }
        dbus_message_iter_recurse(&arg_i, &element_i);
        while (dbus_message_iter_get_arg_type(&element_i) != DBUS_TYPE_INVALID) {
            if (dbus_message_iter_get_arg_type(&element_i) == DBUS_TYPE_DICT_ENTRY) {
                dbus_message_iter_recurse(&element_i, &dict_i);
                dbus_message_iter_get_basic(&dict_i, &key);
                dbus_message_iter_next(&dict_i);
                dbus_message_iter_recurse(&dict_i, &variant_i);
                dbus_message_iter_get_basic(&variant_i, &value);
                if (strcmp(key, "Mode") == 0) {
                    adapter_list_i->mode = pa_xstrdup(value);
                }
                else if (strcmp(key, "Address") == 0) {
                    adapter_list_i->address = pa_xstrdup(value);
                }
            }
            dbus_message_iter_next(&element_i);
        }
        adapter_list_i = adapter_list_i->next;
    }

fail:
    if (m)
        dbus_message_unref(m);
    if (r)
        dbus_message_unref(r);
    dbus_error_free(&e);
}

static void detect_devices(struct userdata *u) {
    DBusError e;
    DBusMessage *m = NULL, *r = NULL;
    DBusMessageIter arg_i, element_i, dict_i, variant_i;
    adapter_t *adapter_list_i;
    device_t *device_list_i, *device_list_prev_i;
    const char *key, *value;
    unsigned int uvalue;

    pa_assert(u);
    dbus_error_init(&e);

    /* get devices of each adapter */
    adapter_list_i = u->adapter_list->next;
    while (adapter_list_i != NULL) {
        pa_assert_se(m = dbus_message_new_method_call("org.bluez", adapter_list_i->object_path, "org.bluez.Adapter", "ListDevices"));
        r = dbus_connection_send_with_reply_and_block(pa_dbus_connection_get(u->conn), m, -1, &e);
        if (!r) {
            pa_log("org.bluez.Adapter.ListDevices failed: %s", e.message);
            goto fail;
        }
        if (!dbus_message_iter_init(r, &arg_i)) {
            pa_log("org.bluez.Adapter.ListDevices reply has no arguments");
            goto fail;
        }
        if (dbus_message_iter_get_arg_type(&arg_i) != DBUS_TYPE_ARRAY) {
            pa_log("org.bluez.Adapter.ListDevices argument is not an array");
            goto fail;
        }
        dbus_message_iter_recurse(&arg_i, &element_i);
        /* TODO: Review error checking
         * should this be changed to while (dbus_message_iter_get_arg_type(&element_i) == DBUS_TYPE_OBJECT_PATH) ? */
        while (dbus_message_iter_get_arg_type(&element_i) != DBUS_TYPE_INVALID) {
            if (dbus_message_iter_get_arg_type(&element_i) == DBUS_TYPE_OBJECT_PATH) {
                dbus_message_iter_get_basic(&element_i, &value);
                device_list_append(adapter_list_i->device_list, value, adapter_list_i);
            }
            dbus_message_iter_next(&element_i);
        }
        adapter_list_i = adapter_list_i->next;
    }

    /* get device properties */
    adapter_list_i = u->adapter_list->next;
    while (adapter_list_i != NULL) {
        device_list_prev_i = adapter_list_i->device_list;
        device_list_i = adapter_list_i->device_list->next;
        while (device_list_i != NULL) {
            pa_assert_se(m = dbus_message_new_method_call("org.bluez", device_list_i->object_path, "org.bluez.Device", "GetProperties"));
            r = dbus_connection_send_with_reply_and_block(pa_dbus_connection_get(u->conn), m, -1, &e);
            if (!r) {
                pa_log("org.bluez.Device.GetProperties failed: %s", e.message);
                goto fail;
            }
            if (!dbus_message_iter_init(r, &arg_i)) {
                pa_log("org.bluez.Device.GetProperties reply has no arguments");
                goto fail;
            }
            if (dbus_message_iter_get_arg_type(&arg_i) != DBUS_TYPE_ARRAY) {
                pa_log("org.bluez.Device.GetProperties argument is not an array");
                goto fail;
            }
            dbus_message_iter_recurse(&arg_i, &element_i);
            while (dbus_message_iter_get_arg_type(&element_i) != DBUS_TYPE_INVALID) {
                if (dbus_message_iter_get_arg_type(&element_i) == DBUS_TYPE_DICT_ENTRY) {
                    dbus_message_iter_recurse(&element_i, &dict_i);
                    dbus_message_iter_get_basic(&dict_i, &key);
                    dbus_message_iter_next(&dict_i);
                    dbus_message_iter_recurse(&dict_i, &variant_i);
                    if (strcmp(key, "Name") == 0) {
                        dbus_message_iter_get_basic(&variant_i, &value);
                        device_list_i->name = pa_xstrdup(value);
                    }
                    else if (strcmp(key, "Paired") == 0) {
                        dbus_message_iter_get_basic(&variant_i, &uvalue);
                        device_list_i->paired = uvalue;
                    }
                    else if (strcmp(key, "Alias") == 0) {
                        dbus_message_iter_get_basic(&variant_i, &value);
                        device_list_i->alias = pa_xstrdup(value);
                    }
                    else if (strcmp(key, "Connected") == 0) {
                        dbus_message_iter_get_basic(&variant_i, &uvalue);
                        device_list_i->connected = uvalue;
                    }
                    else if (strcmp(key, "UUIDs") == 0) {
                        DBusMessageIter uuid_i;
                        pa_bool_t is_audio_device = FALSE;
                        dbus_message_iter_recurse(&variant_i, &uuid_i);
                        while (dbus_message_iter_get_arg_type(&uuid_i) != DBUS_TYPE_INVALID) {
                            dbus_message_iter_get_basic(&uuid_i, &value);
                            if ( (strcasecmp(value, HSP_HS_UUID) == 0) || (strcasecmp(value, HFP_HS_UUID) == 0) ||
                                (strcasecmp(value, A2DP_SOURCE_UUID) == 0) || (strcasecmp(value, A2DP_SINK_UUID) == 0) )
                                is_audio_device = TRUE;
                            uuid_list_append(device_list_i->uuid_list, value);
                            dbus_message_iter_next(&uuid_i);
                        }
                        if (!is_audio_device) {
                            /* remove current device */
                            device_list_prev_i->next = device_list_i->next;
                            pa_xfree(device_list_i);
                            break;
                        }
                    }
                    else if (strcmp(key, "Address") == 0) {
                        dbus_message_iter_get_basic(&variant_i, &value);
                        device_list_i->address = pa_xstrdup(value);
                    }
                    else if (strcmp(key, "Class") == 0) {
                        dbus_message_iter_get_basic(&variant_i, &uvalue);
                        device_list_i->class = uvalue;
                    }
                    else if (strcmp(key, "Trusted") == 0) {
                        dbus_message_iter_get_basic(&variant_i, &uvalue);
                        device_list_i->trusted = uvalue;
                    }
                }
                dbus_message_iter_next(&element_i);
            }
            device_list_prev_i = device_list_prev_i->next;
            if (device_list_prev_i == NULL)
                device_list_i = NULL;
            else
                device_list_i = device_list_prev_i->next;
        }
        adapter_list_i = adapter_list_i->next;
    }

fail:
    if (m)
        dbus_message_unref(m);
    if (r)
        dbus_message_unref(r);
    dbus_error_free(&e);
}

static DBusHandlerResult filter_cb(DBusConnection *bus, DBusMessage *msg, void *userdata) {
    DBusMessageIter arg_i;
    DBusError err;
    const char *value;
    struct userdata *u;

    pa_assert(bus);
    pa_assert(msg);
    pa_assert(userdata);
    u = userdata;
    dbus_error_init(&err);

    pa_log("dbus: interface=%s, path=%s, member=%s\n",
            dbus_message_get_interface(msg),
            dbus_message_get_path(msg),
            dbus_message_get_member(msg));

    if (dbus_message_is_signal(msg, "org.bluez.Manager", "AdapterAdded")) {
        if (!dbus_message_iter_init(msg, &arg_i))
            pa_log("dbus: message has no parameters");
        else if (dbus_message_iter_get_arg_type(&arg_i) != DBUS_TYPE_OBJECT_PATH)
            pa_log("dbus: argument is not object path");
        else {
            dbus_message_iter_get_basic(&arg_i, &value);
            pa_log("hcid: adapter %s added", value);
        }
    }
    else if (dbus_message_is_signal(msg, "org.bluez.Manager", "AdapterRemoved")) {
        if (!dbus_message_iter_init(msg, &arg_i))
            pa_log("dbus: message has no parameters");
        else if (dbus_message_iter_get_arg_type(&arg_i) != DBUS_TYPE_OBJECT_PATH)
            pa_log("dbus: argument is not object path");
        else {
            dbus_message_iter_get_basic(&arg_i, &value);
            pa_log("hcid: adapter %s removed", value);
        }
    }
    else if (dbus_message_is_signal(msg, "org.bluez.Adapter", "DeviceCreated")) {
        if (!dbus_message_iter_init(msg, &arg_i))
            pa_log("dbus: message has no parameters");
        else if (dbus_message_iter_get_arg_type(&arg_i) != DBUS_TYPE_OBJECT_PATH)
            pa_log("dbus: argument is not object path");
        else {
            dbus_message_iter_get_basic(&arg_i, &value);
            pa_log("hcid: device %s created", value);
        }
    }
    else if (dbus_message_is_signal(msg, "org.bluez.Adapter", "DeviceRemoved")) {
        if (!dbus_message_iter_init(msg, &arg_i))
            pa_log("dbus: message has no parameters");
        else if (dbus_message_iter_get_arg_type(&arg_i) != DBUS_TYPE_OBJECT_PATH)
            pa_log("dbus: argument is not object path");
        else {
            dbus_message_iter_get_basic(&arg_i, &value);
            pa_log("hcid: device %s removed", value);
        }
    }

    dbus_error_free(&err);
    return DBUS_HANDLER_RESULT_HANDLED;
}

void pa__done(pa_module* m) {
    struct userdata *u;
    adapter_t *adapter_list_i, *adapter_list_next_i;
    device_t *device_list_i, *device_list_next_i;

    pa_assert(m);

    if (!(u = m->userdata))
        return;

    if ((adapter_list_i = u->adapter_list) != NULL) {
        while ((adapter_list_next_i = adapter_list_i->next) != NULL) {
            if ((device_list_i = adapter_list_i->device_list) != NULL) {
                while ((device_list_next_i = device_list_i->next) != NULL) {
                    pa_xfree(device_list_i);
                    device_list_i = device_list_next_i;
                }
                pa_xfree(device_list_i);
            }
            pa_xfree(adapter_list_i);
            adapter_list_i = adapter_list_next_i;
        }
        pa_xfree(adapter_list_i);
    }

    pa_dbus_connection_unref(u->conn);
    pa_xfree(u);
    pa_log("Unloading module-bt-discover");
    return;
}

int pa__init(pa_module* m) {
    pa_modargs *ma = NULL;
    DBusError err;
    adapter_t *adapter_list_i;
    device_t *device_list_i;
    struct userdata *u;

    pa_assert(m);
    dbus_error_init(&err);
    pa_log("Loading module-bt-discover");
    m->userdata = u = pa_xnew0(struct userdata, 1);
    u->module = m;

    /* connect to the bus */
    u->conn = pa_dbus_bus_get(m->core, DBUS_BUS_SYSTEM, &err);
    if ( dbus_error_is_set(&err) || (u->conn == NULL) ) {
        pa_log("Failed to get D-Bus connection: %s", err.message);
        goto fail;
    }

    /* static detection of bluetooth audio devices */
    u->adapter_list = adapter_new("/ADAPTER_HEAD");
    detect_adapters(u);
    detect_devices(u);

    print_adapters(u->adapter_list);

    /* load device modules */
    adapter_list_i = u->adapter_list->next;
    while (adapter_list_i != NULL) {
        device_list_i = adapter_list_i->device_list->next;
        while (device_list_i != NULL) {
            pa_log("Loading module-bt-device for %s", device_list_i->name); /* CHECK: Should it be name or alias? */
            /* call module */
            device_list_i = device_list_i->next;
        }
        adapter_list_i = adapter_list_i->next;
    }

    /* dynamic detection of bluetooth audio devices */
    if (!dbus_connection_add_filter(pa_dbus_connection_get(u->conn), filter_cb, u, NULL)) {
        pa_log_error("Failed to add filter function");
        goto fail;
    }
    dbus_connection_flush(pa_dbus_connection_get(u->conn));
    dbus_bus_add_match(pa_dbus_connection_get(u->conn), "type='signal',interface='org.bluez.Manager'", &err);
    dbus_connection_flush(pa_dbus_connection_get(u->conn));
    if (dbus_error_is_set(&err)) {
        pa_log_error("Unable to subscribe to org.bluez.Manager signals: %s: %s", err.name, err.message);
        goto fail;
    }
    dbus_bus_add_match(pa_dbus_connection_get(u->conn), "type='signal',interface='org.bluez.Adapter'", &err);
    dbus_connection_flush(pa_dbus_connection_get(u->conn));
    if (dbus_error_is_set(&err)) {
        pa_log_error("Unable to subscribe to org.bluez.Adapter signals: %s: %s", err.name, err.message);
        goto fail;
    }

    return 0;

fail:
    if (ma)
        pa_modargs_free(ma);
    dbus_error_free(&err);
    pa__done(m);
    return -1;
}