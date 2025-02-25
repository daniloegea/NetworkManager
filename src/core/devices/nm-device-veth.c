/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2013 Red Hat, Inc.
 */

#include "src/core/nm-default-daemon.h"

#include <stdlib.h>

#include "libnm-core-intern/nm-core-internal.h"
#include "nm-device-veth.h"
#include "nm-device-private.h"
#include "nm-manager.h"
#include "libnm-platform/nm-platform.h"
#include "nm-device-factory.h"
#include "nm-setting-veth.h"

#define _NMLOG_DEVICE_TYPE NMDeviceVeth
#include "nm-device-logging.h"

/*****************************************************************************/

struct _NMDeviceVeth {
    NMDeviceEthernet parent;
};

struct _NMDeviceVethClass {
    NMDeviceEthernetClass parent;
};

NM_GOBJECT_PROPERTIES_DEFINE(NMDeviceVeth, PROP_PEER, );

/*****************************************************************************/

G_DEFINE_TYPE(NMDeviceVeth, nm_device_veth, NM_TYPE_DEVICE_ETHERNET)

/*****************************************************************************/

static void
update_properties(NMDevice *device)
{
    NMDevice *peer;
    int       ifindex, peer_ifindex;

    ifindex = nm_device_get_ifindex(device);

    if (ifindex <= 0
        || !nm_platform_link_veth_get_properties(nm_device_get_platform(device),
                                                 ifindex,
                                                 &peer_ifindex))
        peer_ifindex = 0;

    nm_device_parent_set_ifindex(device, peer_ifindex);

    peer = nm_device_parent_get_device(device);
    if (peer && NM_IS_DEVICE_VETH(peer) && nm_device_parent_get_ifindex(peer) <= 0)
        update_properties(peer);
}

static gboolean
can_unmanaged_external_down(NMDevice *self)
{
    /* Unless running in a container, an udev rule causes these to be
     * unmanaged. If there's no udev then we're probably in a container
     * and should IFF_UP and configure the veth ourselves even if we
     * didn't create it. */
    return FALSE;
}

static void
link_changed(NMDevice *device, const NMPlatformLink *pllink)
{
    NM_DEVICE_CLASS(nm_device_veth_parent_class)->link_changed(device, pllink);
    update_properties(device);
}

static gboolean
create_and_realize(NMDevice              *device,
                   NMConnection          *connection,
                   NMDevice              *parent,
                   const NMPlatformLink **out_plink,
                   GError               **error)
{
    const char    *iface = nm_device_get_iface(device);
    const char    *peer;
    NMDevice      *peer_device;
    NMSettingVeth *s_veth;
    int            r;

    s_veth = _nm_connection_get_setting(connection, NM_TYPE_SETTING_VETH);
    if (!s_veth) {
        g_set_error(error,
                    NM_DEVICE_ERROR,
                    NM_DEVICE_ERROR_CREATION_FAILED,
                    "Profile %s (%s) is not a suitable veth profile",
                    nm_connection_get_id(connection),
                    nm_connection_get_uuid(connection));
        return FALSE;
    }

    peer        = nm_setting_veth_get_peer(s_veth);
    peer_device = nm_manager_get_device(NM_MANAGER_GET, peer, NM_DEVICE_TYPE_VETH);
    if (peer_device) {
        if (nm_device_parent_get_device(peer_device))
            /* The veth device and its peer already exist. No need to create it again. */
            return TRUE;
    }

    r = nm_platform_link_veth_add(nm_device_get_platform(device), iface, peer, out_plink);
    if (r < 0) {
        g_set_error(error,
                    NM_DEVICE_ERROR,
                    NM_DEVICE_ERROR_CREATION_FAILED,
                    "Failed to create veth interface '%s' for '%s': %s",
                    iface,
                    nm_connection_get_id(connection),
                    nm_strerror(r));
        return FALSE;
    }
    return TRUE;
}

/*****************************************************************************/

static NMDeviceCapabilities
get_generic_capabilities(NMDevice *device)
{
    return NM_DEVICE_CAP_CARRIER_DETECT | NM_DEVICE_CAP_IS_SOFTWARE;
}

/*****************************************************************************/

static void
nm_device_veth_init(NMDeviceVeth *self)
{}

static void
parent_changed_notify(NMDevice *device,
                      int       old_ifindex,
                      NMDevice *old_parent,
                      int       new_ifindex,
                      NMDevice *new_parent)
{
    NM_DEVICE_CLASS(nm_device_veth_parent_class)
        ->parent_changed_notify(device, old_ifindex, old_parent, new_ifindex, new_parent);
    _notify(NM_DEVICE_VETH(device), PROP_PEER);
}

static void
get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
    NMDeviceVeth *self = NM_DEVICE_VETH(object);
    NMDevice     *peer;

    switch (prop_id) {
    case PROP_PEER:
        peer = nm_device_parent_get_device(NM_DEVICE(self));
        if (peer && !NM_IS_DEVICE_VETH(peer))
            peer = NULL;
        nm_dbus_utils_g_value_set_object_path(value, peer);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static const NMDBusInterfaceInfoExtended interface_info_device_veth = {
    .parent = NM_DEFINE_GDBUS_INTERFACE_INFO_INIT(
        NM_DBUS_INTERFACE_DEVICE_VETH,
        .properties = NM_DEFINE_GDBUS_PROPERTY_INFOS(
            NM_DEFINE_DBUS_PROPERTY_INFO_EXTENDED_READABLE("Peer", "o", NM_DEVICE_VETH_PEER), ), ),
};

static void
nm_device_veth_class_init(NMDeviceVethClass *klass)
{
    GObjectClass      *object_class      = G_OBJECT_CLASS(klass);
    NMDBusObjectClass *dbus_object_class = NM_DBUS_OBJECT_CLASS(klass);
    NMDeviceClass     *device_class      = NM_DEVICE_CLASS(klass);

    object_class->get_property = get_property;

    dbus_object_class->interface_infos = NM_DBUS_INTERFACE_INFOS(&interface_info_device_veth);

    device_class->connection_type_supported = NULL;
    device_class->link_types                = NM_DEVICE_DEFINE_LINK_TYPES(NM_LINK_TYPE_VETH);

    device_class->can_unmanaged_external_down = can_unmanaged_external_down;
    device_class->link_changed                = link_changed;
    device_class->parent_changed_notify       = parent_changed_notify;
    device_class->create_and_realize          = create_and_realize;
    device_class->get_generic_capabilities    = get_generic_capabilities;

    obj_properties[PROP_PEER] = g_param_spec_string(NM_DEVICE_VETH_PEER,
                                                    "",
                                                    "",
                                                    NULL,
                                                    G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties(object_class, _PROPERTY_ENUMS_LAST, obj_properties);
}

/*****************************************************************************/

#define NM_TYPE_VETH_DEVICE_FACTORY (nm_veth_device_factory_get_type())
#define NM_VETH_DEVICE_FACTORY(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), NM_TYPE_VETH_DEVICE_FACTORY, NMVethDeviceFactory))

static NMDevice *
create_device(NMDeviceFactory      *factory,
              const char           *iface,
              const NMPlatformLink *plink,
              NMConnection         *connection,
              gboolean             *out_ignore)
{
    return g_object_new(NM_TYPE_DEVICE_VETH,
                        NM_DEVICE_IFACE,
                        iface,
                        NM_DEVICE_TYPE_DESC,
                        "Veth",
                        NM_DEVICE_DEVICE_TYPE,
                        NM_DEVICE_TYPE_VETH,
                        NM_DEVICE_LINK_TYPE,
                        NM_LINK_TYPE_VETH,
                        NULL);
}

NM_DEVICE_FACTORY_DEFINE_INTERNAL(
    VETH,
    Veth,
    veth,
    NM_DEVICE_FACTORY_DECLARE_LINK_TYPES(NM_LINK_TYPE_VETH)
        NM_DEVICE_FACTORY_DECLARE_SETTING_TYPES(NM_SETTING_VETH_SETTING_NAME),
    factory_class->create_device = create_device;);
