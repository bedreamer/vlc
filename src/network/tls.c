/*****************************************************************************
 * tls.c
 *****************************************************************************
 * Copyright © 2004-2007 Rémi Denis-Courmont
 * $Id$
 *
 * Authors: Rémi Denis-Courmont <rem # videolan.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

/**
 * @file
 * libvlc interface to the Transport Layer Security (TLS) plugins.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include "libvlc.h"

#include <vlc_tls.h>
#include <vlc_modules.h>

/*** TLS credentials ***/

static int tls_server_load(void *func, va_list ap)
{
    int (*activate) (vlc_tls_creds_t *, const char *, const char *) = func;
    vlc_tls_creds_t *crd = va_arg (ap, vlc_tls_creds_t *);
    const char *cert = va_arg (ap, const char *);
    const char *key = va_arg (ap, const char *);

    return activate (crd, cert, key);
}

static int tls_client_load(void *func, va_list ap)
{
    int (*activate) (vlc_tls_creds_t *) = func;
    vlc_tls_creds_t *crd = va_arg (ap, vlc_tls_creds_t *);

    return activate (crd);
}

static void tls_unload(void *func, va_list ap)
{
    void (*deactivate) (vlc_tls_creds_t *) = func;
    vlc_tls_creds_t *crd = va_arg (ap, vlc_tls_creds_t *);

    deactivate (crd);
}

/**
 * Allocates a whole server's TLS credentials.
 *
 * @param cert_path required (Unicode) path to an x509 certificate,
 *                  if NULL, anonymous key exchange will be used.
 * @param key_path (UTF-8) path to the PKCS private key for the certificate,
 *                 if NULL; cert_path will be used.
 *
 * @return NULL on error.
 */
vlc_tls_creds_t *
vlc_tls_ServerCreate (vlc_object_t *obj, const char *cert_path,
                      const char *key_path)
{
    vlc_tls_creds_t *srv = vlc_custom_create (obj, sizeof (*srv),
                                              "tls server");
    if (unlikely(srv == NULL))
        return NULL;

    if (key_path == NULL)
        key_path = cert_path;

    srv->module = vlc_module_load (srv, "tls server", NULL, false,
                                   tls_server_load, srv, cert_path, key_path);
    if (srv->module == NULL)
    {
        msg_Err (srv, "TLS server plugin not available");
        vlc_object_release (srv);
        return NULL;
    }

    msg_Dbg (srv, "TLS server plugin initialized");
    return srv;
}


/**
 * Releases data allocated with vlc_tls_ServerCreate().
 * @param srv TLS server object to be destroyed, or NULL
 */
void vlc_tls_Delete (vlc_tls_creds_t *crd)
{
    if (crd == NULL)
        return;

    vlc_module_unload (crd->module, tls_unload, crd);
    vlc_object_release (crd);
}


/**
 * Adds one or more certificate authorities from a file.
 * @return -1 on error, 0 on success.
 */
int vlc_tls_ServerAddCA (vlc_tls_creds_t *srv, const char *path)
{
    return srv->add_CA (srv, path);
}


/**
 * Adds one or more certificate revocation list from a file.
 * @return -1 on error, 0 on success.
 */
int vlc_tls_ServerAddCRL (vlc_tls_creds_t *srv, const char *path)
{
    return srv->add_CRL (srv, path);
}


/*** TLS  session ***/

static vlc_tls_t *vlc_tls_SessionCreate (vlc_tls_creds_t *crd, int fd,
                                         const char *hostname)
{
    vlc_tls_t *session = vlc_custom_create (crd, sizeof (*session),
                                            "tls session");
    int val = crd->open (crd, session, fd, hostname);
    if (val == VLC_SUCCESS)
        return session;
    vlc_object_release (session);
    return NULL;
}

void vlc_tls_SessionDelete (vlc_tls_t *session)
{
    vlc_tls_creds_t *crd = (vlc_tls_creds_t *)(session->p_parent);

    crd->close (crd, session);
    vlc_object_release (session);
}

vlc_tls_t *vlc_tls_ServerSessionCreate (vlc_tls_creds_t *crd, int fd)
{
    return vlc_tls_SessionCreate (crd, fd, NULL);
}

int vlc_tls_ServerSessionHandshake (vlc_tls_t *ses)
{
    int val = ses->handshake (ses);
    if (val < 0)
        vlc_tls_ServerSessionDelete (ses);
    return val;
}

/**
 * Allocates a client's TLS credentials and shakes hands through the network.
 * This is a blocking network operation.
 *
 * @param fd stream socket through which to establish the secure communication
 * layer.
 * @param psz_hostname Server Name Indication to pass to the server, or NULL.
 *
 * @return NULL on error.
 **/
vlc_tls_t *
vlc_tls_ClientCreate (vlc_object_t *obj, int fd, const char *hostname)
{
    vlc_tls_creds_t *crd = vlc_custom_create (obj, sizeof (*crd),
                                              "tls client");
    if (unlikely(crd == NULL))
        return NULL;

    crd->module = vlc_module_load (crd, "tls client", NULL, false,
                                   tls_client_load, crd);
    if (crd->module == NULL)
    {
        msg_Err (crd, "TLS client plugin not available");
        vlc_object_release (crd);
        return NULL;
    }

    /* TODO: separate credentials and sessions, so we do not reload the
     * credentials every time the HTTP access seeks... */
    vlc_tls_t *session = vlc_tls_SessionCreate (crd, fd, hostname);
    if (session == NULL)
        goto error;

    /* TODO: do this directly in the TLS plugin */
    int val;
    do
        val = session->handshake (session);
    while (val > 0);

    if (val != 0)
    {
        msg_Err (session, "TLS client session handshake error");
        vlc_tls_SessionDelete (session);
        goto error;
    }
    msg_Dbg (session, "TLS client session initialized");
    return session;
error:
    vlc_tls_Delete (crd);
    return NULL;
}


/**
 * Releases data allocated with vlc_tls_ClientCreate().
 * It is your job to close the underlying socket.
 */
void vlc_tls_ClientDelete (vlc_tls_t *session)
{
    if (session == NULL)
        return;

    vlc_tls_creds_t *cl = (vlc_tls_creds_t *)(session->p_parent);

    vlc_tls_SessionDelete (session);
    vlc_tls_Delete (cl);
}
