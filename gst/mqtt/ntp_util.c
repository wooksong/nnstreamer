/* SPDX-License-Identifier: LGPL-2.1-only */
/**
 * Copyright (C) 2021 Wook Song <wook16.song@samsung.com>
 */
/**
 * @file    mqttsink.c
 * @date    16 Jul 2021
 * @brief   NTP utility functions
 * @see     https://github.com/nnstreamer/nnstreamer
 * @author  Wook Song <wook16.song@samsung.com>
 * @bug     No known bugs except for NYI items
 */

#include <errno.h>
#include <netdb.h>
#include <netinet/ip.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/**
 *******************************************************************
 * NTP Timestamp Format (https://www.ietf.org/rfc/rfc5905.txt p.12)
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                            Seconds                            |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                            Fraction                           |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *******************************************************************
 */

typedef struct _ntp_timestamp_t
{
  uint32_t sec;
  uint32_t frac;
} ntp_timestamp_t;

/**
 *******************************************************************
 * NTP Packet Header Format (https://www.ietf.org/rfc/rfc5905.txt p.18)
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |LI | VN  |Mode |    Stratum     |     Poll      |  Precision   |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                         Root Delay                            |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                         Root Dispersion                       |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                          Reference ID                         |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                                                               |
 * +                     Reference Timestamp (64)                  +
 * |                                                               |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                                                               |
 * +                      Origin Timestamp (64)                    +
 * |                                                               |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                                                               |
 * +                      Receive Timestamp (64)                   +
 * |                                                               |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                                                               |
 * +                      Transmit Timestamp (64)                  +
 * |                                                               |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                                                               |
 * .                                                               .
 * .                    Extension Field 1 (variable)               .
 * .                                                               .
 * |                                                               |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                                                               |
 * .                                                               .
 * .                    Extension Field 2 (variable)               .
 * .                                                               .
 * |                                                               |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                          Key Identifier                       |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                                                               |
 * |                            dgst (128)                         |
 * |                                                               |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *******************************************************************
 */

typedef struct _ntp_packet_t
{
  uint8_t li_vn_mode;
  uint8_t stratum;
  uint8_t poll;
  uint8_t precision;
  uint32_t root_delay;
  uint32_t root_dispersion;
  uint32_t ref_id;
  ntp_timestamp_t ref_ts;
  ntp_timestamp_t org_ts;
  ntp_timestamp_t recv_ts;
  ntp_timestamp_t xmit_ts;
} ntp_packet_t;

const uint64_t NTP_TIMESTAMP_DELTA = 2208988800ULL;
const double NTP_MAX_FRAC_DOUBLE = 4294967295.0L;
const char ntp_default_hname[] = "pool.ntp.org";
const uint16_t ntp_default_port = 123;
extern int h_errno;

time_t
ntp_util_get_epoch (uint32_t hnums, char **hnames, uint16_t * ports)
{
  struct sockaddr_in serv_addr;
  struct hostent *srv = NULL;
  struct hostent *default_srv = NULL;
  uint16_t port = -1;
  int32_t sockfd = -1;
  uint32_t i;
  time_t ret;

  sockfd = socket (AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sockfd < 0) {
    ret = -1;
    goto ret_normal;
  }

  for (i = 0; i < hnums; ++i) {
    srv = gethostbyname (hnames[i]);
    if (srv != NULL) {
      port = ports[i];
      break;
    }
  }

  if (srv == NULL) {
    default_srv = gethostbyname (ntp_default_hname);
    if (default_srv == NULL) {
      ret = -h_errno;
      goto ret_close_sockfd;
    }
    srv = default_srv;
    port = ntp_default_port;
  }

  memset (&serv_addr, 0, sizeof (serv_addr));
  serv_addr.sin_family = AF_INET;
  memcpy ((uint8_t *) & serv_addr.sin_addr.s_addr,
      (uint8_t *) srv->h_addr_list[0], (size_t) srv->h_length);
  serv_addr.sin_port = htons (port);

  ret = connect (sockfd, (struct sockaddr *) &serv_addr, sizeof (serv_addr));
  if (ret < 0) {
    ret = -errno;
    goto ret_close_sockfd;
  }

  {
    ntp_packet_t packet;
    uint32_t recv_sec;
    uint32_t recv_frac;
    ssize_t n;

    memset (&packet, 0, sizeof (packet));

    /* li = 0, vn = 3, mode = 3 */
    packet.li_vn_mode = 0x1B;

    /* Request */
    n = write (sockfd, &packet, sizeof (packet));
    if (n < 0) {
      ret = -errno;
      goto ret_close_sockfd;
    }

    /* Recieve */
    n = read (sockfd, &packet, sizeof (packet));
    if (n < 0) {
      ret = -errno;
      goto ret_close_sockfd;
    }

    /**
     * These two fields contain the time-stamp seconds as the packet left the NTP server.
     * The number of seconds correspond to the seconds passed since 1900.
     * ntohl() converts the bit/byte order from the network's to host's "endianness".
     */
    recv_sec = ntohl (packet.xmit_ts.sec);
    recv_frac = ntohl (packet.xmit_ts.frac);

    /**
     * Extract the 32 bits that represent the time-stamp seconds (since NTP epoch) from when the packet left the server.
     * Subtract 70 years worth of seconds from the seconds since 1900.
     * This leaves the seconds since the UNIX epoch of 1970.
     * (1900)------------------(1970)**************************************(Time Packet Left the Server)
     */
    ret = (time_t) (packet.xmit_ts.sec - NTP_TIMESTAMP_DELTA);

    printf ("Time: %s", ctime ((const time_t *) &ret));
    printf ("Epoch (sec): %lu\n", recv_sec - NTP_TIMESTAMP_DELTA);
    printf ("Epoch (frac): %lf\n", recv_frac / NTP_MAX_FRAC_DOUBLE);
  }

ret_close_sockfd:
  close (sockfd);

ret_normal:
  return ret;
}

int
main (int argc, char **argv)
{
  ntp_util_get_epoch (0, NULL, NULL);

  return 0;
}
