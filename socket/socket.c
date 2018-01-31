/*
 * This file is part of the Nice GLib ICE library.
 *
 * (C) 2008-2009 Collabora Ltd.
 *  Contact: Youness Alaoui
 * (C) 2008-2009 Nokia Corporation. All rights reserved.
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is the Nice GLib ICE library.
 *
 * The Initial Developers of the Original Code are Collabora Ltd and Nokia
 * Corporation. All Rights Reserved.
 *
 * Contributors:
 *   Youness Alaoui, Collabora Ltd.
 *
 * Alternatively, the contents of this file may be used under the terms of the
 * the GNU Lesser General Public License Version 2.1 (the "LGPL"), in which
 * case the provisions of LGPL are applicable instead of those above. If you
 * wish to allow use of your version of this file only under the terms of the
 * LGPL and not to allow others to use your version of this file under the
 * MPL, indicate your decision by deleting the provisions above and replace
 * them with the notice and other provisions required by the LGPL. If you do
 * not delete the provisions above, a recipient may use your version of this
 * file under either the MPL or the LGPL.
 */
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif


#include <glib.h>

#include "socket.h"


gint
nice_socket_recv (NiceSocket *sock, NiceAddress *from, guint len, gchar *buf)
{
  return sock->recv (sock, from, len, buf);
}

gboolean
nice_socket_send (NiceSocket *sock, const NiceAddress *to,
    guint len, const gchar *buf)
{
  return sock->send (sock, to, len, buf);
}

gint
nice_socket_send_message (NiceSocket *sock, const NiceAddress *to,
    guint len, const gchar *buf, gchar *error)
{
  if (sock->send_message != NULL) {
    return sock->send_message (sock, to, len, buf, error);
  } else {
    memcpy(error, "send function is NULL.", strlen("send function is NULL."));
    return -1;
  }
}

gboolean
nice_socket_is_reliable (NiceSocket *sock)
{
  return sock->is_reliable (sock);
}

void
nice_socket_free (NiceSocket *sock)
{
  if (sock) {
    sock->close (sock);
    g_slice_free (NiceSocket,sock);
  }
}

