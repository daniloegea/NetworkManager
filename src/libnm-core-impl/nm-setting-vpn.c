/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2007 - 2013 Red Hat, Inc.
 * Copyright (C) 2007 - 2008 Novell, Inc.
 */

#include "libnm-core-impl/nm-default-libnm-core.h"

#include "nm-setting-vpn.h"

#include <stdlib.h>

#include "libnm-glib-aux/nm-secret-utils.h"
#include "nm-utils.h"
#include "nm-utils-private.h"
#include "nm-setting-private.h"

/**
 * SECTION:nm-setting-vpn
 * @short_description: Describes connection properties for Virtual Private Networks
 *
 * The #NMSettingVpn object is a #NMSetting subclass that describes properties
 * necessary for connection to Virtual Private Networks.  NetworkManager uses
 * a plugin architecture to allow easier use of new VPN types, and this
 * setting abstracts the configuration for those plugins.  Since the configuration
 * options are only known to the VPN plugins themselves, the VPN configuration
 * options are stored as key/value pairs of strings rather than GObject
 * properties.
 **/

/*****************************************************************************/

NM_GOBJECT_PROPERTIES_DEFINE(NMSettingVpn,
                             PROP_SERVICE_TYPE,
                             PROP_USER_NAME,
                             PROP_PERSISTENT,
                             PROP_DATA,
                             PROP_SECRETS,
                             PROP_TIMEOUT, );

typedef struct {
    char *service_type;

    /* username of the user requesting this connection, thus
     * it's really only valid for user connections, and it also
     * should never be saved out to persistent config.
     */
    char *user_name;

    /* The hash table is created at setting object
     * init time and should not be replaced.  It is
     * a char * -> char * mapping, and both the key
     * and value are owned by the hash table, and should
     * be allocated with functions whose value can be
     * freed with g_free().  Should not contain secrets.
     */
    GHashTable *data;

    /* The hash table is created at setting object
     * init time and should not be replaced.  It is
     * a char * -> char * mapping, and both the key
     * and value are owned by the hash table, and should
     * be allocated with functions whose value can be
     * freed with g_free().  Should contain secrets only.
     */
    GHashTable *secrets;

    guint32 timeout;

    /* Whether the VPN stays up across link changes, until the user
     * explicitly disconnects it.
     */
    bool persistent;

} NMSettingVpnPrivate;

/**
 * NMSettingVpn:
 *
 * VPN Settings
 */
struct _NMSettingVpn {
    NMSetting parent;
    /* In the past, this struct was public API. Preserve ABI! */
};

struct _NMSettingVpnClass {
    NMSettingClass parent;
    /* In the past, this struct was public API. Preserve ABI! */
    gpointer padding[4];
};

G_DEFINE_TYPE(NMSettingVpn, nm_setting_vpn, NM_TYPE_SETTING)

#define NM_SETTING_VPN_GET_PRIVATE(o) \
    (G_TYPE_INSTANCE_GET_PRIVATE((o), NM_TYPE_SETTING_VPN, NMSettingVpnPrivate))

/*****************************************************************************/

static GHashTable *
_ensure_strdict(GHashTable **p_hash, gboolean for_secrets)
{
    if (!*p_hash) {
        *p_hash = g_hash_table_new_full(nm_str_hash,
                                        g_str_equal,
                                        g_free,
                                        for_secrets ? (GDestroyNotify) nm_free_secret : g_free);
    }
    return *p_hash;
}

/*****************************************************************************/

/**
 * nm_setting_vpn_get_service_type:
 * @setting: the #NMSettingVpn
 *
 * Returns the service name of the VPN, which identifies the specific VPN
 * plugin that should be used to connect to this VPN.
 *
 * Returns: the VPN plugin's service name
 **/
const char *
nm_setting_vpn_get_service_type(NMSettingVpn *setting)
{
    g_return_val_if_fail(NM_IS_SETTING_VPN(setting), NULL);

    return NM_SETTING_VPN_GET_PRIVATE(setting)->service_type;
}

/**
 * nm_setting_vpn_get_user_name:
 * @setting: the #NMSettingVpn
 *
 * Returns: the #NMSettingVpn:user-name property of the setting
 **/
const char *
nm_setting_vpn_get_user_name(NMSettingVpn *setting)
{
    g_return_val_if_fail(NM_IS_SETTING_VPN(setting), NULL);

    return NM_SETTING_VPN_GET_PRIVATE(setting)->user_name;
}

/**
 * nm_setting_vpn_get_persistent:
 * @setting: the #NMSettingVpn
 *
 * Returns: the #NMSettingVpn:persistent property of the setting
 *
 * Since: 1.42, 1.40.4
 **/
gboolean
nm_setting_vpn_get_persistent(NMSettingVpn *setting)
{
    g_return_val_if_fail(NM_IS_SETTING_VPN(setting), FALSE);

    return NM_SETTING_VPN_GET_PRIVATE(setting)->persistent;
}

/**
 * nm_setting_vpn_get_num_data_items:
 * @setting: the #NMSettingVpn
 *
 * Gets number of key/value pairs of VPN configuration data.
 *
 * Returns: the number of VPN plugin specific configuration data items
 **/
guint32
nm_setting_vpn_get_num_data_items(NMSettingVpn *setting)
{
    g_return_val_if_fail(NM_IS_SETTING_VPN(setting), 0);

    return nm_g_hash_table_size(NM_SETTING_VPN_GET_PRIVATE(setting)->data);
}

/**
 * nm_setting_vpn_add_data_item:
 * @setting: the #NMSettingVpn
 * @key: a name that uniquely identifies the given value @item
 * @item: (allow-none): the value to be referenced by @key
 *
 * Establishes a relationship between @key and @item internally in the
 * setting which may be retrieved later.  Should not be used to store passwords
 * or other secrets, which is what nm_setting_vpn_add_secret() is for.
 *
 * Before 1.24, @item must not be %NULL and not an empty string. Since 1.24,
 * @item can be set to an empty string. It can also be set to %NULL to unset
 * the key. In that case, the behavior is as if calling nm_setting_vpn_remove_data_item().
 **/
void
nm_setting_vpn_add_data_item(NMSettingVpn *setting, const char *key, const char *item)
{
    if (!item) {
        nm_setting_vpn_remove_data_item(setting, key);
        return;
    }

    g_return_if_fail(NM_IS_SETTING_VPN(setting));
    g_return_if_fail(key && key[0]);

    g_hash_table_insert(_ensure_strdict(&NM_SETTING_VPN_GET_PRIVATE(setting)->data, FALSE),
                        g_strdup(key),
                        g_strdup(item));
    _notify(setting, PROP_DATA);
}

/**
 * nm_setting_vpn_get_data_item:
 * @setting: the #NMSettingVpn
 * @key: the name of the data item to retrieve
 *
 * Retrieves the data item of a key/value relationship previously established
 * by nm_setting_vpn_add_data_item().
 *
 * Returns: the data item, if any
 **/
const char *
nm_setting_vpn_get_data_item(NMSettingVpn *setting, const char *key)
{
    g_return_val_if_fail(NM_IS_SETTING_VPN(setting), NULL);
    g_return_val_if_fail(key && key[0], NULL);

    return nm_g_hash_table_lookup(NM_SETTING_VPN_GET_PRIVATE(setting)->data, key);
}

/**
 * nm_setting_vpn_get_data_keys:
 * @setting: the #NMSettingVpn
 * @out_length: (allow-none) (out): the length of the returned array
 *
 * Retrieves every data key inside @setting, as an array.
 *
 * Returns: (array length=out_length) (transfer container): a
 *   %NULL-terminated array containing each data key or %NULL if
 *   there are no data items.
 *
 * Since: 1.12
 */
const char **
nm_setting_vpn_get_data_keys(NMSettingVpn *setting, guint *out_length)
{
    g_return_val_if_fail(NM_IS_SETTING_VPN(setting), NULL);

    return nm_strdict_get_keys(NM_SETTING_VPN_GET_PRIVATE(setting)->data, TRUE, out_length);
}

/**
 * nm_setting_vpn_remove_data_item:
 * @setting: the #NMSettingVpn
 * @key: the name of the data item to remove
 *
 * Deletes a key/value relationship previously established by
 * nm_setting_vpn_add_data_item().
 *
 * Returns: %TRUE if the data item was found and removed from the internal list,
 * %FALSE if it was not.
 **/
gboolean
nm_setting_vpn_remove_data_item(NMSettingVpn *setting, const char *key)
{
    g_return_val_if_fail(NM_IS_SETTING_VPN(setting), FALSE);
    g_return_val_if_fail(key && key[0], FALSE);

    if (nm_g_hash_table_remove(NM_SETTING_VPN_GET_PRIVATE(setting)->data, key)) {
        _notify(setting, PROP_DATA);
        return TRUE;
    }
    return FALSE;
}

static void
foreach_item_helper(NMSettingVpn *self, GHashTable **p_hash, NMVpnIterFunc func, gpointer user_data)
{
    gs_unref_object NMSettingVpn *self_keep_alive = NULL;
    gs_strfreev char            **keys            = NULL;
    guint                         i, len;

    nm_assert(NM_IS_SETTING_VPN(self));
    nm_assert(func);

    keys = nm_strv_make_deep_copied(nm_strdict_get_keys(*p_hash, TRUE, &len));
    if (len == 0u) {
        nm_assert(!keys);
        return;
    }

    if (len > 1u)
        self_keep_alive = g_object_ref(self);

    for (i = 0; i < len; i++) {
        /* NOTE: note that we call the function with a clone of @key,
         * not with the actual key from the dictionary.
         *
         * The @value on the other hand, is not cloned but retrieved before
         * invoking @func(). That means, if @func() modifies the setting while
         * being called, the values are as they currently are, but the
         * keys (and their order) were pre-determined before starting to
         * invoke the callbacks.
         *
         * The idea is to give some sensible, stable behavior in case the user
         * modifies the settings. Whether this particular behavior is optimal
         * is unclear. It's probably a bad idea to modify the settings while
         * iterating the values. But at least, it's a safe thing to do and we
         * do something sensible. */
        func(keys[i], nm_g_hash_table_lookup(*p_hash, keys[i]), user_data);
    }
}

/**
 * nm_setting_vpn_foreach_data_item:
 * @setting: a #NMSettingVpn
 * @func: (scope call): an user provided function
 * @user_data: data to be passed to @func
 *
 * Iterates all data items stored in this setting.  It is safe to add, remove,
 * and modify data items inside @func, though any additions or removals made
 * during iteration will not be part of the iteration.
 */
void
nm_setting_vpn_foreach_data_item(NMSettingVpn *setting, NMVpnIterFunc func, gpointer user_data)
{
    g_return_if_fail(NM_IS_SETTING_VPN(setting));
    g_return_if_fail(func);

    foreach_item_helper(setting, &NM_SETTING_VPN_GET_PRIVATE(setting)->data, func, user_data);
}

/**
 * nm_setting_vpn_get_num_secrets:
 * @setting: the #NMSettingVpn
 *
 * Gets number of VPN plugin specific secrets in the setting.
 *
 * Returns: the number of VPN plugin specific secrets
 **/
guint32
nm_setting_vpn_get_num_secrets(NMSettingVpn *setting)
{
    g_return_val_if_fail(NM_IS_SETTING_VPN(setting), 0);

    return nm_g_hash_table_size(NM_SETTING_VPN_GET_PRIVATE(setting)->secrets);
}

/**
 * nm_setting_vpn_add_secret:
 * @setting: the #NMSettingVpn
 * @key: a name that uniquely identifies the given secret @secret
 * @secret: (allow-none): the secret to be referenced by @key
 *
 * Establishes a relationship between @key and @secret internally in the
 * setting which may be retrieved later.
 *
 * Before 1.24, @secret must not be %NULL and not an empty string. Since 1.24,
 * @secret can be set to an empty string. It can also be set to %NULL to unset
 * the key. In that case, the behavior is as if calling nm_setting_vpn_remove_secret().
 **/
void
nm_setting_vpn_add_secret(NMSettingVpn *setting, const char *key, const char *secret)
{
    if (!secret) {
        nm_setting_vpn_remove_secret(setting, key);
        return;
    }

    g_return_if_fail(NM_IS_SETTING_VPN(setting));
    g_return_if_fail(key && key[0]);

    g_hash_table_insert(_ensure_strdict(&NM_SETTING_VPN_GET_PRIVATE(setting)->secrets, TRUE),
                        g_strdup(key),
                        g_strdup(secret));
    _notify(setting, PROP_SECRETS);
}

/**
 * nm_setting_vpn_get_secret:
 * @setting: the #NMSettingVpn
 * @key: the name of the secret to retrieve
 *
 * Retrieves the secret of a key/value relationship previously established
 * by nm_setting_vpn_add_secret().
 *
 * Returns: the secret, if any
 **/
const char *
nm_setting_vpn_get_secret(NMSettingVpn *setting, const char *key)
{
    g_return_val_if_fail(NM_IS_SETTING_VPN(setting), NULL);
    g_return_val_if_fail(key && key[0], NULL);

    return nm_g_hash_table_lookup(NM_SETTING_VPN_GET_PRIVATE(setting)->secrets, key);
}

/**
 * nm_setting_vpn_get_secret_keys:
 * @setting: the #NMSettingVpn
 * @out_length: (allow-none) (out): the length of the returned array
 *
 * Retrieves every secret key inside @setting, as an array.
 *
 * Returns: (array length=out_length) (transfer container): a
 *   %NULL-terminated array containing each secret key or %NULL if
 *   there are no secrets.
 *
 * Since: 1.12
 */
const char **
nm_setting_vpn_get_secret_keys(NMSettingVpn *setting, guint *out_length)
{
    g_return_val_if_fail(NM_IS_SETTING_VPN(setting), NULL);

    return nm_strdict_get_keys(NM_SETTING_VPN_GET_PRIVATE(setting)->secrets, TRUE, out_length);
}

/**
 * nm_setting_vpn_remove_secret:
 * @setting: the #NMSettingVpn
 * @key: the name of the secret to remove
 *
 * Deletes a key/value relationship previously established by
 * nm_setting_vpn_add_secret().
 *
 * Returns: %TRUE if the secret was found and removed from the internal list,
 * %FALSE if it was not.
 **/
gboolean
nm_setting_vpn_remove_secret(NMSettingVpn *setting, const char *key)
{
    g_return_val_if_fail(NM_IS_SETTING_VPN(setting), FALSE);
    g_return_val_if_fail(key && key[0], FALSE);

    if (nm_g_hash_table_remove(NM_SETTING_VPN_GET_PRIVATE(setting)->secrets, key)) {
        _notify(setting, PROP_SECRETS);
        return TRUE;
    }
    return FALSE;
}

/**
 * nm_setting_vpn_foreach_secret:
 * @setting: a #NMSettingVpn
 * @func: (scope call): an user provided function
 * @user_data: data to be passed to @func
 *
 * Iterates all secrets stored in this setting.  It is safe to add, remove,
 * and modify secrets inside @func, though any additions or removals made during
 * iteration will not be part of the iteration.
 */
void
nm_setting_vpn_foreach_secret(NMSettingVpn *setting, NMVpnIterFunc func, gpointer user_data)
{
    g_return_if_fail(NM_IS_SETTING_VPN(setting));
    g_return_if_fail(func);

    foreach_item_helper(setting, &NM_SETTING_VPN_GET_PRIVATE(setting)->secrets, func, user_data);
}

static gboolean
aggregate(NMSetting *setting, int type_i, gpointer arg)
{
    NMSettingVpnPrivate      *priv = NM_SETTING_VPN_GET_PRIVATE(setting);
    NMConnectionAggregateType type = type_i;
    NMSettingSecretFlags      secret_flags;
    const char               *key_name;
    GHashTableIter            iter;

    switch (type) {
    case NM_CONNECTION_AGGREGATE_ANY_SECRETS:
        if (nm_g_hash_table_size(priv->secrets) > 0u) {
            *((gboolean *) arg) = TRUE;
            return TRUE;
        }
        return FALSE;

    case NM_CONNECTION_AGGREGATE_ANY_SYSTEM_SECRET_FLAGS:

        if (priv->secrets) {
            g_hash_table_iter_init(&iter, priv->secrets);
            while (g_hash_table_iter_next(&iter, (gpointer *) &key_name, NULL)) {
                if (!nm_setting_get_secret_flags(NM_SETTING(setting),
                                                 key_name,
                                                 &secret_flags,
                                                 NULL))
                    nm_assert_not_reached();
                if (secret_flags == NM_SETTING_SECRET_FLAG_NONE) {
                    *((gboolean *) arg) = TRUE;
                    return TRUE;
                }
            }
        }

        /* OK, we have no secrets with system-secret flags.
         * But do we have any secret-flags (without secrets) that indicate system secrets? */
        if (priv->data) {
            g_hash_table_iter_init(&iter, priv->data);
            while (g_hash_table_iter_next(&iter, (gpointer *) &key_name, NULL)) {
                gs_free char *secret_name = NULL;

                if (!NM_STR_HAS_SUFFIX(key_name, "-flags"))
                    continue;
                secret_name = g_strndup(key_name, strlen(key_name) - NM_STRLEN("-flags"));
                if (secret_name[0] == '\0')
                    continue;
                if (!nm_setting_get_secret_flags(NM_SETTING(setting),
                                                 secret_name,
                                                 &secret_flags,
                                                 NULL))
                    nm_assert_not_reached();
                if (secret_flags == NM_SETTING_SECRET_FLAG_NONE) {
                    *((gboolean *) arg) = TRUE;
                    return TRUE;
                }
            }
        }

        return FALSE;
    }

    g_return_val_if_reached(FALSE);
}

/**
 * nm_setting_vpn_get_timeout:
 * @setting: the #NMSettingVpn
 *
 * Returns: the #NMSettingVpn:timeout property of the setting
 *
 * Since: 1.2
 **/
guint32
nm_setting_vpn_get_timeout(NMSettingVpn *setting)
{
    g_return_val_if_fail(NM_IS_SETTING_VPN(setting), 0);

    return NM_SETTING_VPN_GET_PRIVATE(setting)->timeout;
}

static gboolean
verify(NMSetting *setting, NMConnection *connection, GError **error)
{
    NMSettingVpnPrivate *priv = NM_SETTING_VPN_GET_PRIVATE(setting);
    NMSettingConnection *s_con;

    if (!priv->service_type) {
        g_set_error_literal(error,
                            NM_CONNECTION_ERROR,
                            NM_CONNECTION_ERROR_MISSING_PROPERTY,
                            _("property is missing"));
        g_prefix_error(error, "%s.%s: ", NM_SETTING_VPN_SETTING_NAME, NM_SETTING_VPN_SERVICE_TYPE);
        return FALSE;
    }
    if (!priv->service_type[0]) {
        g_set_error_literal(error,
                            NM_CONNECTION_ERROR,
                            NM_CONNECTION_ERROR_INVALID_PROPERTY,
                            _("property is empty"));
        g_prefix_error(error, "%s.%s: ", NM_SETTING_VPN_SETTING_NAME, NM_SETTING_VPN_SERVICE_TYPE);
        return FALSE;
    }

    /* default username can be NULL, but can't be zero-length */
    if (priv->user_name && !priv->user_name[0]) {
        g_set_error_literal(error,
                            NM_CONNECTION_ERROR,
                            NM_CONNECTION_ERROR_INVALID_PROPERTY,
                            _("property is empty"));
        g_prefix_error(error, "%s.%s: ", NM_SETTING_VPN_SETTING_NAME, NM_SETTING_VPN_USER_NAME);
        return FALSE;
    }

    if (connection && (s_con = nm_connection_get_setting_connection(connection))
        && nm_setting_connection_get_multi_connect(s_con) != NM_CONNECTION_MULTI_CONNECT_DEFAULT) {
        g_set_error_literal(error,
                            NM_CONNECTION_ERROR,
                            NM_CONNECTION_ERROR_INVALID_PROPERTY,
                            _("cannot set connection.multi-connect for VPN setting"));
        return FALSE;
    }

    return TRUE;
}

static NMSettingUpdateSecretResult
update_secret_string(NMSetting *setting, const char *key, const char *value, GError **error)
{
    NMSettingVpnPrivate *priv = NM_SETTING_VPN_GET_PRIVATE(setting);

    g_return_val_if_fail(key && key[0], NM_SETTING_UPDATE_SECRET_ERROR);
    g_return_val_if_fail(value, NM_SETTING_UPDATE_SECRET_ERROR);

    if (nm_streq0(nm_g_hash_table_lookup(priv->secrets, key), value))
        return NM_SETTING_UPDATE_SECRET_SUCCESS_UNCHANGED;

    g_hash_table_insert(_ensure_strdict(&priv->secrets, TRUE), g_strdup(key), g_strdup(value));
    return NM_SETTING_UPDATE_SECRET_SUCCESS_MODIFIED;
}

static NMSettingUpdateSecretResult
update_secret_dict(NMSetting *setting, GVariant *secrets, GError **error)
{
    NMSettingVpnPrivate        *priv = NM_SETTING_VPN_GET_PRIVATE(setting);
    GVariantIter                iter;
    const char                 *name, *value;
    NMSettingUpdateSecretResult result = NM_SETTING_UPDATE_SECRET_SUCCESS_UNCHANGED;

    g_return_val_if_fail(secrets != NULL, NM_SETTING_UPDATE_SECRET_ERROR);

    /* Make sure the items are valid */
    g_variant_iter_init(&iter, secrets);
    while (g_variant_iter_next(&iter, "{&s&s}", &name, &value)) {
        if (!name[0]) {
            g_set_error_literal(error,
                                NM_CONNECTION_ERROR,
                                NM_CONNECTION_ERROR_INVALID_SETTING,
                                _("setting contained a secret with an empty name"));
            g_prefix_error(error, "%s: ", NM_SETTING_VPN_SETTING_NAME);
            return NM_SETTING_UPDATE_SECRET_ERROR;
        }
    }

    /* Now add the items to the settings' secrets list */
    g_variant_iter_init(&iter, secrets);
    while (g_variant_iter_next(&iter, "{&s&s}", &name, &value)) {
        if (nm_streq0(nm_g_hash_table_lookup(priv->secrets, name), value))
            continue;

        g_hash_table_insert(_ensure_strdict(&priv->secrets, TRUE), g_strdup(name), g_strdup(value));
        result = NM_SETTING_UPDATE_SECRET_SUCCESS_MODIFIED;
    }

    return result;
}

static int
update_one_secret(NMSetting *setting, const char *key, GVariant *value, GError **error)
{
    NMSettingUpdateSecretResult success = NM_SETTING_UPDATE_SECRET_ERROR;

    g_return_val_if_fail(key != NULL, NM_SETTING_UPDATE_SECRET_ERROR);
    g_return_val_if_fail(value != NULL, NM_SETTING_UPDATE_SECRET_ERROR);

    if (g_variant_is_of_type(value, G_VARIANT_TYPE_STRING)) {
        /* Passing the string properties individually isn't correct, and won't
         * produce the correct result, but for some reason that's how it used
         * to be done.  So even though it's not correct, keep the code around
         * for compatibility's sake.
         */
        success = update_secret_string(setting, key, g_variant_get_string(value, NULL), error);
    } else if (g_variant_is_of_type(value, G_VARIANT_TYPE("a{ss}"))) {
        if (!nm_streq(key, NM_SETTING_VPN_SECRETS)) {
            g_set_error_literal(error,
                                NM_CONNECTION_ERROR,
                                NM_CONNECTION_ERROR_PROPERTY_NOT_SECRET,
                                _("not a secret property"));
            g_prefix_error(error, "%s.%s ", NM_SETTING_VPN_SETTING_NAME, key);
        } else
            success = update_secret_dict(setting, value, error);
    } else {
        g_set_error_literal(error,
                            NM_CONNECTION_ERROR,
                            NM_CONNECTION_ERROR_INVALID_PROPERTY,
                            _("secret is not of correct type"));
        g_prefix_error(error, "%s.%s: ", NM_SETTING_VPN_SETTING_NAME, key);
    }

    if (success == NM_SETTING_UPDATE_SECRET_SUCCESS_MODIFIED)
        _notify(NM_SETTING_VPN(setting), PROP_SECRETS);

    return success;
}

static void
for_each_secret(NMSetting                     *setting,
                const char                    *secret_name,
                GVariant                      *val,
                gboolean                       remove_non_secrets,
                _NMConnectionForEachSecretFunc callback,
                gpointer                       callback_data,
                GVariantBuilder               *setting_builder)
{
    GVariantBuilder vpn_secrets_builder;
    GVariantIter    vpn_secrets_iter;
    const char     *vpn_secret_name;
    const char     *secret;

    if (!nm_streq(secret_name, NM_SETTING_VPN_SECRETS)) {
        NM_SETTING_CLASS(nm_setting_vpn_parent_class)
            ->for_each_secret(setting,
                              secret_name,
                              val,
                              remove_non_secrets,
                              callback,
                              callback_data,
                              setting_builder);
        return;
    }

    if (!g_variant_is_of_type(val, G_VARIANT_TYPE("a{ss}"))) {
        /* invalid type. Silently ignore the secrets as we cannot find out the
         * secret-flags. */
        return;
    }

    /* Iterate through each secret from the VPN dict in the overall secrets dict */
    g_variant_builder_init(&vpn_secrets_builder, G_VARIANT_TYPE("a{ss}"));
    g_variant_iter_init(&vpn_secrets_iter, val);
    while (g_variant_iter_next(&vpn_secrets_iter, "{&s&s}", &vpn_secret_name, &secret)) {
        NMSettingSecretFlags secret_flags = NM_SETTING_SECRET_FLAG_NONE;

        /* we ignore the return value of get_secret_flags. The function may determine
         * that this is not a secret, based on having not secret-flags and no secrets.
         * But we have the secret at hand. We know it would be a valid secret, if we
         * only add it to the VPN settings. */
        nm_setting_get_secret_flags(setting, vpn_secret_name, &secret_flags, NULL);

        if (callback(secret_flags, callback_data))
            g_variant_builder_add(&vpn_secrets_builder, "{ss}", vpn_secret_name, secret);
    }

    g_variant_builder_add(setting_builder,
                          "{sv}",
                          secret_name,
                          g_variant_builder_end(&vpn_secrets_builder));
}

static gboolean
get_secret_flags(NMSetting            *setting,
                 const char           *secret_name,
                 NMSettingSecretFlags *out_flags,
                 GError              **error)
{
    NMSettingVpnPrivate *priv           = NM_SETTING_VPN_GET_PRIVATE(setting);
    gs_free char        *flags_key_free = NULL;
    const char          *flags_key;
    const char          *flags_val;
    gint64               i64;

    nm_assert(secret_name);

    if (!secret_name[0]) {
        g_set_error(error,
                    NM_CONNECTION_ERROR,
                    NM_CONNECTION_ERROR_PROPERTY_NOT_SECRET,
                    _("secret name cannot be empty"));
        return FALSE;
    }

    flags_key = nm_construct_name_a("%s-flags", secret_name, &flags_key_free);

    if (!priv->data
        || !g_hash_table_lookup_extended(priv->data, flags_key, NULL, (gpointer *) &flags_val)) {
        NM_SET_OUT(out_flags, NM_SETTING_SECRET_FLAG_NONE);

        /* having no secret flag for the secret is fine, as long as there
         * is the secret itself... */
        if (!nm_g_hash_table_lookup(priv->secrets, secret_name)) {
            g_set_error_literal(error,
                                NM_CONNECTION_ERROR,
                                NM_CONNECTION_ERROR_PROPERTY_NOT_SECRET,
                                _("secret flags property not found"));
            g_prefix_error(error, "%s.%s: ", NM_SETTING_VPN_SETTING_NAME, flags_key);
            return FALSE;
        }
        return TRUE;
    }

    i64 = _nm_utils_ascii_str_to_int64(flags_val, 10, 0, NM_SETTING_SECRET_FLAG_ALL, -1);
    if (i64 == -1 || !_nm_setting_secret_flags_valid(i64)) {
        /* The flags keys is set to an unexpected value. That is a configuration
         * error. Note that keys named "*-flags" are reserved for secrets. The user
         * must not use this for anything but secret flags. Hence, we cannot fail
         * to read the secret, we pretend that the secret flag is set to the default
         * NM_SETTING_SECRET_FLAG_NONE. */
        NM_SET_OUT(out_flags, NM_SETTING_SECRET_FLAG_NONE);
        return TRUE;
    }

    NM_SET_OUT(out_flags, (NMSettingSecretFlags) i64);
    return TRUE;
}

static gboolean
set_secret_flags(NMSetting           *setting,
                 const char          *secret_name,
                 NMSettingSecretFlags flags,
                 GError             **error)
{
    nm_assert(secret_name);

    if (!secret_name[0]) {
        g_set_error(error,
                    NM_CONNECTION_ERROR,
                    NM_CONNECTION_ERROR_PROPERTY_NOT_SECRET,
                    _("secret name cannot be empty"));
        return FALSE;
    }

    g_hash_table_insert(_ensure_strdict(&NM_SETTING_VPN_GET_PRIVATE(setting)->data, FALSE),
                        g_strdup_printf("%s-flags", secret_name),
                        g_strdup_printf("%u", flags));
    _notify(NM_SETTING_VPN(setting), PROP_SECRETS);
    return TRUE;
}

static GPtrArray *
need_secrets(NMSetting *setting)
{
    /* Assume that VPN connections need secrets since they almost always will */
    return g_ptr_array_sized_new(1);
}

static NMTernary
compare_property_secrets(NMSettingVpn *a, NMSettingVpn *b, NMSettingCompareFlags flags)
{
    GHashTableIter iter;
    const char    *key, *val;
    int            run;

    if (NM_FLAGS_HAS(flags, NM_SETTING_COMPARE_FLAG_FUZZY))
        return NM_TERNARY_DEFAULT;
    if (NM_FLAGS_HAS(flags, NM_SETTING_COMPARE_FLAG_IGNORE_SECRETS))
        return NM_TERNARY_DEFAULT;

    if (!b)
        return TRUE;

    for (run = 0; run < 2; run++) {
        NMSettingVpn        *current_a = (run == 0) ? a : b;
        NMSettingVpn        *current_b = (run == 0) ? b : a;
        NMSettingVpnPrivate *priv_a    = NM_SETTING_VPN_GET_PRIVATE(current_a);

        if (!priv_a->secrets)
            continue;

        g_hash_table_iter_init(&iter, priv_a->secrets);
        while (g_hash_table_iter_next(&iter, (gpointer) &key, (gpointer) &val)) {
            if (nm_streq0(val, nm_setting_vpn_get_secret(current_b, key)))
                continue;
            if (!_nm_setting_should_compare_secret_property(NM_SETTING(current_a),
                                                            NM_SETTING(current_b),
                                                            key,
                                                            flags))
                continue;

            return FALSE;
        }
    }

    return TRUE;
}

static NMTernary
compare_fcn_secrets(_NM_SETT_INFO_PROP_COMPARE_FCN_ARGS _nm_nil)
{
    if (NM_FLAGS_HAS(flags, NM_SETTING_COMPARE_FLAG_INFERRABLE))
        return NM_TERNARY_DEFAULT;
    return compare_property_secrets(NM_SETTING_VPN(set_a), NM_SETTING_VPN(set_b), flags);
}

static gboolean
clear_secrets(const NMSettInfoSetting         *sett_info,
              const NMSettInfoProperty        *property_info,
              NMSetting                       *setting,
              NMSettingClearSecretsWithFlagsFn func,
              gpointer                         user_data)
{
    NMSettingVpnPrivate *priv = NM_SETTING_VPN_GET_PRIVATE(setting);
    GHashTableIter       iter;
    const char          *secret;
    gboolean             changed = TRUE;

    if (!property_info->param_spec
        || !NM_FLAGS_HAS(property_info->param_spec->flags, NM_SETTING_PARAM_SECRET))
        return FALSE;

    nm_assert(nm_streq(property_info->param_spec->name, NM_SETTING_VPN_SECRETS));

    if (!priv->secrets)
        return FALSE;

    g_hash_table_iter_init(&iter, priv->secrets);
    while (g_hash_table_iter_next(&iter, (gpointer) &secret, NULL)) {
        if (func) {
            NMSettingSecretFlags flags = NM_SETTING_SECRET_FLAG_NONE;

            if (!nm_setting_get_secret_flags(setting, secret, &flags, NULL))
                nm_assert_not_reached();

            if (!func(setting, secret, flags, user_data))
                continue;
        } else
            nm_assert(nm_setting_get_secret_flags(setting, secret, NULL, NULL));

        g_hash_table_iter_remove(&iter);
        changed = TRUE;
    }

    if (changed)
        _notify(NM_SETTING_VPN(setting), PROP_SECRETS);

    return changed;
}

static gboolean
vpn_secrets_from_dbus(_NM_SETT_INFO_PROP_FROM_DBUS_FCN_ARGS _nm_nil)
{
    NMSettingVpn                  *self      = NM_SETTING_VPN(setting);
    NMSettingVpnPrivate           *priv      = NM_SETTING_VPN_GET_PRIVATE(self);
    gs_unref_hashtable GHashTable *hash_free = NULL;
    GVariantIter                   iter;
    const char                    *key;
    const char                    *val;

    hash_free = g_steal_pointer(&priv->secrets);

    g_variant_iter_init(&iter, value);
    while (g_variant_iter_next(&iter, "{&s&s}", &key, &val)) {
        if (!key[0])
            continue;
        g_hash_table_insert(_ensure_strdict(&priv->secrets, TRUE), g_strdup(key), g_strdup(val));
    }

    _notify(self, PROP_SECRETS);
    return TRUE;
}

static GVariant *
vpn_secrets_to_dbus(_NM_SETT_INFO_PROP_TO_DBUS_FCN_ARGS _nm_nil)
{
    NMSettingVpnPrivate *priv = NM_SETTING_VPN_GET_PRIVATE(setting);
    GVariantBuilder      builder;
    gs_free const char **keys = NULL;
    guint                i, len;

    if (flags != NM_CONNECTION_SERIALIZE_ALL
        && !NM_FLAGS_ANY(flags,
                         NM_CONNECTION_SERIALIZE_WITH_SECRETS
                             | NM_CONNECTION_SERIALIZE_WITH_SECRETS_AGENT_OWNED
                             | NM_CONNECTION_SERIALIZE_WITH_SECRETS_SYSTEM_OWNED
                             | NM_CONNECTION_SERIALIZE_WITH_SECRETS_NOT_SAVED))
        return NULL;

    g_variant_builder_init(&builder, G_VARIANT_TYPE("a{ss}"));

    keys = nm_strdict_get_keys(priv->secrets, TRUE, &len);
    for (i = 0; i < len; i++) {
        const char          *key          = keys[i];
        NMSettingSecretFlags secret_flags = NM_SETTING_SECRET_FLAG_NONE;

        if (NM_FLAGS_ANY(flags,
                         NM_CONNECTION_SERIALIZE_WITH_SECRETS_AGENT_OWNED
                             | NM_CONNECTION_SERIALIZE_WITH_SECRETS_SYSTEM_OWNED
                             | NM_CONNECTION_SERIALIZE_WITH_SECRETS_NOT_SAVED))
            nm_setting_get_secret_flags(setting, key, &secret_flags, NULL);

        if (!_nm_connection_serialize_secrets(flags, secret_flags))
            continue;

        g_variant_builder_add(&builder, "{ss}", key, g_hash_table_lookup(priv->secrets, key));
    }

    return g_variant_builder_end(&builder);
}

/*****************************************************************************/

static void
get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
    NMSettingVpn        *setting = NM_SETTING_VPN(object);
    NMSettingVpnPrivate *priv    = NM_SETTING_VPN_GET_PRIVATE(setting);

    switch (prop_id) {
    case PROP_DATA:
        g_value_take_boxed(value, _nm_utils_copy_strdict(priv->data));
        break;
    case PROP_SECRETS:
        g_value_take_boxed(value, _nm_utils_copy_strdict(priv->secrets));
        break;
    default:
        _nm_setting_property_get_property_direct(object, prop_id, value, pspec);
        break;
    }
}

static void
set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
    NMSettingVpnPrivate *priv = NM_SETTING_VPN_GET_PRIVATE(object);

    switch (prop_id) {
    case PROP_DATA:
    case PROP_SECRETS:
    {
        gs_unref_hashtable GHashTable *hash_free = NULL;
        GHashTable                    *src_hash  = g_value_get_boxed(value);
        GHashTable                   **p_hash;
        const gboolean                 is_secrets = (prop_id == PROP_SECRETS);

        if (is_secrets)
            p_hash = &priv->secrets;
        else
            p_hash = &priv->data;

        hash_free = g_steal_pointer(p_hash);

        if (src_hash && g_hash_table_size(src_hash) > 0) {
            GHashTableIter iter;
            const char    *key;
            const char    *val;

            g_hash_table_iter_init(&iter, src_hash);
            while (g_hash_table_iter_next(&iter, (gpointer *) &key, (gpointer *) &val)) {
                if (!key || !key[0] || !val) {
                    /* NULL keys/values and empty key are not allowed. Usually, we would reject them in verify(), but
                         * then our nm_setting_vpn_remove_data_item() also doesn't allow empty keys. So, if we failed
                         * it in verify(), it would be only fixable by setting PROP_DATA again. Instead,
                         * silently ignore them. */
                    continue;
                }
                g_hash_table_insert(_ensure_strdict(p_hash, is_secrets),
                                    g_strdup(key),
                                    g_strdup(val));
            }
        }
    } break;
    default:
        _nm_setting_property_set_property_direct(object, prop_id, value, pspec);
        break;
    }
}

/*****************************************************************************/

static void
nm_setting_vpn_init(NMSettingVpn *setting)
{}

/**
 * nm_setting_vpn_new:
 *
 * Creates a new #NMSettingVpn object with default values.
 *
 * Returns: (transfer full): the new empty #NMSettingVpn object
 **/
NMSetting *
nm_setting_vpn_new(void)
{
    return g_object_new(NM_TYPE_SETTING_VPN, NULL);
}

static void
finalize(GObject *object)
{
    NMSettingVpnPrivate *priv = NM_SETTING_VPN_GET_PRIVATE(object);

    nm_g_hash_table_unref(priv->data);
    nm_g_hash_table_unref(priv->secrets);

    G_OBJECT_CLASS(nm_setting_vpn_parent_class)->finalize(object);
}

static void
nm_setting_vpn_class_init(NMSettingVpnClass *klass)
{
    GObjectClass   *object_class        = G_OBJECT_CLASS(klass);
    NMSettingClass *setting_class       = NM_SETTING_CLASS(klass);
    GArray         *properties_override = _nm_sett_info_property_override_create_array();

    g_type_class_add_private(klass, sizeof(NMSettingVpnPrivate));

    object_class->get_property = get_property;
    object_class->set_property = set_property;
    object_class->finalize     = finalize;

    setting_class->verify            = verify;
    setting_class->update_one_secret = update_one_secret;
    setting_class->for_each_secret   = for_each_secret;
    setting_class->get_secret_flags  = get_secret_flags;
    setting_class->set_secret_flags  = set_secret_flags;
    setting_class->need_secrets      = need_secrets;
    setting_class->clear_secrets     = clear_secrets;
    setting_class->aggregate         = aggregate;

    /**
     * NMSettingVpn:service-type:
     *
     * D-Bus service name of the VPN plugin that this setting uses to connect to
     * its network.  i.e. org.freedesktop.NetworkManager.vpnc for the vpnc
     * plugin.
     **/
    _nm_setting_property_define_direct_string(properties_override,
                                              obj_properties,
                                              NM_SETTING_VPN_SERVICE_TYPE,
                                              PROP_SERVICE_TYPE,
                                              NM_SETTING_PARAM_NONE,
                                              NMSettingVpnPrivate,
                                              service_type);

    /**
     * NMSettingVpn:user-name:
     *
     * If the VPN connection requires a user name for authentication, that name
     * should be provided here.  If the connection is available to more than one
     * user, and the VPN requires each user to supply a different name, then
     * leave this property empty.  If this property is empty, NetworkManager
     * will automatically supply the username of the user which requested the
     * VPN connection.
     **/
    _nm_setting_property_define_direct_string(properties_override,
                                              obj_properties,
                                              NM_SETTING_VPN_USER_NAME,
                                              PROP_USER_NAME,
                                              NM_SETTING_PARAM_NONE,
                                              NMSettingVpnPrivate,
                                              user_name);

    /**
     * NMSettingVpn:persistent:
     *
     * If the VPN service supports persistence, and this property is %TRUE,
     * the VPN will attempt to stay connected across link changes and outages,
     * until explicitly disconnected.
     **/
    _nm_setting_property_define_direct_boolean(properties_override,
                                               obj_properties,
                                               NM_SETTING_VPN_PERSISTENT,
                                               PROP_PERSISTENT,
                                               FALSE,
                                               NM_SETTING_PARAM_NONE,
                                               NMSettingVpnPrivate,
                                               persistent);

    /**
     * NMSettingVpn:data: (type GHashTable(utf8,utf8)):
     *
     * Dictionary of key/value pairs of VPN plugin specific data.  Both keys and
     * values must be strings.
     **/
    /* ---keyfile---
     * property: data
     * variable: separate variables named after keys of the dictionary
     * description: The keys of the data dictionary are used as variable names directly
     *   under [vpn] section.
     * example: remote=ovpn.corp.com cipher=AES-256-CBC username=joe
     * ---end---
     */
    obj_properties[PROP_DATA] = g_param_spec_boxed(NM_SETTING_VPN_DATA,
                                                   "",
                                                   "",
                                                   G_TYPE_HASH_TABLE,
                                                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    _nm_properties_override_gobj(properties_override,
                                 obj_properties[PROP_DATA],
                                 &nm_sett_info_propert_type_strdict);

    /**
     * NMSettingVpn:secrets: (type GHashTable(utf8,utf8)):
     *
     * Dictionary of key/value pairs of VPN plugin specific secrets like
     * passwords or private keys.  Both keys and values must be strings.
     **/
    /* ---keyfile---
     * property: secrets
     * variable: separate variables named after keys of the dictionary
     * description: The keys of the secrets dictionary are used as variable names directly
     *   under [vpn-secrets] section.
     * example: password=Popocatepetl
     * ---end---
     */
    obj_properties[PROP_SECRETS] =
        g_param_spec_boxed(NM_SETTING_VPN_SECRETS,
                           "",
                           "",
                           G_TYPE_HASH_TABLE,
                           G_PARAM_READWRITE | NM_SETTING_PARAM_SECRET
                               | NM_SETTING_PARAM_TO_DBUS_IGNORE_FLAGS | G_PARAM_STATIC_STRINGS);
    _nm_properties_override_gobj(
        properties_override,
        obj_properties[PROP_SECRETS],
        NM_SETT_INFO_PROPERT_TYPE_DBUS(NM_G_VARIANT_TYPE("a{ss}"),
                                       .to_dbus_fcn   = vpn_secrets_to_dbus,
                                       .compare_fcn   = compare_fcn_secrets,
                                       .from_dbus_fcn = vpn_secrets_from_dbus, ));

    /**
     * NMSettingVpn:timeout:
     *
     * Timeout for the VPN service to establish the connection. Some services
     * may take quite a long time to connect.
     * Value of 0 means a default timeout, which is 60 seconds (unless overridden
     * by vpn.timeout in configuration file). Values greater than zero mean
     * timeout in seconds.
     *
     * Since: 1.2
     **/
    _nm_setting_property_define_direct_uint32(properties_override,
                                              obj_properties,
                                              NM_SETTING_VPN_TIMEOUT,
                                              PROP_TIMEOUT,
                                              0,
                                              G_MAXUINT32,
                                              0,
                                              NM_SETTING_PARAM_NONE,
                                              NMSettingVpnPrivate,
                                              timeout);

    g_object_class_install_properties(object_class, _PROPERTY_ENUMS_LAST, obj_properties);

    _nm_setting_class_commit(setting_class,
                             NM_META_SETTING_TYPE_VPN,
                             NULL,
                             properties_override,
                             NM_SETT_INFO_PRIVATE_OFFSET_FROM_CLASS);
}
