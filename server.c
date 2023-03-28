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
#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>

#define DEFAULT_PORT "5555"
#define BACKLOG 10
#define BUF_SIZE 256

static void *buf;
static struct ibv_mr *mr;
static struct rdma_cm_id *listen_id, *cm_id;
static struct rdma_event_channel *event_channel;

static int
get_next_event(struct rdma_event_channel *e_channel,
               struct rdma_cm_event **event,
               enum rdma_cm_event_type e_type)
{
        int rc;

        rc = rdma_get_cm_event(e_channel, event);
        if (rc) {
                fprintf(stderr, "RDMA get cm event failed with rc %d\n", rc);
                return rc;                
        }

        if ((*event)->event != e_type) {
                fprintf(stderr, "%d event was expected, %d was recieved\n", e_type, (*event)->event);
                return -EINVAL;
        }

        return 0;
}

static int
poll_cq(struct ibv_cq *cq)
{
        int poll_rc;
        struct ibv_wc wc = {0};

        printf("Start polling CQ\n");

        do {
                poll_rc = ibv_poll_cq(cq, 1, &wc);
        } while (!poll_rc);

        if (poll_rc < 0) {
                printf("Failed to poll CQ");
                return poll_rc;
        }

        if(wc.status != IBV_WC_SUCCESS) {
                printf("Got bad completion of status %d \n", wc.status);
                return -EINVAL;
        }

        printf("CQ is polled without errors\n");

        return 0;
}

int
main(int argc, char *argv[])
{
        int rc;
        struct rdma_cm_event *event = NULL;
        struct ibv_qp_init_attr qp_init_attr = {0};
        struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons(5555), .sin_addr.s_addr = inet_addr("1.1.1.1") };

        event_channel = rdma_create_event_channel();

        rc = rdma_create_id(event_channel, &listen_id, NULL, RDMA_PS_TCP);
        if (rc) {
                fprintf(stderr, "RDMA create ID failed with rc %d\n", rc);
                return rc;
        }

        rc = rdma_bind_addr(listen_id, (struct sockaddr *)&addr);
        if (rc) {
                fprintf(stderr, "RDMA BIND ADDR failed with rc %d\n", rc);
                return rc;
        }

        rc = rdma_listen(listen_id, 0);
        if (rc) {
                fprintf(stderr, "RDMA LISTEN failed with rc %d\n", rc);
                return rc;
        }
        
        rc = get_next_event(event_channel, &event, RDMA_CM_EVENT_CONNECT_REQUEST);
        if(rc)
                return rc;

        qp_init_attr.qp_type            = IBV_QPT_RC;
        qp_init_attr.sq_sig_all         = 1;
     //   qp_init_attr.send_cq            = NULL;
     //   qp_init_attr.recv_cq            = NULL;
        qp_init_attr.cap.max_send_wr    = 1;
        qp_init_attr.cap.max_recv_wr    = 1;
        qp_init_attr.cap.max_send_sge   = 1;
        qp_init_attr.cap.max_recv_sge   = 1;

        cm_id = event->id;

        rc = rdma_create_qp(cm_id, NULL, &qp_init_attr);
        if (rc) {
                fprintf(stderr, "Failed to create QP rc %d\n", rc);
                return rc; 
        }

        rc = rdma_accept(cm_id, NULL);
        if (rc) {
                fprintf(stderr, "Failed to accept connection rc %d\n", rc);
                return rc; 
        }

        rc = get_next_event(event_channel, &event, RDMA_CM_EVENT_ESTABLISHED);
        if(rc)
                return rc;

        rdma_ack_cm_event(event);
        buf = calloc(1, BUF_SIZE);
        if (!buf) {
                fprintf(stderr, "Failed to alloc memmory for buffer\n");
                return rc; 
        }

        mr = rdma_reg_msgs(cm_id, buf, BUF_SIZE);
        if (!mr) {
                fprintf(stderr, "Failed to create MR\n");
                return rc; 
        }

        rc = rdma_post_recv(cm_id, NULL, buf, 70, mr);
        if(rc) {
                fprintf(stderr, "Failed to POST recv\n");
                return rc; 
        }

        rc = poll_cq(cm_id->recv_cq);
        if (rc)
                return rc;

        printf("Recieved msg : %s\n", buf);

        return 0;
}