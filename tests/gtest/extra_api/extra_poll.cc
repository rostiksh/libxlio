/*
 * Copyright (c) 2001-2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "common/def.h"
#include "common/log.h"
#include "common/sys.h"
#include "common/base.h"
#include "common/cmn.h"

#include "tcp/tcp_base.h"
#include "udp/udp_base.h"
#include "core/xlio_base.h"

#if defined(EXTRA_API_ENABLED) && (EXTRA_API_ENABLED == 1)

class socketxtreme_poll : public xlio_base {
protected:
    void SetUp()
    {
        xlio_base::SetUp();

        SKIP_TRUE((getenv("XLIO_SOCKETXTREME")), "This test requires XLIO_SOCKETXTREME=1");
        SKIP_TRUE(m_family == PF_INET, "sockextreme API supports IPv4 only");
    }
    void TearDown() { xlio_base::TearDown(); }

    tcp_base_sock m_tcp_base;
    udp_base_sock m_udp_base;
};

/**
 * @test socketxtreme_poll.ti_1
 * @brief
 *    Check TCP connection acceptance (XLIO_SOCKETXTREME_NEW_CONNECTION_ACCEPTED)
 * @details
 */
TEST_F(socketxtreme_poll, ti_1)
{
    int rc = EOK;
    int fd;

    errno = EOK;

    int pid = fork();

    if (0 == pid) { /* I am the child */
        struct epoll_event event;

        barrier_fork(pid);

        fd = m_tcp_base.sock_create_fa_nb(m_family);
        ASSERT_LE(0, fd);

        rc = bind(fd, (struct sockaddr *)&client_addr, sizeof(client_addr));
        ASSERT_EQ(0, rc);

        rc = connect(fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
        ASSERT_EQ(EINPROGRESS, errno);
        ASSERT_EQ((-1), rc);

        event.events = EPOLLOUT | EPOLLIN;
        event.data.fd = fd;
        rc = test_base::event_wait(&event);
        EXPECT_LT(0, rc);
        EXPECT_EQ((uint32_t)(EPOLLOUT), event.events);

        log_trace("Established connection: fd=%d to %s\n", fd,
                  sys_addr2str((struct sockaddr *)&server_addr));

        close(fd);

        /* This exit is very important, otherwise the fork
         * keeps running and may duplicate other tests.
         */
        exit(testing::Test::HasFailure());
    } else { /* I am the parent */
        int _xlio_ring_fd = -1;
        struct xlio_socketxtreme_completion_t xlio_comps;
        int fd_peer;
        struct sockaddr peer_addr;

        fd = m_tcp_base.sock_create_fa_nb(m_family);
        ASSERT_LE(0, fd);

        rc = bind(fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
        CHECK_ERR_OK(rc);

        rc = listen(fd, 5);
        CHECK_ERR_OK(rc);

        rc = xlio_api->get_socket_rings_fds(fd, &_xlio_ring_fd, 1);
        ASSERT_EQ(1, rc);
        ASSERT_LE(0, _xlio_ring_fd);

        barrier_fork(pid);
        rc = 0;
        while (rc == 0 && !child_fork_exit()) {
            rc = xlio_api->socketxtreme_poll(_xlio_ring_fd, &xlio_comps, 1, 0);
            if (xlio_comps.events & XLIO_SOCKETXTREME_NEW_CONNECTION_ACCEPTED) {
                EXPECT_EQ(fd, (int)xlio_comps.listen_fd);
                fd_peer = (int)xlio_comps.user_data;
                EXPECT_LE(0, fd_peer);
                memcpy(&peer_addr, &xlio_comps.src, sizeof(peer_addr));
                log_trace("Accepted connection: fd=%d from %s\n", fd_peer,
                          sys_addr2str((struct sockaddr *)&peer_addr));
                rc = 0;
            }
        }

        close(fd_peer);
        close(fd);

        ASSERT_EQ(0, wait_fork(pid));
        sleep(1U); // XLIO timers to clean fd.
    }
}

/**
 * @test socketxtreme_poll.ti_2
 * @brief
 *    Check TCP connection data receiving (XLIO_SOCKETXTREME_PACKET)
 * @details
 */
TEST_F(socketxtreme_poll, ti_2)
{
    int rc = EOK;
    int fd;
    char msg[] = "Hello";

    errno = EOK;

    int pid = fork();

    if (0 == pid) { /* I am the child */
        barrier_fork(pid);

        fd = m_tcp_base.sock_create_fa(m_family);
        ASSERT_LE(0, fd);

        rc = bind(fd, (struct sockaddr *)&client_addr, sizeof(client_addr));
        ASSERT_EQ(0, rc);

        rc = connect(fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
        ASSERT_EQ(0, rc);

        log_trace("Established connection: fd=%d to %s\n", fd,
                  sys_addr2str((struct sockaddr *)&server_addr));

        rc = send(fd, (void *)msg, sizeof(msg), 0);
        EXPECT_EQ(static_cast<int>(sizeof(msg)), rc);

        close(fd);

        /* This exit is very important, otherwise the fork
         * keeps running and may duplicate other tests.
         */
        exit(testing::Test::HasFailure());
    } else { /* I am the parent */
        int _xlio_ring_fd = -1;
        struct xlio_socketxtreme_completion_t xlio_comps;
        int fd_peer;
        struct sockaddr peer_addr;

        fd = m_tcp_base.sock_create_fa_nb(m_family);
        ASSERT_LE(0, fd);

        rc = bind(fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
        CHECK_ERR_OK(rc);

        rc = listen(fd, 5);
        CHECK_ERR_OK(rc);

        rc = xlio_api->get_socket_rings_fds(fd, &_xlio_ring_fd, 1);
        ASSERT_EQ(1, rc);
        ASSERT_LE(0, _xlio_ring_fd);

        barrier_fork(pid);
        rc = 0;
        while (rc == 0 && !child_fork_exit()) {
            rc = xlio_api->socketxtreme_poll(_xlio_ring_fd, &xlio_comps, 1, 0);
            if ((xlio_comps.events & EPOLLERR) || (xlio_comps.events & EPOLLHUP) ||
                (xlio_comps.events & EPOLLRDHUP)) {
                log_trace("Close connection: fd=%d event: 0x%lx\n", (int)xlio_comps.user_data,
                          xlio_comps.events);
                rc = 0;
                break;
            }
            if (xlio_comps.events & XLIO_SOCKETXTREME_NEW_CONNECTION_ACCEPTED) {
                EXPECT_EQ(fd, (int)xlio_comps.listen_fd);
                fd_peer = (int)xlio_comps.user_data;
                EXPECT_LE(0, fd_peer);
                memcpy(&peer_addr, &xlio_comps.src, sizeof(peer_addr));
                log_trace("Accepted connection: fd=%d from %s\n", fd_peer,
                          sys_addr2str((struct sockaddr *)&peer_addr));
                rc = 0;
            }
            if (xlio_comps.events & XLIO_SOCKETXTREME_PACKET) {
                EXPECT_EQ(1U, xlio_comps.packet.num_bufs);
                EXPECT_LE(0, (int)xlio_comps.user_data);
                EXPECT_EQ(sizeof(msg), xlio_comps.packet.total_len);
                EXPECT_TRUE(xlio_comps.packet.buff_lst->payload);
                log_trace("Received data: fd=%d data: %s\n", (int)xlio_comps.user_data,
                          (char *)xlio_comps.packet.buff_lst->payload);
                rc = 0;
            }
        }

        close(fd_peer);
        close(fd);

        ASSERT_EQ(0, wait_fork(pid));
        sleep(1U); // XLIO timers to clean fd.
    }
}

/**
 * @test socketxtreme_poll.ti_3
 * @brief
 *    Check TCP connection data receiving (SO_XLIO_USER_DATA)
 * @details
 */
TEST_F(socketxtreme_poll, ti_3)
{
    int rc = EOK;
    int fd;
    char msg[] = "Hello";

    errno = EOK;

    int pid = fork();

    if (0 == pid) { /* I am the child */
        barrier_fork(pid);

        fd = m_tcp_base.sock_create_fa(m_family);
        ASSERT_LE(0, fd);

        rc = bind(fd, (struct sockaddr *)&client_addr, sizeof(client_addr));
        ASSERT_EQ(0, rc);

        rc = connect(fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
        ASSERT_EQ(0, rc);

        log_trace("Established connection: fd=%d to %s\n", fd,
                  sys_addr2str((struct sockaddr *)&server_addr));

        rc = send(fd, (void *)msg, sizeof(msg), 0);
        EXPECT_EQ(static_cast<int>(sizeof(msg)), rc);

        close(fd);

        /* This exit is very important, otherwise the fork
         * keeps running and may duplicate other tests.
         */
        exit(testing::Test::HasFailure());
    } else { /* I am the parent */
        int _xlio_ring_fd = -1;
        struct xlio_socketxtreme_completion_t xlio_comps;
        int fd_peer = -1;
        struct sockaddr peer_addr;
        const char *user_data = "This is a data";

        fd = m_tcp_base.sock_create_fa_nb(m_family);
        ASSERT_LE(0, fd);

        rc = bind(fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
        CHECK_ERR_OK(rc);

        rc = listen(fd, 5);
        CHECK_ERR_OK(rc);

        rc = xlio_api->get_socket_rings_fds(fd, &_xlio_ring_fd, 1);
        ASSERT_EQ(1, rc);
        ASSERT_LE(0, _xlio_ring_fd);

        barrier_fork(pid);
        rc = 0;
        while (rc == 0 && !child_fork_exit()) {
            rc = xlio_api->socketxtreme_poll(_xlio_ring_fd, &xlio_comps, 1, 0);
            if ((xlio_comps.events & EPOLLERR) || (xlio_comps.events & EPOLLHUP) ||
                (xlio_comps.events & EPOLLRDHUP)) {
                log_trace("Close connection: event: 0x%lx\n", xlio_comps.events);
                rc = 0;
                break;
            }
            if (xlio_comps.events & XLIO_SOCKETXTREME_NEW_CONNECTION_ACCEPTED) {
                EXPECT_EQ(fd, (int)xlio_comps.listen_fd);
                fd_peer = (int)xlio_comps.user_data;
                memcpy(&peer_addr, &xlio_comps.src, sizeof(peer_addr));
                log_trace("Accepted connection: fd: %d from %s\n", fd_peer,
                          sys_addr2str((struct sockaddr *)&peer_addr));

                errno = EOK;
                rc = setsockopt(fd_peer, SOL_SOCKET, SO_XLIO_USER_DATA, &user_data, sizeof(void *));
                EXPECT_EQ(0, rc);
                EXPECT_EQ(EOK, errno);
                log_trace("Set data: %p\n", user_data);
                rc = 0;
            }
            if (xlio_comps.events & XLIO_SOCKETXTREME_PACKET) {
                EXPECT_EQ(1U, xlio_comps.packet.num_bufs);
                EXPECT_EQ((uintptr_t)user_data, (uintptr_t)xlio_comps.user_data);
                EXPECT_EQ(sizeof(msg), xlio_comps.packet.total_len);
                EXPECT_TRUE(xlio_comps.packet.buff_lst->payload);
                log_trace("Received data: user_data: %p data: %s\n",
                          (void *)((uintptr_t)xlio_comps.user_data),
                          (char *)xlio_comps.packet.buff_lst->payload);
                rc = 0;
            }
        }

        close(fd_peer);
        close(fd);

        ASSERT_EQ(0, wait_fork(pid));
        sleep(1U); // XLIO timers to clean fd.
    }
}

#endif /* EXTRA_API_ENABLED */
