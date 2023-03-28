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
#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>

#include "infiniband_init.h"

#define BUF_SIZE 256
#define DEFAULT_PORT "5555"

static const char *msg = "Hello from Client!";
static void *buf;
struct rdma_cm_id *id;
struct ibv_mr *mr;

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
        struct ibv_qp_init_attr qp_init_attr = {0};
        struct rdma_event_channel *event_channel;
        struct rdma_cm_event *event = NULL;
        struct rdma_cm_id *cm_id;
        struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons(5555), .sin_addr.s_addr = inet_addr("1.1.1.1") };
        

        event_channel = rdma_create_event_channel();

        rc = rdma_create_id(event_channel, &cm_id, NULL, RDMA_PS_TCP);
        if (rc) {
                fprintf(stderr, "RDMA create ID failed with rc %d\n", rc);
                return rc;
        }

        rc = rdma_resolve_addr(cm_id, NULL, (struct sockaddr *)&addr, 2000);
        if (rc) {
                fprintf(stderr, "RDMA RESOLV ADDR failed with rc %d\n", rc);
                return rc;                
        }

        rc = get_next_event(event_channel, &event, RDMA_CM_EVENT_ADDR_RESOLVED);
        if (rc)
                return rc;

        rc = rdma_resolve_route(cm_id, 2000);
        if (rc) {
                fprintf(stderr, "RDMA resolv route failed with rc %d\n", rc);
                return rc;                
        }

        rc = get_next_event(event_channel, &event, RDMA_CM_EVENT_ROUTE_RESOLVED);
        if (rc)
                return rc;
        
        qp_init_attr.qp_type            = IBV_QPT_RC;
        qp_init_attr.sq_sig_all         = 1;
     //   qp_init_attr.send_cq            = NULL;
     //   qp_init_attr.recv_cq            = NULL;
        qp_init_attr.cap.max_send_wr    = 1;
        qp_init_attr.cap.max_recv_wr    = 1;
        qp_init_attr.cap.max_send_sge   = 1;
        qp_init_attr.cap.max_recv_sge   = 1;

        rc = rdma_create_qp(cm_id, NULL, &qp_init_attr);
        if(rc)
        {
                fprintf(stderr, "Failed to create QP rc %d\n", rc);
                return rc; 
        }

        rc = rdma_connect(cm_id, NULL);
        if (rc) {
                fprintf(stderr, "RDMA Connect failed rc %d\n", rc);
                return rc;                 
        }

        rc = get_next_event(event_channel, &event, RDMA_CM_EVENT_ESTABLISHED);
        if (rc)
                return rc;

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

        memcpy(buf, msg, strlen(msg));
        rc = rdma_post_send(cm_id, NULL, buf, 70, mr, 0);
        if(rc) {
                fprintf(stderr, "Failed to POST recv\n");
                return rc; 
        }

        rc = poll_cq(cm_id->send_cq);
        if (rc)
                return rc;

        return 0;
}