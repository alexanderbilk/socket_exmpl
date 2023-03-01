#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <errno.h>
#include <unistd.h>

#include "infiniband_init.h"

#define DEFAULT_PORT "5555"
#define BACKLOG 10
#define BUF_SIZE 256

static void *buf;
struct qp_data_s qp_data = {0};

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

        printf("Listening on port %s\n", port);

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
        if (new_fd >= *fdmax)
                *fdmax = new_fd + 1;

        return 0;
}

static inline void
close_connection(int fd, fd_set *master)
{
        printf("Closing connection on fd %d", fd);
        FD_CLR(fd, master);
        close(fd);
}

static int
exchange_data(int fd, fd_set *master)
{
        int rc;
        struct qp_data_s *result;

        rc = recv(fd, buf, BUF_SIZE, 0);

        if (rc == 0)
                return 0;

        if (rc < 0) {
                close_connection(fd, master);
                return rc;
        }

        result = (struct qp_data_s *) buf;
        
        printf("New data has been recieved from connection %d\n", fd);
        printf("Client QP num is %d\n", result->qp_num);
        printf("Client QP GUID is %llu\n", result->guid);

        rc = ib_setup_qp(result);
        if (rc)
                return rc;
        
        printf("Sending data qp_data back to client\n");
        rc = send(fd, &qp_data, sizeof(struct qp_data_s), 0);
        if (rc < 0)
                return rc;

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
                                        rc = exchange_data(i, &master);
                                        if (rc)
                                                perror("recieve data");

                                        goto connection_done;
                                }
                        }
                }
        }

connection_done:
        return 0;
}

 int
 main(int argc, char *argv[])
 {
        int sockfd;
        int rc;
        struct addrinfo hints = {0};

        hints.ai_family = AF_INET; // AF_INET или AF_INET6 если требуется
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_PASSIVE;
        
        rc = setup_socket(&hints, DEFAULT_PORT, &sockfd);
        if (rc)
                return rc;

        printf("Initialisation of QP\n");
        rc = ib_qp_init(0, &qp_data);
        if (rc)
                return rc;
        
        printf("QP num is %d\n", qp_data.qp_num);
        printf("QP port_num is %d\n", qp_data.port_num);

        buf = malloc(BUF_SIZE);

        rc = proccesing(sockfd);
        if (rc)
                return rc;

        printf("Data exchange completed\n");

        rc = ib_post_recieve();
        if (rc)
                return rc;

        rc = ib_poll_cq();
        if (rc)
                return rc;
        
        getchar();

        return 0;
 }