#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <errno.h>

#define DEFAULT_PORT "5555"
#define BACKLOG 10

static int
setup_socket(struct addrinfo *hints, char *port, int *socketfd)
{
        int rc;
        int yes = 1;
        char server_addr[INET_ADDRSTRLEN];
        struct addrinfo *res;

        rc = getaddrinfo("localhost", port, hints, &res);
        if (rc != 0) {
                fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc));
                return 2;
        }

        inet_ntop(AF_INET, &((struct sockaddr_in *)(res->ai_addr))->sin_addr, server_addr, INET_ADDRSTRLEN);
        printf("Server address is %s\n", server_addr);

        *socketfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (*socketfd == -1) {
                fprintf(stderr, "socket: failed to create socket\n");
                return errno;
        }

        setsockopt(*socketfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
        rc = bind(*socketfd, res->ai_addr, res->ai_addrlen);
        if (rc == -1) {
                fprintf(stderr, "bind: failed to bind port %s\n", port);
                return errno;
        }

        rc = listen(*socketfd, BACKLOG);
        if (rc == -1) {
                fprintf(stderr, "listen: failed to start listening on port %s\n", port);
                return errno;
        }

        return 0;
}

static int
accept_new_connection(int sockfd, fd_set *master, int *fdmax)
{
        struct sockaddr_storage remoteaddr;
        socklen_t addrlen;
        struct sockaddr_in *sin;
        char addr_str[INET_ADDRSTRLEN];
        int new_fd;
        
        addrlen = sizeof(remoteaddr);
        new_fd = accept(sockfd, (struct sockaddr *) &remoteaddr, &addrlen);
        if (new_fd == -1) {
                fprintf(stderr, "accept: failed to accept connection on port %s\n", DEFAULT_PORT);
                return errno;
        }

        sin = (struct sockaddr_in *) &remoteaddr;
        inet_ntop(AF_INET, &sin->sin_addr, addr_str, INET_ADDRSTRLEN);
        printf("Accepted new connection from %s Created new fd %d \n", addr_str, new_fd);

        FD_SET(new_fd, master);
        if (new_fd > *fdmax)
                *fdmax = new_fd;

        return 0;
}

static int
proccesing(int sockfd)
{
        fd_set master;
        fd_set active;
        int fdmax;
        int new_fd;
        int rc;
        int i;
        

        FD_ZERO(&master);
        FD_ZERO(&active);
        FD_SET(sockfd, &master);
        fdmax = sockfd + 1;

        while (1) {
                active = master;
                rc = select(fdmax, &active, NULL, NULL, NULL);
                if (rc == -1) {
                        fprintf(stderr, "select: failed to get list of readable fd\n");
                        return errno;
                }

                for (i = sockfd; i < fdmax; i++) {
                        if (FD_ISSET(i, &active)) {
                                if (i == sockfd) {
                                        rc = accept_new_connection(sockfd, &master, &fdmax);
                                        if (rc)
                                                perror("accept new connection");
                                } else {
                                        
                                }
                        }
                }
        }


}

 int
 main(int argc, char *argv[])
 {
        struct addrinfo hints = {0};
        struct addrinfo *res, *p;

        struct sockaddr_in *ipv4;
        struct sockaddr_in6 *ipv6;
        struct sockaddr_storage their_addr;
        socklen_t addr_size;
        void *addr;
        int sockfd;
        int rc;

        hints.ai_family = AF_INET; // AF_INET или AF_INET6 если требуется
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_PASSIVE;
        
        rc = setup_socket(&hints, DEFAULT_PORT, &sockfd);
        if (rc)
                return rc;

        proccesing(sockfd);


        return 0;
 }