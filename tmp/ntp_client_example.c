#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

/**
 * https://lettier.github.io/posts/2016-04-26-lets-make-a-ntp-client-in-c.html
 * https://github.com/lettier/ntpclient/blob/master/source/c/main.c
 */

typedef struct _ntp_packet_t {
  /**
   * @brief Eight bits. li, vn, and mode
   *        li: (Two bits) Leap indicator
   *        vn: (Three bits) Version number of the protocol
   *        mode: (Three bits) Client will pick mode 3 for client.
   */
  uint8_t li_vn_mode;
  /**
   * @brief Eight bits. Stratum level of the local clock
   */
  uint8_t stratum;
  /**
   * @brief Eight bits. Maximum interval between successive messages.
   */
  uint8_t poll;
  /**
   * @brief Eight bits. Precision of the local clock.
   */
  uint8_t precision;

  /**
   * @brief 32 bits. Total round trip delay time.
   */
  uint32_t rootDelay;
  /**
   * @brief 32 bits. Max error aloud from primary clock source.
   */
  uint32_t rootDispersion;
  /**
   * @brief 32 bits. Reference clock identifier.
   */
  uint32_t refId;

  /**
   * @brief 32 bits. Reference time-stamp seconds.
   */
  uint32_t refTm_s;
  /**
   * @brief 32 bits. Reference time-stamp fraction of a second.
   */
  uint32_t refTm_f;

  /**
   * @brief 32 bits. Originate time-stamp seconds.
   */
  uint32_t origTm_s;
  /**
   * @brief 32 bits. Originate time-stamp fraction of a second.
   */
  uint32_t origTm_f;

  /**
   * @brief 32 bits. Received time-stamp seconds.
   */
  uint32_t rxTm_s;
  /**
   * @brief 32 bits. Received time-stamp fraction of a second.
   */
  uint32_t rxTm_f;

  /**
   * @brief 32 bits and the most important field the client cares about. Transmit time-stamp seconds.
   */
  uint32_t txTm_s;
  /**
   * @brief 32 bits. Transmit time-stamp fraction of a second.
   */
  uint32_t txTm_f;
} ntp_packet_t;

int main (int argc, char **argv)
{
  const unsigned long long NTP_TIMESTAMP_DELTA = 2208988800ull;
  ntp_packet_t packet = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  int n;

  memset( &packet, 0, sizeof(packet));
  *((uint8_t *) &packet + 0 ) = 0x1b;

  {
    char host_name[] = "us.pool.ntp.org";
    uint16_t host_port = 123;
    /**
      * Create a UDP socket, convert the host-name to an IP address, set the port number,
      */
    struct sockaddr_in serv_addr; // Server address data structure.
    struct hostent *server;      // Server data structure.
    int sockfd;

    sockfd = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP ); // Create a UDP socket.
    if (sockfd < 0) {
      fprintf(stderr, "ERROR, opening socket\n");
      return -1;
    }

    server = gethostbyname (host_name);
    if (server == NULL) {
      fprintf(stderr, "ERROR, no such host");
      return -1;
    }
    /* Zero out the server address structure.*/
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;

    /* Copy the server's IP address to the server address structure. */
    bcopy((char *)server->h_addr_list[0], ( char* ) &serv_addr.sin_addr.s_addr, server->h_length);
    /* Convert the port number integer to network big-endian style and save it to the server address structure. */
    serv_addr.sin_port = htons(host_port);

    /* Call up the server using its IP address and port number. */
    if (connect( sockfd, ( struct sockaddr * ) &serv_addr, sizeof(serv_addr)) < 0) {
      fprintf(stderr, "ERROR connecting\n");
      return -1;
    }

    n = write( sockfd, ( char* ) &packet, sizeof(packet));
    if (n < 0) {
      fprintf(stderr, "ERROR, writing to socket\n");
    }

    sleep (10);

    /* Wait and receive the packet back from the server. If n == -1, it failed. */
    n = read( sockfd, (char *) &packet, sizeof(packet));
    if ( n < 0 ) {
      fprintf(stderr, "ERROR reading from socket\n");
    }

    /**
     * These two fields contain the time-stamp seconds as the packet left the NTP server.
     * The number of seconds correspond to the seconds passed since 1900.
     * ntohl() converts the bit/byte order from the network's to host's "endianness".
     */
    packet.txTm_s = ntohl( packet.txTm_s ); // Time-stamp seconds.
    packet.txTm_f = ntohl( packet.txTm_f ); // Time-stamp fraction of a second.

    /**
     * Extract the 32 bits that represent the time-stamp seconds (since NTP epoch) from when the packet left the server.
     * Subtract 70 years worth of seconds from the seconds since 1900.
     * This leaves the seconds since the UNIX epoch of 1970.
     * (1900)------------------(1970)**************************************(Time Packet Left the Server)
     */
    time_t txTm = ( time_t ) ( packet.txTm_s - NTP_TIMESTAMP_DELTA );

    printf("Time: %s",ctime((const time_t *) &txTm));

  }

  return 0;
}
