/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2017 Red Hat, Inc.
 */

#include "libnm-core-impl/nm-default-libnm-core.h"

#include "nm-setting-tc-config.h"

#include <linux/pkt_sched.h>

#include "nm-setting-private.h"

/**
 * SECTION:nm-setting-tc-config
 * @short_description: Describes connection properties for the Linux Traffic Control
 * @include: nm-setting-tc-config.h
 **/

/*****************************************************************************/

G_DEFINE_BOXED_TYPE(NMTCQdisc, nm_tc_qdisc, nm_tc_qdisc_dup, nm_tc_qdisc_unref)

struct NMTCQdisc {
    guint refcount;

    char       *kind;
    guint32     handle;
    guint32     parent;
    GHashTable *attributes;
};

/**
 * nm_tc_qdisc_new:
 * @kind: name of the queueing discipline
 * @parent: the parent queueing discipline
 * @error: location to store error, or %NULL
 *
 * Creates a new #NMTCQdisc object.
 *
 * Returns: (transfer full): the new #NMTCQdisc object, or %NULL on error
 *
 * Since: 1.12
 **/
NMTCQdisc *
nm_tc_qdisc_new(const char *kind, guint32 parent, GError **error)
{
    NMTCQdisc *qdisc;

    if (!kind || !*kind) {
        g_set_error(error,
                    NM_CONNECTION_ERROR,
                    NM_CONNECTION_ERROR_INVALID_PROPERTY,
                    _("kind is missing"));
        return NULL;
    }

    if (strchr(kind, ' ') || strchr(kind, '\t')) {
        g_set_error(error,
                    NM_CONNECTION_ERROR,
                    NM_CONNECTION_ERROR_INVALID_PROPERTY,
                    _("'%s' is not a valid kind"),
                    kind);
        return NULL;
    }

    if (!parent) {
        g_set_error_literal(error,
                            NM_CONNECTION_ERROR,
                            NM_CONNECTION_ERROR_INVALID_PROPERTY,
                            _("parent handle missing"));
        return NULL;
    }

    qdisc           = g_slice_new0(NMTCQdisc);
    qdisc->refcount = 1;

    qdisc->kind   = g_strdup(kind);
    qdisc->parent = parent;

    return qdisc;
}

/**
 * nm_tc_qdisc_ref:
 * @qdisc: the #NMTCQdisc
 *
 * Increases the reference count of the object.
 *
 * Since: 1.12
 **/
void
nm_tc_qdisc_ref(NMTCQdisc *qdisc)
{
    g_return_if_fail(qdisc != NULL);
    g_return_if_fail(qdisc->refcount > 0);

    qdisc->refcount++;
}

/**
 * nm_tc_qdisc_unref:
 * @qdisc: the #NMTCQdisc
 *
 * Decreases the reference count of the object.  If the reference count
 * reaches zero, the object will be destroyed.
 *
 * Since: 1.12
 **/
void
nm_tc_qdisc_unref(NMTCQdisc *qdisc)
{
    g_return_if_fail(qdisc != NULL);
    g_return_if_fail(qdisc->refcount > 0);

    qdisc->refcount--;
    if (qdisc->refcount == 0) {
        g_free(qdisc->kind);
        if (qdisc->attributes)
            g_hash_table_unref(qdisc->attributes);
        g_slice_free(NMTCQdisc, qdisc);
    }
}

/**
 * nm_tc_qdisc_equal:
 * @qdisc: the #NMTCQdisc
 * @other: the #NMTCQdisc to compare @qdisc to.
 *
 * Determines if two #NMTCQdisc objects contain the same kind, * handle
 * and parent.
 *
 * Returns: %TRUE if the objects contain the same values, %FALSE if they do not.
 *
 * Since: 1.12
 **/
gboolean
nm_tc_qdisc_equal(NMTCQdisc *qdisc, NMTCQdisc *other)
{
    GHashTableIter iter;
    const char    *key;
    GVariant      *value, *value2;
    guint          n;

    g_return_val_if_fail(qdisc != NULL, FALSE);
    g_return_val_if_fail(qdisc->refcount > 0, FALSE);

    g_return_val_if_fail(other != NULL, FALSE);
    g_return_val_if_fail(other->refcount > 0, FALSE);

    if (qdisc->handle != other->handle || qdisc->parent != other->parent
        || g_strcmp0(qdisc->kind, other->kind) != 0)
        return FALSE;

    n = qdisc->attributes ? g_hash_table_size(qdisc->attributes) : 0;
    if (n != (other->attributes ? g_hash_table_size(other->attributes) : 0))
        return FALSE;
    if (n) {
        g_hash_table_iter_init(&iter, qdisc->attributes);
        while (g_hash_table_iter_next(&iter, (gpointer *) &key, (gpointer *) &value)) {
            value2 = g_hash_table_lookup(other->attributes, key);
            if (!value2)
                return FALSE;
            if (!g_variant_equal(value, value2))
                return FALSE;
        }
    }

    return TRUE;
}

static guint
_nm_tc_qdisc_hash(NMTCQdisc *qdisc)
{
    NMUtilsNamedValue          attrs_static[30];
    gs_free NMUtilsNamedValue *attrs_free = NULL;
    const NMUtilsNamedValue   *attrs;
    NMHashState                h;
    guint                      length;
    guint                      i;

    attrs =
        nm_utils_named_values_from_strdict(qdisc->attributes, &length, attrs_static, &attrs_free);

    nm_hash_init(&h, 43869703);
    nm_hash_update_vals(&h, qdisc->handle, qdisc->parent, length);
    nm_hash_update_str0(&h, qdisc->kind);
    for (i = 0; i < length; i++) {
        const char         *key     = attrs[i].name;
        GVariant           *variant = attrs[i].value_ptr;
        const GVariantType *vtype;

        vtype = g_variant_get_type(variant);

        nm_hash_update_str(&h, key);
        nm_hash_update_str(&h, (const char *) vtype);
        if (g_variant_type_is_basic(vtype))
            nm_hash_update_val(&h, g_variant_hash(variant));
    }

    return nm_hash_complete(&h);
}

/**
 * nm_tc_qdisc_dup:
 * @qdisc: the #NMTCQdisc
 *
 * Creates a copy of @qdisc
 *
 * Returns: (transfer full): a copy of @qdisc
 *
 * Since: 1.12
 **/
NMTCQdisc *
nm_tc_qdisc_dup(NMTCQdisc *qdisc)
{
    NMTCQdisc *copy;

    g_return_val_if_fail(qdisc != NULL, NULL);
    g_return_val_if_fail(qdisc->refcount > 0, NULL);

    copy = nm_tc_qdisc_new(qdisc->kind, qdisc->parent, NULL);
    nm_tc_qdisc_set_handle(copy, qdisc->handle);

    if (qdisc->attributes) {
        GHashTableIter iter;
        const char    *key;
        GVariant      *value;

        g_hash_table_iter_init(&iter, qdisc->attributes);
        while (g_hash_table_iter_next(&iter, (gpointer *) &key, (gpointer *) &value))
            nm_tc_qdisc_set_attribute(copy, key, value);
    }

    return copy;
}

/**
 * nm_tc_qdisc_get_kind:
 * @qdisc: the #NMTCQdisc
 *
 * Returns:
 *
 * Since: 1.12
 **/
const char *
nm_tc_qdisc_get_kind(NMTCQdisc *qdisc)
{
    g_return_val_if_fail(qdisc != NULL, NULL);
    g_return_val_if_fail(qdisc->refcount > 0, NULL);

    return qdisc->kind;
}

/**
 * nm_tc_qdisc_get_handle:
 * @qdisc: the #NMTCQdisc
 *
 * Returns: the queueing discipline handle
 *
 * Since: 1.12
 **/
guint32
nm_tc_qdisc_get_handle(NMTCQdisc *qdisc)
{
    g_return_val_if_fail(qdisc != NULL, TC_H_UNSPEC);
    g_return_val_if_fail(qdisc->refcount > 0, TC_H_UNSPEC);

    return qdisc->handle;
}

/**
 * nm_tc_qdisc_set_handle:
 * @qdisc: the #NMTCQdisc
 * @handle: the queueing discipline handle
 *
 * Sets the queueing discipline handle.
 *
 * Since: 1.12
 **/
void
nm_tc_qdisc_set_handle(NMTCQdisc *qdisc, guint32 handle)
{
    g_return_if_fail(qdisc != NULL);
    g_return_if_fail(qdisc->refcount > 0);

    qdisc->handle = handle;
}

/**
 * nm_tc_qdisc_get_parent:
 * @qdisc: the #NMTCQdisc
 *
 * Returns: the parent class
 *
 * Since: 1.12
 **/
guint32
nm_tc_qdisc_get_parent(NMTCQdisc *qdisc)
{
    g_return_val_if_fail(qdisc != NULL, TC_H_UNSPEC);
    g_return_val_if_fail(qdisc->refcount > 0, TC_H_UNSPEC);

    return qdisc->parent;
}

/**
 * nm_tc_qdisc_get_attribute_names:
 * @qdisc: the #NMTCQdisc
 *
 * Gets an array of attribute names defined on @qdisc.
 *
 * Returns: (transfer container): a %NULL-terminated array of attribute names
 *   or %NULL if no attributes are set.
 *
 * Since: 1.18
 **/
const char **
nm_tc_qdisc_get_attribute_names(NMTCQdisc *qdisc)
{
    g_return_val_if_fail(qdisc, NULL);

    return nm_strdict_get_keys(qdisc->attributes, TRUE, NULL);
}

GHashTable *
_nm_tc_qdisc_get_attributes(NMTCQdisc *qdisc)
{
    nm_assert(qdisc);

    return qdisc->attributes;
}

/**
 * nm_tc_qdisc_get_attribute:
 * @qdisc: the #NMTCQdisc
 * @name: the name of an qdisc attribute
 *
 * Gets the value of the attribute with name @name on @qdisc
 *
 * Returns: (transfer none): the value of the attribute with name @name on
 *   @qdisc, or %NULL if @qdisc has no such attribute.
 *
 * Since: 1.18
 **/
GVariant *
nm_tc_qdisc_get_attribute(NMTCQdisc *qdisc, const char *name)
{
    g_return_val_if_fail(qdisc != NULL, NULL);
    g_return_val_if_fail(name != NULL && *name != '\0', NULL);

    if (qdisc->attributes)
        return g_hash_table_lookup(qdisc->attributes, name);
    else
        return NULL;
}

/**
 * nm_tc_qdisc_set_attribute:
 * @qdisc: the #NMTCQdisc
 * @name: the name of an qdisc attribute
 * @value: (transfer none) (allow-none): the value
 *
 * Sets or clears the named attribute on @qdisc to the given value.
 *
 * Since: 1.18
 **/
void
nm_tc_qdisc_set_attribute(NMTCQdisc *qdisc, const char *name, GVariant *value)
{
    g_return_if_fail(qdisc != NULL);
    g_return_if_fail(name != NULL && *name != '\0');
    g_return_if_fail(strcmp(name, "kind") != 0);

    if (!qdisc->attributes) {
        qdisc->attributes = g_hash_table_new_full(nm_str_hash,
                                                  g_str_equal,
                                                  g_free,
                                                  (GDestroyNotify) g_variant_unref);
    }

    if (value)
        g_hash_table_insert(qdisc->attributes, g_strdup(name), g_variant_ref_sink(value));
    else
        g_hash_table_remove(qdisc->attributes, name);
}

/*****************************************************************************/

G_DEFINE_BOXED_TYPE(NMTCAction, nm_tc_action, nm_tc_action_dup, nm_tc_action_unref)

struct NMTCAction {
    guint refcount;

    char *kind;

    GHashTable *attributes;
};

/**
 * nm_tc_action_new:
 * @kind: name of the queueing discipline
 * @error: location to store error, or %NULL
 *
 * Creates a new #NMTCAction object.
 *
 * Returns: (transfer full): the new #NMTCAction object, or %NULL on error
 *
 * Since: 1.12
 **/
NMTCAction *
nm_tc_action_new(const char *kind, GError **error)
{
    NMTCAction *action;

    if (!kind || !*kind) {
        g_set_error(error,
                    NM_CONNECTION_ERROR,
                    NM_CONNECTION_ERROR_INVALID_PROPERTY,
                    _("kind is missing"));
        return NULL;
    }

    if (strchr(kind, ' ') || strchr(kind, '\t')) {
        g_set_error(error,
                    NM_CONNECTION_ERROR,
                    NM_CONNECTION_ERROR_INVALID_PROPERTY,
                    _("'%s' is not a valid kind"),
                    kind);
        return NULL;
    }

    action           = g_slice_new0(NMTCAction);
    action->refcount = 1;

    action->kind = g_strdup(kind);

    return action;
}

/**
 * nm_tc_action_ref:
 * @action: the #NMTCAction
 *
 * Increases the reference count of the object.
 *
 * Since: 1.12
 **/
void
nm_tc_action_ref(NMTCAction *action)
{
    g_return_if_fail(action != NULL);
    g_return_if_fail(action->refcount > 0);

    action->refcount++;
}

/**
 * nm_tc_action_unref:
 * @action: the #NMTCAction
 *
 * Decreases the reference count of the object.  If the reference count
 * reaches zero, the object will be destroyed.
 *
 * Since: 1.12
 **/
void
nm_tc_action_unref(NMTCAction *action)
{
    g_return_if_fail(action != NULL);
    g_return_if_fail(action->refcount > 0);

    action->refcount--;
    if (action->refcount == 0) {
        g_free(action->kind);
        if (action->attributes)
            g_hash_table_unref(action->attributes);
        g_slice_free(NMTCAction, action);
    }
}

/**
 * nm_tc_action_equal:
 * @action: the #NMTCAction
 * @other: the #NMTCAction to compare @action to.
 *
 * Determines if two #NMTCAction objects contain the same kind, family,
 * handle, parent and info.
 *
 * Returns: %TRUE if the objects contain the same values, %FALSE if they do not.
 *
 * Since: 1.12
 **/
gboolean
nm_tc_action_equal(NMTCAction *action, NMTCAction *other)
{
    GHashTableIter iter;
    const char    *key;
    GVariant      *value, *value2;
    guint          n;

    g_return_val_if_fail(!action || action->refcount > 0, FALSE);
    g_return_val_if_fail(!other || other->refcount > 0, FALSE);

    if (action == other)
        return TRUE;
    if (!action || !other)
        return FALSE;

    if (g_strcmp0(action->kind, other->kind) != 0)
        return FALSE;

    n = action->attributes ? g_hash_table_size(action->attributes) : 0;
    if (n != (other->attributes ? g_hash_table_size(other->attributes) : 0))
        return FALSE;
    if (n) {
        g_hash_table_iter_init(&iter, action->attributes);
        while (g_hash_table_iter_next(&iter, (gpointer *) &key, (gpointer *) &value)) {
            value2 = g_hash_table_lookup(other->attributes, key);
            if (!value2)
                return FALSE;
            if (!g_variant_equal(value, value2))
                return FALSE;
        }
    }

    return TRUE;
}

/**
 * nm_tc_action_dup:
 * @action: the #NMTCAction
 *
 * Creates a copy of @action
 *
 * Returns: (transfer full): a copy of @action
 *
 * Since: 1.12
 **/
NMTCAction *
nm_tc_action_dup(NMTCAction *action)
{
    NMTCAction *copy;

    g_return_val_if_fail(action != NULL, NULL);
    g_return_val_if_fail(action->refcount > 0, NULL);

    copy = nm_tc_action_new(action->kind, NULL);

    if (action->attributes) {
        GHashTableIter iter;
        const char    *key;
        GVariant      *value;

        g_hash_table_iter_init(&iter, action->attributes);
        while (g_hash_table_iter_next(&iter, (gpointer *) &key, (gpointer *) &value))
            nm_tc_action_set_attribute(copy, key, value);
    }

    return copy;
}

/**
 * nm_tc_action_get_kind:
 * @action: the #NMTCAction
 *
 * Returns:
 *
 * Since: 1.12
 **/
const char *
nm_tc_action_get_kind(NMTCAction *action)
{
    g_return_val_if_fail(action != NULL, NULL);
    g_return_val_if_fail(action->refcount > 0, NULL);

    return action->kind;
}

/**
 * nm_tc_action_get_attribute_names:
 * @action: the #NMTCAction
 *
 * Gets an array of attribute names defined on @action.
 *
 * Returns: (transfer full): a %NULL-terminated array of attribute names,
 *
 * Since: 1.12
 **/
char **
nm_tc_action_get_attribute_names(NMTCAction *action)
{
    const char **names;

    g_return_val_if_fail(action, NULL);

    names = nm_strdict_get_keys(action->attributes, TRUE, NULL);
    return nm_strv_make_deep_copied_nonnull(names);
}

GHashTable *
_nm_tc_action_get_attributes(NMTCAction *action)
{
    nm_assert(action);

    return action->attributes;
}

/**
 * nm_tc_action_get_attribute:
 * @action: the #NMTCAction
 * @name: the name of an action attribute
 *
 * Gets the value of the attribute with name @name on @action
 *
 * Returns: (transfer none): the value of the attribute with name @name on
 *   @action, or %NULL if @action has no such attribute.
 *
 * Since: 1.12
 **/
GVariant *
nm_tc_action_get_attribute(NMTCAction *action, const char *name)
{
    g_return_val_if_fail(action != NULL, NULL);
    g_return_val_if_fail(name != NULL && *name != '\0', NULL);

    if (action->attributes)
        return g_hash_table_lookup(action->attributes, name);
    else
        return NULL;
}

/**
 * nm_tc_action_set_attribute:
 * @action: the #NMTCAction
 * @name: the name of an action attribute
 * @value: (transfer none) (allow-none): the value
 *
 * Sets or clears the named attribute on @action to the given value.
 *
 * Since: 1.12
 **/
void
nm_tc_action_set_attribute(NMTCAction *action, const char *name, GVariant *value)
{
    g_return_if_fail(action != NULL);
    g_return_if_fail(name != NULL && *name != '\0');
    g_return_if_fail(strcmp(name, "kind") != 0);

    if (!action->attributes) {
        action->attributes = g_hash_table_new_full(nm_str_hash,
                                                   g_str_equal,
                                                   g_free,
                                                   (GDestroyNotify) g_variant_unref);
    }

    if (value)
        g_hash_table_insert(action->attributes, g_strdup(name), g_variant_ref_sink(value));
    else
        g_hash_table_remove(action->attributes, name);
}

/*****************************************************************************/

G_DEFINE_BOXED_TYPE(NMTCTfilter, nm_tc_tfilter, nm_tc_tfilter_dup, nm_tc_tfilter_unref)

struct NMTCTfilter {
    guint refcount;

    char       *kind;
    guint32     handle;
    guint32     parent;
    NMTCAction *action;
};

/**
 * nm_tc_tfilter_new:
 * @kind: name of the queueing discipline
 * @parent: the parent queueing discipline
 * @error: location to store error, or %NULL
 *
 * Creates a new #NMTCTfilter object.
 *
 * Returns: (transfer full): the new #NMTCTfilter object, or %NULL on error
 *
 * Since: 1.12
 **/
NMTCTfilter *
nm_tc_tfilter_new(const char *kind, guint32 parent, GError **error)
{
    NMTCTfilter *tfilter;

    if (!kind || !*kind) {
        g_set_error(error,
                    NM_CONNECTION_ERROR,
                    NM_CONNECTION_ERROR_INVALID_PROPERTY,
                    _("kind is missing"));
        return NULL;
    }

    if (strchr(kind, ' ') || strchr(kind, '\t')) {
        g_set_error(error,
                    NM_CONNECTION_ERROR,
                    NM_CONNECTION_ERROR_INVALID_PROPERTY,
                    _("'%s' is not a valid kind"),
                    kind);
        return NULL;
    }

    if (!parent) {
        g_set_error_literal(error,
                            NM_CONNECTION_ERROR,
                            NM_CONNECTION_ERROR_INVALID_PROPERTY,
                            _("parent handle missing"));
        return NULL;
    }

    tfilter           = g_slice_new0(NMTCTfilter);
    tfilter->refcount = 1;

    tfilter->kind   = g_strdup(kind);
    tfilter->parent = parent;

    return tfilter;
}

/**
 * nm_tc_tfilter_ref:
 * @tfilter: the #NMTCTfilter
 *
 * Increases the reference count of the object.
 *
 * Since: 1.12
 **/
void
nm_tc_tfilter_ref(NMTCTfilter *tfilter)
{
    g_return_if_fail(tfilter != NULL);
    g_return_if_fail(tfilter->refcount > 0);

    tfilter->refcount++;
}

/**
 * nm_tc_tfilter_unref:
 * @tfilter: the #NMTCTfilter
 *
 * Decreases the reference count of the object.  If the reference count
 * reaches zero, the object will be destroyed.
 *
 * Since: 1.12
 **/
void
nm_tc_tfilter_unref(NMTCTfilter *tfilter)
{
    g_return_if_fail(tfilter != NULL);
    g_return_if_fail(tfilter->refcount > 0);

    tfilter->refcount--;
    if (tfilter->refcount == 0) {
        g_free(tfilter->kind);
        if (tfilter->action)
            nm_tc_action_unref(tfilter->action);
        g_slice_free(NMTCTfilter, tfilter);
    }
}

/**
 * nm_tc_tfilter_equal:
 * @tfilter: the #NMTCTfilter
 * @other: the #NMTCTfilter to compare @tfilter to.
 *
 * Determines if two #NMTCTfilter objects contain the same kind, family,
 * handle, parent and info.
 *
 * Returns: %TRUE if the objects contain the same values, %FALSE if they do not.
 *
 * Since: 1.12
 **/
gboolean
nm_tc_tfilter_equal(NMTCTfilter *tfilter, NMTCTfilter *other)
{
    g_return_val_if_fail(tfilter != NULL, FALSE);
    g_return_val_if_fail(tfilter->refcount > 0, FALSE);

    g_return_val_if_fail(other != NULL, FALSE);
    g_return_val_if_fail(other->refcount > 0, FALSE);

    if (tfilter->handle != other->handle || tfilter->parent != other->parent
        || g_strcmp0(tfilter->kind, other->kind) != 0
        || !nm_tc_action_equal(tfilter->action, other->action))
        return FALSE;

    return TRUE;
}

static guint
_nm_tc_tfilter_hash(NMTCTfilter *tfilter)
{
    NMHashState h;

    nm_hash_init(&h, 63624437);
    nm_hash_update_vals(&h, tfilter->handle, tfilter->parent);
    nm_hash_update_str0(&h, tfilter->kind);

    if (tfilter->action) {
        gs_free NMUtilsNamedValue *attrs_free = NULL;
        NMUtilsNamedValue          attrs_static[30];
        const NMUtilsNamedValue   *attrs;
        guint                      length;
        guint                      i;

        nm_hash_update_str0(&h, tfilter->action->kind);

        attrs = nm_utils_named_values_from_strdict(tfilter->action->attributes,
                                                   &length,
                                                   attrs_static,
                                                   &attrs_free);
        for (i = 0; i < length; i++) {
            GVariant *variant = attrs[i].value_ptr;

            nm_hash_update_str(&h, attrs[i].name);
            if (g_variant_type_is_basic(g_variant_get_type(variant))) {
                guint attr_hash;

                /* g_variant_hash() works only for basic types, thus
                 * we ignore any non-basic attribute. Actions differing
                 * only for non-basic attributes will collide. */
                attr_hash = g_variant_hash(variant);
                nm_hash_update_val(&h, attr_hash);
            }
        }
    }

    return nm_hash_complete(&h);
}

/**
 * nm_tc_tfilter_dup:
 * @tfilter: the #NMTCTfilter
 *
 * Creates a copy of @tfilter
 *
 * Returns: (transfer full): a copy of @tfilter
 *
 * Since: 1.12
 **/
NMTCTfilter *
nm_tc_tfilter_dup(NMTCTfilter *tfilter)
{
    NMTCTfilter *copy;

    g_return_val_if_fail(tfilter != NULL, NULL);
    g_return_val_if_fail(tfilter->refcount > 0, NULL);

    copy = nm_tc_tfilter_new(tfilter->kind, tfilter->parent, NULL);
    nm_tc_tfilter_set_handle(copy, tfilter->handle);
    nm_tc_tfilter_set_action(copy, tfilter->action);

    return copy;
}

/**
 * nm_tc_tfilter_get_kind:
 * @tfilter: the #NMTCTfilter
 *
 * Returns:
 *
 * Since: 1.12
 **/
const char *
nm_tc_tfilter_get_kind(NMTCTfilter *tfilter)
{
    g_return_val_if_fail(tfilter != NULL, NULL);
    g_return_val_if_fail(tfilter->refcount > 0, NULL);

    return tfilter->kind;
}

/**
 * nm_tc_tfilter_get_handle:
 * @tfilter: the #NMTCTfilter
 *
 * Returns: the queueing discipline handle
 *
 * Since: 1.12
 **/
guint32
nm_tc_tfilter_get_handle(NMTCTfilter *tfilter)
{
    g_return_val_if_fail(tfilter != NULL, TC_H_UNSPEC);
    g_return_val_if_fail(tfilter->refcount > 0, TC_H_UNSPEC);

    return tfilter->handle;
}

/**
 * nm_tc_tfilter_set_handle:
 * @tfilter: the #NMTCTfilter
 * @handle: the queueing discipline handle
 *
 * Sets the queueing discipline handle.
 *
 * Since: 1.12
 **/
void
nm_tc_tfilter_set_handle(NMTCTfilter *tfilter, guint32 handle)
{
    g_return_if_fail(tfilter != NULL);
    g_return_if_fail(tfilter->refcount > 0);

    tfilter->handle = handle;
}

/**
 * nm_tc_tfilter_get_parent:
 * @tfilter: the #NMTCTfilter
 *
 * Returns: the parent class
 *
 * Since: 1.12
 **/
guint32
nm_tc_tfilter_get_parent(NMTCTfilter *tfilter)
{
    g_return_val_if_fail(tfilter != NULL, TC_H_UNSPEC);
    g_return_val_if_fail(tfilter->refcount > 0, TC_H_UNSPEC);

    return tfilter->parent;
}

/**
 * nm_tc_tfilter_get_action:
 * @tfilter: the #NMTCTfilter
 *
 * Returns: the action associated with a traffic filter.
 *
 * Since: 1.42, 1.40.4
 **/
NMTCAction *
nm_tc_tfilter_get_action(NMTCTfilter *tfilter)
{
    g_return_val_if_fail(tfilter != NULL, TC_H_UNSPEC);
    g_return_val_if_fail(tfilter->refcount > 0, TC_H_UNSPEC);

    if (tfilter->action == NULL)
        return NULL;

    return tfilter->action;
}

/**
 * nm_tc_tfilter_set_action:
 * @tfilter: the #NMTCTfilter
 * @action: the action object
 *
 * Sets the action associated with a traffic filter.
 *
 * Since: 1.42, 1.40.4
 **/
void
nm_tc_tfilter_set_action(NMTCTfilter *tfilter, NMTCAction *action)
{
    g_return_if_fail(tfilter != NULL);
    g_return_if_fail(tfilter->refcount > 0);

    if (action)
        nm_tc_action_ref(action);
    if (tfilter->action)
        nm_tc_action_unref(tfilter->action);
    tfilter->action = action;
}

/*****************************************************************************/

NM_GOBJECT_PROPERTIES_DEFINE(NMSettingTCConfig, PROP_QDISCS, PROP_TFILTERS, );

/**
 * NMSettingTCConfig:
 *
 * Linux Traffic Control Settings
 *
 * Since: 1.12
 */
struct _NMSettingTCConfig {
    NMSetting  parent;
    GPtrArray *qdiscs;
    GPtrArray *tfilters;
};

struct _NMSettingTCConfigClass {
    NMSettingClass parent;
};

G_DEFINE_TYPE(NMSettingTCConfig, nm_setting_tc_config, NM_TYPE_SETTING)

/*****************************************************************************/

/**
 * nm_setting_tc_config_get_num_qdiscs:
 * @setting: the #NMSettingTCConfig
 *
 * Returns: the number of configured queueing disciplines
 *
 * Since: 1.12
 **/
guint
nm_setting_tc_config_get_num_qdiscs(NMSettingTCConfig *self)
{
    g_return_val_if_fail(NM_IS_SETTING_TC_CONFIG(self), 0);

    return self->qdiscs->len;
}

/**
 * nm_setting_tc_config_get_qdisc:
 * @setting: the #NMSettingTCConfig
 * @idx: index number of the qdisc to return
 *
 * Returns: (transfer none): the qdisc at index @idx
 *
 * Since: 1.12
 **/
NMTCQdisc *
nm_setting_tc_config_get_qdisc(NMSettingTCConfig *self, guint idx)
{
    g_return_val_if_fail(NM_IS_SETTING_TC_CONFIG(self), NULL);
    g_return_val_if_fail(idx < self->qdiscs->len, NULL);

    return self->qdiscs->pdata[idx];
}

/**
 * nm_setting_tc_config_add_qdisc:
 * @setting: the #NMSettingTCConfig
 * @qdisc: the qdisc to add
 *
 * Appends a new qdisc and associated information to the setting.  The
 * given qdisc is duplicated internally and is not changed by this function.
 * If an identical qdisc (considering attributes as well) already exists, the
 * qdisc is not added and the function returns %FALSE.
 *
 * Returns: %TRUE if the qdisc was added; %FALSE if the qdisc was already known.
 *
 * Since: 1.12
 **/
gboolean
nm_setting_tc_config_add_qdisc(NMSettingTCConfig *self, NMTCQdisc *qdisc)
{
    guint i;

    g_return_val_if_fail(NM_IS_SETTING_TC_CONFIG(self), FALSE);
    g_return_val_if_fail(qdisc != NULL, FALSE);

    for (i = 0; i < self->qdiscs->len; i++) {
        if (nm_tc_qdisc_equal(self->qdiscs->pdata[i], qdisc))
            return FALSE;
    }

    g_ptr_array_add(self->qdiscs, nm_tc_qdisc_dup(qdisc));
    _notify(self, PROP_QDISCS);
    return TRUE;
}

/**
 * nm_setting_tc_config_remove_qdisc:
 * @setting: the #NMSettingTCConfig
 * @idx: index number of the qdisc
 *
 * Removes the qdisc at index @idx.
 *
 * Since: 1.12
 **/
void
nm_setting_tc_config_remove_qdisc(NMSettingTCConfig *self, guint idx)
{
    g_return_if_fail(NM_IS_SETTING_TC_CONFIG(self));

    g_return_if_fail(idx < self->qdiscs->len);

    g_ptr_array_remove_index(self->qdiscs, idx);
    _notify(self, PROP_QDISCS);
}

/**
 * nm_setting_tc_config_remove_qdisc_by_value:
 * @setting: the #NMSettingTCConfig
 * @qdisc: the qdisc to remove
 *
 * Removes the first matching qdisc that matches @qdisc.
 *
 * Returns: %TRUE if the qdisc was found and removed; %FALSE if it was not.
 *
 * Since: 1.12
 **/
gboolean
nm_setting_tc_config_remove_qdisc_by_value(NMSettingTCConfig *self, NMTCQdisc *qdisc)
{
    guint i;

    g_return_val_if_fail(NM_IS_SETTING_TC_CONFIG(self), FALSE);
    g_return_val_if_fail(qdisc != NULL, FALSE);

    for (i = 0; i < self->qdiscs->len; i++) {
        if (nm_tc_qdisc_equal(self->qdiscs->pdata[i], qdisc)) {
            g_ptr_array_remove_index(self->qdiscs, i);
            _notify(self, PROP_QDISCS);
            return TRUE;
        }
    }
    return FALSE;
}

/**
 * nm_setting_tc_config_clear_qdiscs:
 * @setting: the #NMSettingTCConfig
 *
 * Removes all configured queueing disciplines.
 *
 * Since: 1.12
 **/
void
nm_setting_tc_config_clear_qdiscs(NMSettingTCConfig *self)
{
    g_return_if_fail(NM_IS_SETTING_TC_CONFIG(self));

    if (self->qdiscs->len != 0) {
        g_ptr_array_set_size(self->qdiscs, 0);
        _notify(self, PROP_QDISCS);
    }
}

/*****************************************************************************/
/**
 * nm_setting_tc_config_get_num_tfilters:
 * @setting: the #NMSettingTCConfig
 *
 * Returns: the number of configured queueing disciplines
 *
 * Since: 1.12
 **/
guint
nm_setting_tc_config_get_num_tfilters(NMSettingTCConfig *self)
{
    g_return_val_if_fail(NM_IS_SETTING_TC_CONFIG(self), 0);

    return self->tfilters->len;
}

/**
 * nm_setting_tc_config_get_tfilter:
 * @setting: the #NMSettingTCConfig
 * @idx: index number of the tfilter to return
 *
 * Returns: (transfer none): the tfilter at index @idx
 *
 * Since: 1.12
 **/
NMTCTfilter *
nm_setting_tc_config_get_tfilter(NMSettingTCConfig *self, guint idx)
{
    g_return_val_if_fail(NM_IS_SETTING_TC_CONFIG(self), NULL);
    g_return_val_if_fail(idx < self->tfilters->len, NULL);

    return self->tfilters->pdata[idx];
}

/**
 * nm_setting_tc_config_add_tfilter:
 * @setting: the #NMSettingTCConfig
 * @tfilter: the tfilter to add
 *
 * Appends a new tfilter and associated information to the setting.  The
 * given tfilter is duplicated internally and is not changed by this function.
 * If an identical tfilter (considering attributes as well) already exists, the
 * tfilter is not added and the function returns %FALSE.
 *
 * Returns: %TRUE if the tfilter was added; %FALSE if the tfilter was already known.
 *
 * Since: 1.12
 **/
gboolean
nm_setting_tc_config_add_tfilter(NMSettingTCConfig *self, NMTCTfilter *tfilter)
{
    guint i;

    g_return_val_if_fail(NM_IS_SETTING_TC_CONFIG(self), FALSE);
    g_return_val_if_fail(tfilter != NULL, FALSE);

    for (i = 0; i < self->tfilters->len; i++) {
        if (nm_tc_tfilter_equal(self->tfilters->pdata[i], tfilter))
            return FALSE;
    }

    g_ptr_array_add(self->tfilters, nm_tc_tfilter_dup(tfilter));
    _notify(self, PROP_TFILTERS);
    return TRUE;
}

/**
 * nm_setting_tc_config_remove_tfilter:
 * @setting: the #NMSettingTCConfig
 * @idx: index number of the tfilter
 *
 * Removes the tfilter at index @idx.
 *
 * Since: 1.12
 **/
void
nm_setting_tc_config_remove_tfilter(NMSettingTCConfig *self, guint idx)
{
    g_return_if_fail(NM_IS_SETTING_TC_CONFIG(self));
    g_return_if_fail(idx < self->tfilters->len);

    g_ptr_array_remove_index(self->tfilters, idx);
    _notify(self, PROP_TFILTERS);
}

/**
 * nm_setting_tc_config_remove_tfilter_by_value:
 * @setting: the #NMSettingTCConfig
 * @tfilter: the tfilter to remove
 *
 * Removes the first matching tfilter that matches @tfilter.
 *
 * Returns: %TRUE if the tfilter was found and removed; %FALSE if it was not.
 *
 * Since: 1.12
 **/
gboolean
nm_setting_tc_config_remove_tfilter_by_value(NMSettingTCConfig *self, NMTCTfilter *tfilter)
{
    guint i;

    g_return_val_if_fail(NM_IS_SETTING_TC_CONFIG(self), FALSE);
    g_return_val_if_fail(tfilter != NULL, FALSE);

    for (i = 0; i < self->tfilters->len; i++) {
        if (nm_tc_tfilter_equal(self->tfilters->pdata[i], tfilter)) {
            g_ptr_array_remove_index(self->tfilters, i);
            _notify(self, PROP_TFILTERS);
            return TRUE;
        }
    }
    return FALSE;
}

/**
 * nm_setting_tc_config_clear_tfilters:
 * @setting: the #NMSettingTCConfig
 *
 * Removes all configured queueing disciplines.
 *
 * Since: 1.12
 **/
void
nm_setting_tc_config_clear_tfilters(NMSettingTCConfig *self)
{
    g_return_if_fail(NM_IS_SETTING_TC_CONFIG(self));

    if (self->tfilters->len != 0) {
        g_ptr_array_set_size(self->tfilters, 0);
        _notify(self, PROP_TFILTERS);
    }
}

/*****************************************************************************/

static gboolean
verify(NMSetting *setting, NMConnection *connection, GError **error)
{
    NMSettingTCConfig *self = NM_SETTING_TC_CONFIG(setting);
    guint              i;

    if (self->qdiscs->len != 0) {
        gs_unref_hashtable GHashTable *ht = NULL;

        ht = g_hash_table_new((GHashFunc) _nm_tc_qdisc_hash, (GEqualFunc) nm_tc_qdisc_equal);
        for (i = 0; i < self->qdiscs->len; i++) {
            if (!g_hash_table_add(ht, self->qdiscs->pdata[i])) {
                g_set_error_literal(error,
                                    NM_CONNECTION_ERROR,
                                    NM_CONNECTION_ERROR_INVALID_PROPERTY,
                                    _("there are duplicate TC qdiscs"));
                g_prefix_error(error,
                               "%s.%s: ",
                               NM_SETTING_TC_CONFIG_SETTING_NAME,
                               NM_SETTING_TC_CONFIG_QDISCS);
                return FALSE;
            }
        }
    }

    if (self->tfilters->len != 0) {
        gs_unref_hashtable GHashTable *ht = NULL;

        ht = g_hash_table_new((GHashFunc) _nm_tc_tfilter_hash, (GEqualFunc) nm_tc_tfilter_equal);
        for (i = 0; i < self->tfilters->len; i++) {
            if (!g_hash_table_add(ht, self->tfilters->pdata[i])) {
                g_set_error_literal(error,
                                    NM_CONNECTION_ERROR,
                                    NM_CONNECTION_ERROR_INVALID_PROPERTY,
                                    _("there are duplicate TC filters"));
                g_prefix_error(error,
                               "%s.%s: ",
                               NM_SETTING_TC_CONFIG_SETTING_NAME,
                               NM_SETTING_TC_CONFIG_TFILTERS);
                return FALSE;
            }
        }
    }

    return TRUE;
}

static NMTernary
compare_fcn_qdiscs(_NM_SETT_INFO_PROP_COMPARE_FCN_ARGS _nm_nil)
{
    NMSettingTCConfig *a_tc_config = NM_SETTING_TC_CONFIG(set_a);
    NMSettingTCConfig *b_tc_config = NM_SETTING_TC_CONFIG(set_b);
    guint              i;

    if (set_b) {
        if (a_tc_config->qdiscs->len != b_tc_config->qdiscs->len)
            return FALSE;
        for (i = 0; i < a_tc_config->qdiscs->len; i++) {
            if (!nm_tc_qdisc_equal(a_tc_config->qdiscs->pdata[i], b_tc_config->qdiscs->pdata[i]))
                return FALSE;
        }
    }
    return TRUE;
}

static NMTernary
compare_fcn_tfilter(_NM_SETT_INFO_PROP_COMPARE_FCN_ARGS _nm_nil)
{
    NMSettingTCConfig *a_tc_config = NM_SETTING_TC_CONFIG(set_a);
    NMSettingTCConfig *b_tc_config = NM_SETTING_TC_CONFIG(set_b);
    guint              i;

    if (set_b) {
        if (a_tc_config->tfilters->len != b_tc_config->tfilters->len)
            return FALSE;
        for (i = 0; i < a_tc_config->tfilters->len; i++) {
            if (!nm_tc_tfilter_equal(a_tc_config->tfilters->pdata[i],
                                     b_tc_config->tfilters->pdata[i]))
                return FALSE;
        }
    }
    return TRUE;
}

/**
 * _qdiscs_to_variant:
 * @qdiscs: (element-type NMTCQdisc): an array of #NMTCQdisc objects
 *
 * Utility function to convert a #GPtrArray of #NMTCQdisc objects representing
 * TC qdiscs into a #GVariant of type 'aa{sv}' representing an array
 * of NetworkManager TC qdiscs.
 *
 * Returns: (transfer none): a new floating #GVariant representing @qdiscs.
 **/
static GVariant *
_qdiscs_to_variant(GPtrArray *qdiscs)
{
    GVariantBuilder builder;
    int             i;

    g_variant_builder_init(&builder, G_VARIANT_TYPE("aa{sv}"));

    if (qdiscs) {
        for (i = 0; i < qdiscs->len; i++) {
            NMUtilsNamedValue          attrs_static[30];
            gs_free NMUtilsNamedValue *attrs_free = NULL;
            const NMUtilsNamedValue   *attrs;
            NMTCQdisc                 *qdisc = qdiscs->pdata[i];
            guint                      length;
            GVariantBuilder            qdisc_builder;
            guint                      y;

            g_variant_builder_init(&qdisc_builder, G_VARIANT_TYPE_VARDICT);

            g_variant_builder_add(&qdisc_builder,
                                  "{sv}",
                                  "kind",
                                  g_variant_new_string(nm_tc_qdisc_get_kind(qdisc)));

            g_variant_builder_add(&qdisc_builder,
                                  "{sv}",
                                  "handle",
                                  g_variant_new_uint32(nm_tc_qdisc_get_handle(qdisc)));

            g_variant_builder_add(&qdisc_builder,
                                  "{sv}",
                                  "parent",
                                  g_variant_new_uint32(nm_tc_qdisc_get_parent(qdisc)));

            attrs = nm_utils_named_values_from_strdict(qdisc->attributes,
                                                       &length,
                                                       attrs_static,
                                                       &attrs_free);
            for (y = 0; y < length; y++) {
                g_variant_builder_add(&qdisc_builder, "{sv}", attrs[y].name, attrs[y].value_ptr);
            }

            g_variant_builder_add(&builder, "a{sv}", &qdisc_builder);
        }
    }

    return g_variant_builder_end(&builder);
}

/**
 * _qdiscs_from_variant:
 * @value: a #GVariant of type 'aa{sv}'
 *
 * Utility function to convert a #GVariant representing a list of TC qdiscs
 * into a #GPtrArray of * #NMTCQdisc objects.
 *
 * Returns: (transfer full) (element-type NMTCQdisc): a newly allocated
 *   #GPtrArray of #NMTCQdisc objects
 **/
static GPtrArray *
_qdiscs_from_variant(GVariant *value)
{
    GPtrArray   *qdiscs;
    GVariant    *qdisc_var;
    GVariantIter iter;
    GError      *error = NULL;

    g_return_val_if_fail(g_variant_is_of_type(value, G_VARIANT_TYPE("aa{sv}")), NULL);

    g_variant_iter_init(&iter, value);
    qdiscs = g_ptr_array_new_with_free_func((GDestroyNotify) nm_tc_qdisc_unref);

    while (g_variant_iter_next(&iter, "@a{sv}", &qdisc_var)) {
        const char  *kind;
        guint32      parent;
        NMTCQdisc   *qdisc;
        GVariantIter qdisc_iter;
        const char  *key;
        GVariant    *attr_value;

        if (!g_variant_lookup(qdisc_var, "kind", "&s", &kind)
            || !g_variant_lookup(qdisc_var, "parent", "u", &parent)) {
            //g_warning ("Ignoring invalid qdisc");
            goto next;
        }

        qdisc = nm_tc_qdisc_new(kind, parent, &error);
        if (!qdisc) {
            //g_warning ("Ignoring invalid qdisc: %s", error->message);
            g_clear_error(&error);
            goto next;
        }

        g_variant_iter_init(&qdisc_iter, qdisc_var);
        while (g_variant_iter_next(&qdisc_iter, "{&sv}", &key, &attr_value)) {
            if (strcmp(key, "kind") == 0 || strcmp(key, "parent") == 0) {
                /* Already processed above */
            } else if (strcmp(key, "handle") == 0) {
                nm_tc_qdisc_set_handle(qdisc, g_variant_get_uint32(attr_value));
            } else {
                nm_tc_qdisc_set_attribute(qdisc, key, attr_value);
            }
            g_variant_unref(attr_value);
        }

        g_ptr_array_add(qdiscs, qdisc);
next:
        g_variant_unref(qdisc_var);
    }

    return qdiscs;
}

static GVariant *
tc_qdiscs_get(_NM_SETT_INFO_PROP_TO_DBUS_FCN_ARGS _nm_nil)
{
    gs_unref_ptrarray GPtrArray *qdiscs = NULL;

    g_object_get(setting, NM_SETTING_TC_CONFIG_QDISCS, &qdiscs, NULL);
    return _qdiscs_to_variant(qdiscs);
}

static gboolean
tc_qdiscs_set(_NM_SETT_INFO_PROP_FROM_DBUS_FCN_ARGS _nm_nil)
{
    gs_unref_ptrarray GPtrArray *qdiscs = NULL;

    qdiscs = _qdiscs_from_variant(value);
    g_object_set(setting, NM_SETTING_TC_CONFIG_QDISCS, qdiscs, NULL);
    return TRUE;
}

static GVariant *
_action_to_variant(NMTCAction *action)
{
    GVariantBuilder    builder;
    gs_strfreev char **attrs = nm_tc_action_get_attribute_names(action);
    int                i;

    g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);

    g_variant_builder_add(&builder,
                          "{sv}",
                          "kind",
                          g_variant_new_string(nm_tc_action_get_kind(action)));

    for (i = 0; attrs[i]; i++) {
        g_variant_builder_add(&builder,
                              "{sv}",
                              attrs[i],
                              nm_tc_action_get_attribute(action, attrs[i]));
    }

    return g_variant_builder_end(&builder);
}

/**
 * _tfilters_to_variant:
 * @tfilters: (element-type NMTCTfilter): an array of #NMTCTfilter objects
 *
 * Utility function to convert a #GPtrArray of #NMTCTfilter objects representing
 * TC tfilters into a #GVariant of type 'aa{sv}' representing an array
 * of NetworkManager TC tfilters.
 *
 * Returns: (transfer none): a new floating #GVariant representing @tfilters.
 **/
static GVariant *
_tfilters_to_variant(GPtrArray *tfilters)
{
    GVariantBuilder builder;
    int             i;

    g_variant_builder_init(&builder, G_VARIANT_TYPE("aa{sv}"));

    if (tfilters) {
        for (i = 0; i < tfilters->len; i++) {
            NMTCTfilter    *tfilter = tfilters->pdata[i];
            NMTCAction     *action  = nm_tc_tfilter_get_action(tfilter);
            GVariantBuilder tfilter_builder;

            g_variant_builder_init(&tfilter_builder, G_VARIANT_TYPE("a{sv}"));

            g_variant_builder_add(&tfilter_builder,
                                  "{sv}",
                                  "kind",
                                  g_variant_new_string(nm_tc_tfilter_get_kind(tfilter)));
            g_variant_builder_add(&tfilter_builder,
                                  "{sv}",
                                  "handle",
                                  g_variant_new_uint32(nm_tc_tfilter_get_handle(tfilter)));
            g_variant_builder_add(&tfilter_builder,
                                  "{sv}",
                                  "parent",
                                  g_variant_new_uint32(nm_tc_tfilter_get_parent(tfilter)));

            if (action) {
                g_variant_builder_add(&tfilter_builder,
                                      "{sv}",
                                      "action",
                                      _action_to_variant(action));
            }

            g_variant_builder_add(&builder, "a{sv}", &tfilter_builder);
        }
    }

    return g_variant_builder_end(&builder);
}

/**
 * _tfilters_from_variant:
 * @value: a #GVariant of type 'aa{sv}'
 *
 * Utility function to convert a #GVariant representing a list of TC tfilters
 * into a #GPtrArray of * #NMTCTfilter objects.
 *
 * Returns: (transfer full) (element-type NMTCTfilter): a newly allocated
 *   #GPtrArray of #NMTCTfilter objects
 **/
static GPtrArray *
_tfilters_from_variant(GVariant *value)
{
    GPtrArray   *tfilters;
    GVariant    *tfilter_var;
    GVariantIter iter;
    GError      *error = NULL;

    g_return_val_if_fail(g_variant_is_of_type(value, G_VARIANT_TYPE("aa{sv}")), NULL);

    g_variant_iter_init(&iter, value);
    tfilters = g_ptr_array_new_with_free_func((GDestroyNotify) nm_tc_tfilter_unref);

    while (g_variant_iter_next(&iter, "@a{sv}", &tfilter_var)) {
        NMTCTfilter *tfilter = NULL;
        const char  *kind;
        guint32      handle;
        guint32      parent;
        NMTCAction  *action;
        const char  *action_kind = NULL;
        char        *action_key;
        GVariantIter action_iter;
        GVariant    *action_var = NULL;
        GVariant    *action_val;

        if (!g_variant_lookup(tfilter_var, "kind", "&s", &kind)
            || !g_variant_lookup(tfilter_var, "parent", "u", &parent)) {
            //g_warning ("Ignoring invalid tfilter");
            goto next;
        }

        tfilter = nm_tc_tfilter_new(kind, parent, &error);
        if (!tfilter) {
            //g_warning ("Ignoring invalid tfilter: %s", error->message);
            g_clear_error(&error);
            goto next;
        }

        if (g_variant_lookup(tfilter_var, "handle", "u", &handle))
            nm_tc_tfilter_set_handle(tfilter, handle);

        action_var = g_variant_lookup_value(tfilter_var, "action", G_VARIANT_TYPE_VARDICT);

        if (action_var) {
            if (!g_variant_lookup(action_var, "kind", "&s", &action_kind)) {
                //g_warning ("Ignoring tfilter with invalid action");
                goto next;
            }

            action = nm_tc_action_new(action_kind, &error);
            if (!action) {
                //g_warning ("Ignoring tfilter with invalid action: %s", error->message);
                g_clear_error(&error);
                goto next;
            }

            g_variant_iter_init(&action_iter, action_var);
            while (g_variant_iter_next(&action_iter, "{&sv}", &action_key, &action_val)) {
                if (strcmp(action_key, "kind") != 0)
                    nm_tc_action_set_attribute(action, action_key, action_val);
                g_variant_unref(action_val);
            }

            nm_tc_tfilter_set_action(tfilter, action);
            nm_tc_action_unref(action);
        }

        nm_tc_tfilter_ref(tfilter);
        g_ptr_array_add(tfilters, tfilter);
next:
        if (tfilter)
            nm_tc_tfilter_unref(tfilter);
        if (action_var)
            g_variant_unref(action_var);
        g_variant_unref(tfilter_var);
    }

    return tfilters;
}

static GVariant *
tc_tfilters_get(_NM_SETT_INFO_PROP_TO_DBUS_FCN_ARGS _nm_nil)
{
    gs_unref_ptrarray GPtrArray *tfilters = NULL;

    g_object_get(setting, NM_SETTING_TC_CONFIG_TFILTERS, &tfilters, NULL);
    return _tfilters_to_variant(tfilters);
}

static gboolean
tc_tfilters_set(_NM_SETT_INFO_PROP_FROM_DBUS_FCN_ARGS _nm_nil)
{
    gs_unref_ptrarray GPtrArray *tfilters = NULL;

    tfilters = _tfilters_from_variant(value);
    g_object_set(setting, NM_SETTING_TC_CONFIG_TFILTERS, tfilters, NULL);
    return TRUE;
}

/*****************************************************************************/

static void
get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
    NMSettingTCConfig *self = NM_SETTING_TC_CONFIG(object);

    switch (prop_id) {
    case PROP_QDISCS:
        g_value_take_boxed(value,
                           _nm_utils_copy_array(self->qdiscs,
                                                (NMUtilsCopyFunc) nm_tc_qdisc_dup,
                                                (GDestroyNotify) nm_tc_qdisc_unref));
        break;
    case PROP_TFILTERS:
        g_value_take_boxed(value,
                           _nm_utils_copy_array(self->tfilters,
                                                (NMUtilsCopyFunc) nm_tc_tfilter_dup,
                                                (GDestroyNotify) nm_tc_tfilter_unref));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
    NMSettingTCConfig *self = NM_SETTING_TC_CONFIG(object);

    switch (prop_id) {
    case PROP_QDISCS:
        g_ptr_array_unref(self->qdiscs);
        self->qdiscs = _nm_utils_copy_array(g_value_get_boxed(value),
                                            (NMUtilsCopyFunc) nm_tc_qdisc_dup,
                                            (GDestroyNotify) nm_tc_qdisc_unref);
        break;
    case PROP_TFILTERS:
        g_ptr_array_unref(self->tfilters);
        self->tfilters = _nm_utils_copy_array(g_value_get_boxed(value),
                                              (NMUtilsCopyFunc) nm_tc_tfilter_dup,
                                              (GDestroyNotify) nm_tc_tfilter_unref);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

/*****************************************************************************/

static void
nm_setting_tc_config_init(NMSettingTCConfig *self)
{
    self->qdiscs   = g_ptr_array_new_with_free_func((GDestroyNotify) nm_tc_qdisc_unref);
    self->tfilters = g_ptr_array_new_with_free_func((GDestroyNotify) nm_tc_tfilter_unref);
}

/**
 * nm_setting_tc_config_new:
 *
 * Creates a new #NMSettingTCConfig object with default values.
 *
 * Returns: (transfer full): the new empty #NMSettingTCConfig object
 *
 * Since: 1.12
 **/
NMSetting *
nm_setting_tc_config_new(void)
{
    return g_object_new(NM_TYPE_SETTING_TC_CONFIG, NULL);
}

static void
finalize(GObject *object)
{
    NMSettingTCConfig *self = NM_SETTING_TC_CONFIG(object);

    g_ptr_array_unref(self->qdiscs);
    g_ptr_array_unref(self->tfilters);

    G_OBJECT_CLASS(nm_setting_tc_config_parent_class)->finalize(object);
}

static void
nm_setting_tc_config_class_init(NMSettingTCConfigClass *klass)
{
    GObjectClass   *object_class        = G_OBJECT_CLASS(klass);
    NMSettingClass *setting_class       = NM_SETTING_CLASS(klass);
    GArray         *properties_override = _nm_sett_info_property_override_create_array();

    object_class->get_property = get_property;
    object_class->set_property = set_property;
    object_class->finalize     = finalize;

    setting_class->verify = verify;

    /**
     * NMSettingTCConfig:qdiscs: (type GPtrArray(NMTCQdisc))
     *
     * Array of TC queueing disciplines.
     *
     * When the #NMSettingTCConfig setting is present, qdiscs from this
     * property are applied upon activation. If the property is empty,
     * all qdiscs are removed and the device will only
     * have the default qdisc assigned by kernel according to the
     * "net.core.default_qdisc" sysctl.
     *
     * If the #NMSettingTCConfig setting is not present, NetworkManager
     * doesn't touch the qdiscs present on the interface.
     **/
    /* ---nmcli---
     * property: qdiscs
     * format: GPtrArray(NMTCQdisc)
     * description-docbook:
     *  <para>
     *  Array of TC queueing disciplines. qdisc is a basic block in the
     *  Linux traffic control subsystem
     *  </para>
     *  <para>
     *   Each qdisc can be specified by the following attributes:
     *  </para>
     *  <variablelist>
     *    <varlistentry>
     *      <term><varname>handle HANDLE</varname></term>
     *      <listitem>
     *        <para>
     *          specifies the qdisc handle. A qdisc, which potentially can have children, gets
     *          assigned a major number, called a 'handle', leaving the minor number namespace
     *          available for classes. The handle is expressed as '10:'. It is customary to
     *          explicitly assign a handle to qdiscs expected to have children.
     *        </para>
     *      </listitem>
     *  </varlistentry>
     *      <varlistentry>
     *      <term><varname>parent HANDLE</varname></term>
     *      <listitem>
     *        <para>
     *          specifies the handle of the parent qdisc the current qdisc must be
     *          attached to.
     *        </para>
     *      </listitem>
     *    </varlistentry>
     *      <varlistentry>
     *      <term><varname>root</varname></term>
     *      <listitem>
     *        <para>
     *          specifies that the qdisc is attached to the root of device.
     *        </para>
     *      </listitem>
     *    </varlistentry>
     *      <varlistentry>
     *      <term><varname>KIND</varname></term>
     *      <listitem>
     *        <para>
     *          this is the qdisc kind. NetworkManager currently supports the
     *          following kinds: fq_codel, sfq, tbf. Each qdisc kind has a
     *          different set of parameters, described below. There are also some
     *          kinds like pfifo, pfifo_fast, prio supported by NetworkManager
     *          but their parameters are not supported by NetworkManager.
     *        </para>
     *      </listitem>
     *    </varlistentry>
     *  </variablelist>
     *  <para>
     *   Parameters for 'fq_codel':
     *  </para>
     *  <variablelist>
     *    <varlistentry>
     *      <term><varname>limit U32</varname></term>
     *      <listitem>
     *        <para>
     *          the hard limit on the real queue size.  When this limit is
     *          reached, incoming packets are dropped. Default is 10240 packets.
     *        </para>
     *      </listitem>
     *  </varlistentry>
     *      <varlistentry>
     *      <term><varname>memory_limit U32</varname></term>
     *      <listitem>
     *        <para>
     *          sets a limit on the total number of bytes that can be queued in
     *          this FQ-CoDel instance. The lower of the packet limit of the
     *          limit parameter and the memory limit will be enforced. Default is
     *          32 MB.
     *        </para>
     *      </listitem>
     *    </varlistentry>
     *      <varlistentry>
     *      <term><varname>flows U32</varname></term>
     *      <listitem>
     *        <para>
     *     the number of flows into which the incoming packets are
     *     classified. Due to the stochastic nature of hashing, multiple
     *     flows may end up being hashed into the same slot. Newer flows
     *     have priority over older ones. This parameter can be set only at
     *     load time since memory has to be allocated for the hash table.
     *     Default value is 1024.
     *        </para>
     *      </listitem>
     *    </varlistentry>
     *      <varlistentry>
     *      <term><varname>target U32</varname></term>
     *      <listitem>
     *        <para>
     *     the acceptable minimum standing/persistent queue delay. This minimum
     *     delay is identified by tracking the local minimum queue delay that packets
     *     experience. The unit of measurement is microsecond(us). Default value is 5ms.
     *        </para>
     *      </listitem>
     *    </varlistentry>
     *      <varlistentry>
     *      <term><varname>interval U32</varname></term>
     *      <listitem>
     *        <para>
     *     used to ensure that the measured minimum delay does not become too stale.
     *     The minimum delay must be experienced in the last epoch of length .B
     *     interval.  It should be set on the order of the worst-case RTT
     *     through the bottleneck to give endpoints sufficient time to
     *     react. Default value is 100ms.
     *        </para>
     *      </listitem>
     *    </varlistentry>
     *      <varlistentry>
     *      <term><varname>quantum U32</varname></term>
     *      <listitem>
     *        <para>
     *     the number of bytes used as 'deficit' in the fair queuing
     *     algorithm. Default is set to 1514 bytes which corresponds to the
     *     Ethernet MTU plus the hardware header length of 14 bytes.
     *        </para>
     *      </listitem>
     *    </varlistentry>
     *      <varlistentry>
     *      <term><varname>ecn BOOL</varname></term>
     *      <listitem>
     *        <para>
     *     can be used to mark packets instead of dropping them. ecn is turned
     *     on by default.
     *        </para>
     *      </listitem>
     *    </varlistentry>
     *      <varlistentry>
     *      <term><varname>ce_threshold U32</varname></term>
     *      <listitem>
     *        <para>
     *     sets a threshold above which all packets are marked with ECN
     *     Congestion Experienced. This is useful for DCTCP-style congestion
     *     control algorithms that require marking at very shallow queueing
     *     thresholds.
     *        </para>
     *      </listitem>
     *    </varlistentry>
     *  </variablelist>
     *  <para>
     *   Parameters for 'sfq':
     *  </para>
     *  <variablelist>
     *    <varlistentry>
     *      <term><varname>divisor U32</varname></term>
     *      <listitem>
     *        <para>
     *     can be used to set a different hash table size, available
     *     from kernel 2.6.39 onwards.  The specified divisor must be
     *     a power of two and cannot be larger than 65536.  Default
     *     value: 1024.
     *        </para>
     *      </listitem>
     *  </varlistentry>
     *      <varlistentry>
     *      <term><varname>limit U32</varname></term>
     *      <listitem>
     *        <para>
     *     Upper limit of the SFQ. Can be used to reduce the default
     *     length of 127 packets.
     *        </para>
     *      </listitem>
     *    </varlistentry>
     *      <varlistentry>
     *      <term><varname>depth U32</varname></term>
     *      <listitem>
     *        <para>
     *     Limit of packets per flow. Default to
     *     127 and can be lowered.
     *        </para>
     *      </listitem>
     *    </varlistentry>
     *      <varlistentry>
     *      <term><varname>perturb_period U32</varname></term>
     *      <listitem>
     *        <para>
     *     Interval in seconds for queue algorithm perturbation.
     *     Defaults to 0, which means that no perturbation occurs. Do
     *     not set too low for each perturbation may cause some
     *     packet reordering or losses. Advised value: 60 This value
     *     has no effect when external flow classification is used.
     *     Its better to increase divisor value to lower risk of hash
     *     collisions.
     *        </para>
     *      </listitem>
     *    </varlistentry>
     *      <varlistentry>
     *      <term><varname>quantum U32</varname></term>
     *      <listitem>
     *        <para>
     *     Amount of bytes a flow is allowed to dequeue during a
     *     round of the round robin process.  Defaults to the MTU of
     *     the interface which is also the advised value and the
     *     minimum value.
     *        </para>
     *      </listitem>
     *    </varlistentry>
     *      <varlistentry>
     *      <term><varname>flows U32</varname></term>
     *      <listitem>
     *        <para>
     *     Default value is 127.
     *        </para>
     *      </listitem>
     *    </varlistentry>
     *  </variablelist>
     *  <para>
     *   Parameters for 'tbf':
     *  </para>
     *  <variablelist>
     *    <varlistentry>
     *      <term><varname>rate U64</varname></term>
     *      <listitem>
     *        <para>
     *     Bandwidth or rate.  These parameters accept a floating
     *     point number, possibly followed by either a unit (both SI
     *     and IEC units supported), or a float followed by a percent
     *     character to specify the rate as a percentage of the
     *     device's speed.
     *        </para>
     *      </listitem>
     *  </varlistentry>
     *      <varlistentry>
     *      <term><varname>burst U32</varname></term>
     *      <listitem>
     *        <para>
     *     Also known as buffer or maxburst.  Size of the bucket, in
     *     bytes. This is the maximum amount of bytes that tokens can
     *     be available for instantaneously.  In general, larger
     *     shaping rates require a larger buffer. For 10mbit/s on
     *     Intel, you need at least 10kbyte buffer if you want to
     *     reach your configured rate!
     *        </para>
     *        <para>
     *     If your buffer is too small, packets may be dropped
     *     because more tokens arrive per timer tick than fit in your
     *     bucket.  The minimum buffer size can be calculated by
     *     dividing the rate by HZ.
     *        </para>
     *        <para>
     *     Token usage calculations are performed using a table which
     *     by default has a resolution of 8 packets.  This resolution
     *     can be changed by specifying the cell size with the burst.
     *     For example, to specify a 6000 byte buffer with a 16 byte
     *     cell size, set a burst of 6000/16. You will probably never
     *     have to set this. Must be an integral power of 2.
     *        </para>
     *      </listitem>
     *    </varlistentry>
     *      <varlistentry>
     *      <term><varname>limit U32</varname></term>
     *      <listitem>
     *        <para>
     *     Limit is the number of bytes that can be queued waiting
     *     for tokens to become available.
     *        </para>
     *      </listitem>
     *    </varlistentry>
     *      <varlistentry>
     *      <term><varname>latency U32</varname></term>
     *      <listitem>
     *        <para>
     *     specifies the maximum amount of time a packet can
     *     sit in the TBF. The latency calculation takes into account
     *     the size of the bucket, the rate and possibly the peakrate
     *     (if set). The latency and limit are mutually exclusive.
     *        </para>
     *      </listitem>
     *    </varlistentry>
     *  </variablelist>
     * ---end---
     **/
    /* ---ifcfg-rh---
     * property: qdiscs
     * variable: QDISC1(+), QDISC2(+), ..., TC_COMMIT(+)
     * description: Queueing disciplines to set on the interface. When no
     *  QDISC1, QDISC2, ..., FILTER1, FILTER2, ... keys are present,
     *  NetworkManager doesn't touch qdiscs and filters present on the
     *  interface, unless TC_COMMIT is set to 'yes'.
     * example: QDISC1=ingress, QDISC2="root handle 1234: fq_codel"
     * ---end---
     */
    obj_properties[PROP_QDISCS] = g_param_spec_boxed(NM_SETTING_TC_CONFIG_QDISCS,
                                                     "",
                                                     "",
                                                     G_TYPE_PTR_ARRAY,
                                                     G_PARAM_READWRITE | NM_SETTING_PARAM_INFERRABLE
                                                         | G_PARAM_STATIC_STRINGS);
    _nm_properties_override_gobj(properties_override,
                                 obj_properties[PROP_QDISCS],
                                 NM_SETT_INFO_PROPERT_TYPE_DBUS(NM_G_VARIANT_TYPE("aa{sv}"),
                                                                .to_dbus_fcn   = tc_qdiscs_get,
                                                                .compare_fcn   = compare_fcn_qdiscs,
                                                                .from_dbus_fcn = tc_qdiscs_set, ));

    /**
     * NMSettingTCConfig:tfilters: (type GPtrArray(NMTCTfilter))
     *
     * Array of TC traffic filters.
     *
     * When the #NMSettingTCConfig setting is present, filters from this
     * property are applied upon activation. If the property is empty,
     * NetworkManager removes all the filters.
     *
     * If the #NMSettingTCConfig setting is not present, NetworkManager
     * doesn't touch the filters present on the interface.
     **/
    /* ---nmcli---
     * property: tfilters
     * format: GPtrArray(NMTCTfilter)
     * description-docbook:
     *  <para>
     * Array of TC traffic filters. Traffic control can manage the packet content during
     * classification by using filters.
     *  </para>
     *  <para>
     *   Each tfilters can be specified by the following attributes:
     *  </para>
     *  <variablelist>
     *    <varlistentry>
     *      <term><varname>handle HANDLE</varname></term>
     *      <listitem>
     *        <para>
     *          specifies the tfilters handle. A filter is used by a classful qdisc to determine in which class
     *          a packet will be enqueued. It is important to notice that filters reside within qdiscs. Therefore,
     *          see qdiscs handle for detailed information.
     *        </para>
     *      </listitem>
     *  </varlistentry>
     *      <varlistentry>
     *      <term><varname>parent HANDLE</varname></term>
     *      <listitem>
     *        <para>
     *          specifies the handle of the parent qdisc the current qdisc must be
     *          attached to.
     *        </para>
     *      </listitem>
     *    </varlistentry>
     *      <varlistentry>
     *      <term><varname>root</varname></term>
     *      <listitem>
     *        <para>
     *          specifies that the qdisc is attached to the root of device.
     *        </para>
     *      </listitem>
     *    </varlistentry>
     *      <varlistentry>
     *      <term><varname>KIND</varname></term>
     *      <listitem>
     *        <para>
     *          this is the tfilters kind. NetworkManager currently supports
     *          following kinds: mirred, simple. Each filter kind has a
     *          different set of actions, described below. There are also some
     *          other kinds like matchall, basic, u32 supported by NetworkManager.
     *        </para>
     *      </listitem>
     *    </varlistentry>
     *  </variablelist>
     *  <para>
     *   Actions for 'mirred':
     *  </para>
     *  <variablelist>
     *    <varlistentry>
     *      <term><varname>egress bool</varname></term>
     *      <listitem>
     *        <para>
     *          Define whether the packet should exit from the interface.
     *        </para>
     *      </listitem>
     *  </varlistentry>
     *      <varlistentry>
     *      <term><varname>ingress bool</varname></term>
     *      <listitem>
     *        <para>
     *          Define whether the packet should come into the interface.
     *        </para>
     *      </listitem>
     *    </varlistentry>
     *      <varlistentry>
     *      <term><varname>mirror bool</varname></term>
     *      <listitem>
     *        <para>
     *     Define whether the packet should be copied to the destination space.
     *        </para>
     *      </listitem>
     *    </varlistentry>
     *      <varlistentry>
     *      <term><varname>redirect bool</varname></term>
     *      <listitem>
     *        <para>
     *     Define whether the packet should be moved to the destination space.
     *        </para>
     *      </listitem>
     *    </varlistentry>
     *  </variablelist>
     *  <para>
     *   Action for 'simple':
     *  </para>
     *  <variablelist>
     *    <varlistentry>
     *      <term><varname>sdata char[32]</varname></term>
     *      <listitem>
     *        <para>
     *      The actual string to print.
     *        </para>
     *      </listitem>
     *  </varlistentry>
     *  </variablelist>
     * ---end---
     **/
    /* ---ifcfg-rh---
     * property: qdiscs
     * variable: FILTER1(+), FILTER2(+), ..., TC_COMMIT(+)
     * description: Traffic filters to set on the interface. When no
     *  QDISC1, QDISC2, ..., FILTER1, FILTER2, ... keys are present,
     *  NetworkManager doesn't touch qdiscs and filters present on the
     *  interface, unless TC_COMMIT is set to 'yes'.
     * example: FILTER1="parent ffff: matchall action simple sdata Input", ...
     * ---end---
     */
    obj_properties[PROP_TFILTERS] = g_param_spec_boxed(
        NM_SETTING_TC_CONFIG_TFILTERS,
        "",
        "",
        G_TYPE_PTR_ARRAY,
        G_PARAM_READWRITE | NM_SETTING_PARAM_INFERRABLE | G_PARAM_STATIC_STRINGS);
    _nm_properties_override_gobj(
        properties_override,
        obj_properties[PROP_TFILTERS],
        NM_SETT_INFO_PROPERT_TYPE_DBUS(NM_G_VARIANT_TYPE("aa{sv}"),
                                       .to_dbus_fcn   = tc_tfilters_get,
                                       .compare_fcn   = compare_fcn_tfilter,
                                       .from_dbus_fcn = tc_tfilters_set, ));

    g_object_class_install_properties(object_class, _PROPERTY_ENUMS_LAST, obj_properties);

    _nm_setting_class_commit(setting_class,
                             NM_META_SETTING_TYPE_TC_CONFIG,
                             NULL,
                             properties_override,
                             0);
}
