#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <errno.h>

#include "infiniband_init.h"

#define BUF_SIZE (512)
#define PORT_ID (1)
#define SUBNET_ID (5);

static char *mem_buf;
static uint32_t rkey;
static uintptr_t raddr;
static struct ibv_cq *cq;
static struct ibv_qp *qp;
static struct ibv_pd *pd;
static struct ibv_mr *mr;

static int
extract_dev_info(struct ibv_device *dev)
{
        unsigned long long guid;
        const char *dev_name = ibv_get_device_name(dev);

        guid = ibv_get_device_guid(dev);

        printf ("Device name is %s\n", dev_name);
        printf ("Device GUID is %llu\n", guid);

        return 0;
}

static int
query_dev(struct ibv_device *dev, struct ibv_context **dev_ctx, struct ibv_port_attr *port_attr)
{
        int rc;
        struct ibv_device_attr dev_attr = {0};

        *dev_ctx = ibv_open_device(dev);
        if (!*dev_ctx) {
                printf("Failed to open device\n");
                return -ENODEV;
        }

        rc = ibv_query_device(*dev_ctx, &dev_attr);
        if (rc) {
                printf("Failed to query device\n");
                return rc;
        }

        rc = ibv_query_port(*dev_ctx, PORT_ID, port_attr);
        if (rc) {
                printf("Failed to query port %d\n", PORT_ID);
                return rc;
        }

        return 0;
}

static int
create_cq(struct ibv_context *dev_ctx, struct ibv_cq **cq)
{
        struct ibv_comp_channel *ibv_comp_chanel;
        void *cq_ctx = NULL;

        ibv_comp_chanel = ibv_create_comp_channel(dev_ctx);
        if (!ibv_comp_chanel) {
                printf("Failed to alloc comp channel");
                return -ENOMEM;
        }

        *cq = ibv_create_cq(dev_ctx, 100, cq_ctx, NULL /* ibv_comp_chanel*/, 0);
        if (!*cq) {
                printf("Failed to create cq");
                return -EINVAL;
        }

        return 0;
}

static int
create_pd(struct ibv_context *dev_ctx, struct ibv_pd **pd)
{
        *pd = ibv_alloc_pd(dev_ctx);
        if(!*pd) {
                printf("Failed to alloc pd\n");
                return -ENOMEM;
        }

        return 0;
}

static int
create_qp(struct ibv_pd *pd, struct ibv_cq *cq, struct ibv_qp **qp)
{
        struct ibv_qp_init_attr qp_init_attr = {0};

        qp_init_attr.recv_cq = cq;
        qp_init_attr.send_cq = cq;
        qp_init_attr.sq_sig_all = 1;
        qp_init_attr.qp_type = IBV_QPT_RC;
        qp_init_attr.cap.max_recv_wr = 8;
        qp_init_attr.cap.max_send_wr = 8;
        qp_init_attr.cap.max_send_sge = 1;
        qp_init_attr.cap.max_recv_sge = 1;

        *qp = ibv_create_qp(pd, &qp_init_attr);
        if (!*qp) {
                printf("Failed to creater qp\n");
                return -EINVAL;
        }

        return 0;
}

static int
register_mr(struct ibv_pd *pd, struct ibv_mr **mr)
{
        int flags;

        mem_buf = calloc(1, BUF_SIZE);
        if (!mem_buf) {
                printf("Failed to alloc buffer memmory of size %d", BUF_SIZE);
                return -ENOMEM;
        }

        flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_ATOMIC;
        *mr = ibv_reg_mr(pd, mem_buf, BUF_SIZE, flags);
        if (!*mr) {
                printf("Failed to register mr\n");
                return -EINVAL;
        }        

        return 0;
}

int
ib_qp_init(uint8_t dev_idx, struct qp_data_s *res)
{
        int rc;
        struct ibv_device **device_arr;
        struct ibv_device *dev;
        struct ibv_context *dev_ctx;
        struct ibv_port_attr port_attr = {0};
        struct ibv_qp_attr attr = {0};
        int attr_flags = 0;
        union ibv_gid gid = {0};

        device_arr = ibv_get_device_list(NULL);

        if (!device_arr) {
                printf("No IB devices found\n");
                return -1;
        }
        
        dev = device_arr[dev_idx];

        rc = extract_dev_info(dev);
        if (rc)
                return rc;

        /* port_id by default is 1 */
        rc = query_dev(dev, &dev_ctx, &port_attr);
        if (rc)
                return rc;
        
        rc = ibv_query_gid(dev_ctx, PORT_ID, 0, &gid);
        if (rc)
                return rc;

        rc = create_cq(dev_ctx, &cq);
        if (rc)
                return rc;

        rc = create_pd(dev_ctx, &pd);
        if (rc)
                return rc;

        rc = register_mr(pd, &mr);
        if (rc)
                return rc;

        rc = create_qp(pd, cq, &qp);
        if (rc)
                return rc;

        attr.qp_state = IBV_QPS_INIT;
        attr.port_num = PORT_ID;
        attr.pkey_index = 0;
        attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_ATOMIC;

        attr_flags = IBV_QP_STATE | IBV_QP_PORT | IBV_QP_PKEY_INDEX | IBV_QP_ACCESS_FLAGS;
        rc = ibv_modify_qp(qp, &attr, attr_flags);
        if (rc) {
                printf("Failed to restey qp state to INIT");
                return rc;
        }

        res->port_num = PORT_ID;
        res->qp_num = qp->qp_num;
        res->guid = ibv_get_device_guid(dev);
        res->gid = gid;
        res->qp = qp;
        res->rkey = mr->rkey;
        res->raddr = (uintptr_t) mr->addr;

        ibv_free_device_list(device_arr);

        return 0;
}

int
ib_setup_qp(struct qp_data_s *remote_qp)
{
        int rc;
        int attr_flags = 0;
        struct ibv_qp_attr attr = {0};

        rkey = remote_qp->rkey;
        raddr = remote_qp->raddr;

        attr.ah_attr.is_global = 1;
        attr.ah_attr.grh.dgid = remote_qp->gid;
        attr.ah_attr.sl = 0;
        attr.ah_attr.port_num = PORT_ID;

        attr.qp_state = IBV_QPS_RTR;
        attr.path_mtu = IBV_MTU_1024;
        attr.dest_qp_num = remote_qp->qp_num;
        attr.rq_psn = 0;
        attr.max_dest_rd_atomic = 1;
        attr.min_rnr_timer = 0x12;

        attr_flags = IBV_QP_STATE | IBV_QP_AV | IBV_QP_DEST_QPN | IBV_QP_PATH_MTU | IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;

        rc = ibv_modify_qp(qp, &attr, attr_flags);
        if (rc) {
                printf("Failed to reset qp state to RTR");
                return rc;
        }

        memset(&attr, 0, sizeof(attr));
        attr.qp_state = IBV_QPS_RTS;
        attr.timeout = 0x12;
        attr.retry_cnt = 6;
        attr.sq_psn = 0;
        attr.max_rd_atomic = 0;

        attr_flags = IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC;
        rc = ibv_modify_qp(qp, &attr, attr_flags);
        if (rc) {
                printf("Failed to reset qp state to RTS");
                return rc;
        }

        return 0;
}

int
ib_post_recieve()
{
        int rc;
        struct ibv_recv_wr wr = {0};
        struct ibv_recv_wr *bad_wr;// = calloc(1, sizeof(struct ibv_recv_wr));
        struct ibv_sge *sge = calloc(1, sizeof(struct ibv_sge));

        sge->addr = (uintptr_t)mem_buf;
        sge->length = BUF_SIZE;
        sge->lkey = mr->lkey;

        wr.next = NULL;
        wr.wr_id = 0;
        wr.sg_list = sge;
        wr.num_sge = 1;

        rc = ibv_post_recv(qp, &wr, &bad_wr);
        if (rc) {
                printf("Failed to post Recieve Request");
                return rc;
        }

        printf ("Recive Request posted\n");

        return 0;
}

int
ib_post_send(char *msg)
{
        int rc;
        struct ibv_send_wr wr = {0};
        struct ibv_send_wr *bad_wr = calloc(1, sizeof(struct ibv_send_wr));
        struct ibv_sge *sge = calloc(1, sizeof(struct ibv_sge));

        sge->addr = (uintptr_t)mem_buf;
        sge->length = BUF_SIZE;
        sge->lkey = mr->lkey;

        wr.next = NULL;
        wr.wr_id = 0;
        wr.sg_list = sge;
        wr.num_sge = 1;
        wr.opcode = IBV_WR_SEND;
        wr.send_flags = IBV_SEND_SIGNALED;

        memcpy(mem_buf, msg, strlen(msg));
        rc = ibv_post_send(qp, &wr, &bad_wr);
        if (rc) {
                printf("Failed to post Send Request");
                return rc;
        }

        printf ("Send Request posted\n");

        return 0;
}

int
ib_post_rdma_write(char *msg)
{
        int rc;
        struct ibv_send_wr wr = {0};
        struct ibv_send_wr *bad_wr = calloc(1, sizeof(struct ibv_send_wr));
        struct ibv_sge *sge = calloc(1, sizeof(struct ibv_sge));

        sge->addr = (uintptr_t)mem_buf;
        sge->length = BUF_SIZE;
        sge->lkey = mr->lkey;

        wr.next = NULL;
        wr.wr_id = 0;
        wr.sg_list = sge;
        wr.num_sge = 1;
        wr.opcode = IBV_WR_RDMA_WRITE;
        wr.send_flags = IBV_SEND_SIGNALED;
        wr.wr.rdma.rkey = rkey;
        wr.wr.rdma.remote_addr = raddr;

        printf("RDMA WRTIE local address %p, remote address %p\n", mem_buf, raddr);

        memcpy(mem_buf, msg, strlen(msg));
        rc = ibv_post_send(qp, &wr, &bad_wr);
        if (rc) {
                printf("Failed to post RDMA WRITE Request");
                return rc;
        }

        printf ("RDMA WRITE request posted\n");

        return 0;     
}

int
ib_poll_cq()
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
ib_post_rdma_read()
{
        int rc;
        struct ibv_send_wr wr = {0};
        struct ibv_send_wr *bad_wr = calloc(1, sizeof(struct ibv_send_wr));
        struct ibv_sge *sge = calloc(1, sizeof(struct ibv_sge));

        sge->addr = (uintptr_t)mem_buf;
        sge->length = BUF_SIZE;
        sge->lkey = mr->lkey;

        wr.next = NULL;
        wr.wr_id = 0;
        wr.sg_list = sge;
        wr.num_sge = 1;
        wr.opcode = IBV_WR_RDMA_READ;
        wr.send_flags = IBV_SEND_SIGNALED;
        wr.wr.rdma.rkey = rkey;
        wr.wr.rdma.remote_addr = raddr;

         printf("RDMA READ local address %p, remote address %p\n", mem_buf, raddr);

        rc = ibv_post_send(qp, &wr, &bad_wr);
        if (rc) {
                printf("Failed to post RDMA READ Request");
                return rc;
        }

        printf ("RDMA read Request posted\n");

        return 0;     
}

void
ib_fill_buffer(char *msg)
{
        memcpy(mem_buf, msg, strlen(msg));
}

void
ib_print_buffer_and_flush()
{
        printf("Buffer data is %s\n", mem_buf);
        memset(mem_buf, 0, BUF_SIZE);
}