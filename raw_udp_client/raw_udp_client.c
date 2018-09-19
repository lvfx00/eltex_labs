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
#define PORT_TO_USE 52675
#define DATAGRAM_SIZE 4096

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

  char datagram[DATAGRAM_SIZE];       // datagram buffer including udp header
  memset(datagram, 0, DATAGRAM_SIZE); /* zero out the buffer */

  struct udphdr *udph = (struct udphdr *)datagram; // udp header pointer
  char *data = datagram + sizeof(struct udphdr);
  strcpy(data, argv[2]);

  // init destination address
  struct sockaddr_in sin;
  sin.sin_family = AF_INET;
  sin.sin_port = htons(SERVICE); // set network byte order
  int ret = inet_pton(AF_INET, argv[1], &sin.sin_addr);
  if (ret == 0) {
    fprintf(stderr, "inet_pton - invalid address string\n");
    exit(EXIT_FAILURE);
  }

  // fill UDP header
  udph->source = htons(PORT_TO_USE);
  udph->dest = htons(SERVICE);
  udph->len =
      htons(sizeof(struct udphdr) + strlen(data)); // udp header + payload size
  udph->check = 0; // leave zero so that kernel will fill the correct value

  // send data
  if (sendto(sockfd, datagram, sizeof(struct udphdr) + strlen(data), 0, (struct sockaddr *)&sin,
             sizeof(sin)) < 0) {
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
      printf("%d\n", ntohs(header->len));
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
