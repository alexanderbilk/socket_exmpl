#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>


static int
connect_to_server(struct addrinfo *hints, char *port, int *sockfd)
{
        int rc;
        struct addrinfo *res;

        rc = getaddrinfo("localhost", "5555", hints, &res);
        if (rc) {
                fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc));
                return 2;
        }

        *sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (*sockfd == -1) {
                fprintf(stderr, "socket: failed to create socket\n");
                return errno;
        }

        rc = connect(*sockfd, res->ai_addr, res->ai_addrlen);
        if (rc == -1) {
                fprintf(stderr, "connect: failed to connected\n");
                return errno;
        }
}

static int
send_message(int sockfd, char *msg)
{
        int rc;

        rc = send(sockfd, msg, strlen(msg), 0);
        if (rc) {
                perror("send msg");
                return rc;
        }

        return 0;
}

 int
 main(int argc, char *argv[])
 {
        struct addrinfo hints = {0};
        int sockfd;
        int rc;

        hints.ai_family = AF_INET; // AF_INET или AF_INET6 если требуется
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_PASSIVE;

        rc = connect_to_server(&hints, "5555", &sockfd);
        if (rc)
                return rc;

        send_message(sockfd, "Hello there");
        send_message(sockfd, "Knock knock");

        return 0;
 }