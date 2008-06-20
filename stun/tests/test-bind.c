/*
 * This file is part of the Nice GLib ICE library.
 *
 * (C) 2007 Nokia Corporation. All rights reserved.
 *  Contact: Rémi Denis-Courmont
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
 *   Rémi Denis-Courmont, Nokia
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
# include <config.h>
#endif

#include <sys/types.h>
#include <sys/socket.h>
#include "stun/stunagent.h"
#include "stun/usages/bind.h"

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <netdb.h>

#undef NDEBUG /* ensure assertions are built-in */
#include <assert.h>


static int listen_dgram (void)
{
  struct addrinfo hints, *res;
  int val = -1;

  memset (&hints, 0, sizeof (hints));
  hints.ai_socktype = SOCK_DGRAM;

  if (getaddrinfo (NULL, "0", &hints, &res))
    return -1;

  for (const struct addrinfo *ptr = res; ptr != NULL; ptr = ptr->ai_next)
  {
    int fd = socket (ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
    if (fd == -1)
      continue;

    if (bind (fd, ptr->ai_addr, ptr->ai_addrlen))
    {
      close (fd);
      continue;
    }

    val = fd;
    break;
  }

  freeaddrinfo (res);
  return val;
}


/** Incorrect socket family test */
static void bad_family (void)
{
  struct sockaddr addr, dummy;
  int val;

  memset (&addr, 0, sizeof (addr));
  addr.sa_family = AF_UNSPEC;
#ifdef HAVE_SA_LEN
  addr.sa_len = sizeof (addr);
#endif

  val = stun_bind_run (-1, &addr, sizeof (addr),
                       &dummy, &(socklen_t){ sizeof (dummy) }, 0);
  assert (val != 0);
}


/** Too small socket address test */
static void small_srv_addr (void)
{
  struct sockaddr addr, dummy;
  int val;

  memset (&addr, 0, sizeof (addr));
  addr.sa_family = AF_INET;
#ifdef HAVE_SA_LEN
  addr.sa_len = sizeof (addr);
#endif

  val = stun_bind_run (-1, &addr, 1,
                       &dummy, &(socklen_t){ sizeof (dummy) }, 0);
  assert (val == EINVAL);
}


/** Too big socket address test */
static void big_srv_addr (void)
{
  uint8_t buf[sizeof (struct sockaddr_storage) + 16];
  struct sockaddr dummy;
  int fd, val;

  fd = socket (AF_INET, SOCK_DGRAM, 0);
  assert (fd != -1);

  memset (buf, 0, sizeof (buf));
  val = stun_bind_run (fd, (struct sockaddr *)buf, sizeof (buf),
                       &dummy, &(socklen_t){ sizeof (dummy) }, 0);
  assert (val == ENOBUFS);
  close (fd);
}


/** Timeout test */
static void timeout (void)
{
  struct sockaddr_storage srv;
  struct sockaddr dummy;
  socklen_t srvlen = sizeof (srv);
  int val;

  /* Allocate a local UDP port, so we are 100% sure nobody responds there */
  int servfd = listen_dgram ();
  assert (servfd != -1);

  val = getsockname (servfd, (struct sockaddr *)&srv, &srvlen);
  assert (val == 0);

  val = stun_bind_run (-1, (struct sockaddr *)&srv, srvlen,
                       &dummy, &(socklen_t){ sizeof (dummy) }, 0);
  assert (val == ETIMEDOUT);

  close (servfd);
}


/** Malformed responses test */
static void bad_responses (void)
{
  stun_bind_t *ctx;
  struct sockaddr_storage addr;
  socklen_t addrlen = sizeof (addr);
  ssize_t val, len;
  uint8_t buf[1000];

  /* Allocate a local UDP port */
  int servfd = listen_dgram (), fd;
  assert (servfd != -1);

  val = getsockname (servfd, (struct sockaddr *)&addr, &addrlen);
  assert (val == 0);

  fd = socket (addr.ss_family, SOCK_DGRAM, 0);
  assert (fd != -1);

  val = stun_bind_start (&ctx, fd, (struct sockaddr *)&addr, addrlen, 0);
  assert (val == 0);

  /* Send to/receive from our client instance only */
  val = getsockname (fd, (struct sockaddr *)&addr, &addrlen);
  assert (val == 0);

  val = connect (servfd, (struct sockaddr *)&addr, addrlen);
  assert (val == 0);

  /* Send crap response */
  val = getsockname (servfd, (struct sockaddr *)&addr, &addrlen);
  assert (val == 0);
  val = stun_bind_process (ctx, "foobar", 6,
                           (struct sockaddr *)&addr, &addrlen);
  assert (val == EAGAIN);

  /* Send request instead of response */
  val = getsockname (servfd, (struct sockaddr *)&addr, &addrlen);
  assert (val == 0);
  len = recv (servfd, buf, 1000, MSG_DONTWAIT);
  assert (len >= 20);

  val = stun_bind_process (ctx, buf, len,
                           (struct sockaddr *)&addr, &addrlen);
  assert (val == EAGAIN);

  /* Send response with wrong request type */
  buf[0] |= 0x03;
  val = stun_bind_process (ctx, buf, len,
                           (struct sockaddr *)&addr, &addrlen);
  assert (val == EAGAIN);
  buf[0] ^= 0x02;

  /* Send error response without ERROR-CODE */
  buf[1] |= 0x10;
  val = stun_bind_process (ctx, buf, len,
                           (struct sockaddr *)&addr, &addrlen);
  assert (val == EAGAIN);

  stun_bind_cancel (ctx);
  close (fd);
  close (servfd);
}


/** Various responses test */
static void responses (void)
{
  stun_bind_t *ctx;
  struct sockaddr_storage addr;
  socklen_t addrlen = sizeof (addr);
  ssize_t val;
  size_t len;
  int servfd, fd;
  uint8_t buf[STUN_MAX_MESSAGE_SIZE];
  StunAgent agent;
  StunMessage msg;

  uint16_t known_attributes[] = {
    STUN_ATTRIBUTE_MAPPED_ADDRESS,
    STUN_ATTRIBUTE_XOR_MAPPED_ADDRESS,
    STUN_ATTRIBUTE_XOR_INTERNAL_ADDRESS,
    STUN_ATTRIBUTE_PRIORITY,
    STUN_ATTRIBUTE_USERNAME,
    STUN_ATTRIBUTE_MESSAGE_INTEGRITY,
    STUN_ATTRIBUTE_ERROR_CODE, 0};

  stun_agent_init (&agent, known_attributes,
      STUN_COMPATIBILITY_3489BIS, 0);

  /* Allocate a local UDP port for server */
  servfd = listen_dgram ();
  assert (servfd != -1);

  val = getsockname (servfd, (struct sockaddr *)&addr, &addrlen);
  assert (val == 0);

  /* Allocate a client socket and connect to server */
  fd = socket (addr.ss_family, SOCK_DGRAM, 0);
  assert (fd != -1);

  val = connect (fd, (struct sockaddr *)&addr, addrlen);
  assert (val == 0);

  /* Send to/receive from our client instance only */
  val = getsockname (fd, (struct sockaddr *)&addr, &addrlen);
  assert (val == 0);

  val = connect (servfd, (struct sockaddr *)&addr, addrlen);
  assert (val == 0);

  /* Send error response */
  val = stun_bind_start (&ctx, fd, NULL, 0, 0);
  assert (val == 0);

  val = recv (servfd, buf, 1000, MSG_DONTWAIT);
  assert (val >= 0);

  assert (stun_agent_validate (&agent, &msg, buf, val, NULL, NULL)
      == STUN_VALIDATION_SUCCESS);

  stun_agent_init_error (&agent, &msg, buf, sizeof (buf), &msg, STUN_ERROR_SERVER_ERROR);
  len = stun_agent_finish_message (&agent, &msg, NULL, 0);
  assert (len > 0);

  val = getsockname (servfd, (struct sockaddr *)&addr, &addrlen);
  assert (val == 0);
  val = stun_bind_process (ctx, buf, len,
                           (struct sockaddr *)&addr, &addrlen);
  assert (val == ECONNREFUSED);

  /* Send response with an unknown attribute */
  val = stun_bind_start (&ctx, fd, NULL, 0, 0);
  assert (val == 0);

  val = recv (servfd, buf, 1000, MSG_DONTWAIT);
  assert (val >= 0);

  assert (stun_agent_validate (&agent, &msg, buf, val, NULL, NULL)
      == STUN_VALIDATION_SUCCESS);

  stun_agent_init_response (&agent, &msg, buf, sizeof (buf), &msg);
  val = stun_message_append_string (&msg, 0x6000,
                            "This is an unknown attribute!");
  assert (val == 0);
  len = stun_agent_finish_message (&agent, &msg, NULL, 0);
  assert (len > 0);


  val = getsockname (servfd, (struct sockaddr *)&addr, &addrlen);
  assert (val == 0);

  val = stun_bind_process (ctx, buf, len,
                           (struct sockaddr *)&addr, &addrlen);
  assert (val == EPROTO);

  /* Send response with a no mapped address at all */
  val = stun_bind_start (&ctx, fd, NULL, 0, 0);
  assert (val == 0);

  val = recv (servfd, buf, 1000, MSG_DONTWAIT);
  assert (val >= 0);

  assert (stun_agent_validate (&agent, &msg, buf, val, NULL, NULL)
      == STUN_VALIDATION_SUCCESS);

  stun_agent_init_response (&agent, &msg, buf, sizeof (buf), &msg);
  len = stun_agent_finish_message (&agent, &msg, NULL, 0);
  assert (len > 0);

  val = getsockname (servfd, (struct sockaddr *)&addr, &addrlen);
  assert (val == 0);

  val = stun_bind_process (ctx, buf, len,
                           (struct sockaddr *)&addr, &addrlen);
  assert (val == ENOENT);

  /* Send old-style response */
  val = stun_bind_start (&ctx, fd, NULL, 0, 0);
  assert (val == 0);

  val = recv (servfd, buf, 1000, MSG_DONTWAIT);
  assert (val >= 0);

  assert (stun_agent_validate (&agent, &msg, buf, val, NULL, NULL)
      == STUN_VALIDATION_SUCCESS);

  stun_agent_init_response (&agent, &msg, buf, sizeof (buf), &msg);
  val = stun_message_append_addr (&msg, STUN_ATTRIBUTE_MAPPED_ADDRESS,
                          (struct sockaddr *)&addr, addrlen);
  assert (val == 0);
  len = stun_agent_finish_message (&agent, &msg, NULL, 0);
  assert (len > 0);

  val = getsockname (servfd, (struct sockaddr *)&addr, &addrlen);
  assert (val == 0);

  val = stun_bind_process (ctx, buf, len,
                           (struct sockaddr *)&addr, &addrlen);
  assert (val == 0);

  /* End */
  close (servfd);

  val = close (fd);
  assert (val == 0);
}


static void keepalive (void)
{
  struct sockaddr_storage addr;
  socklen_t addrlen = sizeof (addr);
  int val, servfd, fd;

  /* Allocate a local UDP port for server */
  servfd = listen_dgram ();
  assert (servfd != -1);

  val = getsockname (servfd, (struct sockaddr *)&addr, &addrlen);
  assert (val == 0);

  /* Allocate a client socket and connect to server */
  fd = socket (addr.ss_family, SOCK_DGRAM, 0);
  assert (fd != -1);

  /* Keep alive sending smoke test */
  val = stun_bind_keepalive (fd, (struct sockaddr *)&addr, addrlen, 0);
  assert (val == 0);

  /* Wrong address family test */
  addr.ss_family = addr.ss_family == AF_INET ? AF_INET6 : AF_INET;
  val = stun_bind_keepalive (fd, (struct sockaddr *)&addr, addrlen, 0);
  assert (val != 0);

  /* End */
  close (servfd);

  val = close (fd);
  assert (val == 0);
}


static void test (void (*func) (void), const char *name)
{
  //alarm (10);

  printf ("%s test... ", name);
  func ();
  puts ("OK");
}


int main (void)
{
  test (bad_family, "Bad socket family");
  test (small_srv_addr, "Too small server address");
  test (big_srv_addr, "Too big server address");
  test (bad_responses, "Bad responses");
  test (responses, "Error responses");
  test (keepalive, "Keep alives");
  test (timeout, "Binding discovery timeout");
  return 0;
}
