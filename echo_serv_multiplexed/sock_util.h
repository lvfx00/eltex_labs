#pragma once

#include <sys/socket.h>

#define IS_ADDR_STR_LEN 4096 // Suggested length for string buffer that caller should pass to inetAddressStr().
                             // Must be greater than (NI_MAXHOST + NI_MAXSERV + 4) */

int inet_connect(const char *host, const char *service, int type);

int inet_listen(const char *service, int backlog, socklen_t *addrlen);

int inet_bind(const char *service, int type, socklen_t *addrlen);

char *inet_address_str(const struct sockaddr *addr, socklen_t addrlen, char *addrStr, int addrStrLen);

