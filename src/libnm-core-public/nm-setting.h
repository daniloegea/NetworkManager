/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2007 - 2011 Red Hat, Inc.
 * Copyright (C) 2007 - 2008 Novell, Inc.
 */

#ifndef __NM_SETTING_H__
#define __NM_SETTING_H__

#if !defined(__NETWORKMANAGER_H_INSIDE__) && !defined(NETWORKMANAGER_COMPILATION)
#error "Only <NetworkManager.h> can be included directly."
#endif

#include "nm-core-types.h"

G_BEGIN_DECLS

#define NM_TYPE_SETTING            (nm_setting_get_type())
#define NM_SETTING(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), NM_TYPE_SETTING, NMSetting))
#define NM_SETTING_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), NM_TYPE_SETTING, NMSettingClass))
#define NM_IS_SETTING(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), NM_TYPE_SETTING))
#define NM_IS_SETTING_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), NM_TYPE_SETTING))
#define NM_SETTING_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), NM_TYPE_SETTING, NMSettingClass))

/* The property of the #NMSetting is required for the setting to be valid */
#define NM_SETTING_PARAM_REQUIRED 0x200

/* The property of the #NMSetting is a secret */
#define NM_SETTING_PARAM_SECRET 0x400

/* The property of the #NMSetting should be ignored during comparisons that
 * use the %NM_SETTING_COMPARE_FLAG_FUZZY flag.
 */
#define NM_SETTING_PARAM_FUZZY_IGNORE 0x800

/* Note: all non-glib GParamFlags bits are reserved by NetworkManager */

#define NM_SETTING_NAME "name"

/**
 * NMSettingSecretFlags:
 * @NM_SETTING_SECRET_FLAG_NONE: the system is responsible for providing and
 * storing this secret (default)
 * @NM_SETTING_SECRET_FLAG_AGENT_OWNED: a user secret agent is responsible
 * for providing and storing this secret; when it is required agents will be
 * asked to retrieve it
 * @NM_SETTING_SECRET_FLAG_NOT_SAVED: this secret should not be saved, but
 * should be requested from the user each time it is needed
 * @NM_SETTING_SECRET_FLAG_NOT_REQUIRED: in situations where it cannot be
 * automatically determined that the secret is required (some VPNs and PPP
 * providers don't require all secrets) this flag indicates that the specific
 * secret is not required
 *
 * These flags indicate specific behavior related to handling of a secret.  Each
 * secret has a corresponding set of these flags which indicate how the secret
 * is to be stored and/or requested when it is needed.
 *
 **/
typedef enum /*< flags >*/ {
    NM_SETTING_SECRET_FLAG_NONE         = 0x00000000,
    NM_SETTING_SECRET_FLAG_AGENT_OWNED  = 0x00000001,
    NM_SETTING_SECRET_FLAG_NOT_SAVED    = 0x00000002,
    NM_SETTING_SECRET_FLAG_NOT_REQUIRED = 0x00000004

    /* NOTE: if adding flags, update nm-core-internal.h as well */
} NMSettingSecretFlags;

/**
 * NMSettingCompareFlags:
 * @NM_SETTING_COMPARE_FLAG_EXACT: match all properties exactly
 * @NM_SETTING_COMPARE_FLAG_FUZZY: match only important attributes, like SSID,
 *   type, security settings, etc.  Does not match, for example, connection ID
 *   or UUID.
 * @NM_SETTING_COMPARE_FLAG_IGNORE_ID: ignore the connection's ID
 * @NM_SETTING_COMPARE_FLAG_IGNORE_SECRETS: ignore all secrets
 * @NM_SETTING_COMPARE_FLAG_IGNORE_AGENT_OWNED_SECRETS: ignore secrets for which
 *   the secret's flags indicate the secret is owned by a user secret agent
 *   (ie, the secret's flag includes @NM_SETTING_SECRET_FLAG_AGENT_OWNED)
 * @NM_SETTING_COMPARE_FLAG_IGNORE_NOT_SAVED_SECRETS: ignore secrets for which
 *   the secret's flags indicate the secret should not be saved to persistent
 *   storage (ie, the secret's flag includes @NM_SETTING_SECRET_FLAG_NOT_SAVED)
 * @NM_SETTING_COMPARE_FLAG_DIFF_RESULT_WITH_DEFAULT: if this flag is set,
 *   nm_setting_diff() and nm_connection_diff() will also include properties that
 *   are set to their default value. See also @NM_SETTING_COMPARE_FLAG_DIFF_RESULT_NO_DEFAULT.
 * @NM_SETTING_COMPARE_FLAG_DIFF_RESULT_NO_DEFAULT: if this flag is set,
 *   nm_setting_diff() and nm_connection_diff() will not include properties that
 *   are set to their default value. This is the opposite of
 *   @NM_SETTING_COMPARE_FLAG_DIFF_RESULT_WITH_DEFAULT. If both flags are set together,
 *   @NM_SETTING_COMPARE_FLAG_DIFF_RESULT_WITH_DEFAULT wins. If both flags are unset,
 *   this means to exclude default properties if there is a setting to compare,
 *   but include all properties, if the setting 'b' is missing. This is the legacy
 *   behaviour of libnm-util, where nm_setting_diff() behaved differently depending
 *   on whether the setting 'b' was available. If @NM_SETTING_COMPARE_FLAG_DIFF_RESULT_WITH_DEFAULT
 *   is set, nm_setting_diff() will also set the flags @NM_SETTING_DIFF_RESULT_IN_A_DEFAULT
 *   and @NM_SETTING_DIFF_RESULT_IN_B_DEFAULT, if the values are default values.
 * @NM_SETTING_COMPARE_FLAG_IGNORE_TIMESTAMP: ignore the connection's timestamp
 *
 * These flags modify the comparison behavior when comparing two settings or
 * two connections.
 *
 **/
typedef enum {
    NM_SETTING_COMPARE_FLAG_EXACT                      = 0x00000000,
    NM_SETTING_COMPARE_FLAG_FUZZY                      = 0x00000001,
    NM_SETTING_COMPARE_FLAG_IGNORE_ID                  = 0x00000002,
    NM_SETTING_COMPARE_FLAG_IGNORE_SECRETS             = 0x00000004,
    NM_SETTING_COMPARE_FLAG_IGNORE_AGENT_OWNED_SECRETS = 0x00000008,
    NM_SETTING_COMPARE_FLAG_IGNORE_NOT_SAVED_SECRETS   = 0x00000010,
    NM_SETTING_COMPARE_FLAG_DIFF_RESULT_WITH_DEFAULT   = 0x00000020,
    NM_SETTING_COMPARE_FLAG_DIFF_RESULT_NO_DEFAULT     = 0x00000040,
    NM_SETTING_COMPARE_FLAG_IGNORE_TIMESTAMP           = 0x00000080,

    /* Higher flags like 0x80000000 and 0x40000000 are used internally as private flags */
} NMSettingCompareFlags;

/**
 * NMSettingMacRandomization:
 * @NM_SETTING_MAC_RANDOMIZATION_DEFAULT: the default value, which unless
 * overridden by user-controlled defaults configuration, is "never".
 * @NM_SETTING_MAC_RANDOMIZATION_NEVER: the device's MAC address is always used.
 * @NM_SETTING_MAC_RANDOMIZATION_ALWAYS: a random MAC address is used.
 *
 * Controls if and how the MAC address of a device is randomzied.
 *
 * Since: 1.2
 **/
typedef enum {
    NM_SETTING_MAC_RANDOMIZATION_DEFAULT = 0,
    NM_SETTING_MAC_RANDOMIZATION_NEVER,
    NM_SETTING_MAC_RANDOMIZATION_ALWAYS,
} NMSettingMacRandomization;

/**
 * NMSettingClearSecretsWithFlagsFn:
 * @setting: The setting for which secrets are being iterated
 * @secret: The secret's name
 * @flags: The secret's flags, eg %NM_SETTING_SECRET_FLAG_AGENT_OWNED
 * @user_data: User data passed to nm_connection_clear_secrets_with_flags()
 *
 * Returns: %TRUE to clear the secret, %FALSE to not clear the secret
 */
typedef gboolean (*NMSettingClearSecretsWithFlagsFn)(NMSetting           *setting,
                                                     const char          *secret,
                                                     NMSettingSecretFlags flags,
                                                     gpointer             user_data);

struct _NMMetaSettingInfo;
struct _NMSettInfoSetting;
struct _NMSettInfoProperty;

/**
 * NMSettingValueIterFn:
 * @setting: The setting for which properties are being iterated, given to
 * nm_setting_enumerate_values()
 * @key: The value/property name
 * @value: The property's value
 * @flags: The property's flags, like %NM_SETTING_PARAM_SECRET
 * @user_data: User data passed to nm_setting_enumerate_values()
 */
typedef void (*NMSettingValueIterFn)(NMSetting    *setting,
                                     const char   *key,
                                     const GValue *value,
                                     GParamFlags   flags,
                                     gpointer      user_data);

/*< private >*/
typedef gboolean (*_NMConnectionForEachSecretFunc)(NMSettingSecretFlags flags, gpointer user_data);

typedef struct _NMSettingClass NMSettingClass;

GType nm_setting_get_type(void);

GType nm_setting_lookup_type(const char *name);

NMSetting *nm_setting_duplicate(NMSetting *setting);

const char *nm_setting_get_name(NMSetting *setting);

gboolean nm_setting_verify(NMSetting *setting, NMConnection *connection, GError **error);

NM_AVAILABLE_IN_1_2
gboolean nm_setting_verify_secrets(NMSetting *setting, NMConnection *connection, GError **error);

gboolean nm_setting_compare(NMSetting *a, NMSetting *b, NMSettingCompareFlags flags);

/**
 * NMSettingDiffResult:
 * @NM_SETTING_DIFF_RESULT_UNKNOWN: unknown result
 * @NM_SETTING_DIFF_RESULT_IN_A: the property is present in setting A
 * @NM_SETTING_DIFF_RESULT_IN_B: the property is present in setting B
 * @NM_SETTING_DIFF_RESULT_IN_A_DEFAULT: the property is present in
 * setting A but is set to the default value. This flag is only set,
 * if you specify @NM_SETTING_COMPARE_FLAG_DIFF_RESULT_WITH_DEFAULT.
 * @NM_SETTING_DIFF_RESULT_IN_B_DEFAULT: analog to @NM_SETTING_DIFF_RESULT_IN_A_DEFAULT.
 *
 * These values indicate the result of a setting difference operation.
 **/
typedef enum {
    NM_SETTING_DIFF_RESULT_UNKNOWN      = 0x00000000,
    NM_SETTING_DIFF_RESULT_IN_A         = 0x00000001,
    NM_SETTING_DIFF_RESULT_IN_B         = 0x00000002,
    NM_SETTING_DIFF_RESULT_IN_A_DEFAULT = 0x00000004,
    NM_SETTING_DIFF_RESULT_IN_B_DEFAULT = 0x00000008,
} NMSettingDiffResult;

gboolean nm_setting_diff(NMSetting            *a,
                         NMSetting            *b,
                         NMSettingCompareFlags flags,
                         gboolean              invert_results,
                         GHashTable          **results);

void nm_setting_enumerate_values(NMSetting *setting, NMSettingValueIterFn func, gpointer user_data);

char *nm_setting_to_string(NMSetting *setting);

/*****************************************************************************/

gboolean nm_setting_get_secret_flags(NMSetting            *setting,
                                     const char           *secret_name,
                                     NMSettingSecretFlags *out_flags,
                                     GError              **error);

gboolean nm_setting_set_secret_flags(NMSetting           *setting,
                                     const char          *secret_name,
                                     NMSettingSecretFlags flags,
                                     GError             **error);

/*****************************************************************************/

NM_AVAILABLE_IN_1_26
GVariant *nm_setting_option_get(NMSetting *setting, const char *opt_name);

NM_AVAILABLE_IN_1_26
gboolean
nm_setting_option_get_boolean(NMSetting *setting, const char *opt_name, gboolean *out_value);

NM_AVAILABLE_IN_1_26
gboolean nm_setting_option_get_uint32(NMSetting *setting, const char *opt_name, guint32 *out_value);

NM_AVAILABLE_IN_1_26
void nm_setting_option_set(NMSetting *setting, const char *opt_name, GVariant *variant);

NM_AVAILABLE_IN_1_26
void nm_setting_option_set_uint32(NMSetting *setting, const char *opt_name, guint32 value);

NM_AVAILABLE_IN_1_26
void nm_setting_option_set_boolean(NMSetting *setting, const char *opt_name, gboolean value);

NM_AVAILABLE_IN_1_26
const char *const *nm_setting_option_get_all_names(NMSetting *setting, guint *out_len);

NM_AVAILABLE_IN_1_26
void nm_setting_option_clear_by_name(NMSetting *setting, NMUtilsPredicateStr predicate);

/*****************************************************************************/

const GVariantType *nm_setting_get_dbus_property_type(NMSetting  *setting,
                                                      const char *property_name);

/*****************************************************************************/

G_END_DECLS

#endif /* __NM_SETTING_H__ */
