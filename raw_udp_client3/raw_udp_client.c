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
#include <net/ethernet.h>

#define SERVICE 52674
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

// returns index of specified interface upon success
// or -1 upon failure with appropirate errno setted up
unsigned int get_index_of_iface(const char *iface_name) {
  int ret;
  int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
  if (-1 == sock) { 
    return -1;
  }

  struct ifreq ifr;
  strncpy(ifr.ifr_name, iface_name, IFNAMSIZ - 1); 
  ret = ioctl(sock, SIOCGIFINDEX, &ifr);
  if (-1 == ret) {
    return -1;
  }

  ret = close(sock);
  if (-1 == ret) {
    return -1;
  }
  return ifr.ifr_ifindex;
}

// returns 0 and sets interface name to specified buffer upon success
// or -1 upon failure with appropirate errno setted up
// rough way...
int get_default_iface_name(char *strbuf, unsigned int len) {
  char ip_route_output[MAX_IP_OUTPUT_LEN];

  FILE *cmd_ip_route = popen("ip route show default", "r");
  if (NULL == cmd_ip_route) {
    return -1;
  }

  fgets(ip_route_output, MAX_IP_OUTPUT_LEN, cmd_ip_route);
  // got output string on the following format:
  // default via X.Y.Z.T dev eth0  proto static
  //                         ^^^^ need this
  // skip first 4 words
  char *interface_name = strtok(ip_route_output, " "); // " " is delimeter
  for(int i = 0; i < 4; ++i) {
  	interface_name = strtok(NULL, " ");
  }
  strncpy(strbuf, interface_name, len);

  return 0;
}

// returns 0 and sets mac address to specified buffer upon success
// or -1 upon failure with appropirate errno setted up
// rough way...
int get_default_gateway_mac(const char *iface_name, uint8_t buf[ETHER_ADDR_LEN]) {
  char ip_neigh_output[MAX_IP_OUTPUT_LEN];
  char ip_neigh_cmd[MAX_IP_INPUT_LEN];

  snprintf(ip_neigh_cmd, MAX_IP_INPUT_LEN, "ip neigh show dev %s", iface_name);

  FILE *cmd_ip_neigh = popen(ip_neigh_cmd, "r");
  if (NULL == cmd_ip_neigh) {
    return -1;
  }

  fgets(ip_neigh_output, MAX_IP_OUTPUT_LEN, cmd_ip_neigh);
  // got output string on the following format:
  // 192.168.1.1 lladdr 44:94:fc:8e:13:f0 REACHABLE
  //                    ^^^^^^^^^^^^^^^^^ need this
  // skip first 2 words
  char *default_gateway_mac = strtok(ip_neigh_output, " "); // " " is delimeter
  for(int i = 0; i < 2; ++i) {
  	default_gateway_mac = strtok(NULL, " ");
  }
  
  int values[ETHER_ADDR_LEN];
  if (ETHER_ADDR_LEN != sscanf(default_gateway_mac, "%x:%x:%x:%x:%x:%x", 
  &values[0], &values[1], &values[2], &values[3], &values[4], &values[5])) {
    return -1;
  }
  for(int i = 0; i < ETHER_ADDR_LEN; ++i ) {
    buf[i] = (uint8_t) values[i];
  }

  printf("Mac of default gateway: %.2X:%.2X:%.2X:%.2X:%.2X:%.2X\n" , buf[0], buf[1], buf[2], buf[3], buf[4], buf[5]);
  return 0;
}

// returns 0 and sets mac address to specified buffer upon success
// or -1 upon failure with appropirate errno setted up
int get_iface_mac(const char *iface_name, uint8_t buf[ETHER_ADDR_LEN]) {
  int ret;
  int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
  if (-1 == sock) { 
    return -1;
  }

  struct ifreq ifr;
  strncpy(ifr.ifr_name, iface_name, IFNAMSIZ - 1);
  ret = ioctl(sock, SIOCGIFHWADDR, &ifr);
  if (-1 == ret) {
    return -1;
  }

  memcpy(buf, ifr.ifr_hwaddr.sa_data, ETHER_ADDR_LEN);
  printf("Mac of default interface: %.2X:%.2X:%.2X:%.2X:%.2X:%.2X\n", buf[0], buf[1], buf[2], buf[3], buf[4], buf[5]);

  ret = close(sock);
  if (-1 == ret) {
    return -1;
  }
  return 0;
}

int get_iface_ipv4_addr(const char *iface_name, struct sockaddr_in *sa) {
  int ret;
  int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
  if (-1 == sock) { 
    return -1;
  }

  struct ifreq ifr;
  ifr.ifr_addr.sa_family = AF_INET;
  strncpy(ifr.ifr_name, iface_name, IFNAMSIZ - 1); 

  ret = ioctl(sock, SIOCGIFADDR, &ifr);
  if (-1 == ret) {
    return -1;
  }

  memcpy(sa, &ifr.ifr_addr, sizeof(struct sockaddr_in));
  ret = close(sock);
  if (-1 == ret) {
    return -1;
  }
  return 0;
}

int main(int argc, char **argv) {
  int ret;

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
  ret = inet_pton(AF_INET, argv[1], &din.sin_addr);
  if (0 == ret) {
    fprintf(stderr, "inet_pton - invalid address string\n");
    exit(EXIT_FAILURE);
  }
  
  char interface_name[MAX_IP_OUTPUT_LEN]; // TODO set more meaningful size constant
  ret = get_default_iface_name(interface_name, MAX_IP_OUTPUT_LEN); 
  if (-1 == ret) {
    perror("get_default_iface_name");
    exit(EXIT_FAILURE);
  }

  struct sockaddr_in sin;
  ret = get_iface_ipv4_addr(interface_name, &sin);
  if (-1 == ret) {
    perror("get_iface_ipv4_addr");
    exit(EXIT_FAILURE);
  }

  // mac address of default interface
  uint8_t src_mac[ETHER_ADDR_LEN];
  ret = get_iface_mac(interface_name, src_mac);
  if (-1 == ret) {
    perror("get_iface_mac");
    exit(EXIT_FAILURE);
  }

  // mac address of default gateway for default interface
  uint8_t dest_mac[ETHER_ADDR_LEN];
  ret = get_default_gateway_mac(interface_name, dest_mac);
  if (-1 == ret) {
    perror("get_default_gateway_mac");
    exit(EXIT_FAILURE);
  }

  unsigned int iface_index = get_index_of_iface(interface_name);
  if (-1 == iface_index) {
    perror("get_inde_of_iface");
    exit(EXIT_FAILURE);
  }

  char datagram[DATAGRAM_SIZE];       // datagram buffer including udp and ip header
  memset(datagram, 0, DATAGRAM_SIZE); // zero out the buffer

  // init protocol headers and payload pointer
  struct ethhdr *ethh = (struct ethhdr *) datagram;
  struct ip *iph = (struct ip *) (datagram + sizeof(struct ethhdr));
  struct udphdr *udph = (struct udphdr *)(datagram + sizeof(struct ethhdr) + sizeof(struct ip));
  char *data = datagram + sizeof(struct ethhdr) + sizeof(struct ip) + sizeof(struct udphdr);

  size_t udp_packet_size = sizeof(struct udphdr) + strlen(argv[2]);
  size_t ip_packet_size = udp_packet_size + sizeof(struct ip);
  size_t eth_packet_size = ip_packet_size + sizeof(struct ethhdr);

  // fill payload
  strcpy(data, argv[2]);

  // fill UDP header
  udph->source = htons(PORT_TO_USE);
  udph->dest = htons(SERVICE);
  udph->len = htons(udp_packet_size); // udp header + payload size
  udph->check = csum((unsigned short *)udph, udp_packet_size >> 1);

  // fill IP header
  iph->ip_hl = 5; // header length in 32bit octes
  iph->ip_v = 4; // IP protocol version
  iph->ip_tos = 16; // type of service (controls priority)
  iph->ip_len = htons(ip_packet_size);
  iph->ip_id = htons(54321); //id sequence number (for fragmentation, doesn't matter here)
  iph->ip_off = 0; // offset in datagram
  iph->ip_ttl = 64; // time to live
  iph->ip_p = IPPROTO_UDP; // transport layer protocol
  iph->ip_sum = csum((unsigned short *)iph, ip_packet_size >> 1); // >> 2 to get number of words (2 bytes)
  iph->ip_src.s_addr = sin.sin_addr.s_addr;
  iph->ip_dst.s_addr = din.sin_addr.s_addr;

  // fill Ethernet header
  memcpy(ethh->h_source, src_mac, ETH_ALEN);
  memcpy(ethh->h_dest, dest_mac, ETH_ALEN);
  ethh->h_proto = ETH_P_IP;

  // init link layer address
  struct sockaddr_ll sa;
  memset(&sa, 0, sizeof(struct sockaddr_ll));

  sa.sll_family = PF_PACKET;
  sa.sll_protocol = ETH_P_IP;      
  sa.sll_halen = ETHER_ADDR_LEN; 
  memcpy(sa.sll_addr, src_mac, ETHER_ADDR_LEN);
  sa.sll_ifindex = iface_index;
  sa.sll_pkttype = PACKET_OUTGOING;

  // send data
  if (sendto(sockfd, datagram, eth_packet_size, 0, (struct sockaddr *)&sa, sizeof(struct sockaddr_ll)) != eth_packet_size) {
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
