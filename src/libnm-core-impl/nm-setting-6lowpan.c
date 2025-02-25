/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2018 Red Hat, Inc.
 */

#include "libnm-core-impl/nm-default-libnm-core.h"

#include "nm-setting-6lowpan.h"

#include "nm-setting-private.h"

/**
 * SECTION:nm-setting-6lowpan
 * @short_description: Describes connection properties for 6LoWPAN interfaces
 *
 * The #NMSetting6Lowpan object is a #NMSetting subclass that describes properties
 * necessary for connection to 6LoWPAN interfaces.
 **/

/*****************************************************************************/

NM_GOBJECT_PROPERTIES_DEFINE_BASE(PROP_PARENT, );

typedef struct {
    char *parent;
} NMSetting6LowpanPrivate;

/**
 * NMSetting6Lowpan:
 *
 * 6LoWPAN Settings
 *
 * Since: 1.14
 */
struct _NMSetting6Lowpan {
    NMSetting parent;
};

struct _NMSetting6LowpanClass {
    NMSettingClass parent;
};

G_DEFINE_TYPE(NMSetting6Lowpan, nm_setting_6lowpan, NM_TYPE_SETTING)

#define NM_SETTING_6LOWPAN_GET_PRIVATE(o) \
    (G_TYPE_INSTANCE_GET_PRIVATE((o), NM_TYPE_SETTING_6LOWPAN, NMSetting6LowpanPrivate))

/*****************************************************************************/

/**
 * nm_setting_6lowpan_get_parent:
 * @setting: the #NMSetting6Lowpan
 *
 * Returns: the #NMSetting6Lowpan:parent property of the setting
 *
 * Since: 1.42, 1.40.4
 **/
const char *
nm_setting_6lowpan_get_parent(NMSetting6Lowpan *setting)
{
    g_return_val_if_fail(NM_IS_SETTING_6LOWPAN(setting), NULL);
    return NM_SETTING_6LOWPAN_GET_PRIVATE(setting)->parent;
}

/*********************************************************************/

static gboolean
verify(NMSetting *setting, NMConnection *connection, GError **error)
{
    NMSetting6LowpanPrivate *priv  = NM_SETTING_6LOWPAN_GET_PRIVATE(setting);
    NMSettingConnection     *s_con = NULL;

    if (connection)
        s_con = nm_connection_get_setting_connection(connection);

    if (!priv->parent) {
        g_set_error(error,
                    NM_CONNECTION_ERROR,
                    NM_CONNECTION_ERROR_MISSING_PROPERTY,
                    _("property is not specified"));
        g_prefix_error(error,
                       "%s.%s: ",
                       NM_SETTING_6LOWPAN_SETTING_NAME,
                       NM_SETTING_6LOWPAN_PARENT);
        return FALSE;
    }

    if (nm_utils_is_uuid(priv->parent)) {
        /* If we have an NMSettingConnection:master with slave-type="6lowpan",
         * then it must be the same UUID.
         */
        if (s_con) {
            const char *master = NULL, *slave_type = NULL;

            slave_type = nm_setting_connection_get_slave_type(s_con);
            if (!g_strcmp0(slave_type, NM_SETTING_6LOWPAN_SETTING_NAME))
                master = nm_setting_connection_get_master(s_con);

            if (master && g_strcmp0(priv->parent, master) != 0) {
                g_set_error(error,
                            NM_CONNECTION_ERROR,
                            NM_CONNECTION_ERROR_INVALID_PROPERTY,
                            _("'%s' value doesn't match '%s=%s'"),
                            priv->parent,
                            NM_SETTING_CONNECTION_MASTER,
                            master);
                g_prefix_error(error,
                               "%s.%s: ",
                               NM_SETTING_6LOWPAN_SETTING_NAME,
                               NM_SETTING_6LOWPAN_PARENT);
                return FALSE;
            }
        }
    } else if (!nm_utils_iface_valid_name(priv->parent)) {
        /* parent must be either a UUID or an interface name */
        g_set_error(error,
                    NM_CONNECTION_ERROR,
                    NM_CONNECTION_ERROR_INVALID_PROPERTY,
                    _("'%s' is neither an UUID nor an interface name"),
                    priv->parent);
        g_prefix_error(error,
                       "%s.%s: ",
                       NM_SETTING_6LOWPAN_SETTING_NAME,
                       NM_SETTING_6LOWPAN_PARENT);
        return FALSE;
    }

    return TRUE;
}

/*****************************************************************************/

static void
nm_setting_6lowpan_init(NMSetting6Lowpan *setting)
{}

/**
 * nm_setting_6lowpan_new:
 *
 * Creates a new #NMSetting6Lowpan object with default values.
 *
 * Returns: (transfer full): the new empty #NMSetting6Lowpan object
 *
 * Since: 1.42, 1.40.4
 **/
NMSetting *
nm_setting_6lowpan_new(void)
{
    return g_object_new(NM_TYPE_SETTING_6LOWPAN, NULL);
}

static void
nm_setting_6lowpan_class_init(NMSetting6LowpanClass *klass)
{
    GObjectClass   *object_class        = G_OBJECT_CLASS(klass);
    NMSettingClass *setting_class       = NM_SETTING_CLASS(klass);
    GArray         *properties_override = _nm_sett_info_property_override_create_array();

    g_type_class_add_private(klass, sizeof(NMSetting6LowpanPrivate));

    object_class->get_property = _nm_setting_property_get_property_direct;
    object_class->set_property = _nm_setting_property_set_property_direct;

    setting_class->verify = verify;

    /**
     * NMSetting6Lowpan:parent:
     *
     * If given, specifies the parent interface name or parent connection UUID
     * from which this 6LowPAN interface should be created.
     *
     * Since: 1.14
     **/
    _nm_setting_property_define_direct_string(properties_override,
                                              obj_properties,
                                              NM_SETTING_6LOWPAN_PARENT,
                                              PROP_PARENT,
                                              NM_SETTING_PARAM_INFERRABLE,
                                              NMSetting6LowpanPrivate,
                                              parent);

    g_object_class_install_properties(object_class, _PROPERTY_ENUMS_LAST, obj_properties);

    _nm_setting_class_commit(setting_class,
                             NM_META_SETTING_TYPE_6LOWPAN,
                             NULL,
                             properties_override,
                             NM_SETT_INFO_PRIVATE_OFFSET_FROM_CLASS);
}
