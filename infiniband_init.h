#ifndef _INFINIBAND_INIT_H
#define _INFINIBAND_INIT_H

#include <infiniband/verbs.h>

struct qp_data_s {
        uint8_t port_num;
        uint32_t qp_num;
        uint32_t rkey;
        uintptr_t raddr;
        unsigned long long guid;
        union ibv_gid gid;
        struct ibv_qp *qp;
};

int ib_qp_init(uint8_t dev_idx, struct qp_data_s *res);

int ib_setup_qp(struct qp_data_s *remote_qp);

int ib_post_recieve();

int ib_post_send(char *msg);

int ib_post_rdma_write(char *msg);

int ib_post_rdma_read();

int ib_poll_cq();

void ib_fill_buffer(char *msg);

void ib_print_buffer_and_flush();

void ib_destroy();

#endif /* _INFINIBAND_INIT_H */
