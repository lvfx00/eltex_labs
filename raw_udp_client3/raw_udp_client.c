#include <arpa/inet.h>
#include <errno.h>
#include <netinet/ip.h>
#include <net/if.h>
#include <netinet/udp.h>
#include <netinet/ether.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/if.h>
#include <linux/if_packet.h>

#define SERVICE 52674
#define UDP 17
#define PORT_TO_USE 52675
#define DATAGRAM_SIZE 4096
#define MAX_IP_OUTPUT_LEN 100
#define MAX_IP_INPUT_LEN 100

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

  int sockfd = socket(PF_PACKET, SOCK_RAW, ETH_P_IP);
  if (sockfd == -1) {
    perror("socket");
    exit(EXIT_FAILURE);
  }

  // init destination IP address structure
  struct sockaddr_in din;
  din.sin_family = AF_INET;
  din.sin_port = htons(SERVICE); // set network byte order
  int ret = inet_pton(AF_INET, argv[1], &din.sin_addr);
  if (ret == 0) {
    fprintf(stderr, "inet_pton - invalid address string\n");
    exit(EXIT_FAILURE);
  }

  char ip_route_output[MAX_IP_OUTPUT_LEN];
  // get default interface name
  // rough way...
  FILE *cmd_ip_route = popen("ip route show", "r");
  fgets(ip_route_output, MAX_IP_OUTPUT_LEN, cmd_ip_route);
  // got output string on the following format:
  // default via X.Y.Z.T dev eth0  proto static
  //                         ^^^^ need this
  // skip first 4 words
  char *interface_name = strtok(ip_route_output, " "); // " " is delimeter
  for(int i = 0; i < 4; ++i) {
  	interface_name = strtok(NULL, " ");
  }

  // get index number of default interface
  // unsigned int index = if_nametoindex(interface_name);
  // if (0 == index) {
  //   perror("if_nametoindex");
  //   exit(EXIT_FAILURE);
  // }
  struct ifreq ifr3;
  strncpy(ifr3.ifr_name, interface_name, IFNAMSIZ - 1); 
  if (ioctl(sockfd, SIOCGIFINDEX, &ifr3) == -1) {
    perror("ioctl - SIOCGIFINDEX");
    exit(EXIT_FAILURE);
  }
  unsigned int index = ifr3.ifr_ifindex;

  // get MAC address of default gateway
  // rough way...
  char ip_neigh_output[MAX_IP_OUTPUT_LEN];
  char ip_neigh_cmd[MAX_IP_INPUT_LEN];
  snprintf(ip_neigh_cmd, MAX_IP_INPUT_LEN, "ip neigh show dev %s", interface_name);
  FILE *cmd_ip_neigh = popen(ip_neigh_cmd, "r");
  fgets(ip_neigh_output, MAX_IP_OUTPUT_LEN, cmd_ip_neigh);
  // got output string on the following format:
  // 192.168.1.1 lladdr 44:94:fc:8e:13:f0 REACHABLE
  //                    ^^^^^^^^^^^^^^^^^ need this
  // skip first 2 words
  char *default_gateway_mac = strtok(ip_neigh_output, " "); // " " is delimeter
  for(int i = 0; i < 2; ++i) {
  	default_gateway_mac = strtok(NULL, " ");
  }
  unsigned char mac2[6];
  if (6 != sscanf(default_gateway_mac, "%x:%x:%x:%x:%x:%x", &mac2[0], &mac2[1], &mac2[2], &mac2[3], &mac2[4], &mac2[5])) {
    perror("sscanf - invalid default gateway mac address string");
    exit(EXIT_FAILURE);
  }
  // printf("Mac of default gateway: %.2X:%.2X:%.2X:%.2X:%.2X:%.2X\n" , mac2[0], mac2[1], mac2[2], mac2[3], mac2[4], mac2[5]);

  // get IPv4 address structure of default interface
  struct ifreq ifr;
  ifr.ifr_addr.sa_family = AF_INET;
  strncpy(ifr.ifr_name, interface_name, IFNAMSIZ - 1); 
  if (ioctl(sockfd, SIOCGIFADDR, &ifr) == -1) {
    perror("ioctl - SIOCGIFADDR");
    exit(EXIT_FAILURE);
  }

  // get MAC address structure of default interface
  struct ifreq ifr2;
  strncpy(ifr2.ifr_name, interface_name, IFNAMSIZ - 1);
  if (ioctl(sockfd, SIOCGIFHWADDR, &ifr2) == -1) {
    perror("ioctl - SIOCGIFHWADDR");
    exit(EXIT_FAILURE);
  }
  // and save it to string
  unsigned char *mac = (unsigned char *)ifr2.ifr_hwaddr.sa_data;
  // printf("Mac of default interface: %.2X:%.2X:%.2X:%.2X:%.2X:%.2X\n" , mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  char datagram[DATAGRAM_SIZE];       // datagram buffer including udp and ip header
  memset(datagram, 0, DATAGRAM_SIZE); /* zero out the buffer */

  struct ethhdr *ethh = (struct ethhdr *) datagram;
  ethh->h_proto = htons(ETH_P_IP);
  // fill Ethernet header
  /// ethh->h_dest = ; // mac address of default gateway
  memcpy(ethh->h_dest, mac2, ETH_ALEN);
  // ethh->h_source = def_iface_mac; // mac address of default interface
  memcpy(ethh->h_source, ifr2.ifr_hwaddr.sa_data, ETH_ALEN);

  struct ip *iph = (struct ip *) (datagram + sizeof(struct ethhdr));
  // fill IP header
  iph->ip_hl = 5; // header length in 32bit octes
  iph->ip_v = 4; // IP protocol version
  iph->ip_tos = 16; // type of service (controls priority)
  iph->ip_len = htons(sizeof(struct ip) + sizeof(struct udphdr) + strlen(argv[2])); // packet size including ip and udp headers
  iph->ip_id = htons(54321); //id sequence number (for fragmentation, doesn't matter here)
  iph->ip_off = 0; // offset in datagram
  iph->ip_ttl = 64; // time to live
  iph->ip_p = UDP; // transport layer protocol
  iph->ip_sum = 0;		/* set it to 0 before computing the actual checksum later */
  iph->ip_src.s_addr = ((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr.s_addr;
  iph->ip_dst.s_addr = din.sin_addr.s_addr;

  struct udphdr *udph = (struct udphdr *)(datagram + sizeof(struct ethhdr) + sizeof(struct ip)); // udp header pointer
  // fill UDP header
  udph->source = htons(PORT_TO_USE);
  udph->dest = htons(SERVICE);
  udph->len = htons(sizeof(struct udphdr) + strlen(argv[2])); // udp header + payload size
  udph->check = 0; // leave zero so that kernel will fill the correct value

  // fill payload
  char *data = datagram + sizeof(struct ethhdr) + sizeof(struct ip) + sizeof(struct udphdr);
  strcpy(data, argv[2]);

  // fill checksum
  iph->ip_sum = csum((unsigned short *)(datagram + sizeof(struct ethhdr)), iph->ip_len >> 1); // >> 2 to get number of words (2 bytes)

  // init link layer address
  struct sockaddr_ll sa;
  memset(&sa, 0, sizeof(struct sockaddr_ll));

  sa.sll_family = PF_PACKET;
  sa.sll_protocol = htons(ETH_P_IP);      
  sa.sll_halen = 6; 
  memcpy(sa.sll_addr, ifr2.ifr_hwaddr.sa_data, 6);
  sa.sll_ifindex = index;
  sa.sll_pkttype = PACKET_OUTGOING;

  // send data
  if (sendto(sockfd, datagram, sizeof(struct ethhdr) + sizeof(struct ip) + sizeof(struct udphdr) + strlen(argv[2]), 0, (struct sockaddr *)&sa, sizeof(struct sockaddr_ll)) !=  sizeof(struct ethhdr) + sizeof(struct ip) + sizeof(struct udphdr) + strlen(argv[2])) {
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
