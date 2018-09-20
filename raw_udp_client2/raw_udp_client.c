#include <arpa/inet.h>
#include <errno.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define SERVICE 52674
#define UDP 17
#define PORT_TO_USE 52675
#define DATAGRAM_SIZE 4096

unsigned short csum (unsigned short *buf, int nwords) {
  unsigned long sum;
  for (sum = 0; nwords > 0; nwords--)
    sum += *buf++;
  sum = (sum >> 16) + (sum & 0xffff);
  sum += (sum >> 16);
  return ~sum;
}

int main(int argc, char **argv) {
  if (argc < 3 || strcmp(argv[1], "--help") == 0) {
    fprintf(stderr, "usage: %s server-host sending-string\n", argv[0]);
    exit(EXIT_FAILURE);
  }
  if (strlen(argv[2]) > (DATAGRAM_SIZE - sizeof(struct udphdr))) {
    fprintf(stderr, "provided string too big\n");
    exit(EXIT_FAILURE);
  }

  int sockfd = socket(PF_INET, SOCK_RAW, IPPROTO_UDP);
  if (sockfd == -1) {
    perror("socket");
    exit(EXIT_FAILURE);
  }

  // init destination address
  struct sockaddr_in din;
  din.sin_family = AF_INET;
  din.sin_port = htons(SERVICE); // set network byte order
  int ret = inet_pton(AF_INET, argv[1], &din.sin_addr);
  if (ret == 0) {
    fprintf(stderr, "inet_pton - invalid address string\n");
    exit(EXIT_FAILURE);
  }

  char datagram[DATAGRAM_SIZE];       // datagram buffer including udp and ip header
  memset(datagram, 0, DATAGRAM_SIZE); /* zero out the buffer */

  struct ip *iph = (struct ip *)datagram;
  // fill IP header
  iph->ip_hl = 5; // header length in 32bit octes
  iph->ip_v = 4; // IP protocol version
  iph->ip_tos = 16; // type of service (controls priority)
  iph->ip_len = htons(sizeof(struct ip) + sizeof(struct udphdr) + strlen(argv[2])); // packet size including header
  iph->ip_id = htons(54321); //id sequence number (for fragmentation, doesn't matter here)
  iph->ip_off = 0; // offset in datagram
  iph->ip_ttl = 64; // time to live
  iph->ip_p = UDP; // transport layer protocol
  iph->ip_sum = 0;		/* set it to 0 before computing the actual checksum later */
  iph->ip_src.s_addr = 0;
  iph->ip_dst.s_addr = din.sin_addr.s_addr;

  struct udphdr *udph = (struct udphdr *)(datagram + sizeof(struct ip)); // udp header pointer
  // fill UDP header
  udph->source = htons(PORT_TO_USE);
  udph->dest = htons(SERVICE);
  udph->len = htons(sizeof(struct udphdr) + strlen(argv[2])); // udp header + payload size
  udph->check = 0; // leave zero so that kernel will fill the correct value

  // fill payload
  char *data = datagram + sizeof(struct udphdr) + sizeof(struct ip);
  strcpy(data, argv[2]);

  // fill checksum
  iph->ip_sum = csum((unsigned short *) datagram, iph->ip_len >> 1); // >> 2 to get number of words (2 bytes)

  // ensure that our ip header will be included
  int one = 1;
  if (setsockopt (sockfd, IPPROTO_IP, IP_HDRINCL, &one, sizeof (one)) < 0) {
      perror("setsockopt - IP_HDRINCL");
      exit(EXIT_FAILURE);
  }

  // send data
  if (sendto(sockfd, datagram, iph->ip_len, 0, (struct sockaddr *)&din,
             sizeof(din)) < 0) {
    perror("sendto");
    exit(EXIT_FAILURE);
  }

  ssize_t recvnum;
  char recvbuf[DATAGRAM_SIZE];
  for (;;) {
    recvnum = recv(sockfd, recvbuf, DATAGRAM_SIZE, 0);
    if (recvnum == -1) {
      perror("recv");
      exit(EXIT_FAILURE);
    }

    struct udphdr *header = (struct udphdr *)(recvbuf + sizeof(struct ip));

    // find udp datagrams for specified port
    if (ntohs(header->dest) == PORT_TO_USE) {
      if (write(STDIN_FILENO, recvbuf + sizeof(struct udphdr) + sizeof(struct ip),
                ntohs(header->len) - sizeof(struct udphdr)) != ntohs(header->len)- sizeof(struct udphdr)) {
        perror("write");
        exit(EXIT_FAILURE);
      }
      printf("\n");
      return 0;
    }
  }
  return 0;
}
