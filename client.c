#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>

#include "infiniband_init.h"

#define BUF_SIZE 256

static void *buf;

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
exchange_data(int sockfd, struct qp_data_s *qp_data)
{
        int rc;
        struct qp_data_s *result;

        rc = send(sockfd, qp_data, sizeof(struct qp_data_s), 0);
        if (rc < 0) {
                perror("send msg");
                return rc;
        }

        sleep(1);

        rc = recv(sockfd, buf, BUF_SIZE, 0);

        if (rc <= 0)
                return rc;

        result = (struct qp_data_s *) buf;

        printf("Recieved qp data from server\n");
        printf("Server QP num is %d\n", result->qp_num);
        printf("Server QP GUID is %llu\n", result->guid);

        ib_setup_qp(result);

        return 0;
}

 int
 main(int argc, char *argv[])
 {
        struct addrinfo hints = {0};
        struct qp_data_s qp_data = {0};
        int sockfd;
        int rc;

        hints.ai_family = AF_INET; // AF_INET или AF_INET6 если требуется
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_PASSIVE;

        rc = connect_to_server(&hints, "5555", &sockfd);
        if (rc)
                return rc;

        printf("Initialisation of QP\n");
        rc = ib_qp_init(1, &qp_data);
        if (rc)
                return rc;

        printf("QP num is %d\n", qp_data.qp_num);
        printf("QP port_num is %d\n", qp_data.port_num);

        buf = malloc(BUF_SIZE);

        exchange_data(sockfd, &qp_data);

        rc = ib_post_rdma_write("Hello from client side\n");
        if (rc)
                return rc;

        rc = ib_poll_cq();
        if (rc)
                return rc;

        getchar();

        rc = ib_post_rdma_read();
        if (rc)
                return rc;

        rc = ib_poll_cq();
        if (rc)
                return rc;

        ib_print_buffer_and_flush();

        return 0;
 }