/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2010 - 2018 Red Hat, Inc.
 */

#include "src/core/nm-default-daemon.h"

#include "nms-keyfile-utils.h"

#include <stdlib.h>
#include <sys/stat.h>
#include <glob.h>

#include "libnm-glib-aux/nm-uuid.h"
#include "libnm-glib-aux/nm-io-utils.h"
#include "libnm-core-intern/nm-keyfile-internal.h"
#include "nm-utils.h"
#include "nm-setting-wired.h"
#include "nm-setting-wireless.h"
#include "nm-setting-wireless-security.h"
#include "nm-config.h"

/*****************************************************************************/

#define NMMETA_KF_GROUP_NAME_NMMETA                "nmmeta"
#define NMMETA_KF_KEY_NAME_NMMETA_UUID             "uuid"
#define NMMETA_KF_KEY_NAME_NMMETA_LOADED_PATH      "loaded-path"
#define NMMETA_KF_KEY_NAME_NMMETA_SHADOWED_STORAGE "shadowed-storage"

/*****************************************************************************/

/* This function can be removed, once https://pad.lv/1927350 is resolved */
gboolean
_fix_netplan_interface_name(const char* rootdir)
{
    glob_t gl;
    mode_t orig_umask;
    int rc;
    g_autofree char* path = NULL;
    gchar *iface = NULL;
    path = g_build_path(G_DIR_SEPARATOR_S, rootdir ?: "/", "run",
                        "NetworkManager", "system-connections", NULL);
    g_autofree char* rglob = g_strjoin(NULL, path, G_DIR_SEPARATOR_S,
                                       "*.nmconnection", NULL);
    rc = glob(rglob, GLOB_BRACE, NULL, &gl);
    if (rc != 0 && rc != GLOB_NOMATCH) {
        g_warning ("failed to glob for %s: %m", rglob);
        return FALSE;
    }

    for (size_t i = 0; i < gl.gl_pathc; ++i) {
        GKeyFile *kf = g_key_file_new ();
        g_key_file_load_from_file (kf, gl.gl_pathv[i], G_KEY_FILE_KEEP_COMMENTS, NULL);
        iface = g_key_file_get_string (kf, "connection", "interface-name", NULL);
        if (iface && g_str_has_prefix (iface, "NM-") && strlen (iface) > 15) {
            g_key_file_remove_key (kf, "connection", "interface-name", NULL);
            orig_umask = umask(077);
            if (!g_key_file_save_to_file (kf, gl.gl_pathv[i], NULL)) {
                g_warning ("failed to write updated keyfile %s", gl.gl_pathv[i]);
                globfree(&gl);
                return FALSE;
            }
            g_info("netplan: deleted invalid connection.interface-name=%s in %s",
                   iface, gl.gl_pathv[i]);
            umask(orig_umask);
        }
        g_free (iface);
        g_key_file_free (kf);
    }
    globfree(&gl);
    return TRUE;
}

const char *
nms_keyfile_nmmeta_check_filename(const char *filename, guint *out_uuid_len)
{
    const char *s;
    gsize       len;
    char        uuid[37];

    s = strrchr(filename, '/');
    if (s)
        filename = &s[1];

    len = strlen(filename);
    if (len <= NM_STRLEN(NM_KEYFILE_PATH_SUFFIX_NMMETA)
        || memcmp(&filename[len - NM_STRLEN(NM_KEYFILE_PATH_SUFFIX_NMMETA)],
                  NM_KEYFILE_PATH_SUFFIX_NMMETA,
                  NM_STRLEN(NM_KEYFILE_PATH_SUFFIX_NMMETA))
               != 0) {
        /* the filename does not have the right suffix. */
        return NULL;
    }

    len -= NM_STRLEN(NM_KEYFILE_PATH_SUFFIX_NMMETA);

    if (len != 36) {
        /* the remaining part of the filename has not the right length to
         * contain a UUID (according to nm_uuid_is_normalized()). */
        return NULL;
    }

    memcpy(uuid, filename, 36);
    uuid[36] = '\0';
    if (!nm_uuid_is_normalized(uuid))
        return NULL;

    NM_SET_OUT(out_uuid_len, 36);
    return filename;
}

char *
nms_keyfile_nmmeta_filename(const char *dirname, const char *uuid, gboolean temporary)
{
    char  filename[250];
    char *s;

    nm_assert(dirname && dirname[0] == '/');
    nm_assert(nm_uuid_is_normalized(uuid) && !strchr(uuid, '/'));

    if (g_snprintf(filename,
                   sizeof(filename),
                   "%s%s%s",
                   uuid,
                   NM_KEYFILE_PATH_SUFFIX_NMMETA,
                   temporary ? "~" : "")
        >= sizeof(filename)) {
        /* valid uuids are limited in length (nm_uuid_is_normalized). The buffer should always
         * be large enough. */
        nm_assert_not_reached();
    }

    s = g_build_filename(dirname, filename, NULL);

    nm_assert(nm_keyfile_utils_ignore_filename(s, FALSE));

    return s;
}

gboolean
nms_keyfile_nmmeta_read(const char  *dirname,
                        const char  *filename,
                        char       **out_full_filename,
                        char       **out_uuid,
                        char       **out_loaded_path,
                        char       **out_shadowed_storage,
                        struct stat *out_st)
{
    const char   *uuid;
    guint         uuid_len;
    gs_free char *full_filename    = NULL;
    gs_free char *loaded_path      = NULL;
    gs_free char *shadowed_storage = NULL;
    struct stat   st_stack;
    struct stat  *st = out_st ?: &st_stack;

    nm_assert(dirname && dirname[0] == '/');
    nm_assert(filename && filename[0] && !strchr(filename, '/'));

    uuid = nms_keyfile_nmmeta_check_filename(filename, &uuid_len);
    if (!uuid)
        return FALSE;

    full_filename = g_build_filename(dirname, filename, NULL);

    if (!nms_keyfile_utils_check_file_permissions(NMS_KEYFILE_FILETYPE_NMMETA,
                                                  full_filename,
                                                  st,
                                                  NULL))
        return FALSE;

    if (S_ISREG(st->st_mode)) {
        nm_auto_unref_keyfile GKeyFile *kf     = NULL;
        gs_free char                   *v_uuid = NULL;

        kf = g_key_file_new();

        if (!g_key_file_load_from_file(kf, full_filename, G_KEY_FILE_NONE, NULL))
            return FALSE;

        v_uuid = g_key_file_get_string(kf,
                                       NMMETA_KF_GROUP_NAME_NMMETA,
                                       NMMETA_KF_KEY_NAME_NMMETA_UUID,
                                       NULL);
        if (!v_uuid)
            return FALSE;
        if (strncmp(v_uuid, uuid, uuid_len) != 0)
            return FALSE;
        if (v_uuid[uuid_len] != '\0')
            return FALSE;

        loaded_path      = g_key_file_get_string(kf,
                                            NMMETA_KF_GROUP_NAME_NMMETA,
                                            NMMETA_KF_KEY_NAME_NMMETA_LOADED_PATH,
                                            NULL);
        shadowed_storage = g_key_file_get_string(kf,
                                                 NMMETA_KF_GROUP_NAME_NMMETA,
                                                 NMMETA_KF_KEY_NAME_NMMETA_SHADOWED_STORAGE,
                                                 NULL);

        if (!loaded_path && !shadowed_storage) {
            /* if there is no useful information in the file, it is the same as if
             * the file is not present. Signal failure. */
            return FALSE;
        }

    } else {
        loaded_path = nm_utils_read_link_absolute(full_filename, NULL);
        if (!loaded_path)
            return FALSE;
    }

    NM_SET_OUT(out_uuid, g_strndup(uuid, uuid_len));
    NM_SET_OUT(out_full_filename, g_steal_pointer(&full_filename));
    NM_SET_OUT(out_loaded_path, g_steal_pointer(&loaded_path));
    NM_SET_OUT(out_shadowed_storage, g_steal_pointer(&shadowed_storage));
    return TRUE;
}

gboolean
nms_keyfile_nmmeta_read_from_file(const char *full_filename,
                                  char      **out_dirname,
                                  char      **out_filename,
                                  char      **out_uuid,
                                  char      **out_loaded_path,
                                  char      **out_shadowed_storage)
{
    gs_free char *dirname  = NULL;
    gs_free char *filename = NULL;

    nm_assert(full_filename && full_filename[0] == '/');

    filename = g_path_get_basename(full_filename);
    dirname  = g_path_get_dirname(full_filename);

    if (!nms_keyfile_nmmeta_read(dirname,
                                 filename,
                                 NULL,
                                 out_uuid,
                                 out_loaded_path,
                                 out_shadowed_storage,
                                 NULL))
        return FALSE;

    NM_SET_OUT(out_dirname, g_steal_pointer(&dirname));
    NM_SET_OUT(out_filename, g_steal_pointer(&filename));
    return TRUE;
}

int
nms_keyfile_nmmeta_write(const char *dirname,
                         const char *uuid,
                         const char *loaded_path,
                         gboolean    loaded_path_allow_relative,
                         const char *shadowed_storage,
                         char      **out_full_filename)
{
    gs_free char *full_filename_tmp = NULL;
    gs_free char *full_filename     = NULL;
    int           errsv;

    nm_assert(dirname && dirname[0] == '/');
    nm_assert(nm_uuid_is_normalized(uuid) && !strchr(uuid, '/'));
    nm_assert(!loaded_path || loaded_path[0] == '/');
    nm_assert(!shadowed_storage || loaded_path);

    full_filename_tmp = nms_keyfile_nmmeta_filename(dirname, uuid, TRUE);

    nm_assert(g_str_has_suffix(full_filename_tmp, "~"));
    nm_assert(nm_utils_file_is_in_path(full_filename_tmp, dirname));

    (void) unlink(full_filename_tmp);

    if (!loaded_path) {
        full_filename_tmp[strlen(full_filename_tmp) - 1] = '\0';
        errsv                                            = 0;
        if (unlink(full_filename_tmp) != 0) {
            errsv = -NM_ERRNO_NATIVE(errno);
            if (errsv == -ENOENT)
                errsv = 0;
        }
        NM_SET_OUT(out_full_filename, g_steal_pointer(&full_filename_tmp));
        return errsv;
    }

    if (loaded_path_allow_relative) {
        const char *f;

        f = nm_utils_file_is_in_path(loaded_path, dirname);
        if (f) {
            /* @loaded_path points to a file directly in @dirname.
             * Don't use absolute paths. */
            loaded_path = f;
        }
    }

    full_filename = g_strndup(full_filename_tmp, strlen(full_filename_tmp) - 1);

    if (shadowed_storage) {
        nm_auto_unref_keyfile GKeyFile *kf       = NULL;
        gs_free char                   *contents = NULL;
        gsize                           length;

        kf = g_key_file_new();

        g_key_file_set_string(kf,
                              NMMETA_KF_GROUP_NAME_NMMETA,
                              NMMETA_KF_KEY_NAME_NMMETA_UUID,
                              uuid);
        g_key_file_set_string(kf,
                              NMMETA_KF_GROUP_NAME_NMMETA,
                              NMMETA_KF_KEY_NAME_NMMETA_LOADED_PATH,
                              loaded_path);
        g_key_file_set_string(kf,
                              NMMETA_KF_GROUP_NAME_NMMETA,
                              NMMETA_KF_KEY_NAME_NMMETA_SHADOWED_STORAGE,
                              shadowed_storage);

        contents = g_key_file_to_data(kf, &length, NULL);

        if (!nm_utils_file_set_contents(full_filename,
                                        contents,
                                        length,
                                        0600,
                                        NULL,
                                        &errsv,
                                        NULL)) {
            NM_SET_OUT(out_full_filename, g_steal_pointer(&full_filename_tmp));
            return -NM_ERRNO_NATIVE(errsv);
        }
    } else {
        /* we only have the "loaded_path" to store. That is commonly used for the tombstones to
         * link to /dev/null. A symlink is sufficient to store that amount of information.
         * No need to bother with a keyfile. */
        if (symlink(loaded_path, full_filename_tmp) != 0) {
            errsv                                            = -NM_ERRNO_NATIVE(errno);
            full_filename_tmp[strlen(full_filename_tmp) - 1] = '\0';
            NM_SET_OUT(out_full_filename, g_steal_pointer(&full_filename_tmp));
            return errsv;
        }

        if (rename(full_filename_tmp, full_filename) != 0) {
            errsv = -NM_ERRNO_NATIVE(errno);
            (void) unlink(full_filename_tmp);
            NM_SET_OUT(out_full_filename, g_steal_pointer(&full_filename));
            return errsv;
        }
    }

    NM_SET_OUT(out_full_filename, g_steal_pointer(&full_filename));
    return 0;
}

/*****************************************************************************/

gboolean
nms_keyfile_utils_check_file_permissions_stat(NMSKeyfileFiletype filetype,
                                              const struct stat *st,
                                              GError           **error)
{
    g_return_val_if_fail(st, FALSE);

    if (filetype == NMS_KEYFILE_FILETYPE_KEYFILE) {
        if (!S_ISREG(st->st_mode)) {
            g_set_error_literal(error,
                                NM_SETTINGS_ERROR,
                                NM_SETTINGS_ERROR_INVALID_CONNECTION,
                                "file is not a regular file");
            return FALSE;
        }
    } else if (filetype == NMS_KEYFILE_FILETYPE_NMMETA) {
        if (!S_ISLNK(st->st_mode) && !S_ISREG(st->st_mode)) {
            g_set_error_literal(error,
                                NM_SETTINGS_ERROR,
                                NM_SETTINGS_ERROR_INVALID_CONNECTION,
                                "file is neither a symlink nor a regular file");
            return FALSE;
        }
    } else
        g_return_val_if_reached(FALSE);

    if (!NM_FLAGS_HAS(nm_utils_get_testing(), NM_UTILS_TEST_NO_KEYFILE_OWNER_CHECK)) {
        if (!NM_IN_SET(st->st_uid, 0, nm_utils_get_nm_uid())) {
            g_set_error(error,
                        NM_SETTINGS_ERROR,
                        NM_SETTINGS_ERROR_INVALID_CONNECTION,
                        "File owner (%lld) is insecure",
                        (long long) st->st_uid);
            return FALSE;
        }

        if (S_ISREG(st->st_mode) && (st->st_mode & 0077)) {
            g_set_error(error,
                        NM_SETTINGS_ERROR,
                        NM_SETTINGS_ERROR_INVALID_CONNECTION,
                        "File permissions (%03o) are insecure",
                        st->st_mode);
            return FALSE;
        }
    }

    return TRUE;
}

gboolean
nms_keyfile_utils_check_file_permissions(NMSKeyfileFiletype filetype,
                                         const char        *filename,
                                         struct stat       *out_st,
                                         GError           **error)
{
    struct stat st;
    int         errsv;

    g_return_val_if_fail(filename && filename[0] == '/', FALSE);

    if (filetype == NMS_KEYFILE_FILETYPE_KEYFILE) {
        if (stat(filename, &st) != 0) {
            errsv = errno;
            g_set_error(error,
                        NM_SETTINGS_ERROR,
                        NM_SETTINGS_ERROR_INVALID_CONNECTION,
                        "cannot access file: %s",
                        nm_strerror_native(errsv));
            return FALSE;
        }
    } else if (filetype == NMS_KEYFILE_FILETYPE_NMMETA) {
        if (lstat(filename, &st) != 0) {
            errsv = errno;
            g_set_error(error,
                        NM_SETTINGS_ERROR,
                        NM_SETTINGS_ERROR_INVALID_CONNECTION,
                        "cannot access file: %s",
                        nm_strerror_native(errsv));
            return FALSE;
        }
    } else
        g_return_val_if_reached(FALSE);

    if (!nms_keyfile_utils_check_file_permissions_stat(filetype, &st, error))
        return FALSE;

    NM_SET_OUT(out_st, st);
    return TRUE;
}
