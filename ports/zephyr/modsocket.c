/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2017 Linaro Limited
 * Copyright (c) 2026 MASSDRIVER EI (massdriver.space)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "py/mpconfig.h"

#ifdef MICROPY_PY_SOCKET

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/posix/fcntl.h>

#include "py/runtime.h"
#include "py/stream.h"
#include "py/objstr.h"

typedef enum _socket_obj_state_t {
    SOCK_STATE_NEW = 0,
    SOCK_STATE_CONNECTING = 1,
    SOCK_STATE_CONNECTED = 2,
    SOCK_STATE_PEER_CLOSED = 3,
} socket_obj_state_t;

typedef struct _socket_obj_t {
    mp_obj_base_t base;
    int ctx;
    enum net_sock_type type;
    net_sa_family_t family;
    int proto;
    bool bound;
    int timeout;
    socket_obj_state_t state;
} socket_obj_t;

static const mp_obj_type_t socket_type;

// Helper functions

socket_obj_t *socket_new(socket_obj_t *from) {
    socket_obj_t *socket = mp_obj_malloc_with_finaliser(socket_obj_t, &socket_type);
    socket->state = SOCK_STATE_NEW;
    socket->bound = false;
    socket->timeout = -1;
    if (from != NULL) {
        socket->family = from->family;
        socket->proto = from->proto;
        socket->type = from->type;
    }
    return socket;
}

static void socket_check_closed(socket_obj_t *socket) {
    if (socket->ctx < 0) {
        // already closed
        mp_raise_ValueError(MP_ERROR_TEXT("socket is closed"));
    }
}

typedef struct _socket_proto_t {
    const char *str;
    bool secure;
} socket_proto_t;

static const socket_proto_t socket_proto_get(const int id) {
    switch (id) {
    case NET_IPPROTO_IP:
        return (const socket_proto_t) { .str = "IPPROTO_IP", .secure = false, };
    case NET_IPPROTO_ICMP:
        return (const socket_proto_t) { .str = "IPPROTO_ICMP", .secure = false, };
    case NET_IPPROTO_IGMP:
        return (const socket_proto_t) { .str = "IPPROTO_IGMP", .secure = false, };
    case NET_IPPROTO_ETH_P_ALL:
        return (const socket_proto_t) { .str = "IPPROTO_ETH_P_ALL", .secure = false, };
    case NET_IPPROTO_IPIP:
        return (const socket_proto_t) { .str = "IPPROTO_IPIP", .secure = false, };
    case NET_IPPROTO_TCP:
        return (const socket_proto_t) { .str = "IPPROTO_TCP", .secure = false, };
    case NET_IPPROTO_UDP:
        return (const socket_proto_t) { .str = "IPPROTO_UDP", .secure = false, };
    case NET_IPPROTO_IPV6:
        return (const socket_proto_t) { .str = "IPPROTO_IPV6", .secure = false, };
    case NET_IPPROTO_ICMPV6:
        return (const socket_proto_t) { .str = "IPPROTO_ICMPV6", .secure = false, };
    case NET_IPPROTO_RAW:
        return (const socket_proto_t) { .str = "IPPROTO_RAW", .secure = false, };
    case NET_IPPROTO_TLS_1_0:
        return (const socket_proto_t) { .str = "IPPROTO_TLS_1_0", .secure = true, };
    case NET_IPPROTO_TLS_1_1:
        return (const socket_proto_t) { .str = "IPPROTO_TLS_1_1", .secure = true, };
    case NET_IPPROTO_TLS_1_2:
        return (const socket_proto_t) { .str = "IPPROTO_TLS_1_2", .secure = true, };
    case NET_IPPROTO_TLS_1_3:
        return (const socket_proto_t) { .str = "IPPROTO_TLS_1_3", .secure = true, };
    case NET_IPPROTO_DTLS_1_0:
        return (const socket_proto_t) { .str = "IPPROTO_DTLS_1_0", .secure = true, };
    case NET_IPPROTO_DTLS_1_2:
        return (const socket_proto_t) { .str = "IPPROTO_DTLS_1_2", .secure = true, };
    case NET_IPPROTO_QUIC:
        return (const socket_proto_t) { .str = "IPPROTO_QUIC", .secure = true, };
    }

    mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("%d is not a socket protocol"), id);
    return (const socket_proto_t) { 0 };
}

static const char *socket_type_get_str(const enum net_sock_type type) {
    switch (type) {
    case NET_SOCK_STREAM:
        return "SOCK_STREAM";
    case NET_SOCK_DGRAM:
        return "SOCK_DGRAM";
    case NET_SOCK_RAW:
        return "SOCK_RAW";
    }

    mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("%d is not a socket type"), type);
    return NULL;
}

static const char *socket_family_get_str(const net_sa_family_t family) {
    switch (family) {
    case NET_AF_UNSPEC:
        return "AF_UNSPEC";
    case NET_AF_INET:
        return "AF_INET";
    case NET_AF_INET6:
        return "AF_INET6";
    case NET_AF_PACKET:
        return "AF_PACKET";
    case NET_AF_CAN:
        return "AF_CAN";
    case NET_AF_NET_MGMT:
        return "AF_NET_MGMT";
    case NET_AF_LOCAL: /* NET_AF_UNIX*/
        return "AF_LOCAL";
    }

    mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("%d is not a socket address family"), family);
    return NULL;
}

static int socket_parse_addr(socket_obj_t *socket, mp_obj_t addr_in, struct net_sockaddr *saddr_in) {
    saddr_in->sa_family = socket->family;

    /* Single address string */
    #if defined(CONFIG_NET_IPV4) || defined(CONFIG_NET_IPV6)
    if (mp_obj_is_str(addr_in)) {
        mp_warning(MP_WARN_CAT(ValueWarning), "use address tuple");

        GET_STR_DATA_LEN(addr_in, addr_in_str, addr_in_len);
        if (!net_ipaddr_parse(addr_in_str, addr_in_len, saddr_in)) {
            mp_raise_ValueError(MP_ERROR_TEXT("could not parse socket address"));
            return -EINVAL;
        }

        return 0;
    }
    #endif

    switch (socket->family) {
    #if defined(CONFIG_NET_IPV4)
    case NET_AF_INET: {
        struct net_sockaddr_in *addr_sin = (struct net_sockaddr_in *)saddr_in;
        mp_obj_t *addr_items;
        mp_obj_get_array_fixed_n(addr_in, 2, &addr_items);
        int ret;

        ret = net_addr_pton(socket->family, mp_obj_str_get_str(addr_items[0]), &addr_sin->sin_addr);
        if (ret < 0) {
            mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("failed to parse socket address: %d"), ret);
        }

        addr_sin->sin_port = htons(mp_obj_get_int(addr_items[1]));

        return 0;
    }
    #endif
    #if defined(CONFIG_NET_IPV6)
    case NET_AF_INET6: {
        struct net_sockaddr_in6 *addr_sin = (struct net_sockaddr_in6 *)saddr_in;
        mp_obj_t *addr_items;
        mp_obj_get_array_fixed_n(addr_in, 4, &addr_items);
        int ret;

        ret = net_addr_pton(socket->family, mp_obj_str_get_str(addr_items[0]), &addr_sin->sin6_addr);
        if (ret < 0) {
            mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("failed to parse socket address: %d"), ret);
        }

        addr_sin->sin6_port = htons(mp_obj_get_int(addr_items[1]));

        if (mp_obj_get_int(addr_items[2]) != 0) {
            mp_raise_ValueError(MP_ERROR_TEXT("flowinfo (addr[2]) must be 0"));
        }

        addr_sin->sin6_scope_id = mp_obj_get_uint(addr_items[3]);

        return 0;
    }
    #endif
    #if defined(NET_SOCKETS_CAN)
    case NET_AF_CAN: {
        struct net_sockaddr_can *addr_sin = (struct net_sockaddr_can *)saddr_in;
        if (mp_obj_is_int(addr_in)) {
            addr_sin->can_ifindex = mp_obj_get_int(addr_in);
        } else {
            mp_obj_t *addr_items;
            mp_obj_get_array_fixed_n(addr_in, 1, &addr_items);
            addr_sin->can_ifindex = mp_obj_get_int(addr_items[0]);
        }
        return 0;
    }
    #endif
    default:
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("%s is not supported"), socket_family_get_str(socket->family));
        return -ENOTSUP;
    }

    return -EINVAL;
}

static mp_obj_t socket_format_addr(const struct net_sockaddr *saddr_in) {

    switch (saddr_in->sa_family) {
    #if defined(CONFIG_NET_IPV4)
    case NET_AF_INET: {
        struct net_sockaddr_in *addr_sin = (struct net_sockaddr_in *)saddr_in;
        char buf[NET_IPV4_ADDR_LEN];
        if (net_addr_ntop(saddr_in->sa_family, &addr_sin->sin_addr, buf, NET_IPV4_ADDR_LEN) == NULL) {
            mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("failed to convert ip to string"));
        }
        mp_obj_tuple_t *tuple = mp_obj_new_tuple(2, NULL);
        tuple->items[0] = mp_obj_new_str_from_cstr(buf);
        tuple->items[1] = mp_obj_new_int(addr_sin->sin_port);
        return tuple;
    }
    #endif
    #if defined(CONFIG_NET_IPV6)
    case NET_AF_INET6: {
        struct net_sockaddr_in6 *addr_sin = (struct net_sockaddr_in6 *)saddr_in;
        char buf[NET_IPV4_ADDR_LEN];
        if (net_addr_ntop(saddr_in->sa_family, &addr_sin->sin6_addr, buf, NET_IPV4_ADDR_LEN) == NULL) {
            mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("failed to convert ip to string"));
        }
        mp_obj_tuple_t *tuple = mp_obj_new_tuple(4, NULL);
        tuple->items[0] = mp_obj_new_str_from_cstr(buf);
        tuple->items[1] = mp_obj_new_int(addr_sin->sin6_port);
        tuple->items[2] = mp_obj_new_int(0);
        tuple->items[3] = mp_obj_new_int(addr_sin->sin6_scope_id);
        return tuple;
    }
    #endif
    #if defined(NET_SOCKETS_CAN)
    case NET_AF_CAN: {
        struct net_sockaddr_can *addr_sin = (struct net_sockaddr_can *)saddr_in;
        mp_obj_tuple_t *tuple = mp_obj_new_tuple(1, NULL);
        tuple->items[0] = mp_obj_new_int(addr_sin->can_ifindex);
        return tuple;
    }
    #endif
    default:
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("%s is not supported"), socket_family_get_str(saddr_in->sa_family));
        return mp_const_none;
    }

    return mp_const_none;
}

static int socket_read_handler(socket_obj_t *self, void *buf, size_t *len, uint32_t flags,
    struct net_sockaddr *from, net_socklen_t *from_len) {

    if (self->state == SOCK_STATE_NEW) {
        return MP_ENOTCONN;
    }

    if (self->state == SOCK_STATE_PEER_CLOSED) {
        *len = 0;
        return 0;
    }

    struct zsock_pollfd fds = {
        .fd = self->ctx,
        .events = ZSOCK_POLLIN,
        .revents = 0,
    };

    /* If data is waiting, don't lock GIL */
    int ret = zsock_poll(&fds, 1, 0);
    if (ret < 0) {
        return MP_EIO;
    }

    if (fds.revents == 0) {
        MP_THREAD_GIL_EXIT();
    }
    ssize_t recv_len = zsock_recvfrom(self->ctx, buf, *len, flags, from, from_len);
    if (fds.revents == 0) {
        MP_THREAD_GIL_ENTER();
    }

    if (recv_len < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return MP_EWOULDBLOCK;
        }
        return errno;
    }

    if (recv_len == 0) {
        self->state = SOCK_STATE_PEER_CLOSED;
    }

    *len = recv_len;

    return 0;
}

// Methods

static void socket_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    socket_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->ctx < 0) {
        mp_printf(print, "<socket NULL>");
    } else {
        const socket_proto_t proto = socket_proto_get(self->proto);
        mp_printf(print, "<socket %p fd=%d timeout=%d domain=%s type=%s proto=%s bound=%b>",
            self,
            self->ctx,
            self->timeout,
            socket_family_get_str(self->family),
            socket_type_get_str(self->type),
            proto.str,
            self->bound);
    }
}

static mp_obj_t socket_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    enum { ARG_family, ARG_type, ARG_proto, ARG_blocking };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_family, MP_ARG_INT, {.u_int = NET_AF_INET} },
        { MP_QSTR_type, MP_ARG_INT, {.u_int = NET_SOCK_STREAM} },
        { MP_QSTR_proto, MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_blocking, MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = true} },
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    socket_family_get_str(args[ARG_family].u_int);

    socket_type_get_str(args[ARG_type].u_int);

    if (args[ARG_proto].u_int == -1) {
        args[ARG_proto].u_int = NET_IPPROTO_TCP;
        if (args[ARG_type].u_int != NET_SOCK_STREAM) {
            args[ARG_proto].u_int = NET_IPPROTO_UDP;
        }
    } else {
        socket_proto_get(args[ARG_proto].u_int);
    }

    socket_obj_t *self = socket_new(NULL);

    self->ctx = zsock_socket(args[ARG_family].u_int, args[ARG_type].u_int, args[ARG_proto].u_int);
    if (self->ctx < 0) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("could not create socket"));
    }

    if (!args[ARG_blocking].u_bool) {
        /* Function only for setting NONBLOCK */
        if (zsock_fcntl_impl(self->ctx, F_SETFL, O_NONBLOCK) != 0) {
            mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("could not set socket to nonblock"));
        }
        self->timeout = 0;
    } else {
        self->timeout = -1;
    }

    self->family = args[ARG_family].u_int;
    self->type = args[ARG_type].u_int;
    self->proto = args[ARG_proto].u_int;

    return MP_OBJ_FROM_PTR(self);
}

static mp_obj_t socket_bind(mp_obj_t self_in, mp_obj_t addr_in) {
    socket_obj_t *self = MP_OBJ_TO_PTR(self_in);
    socket_check_closed(self);

    struct net_sockaddr saddr_in;
    socket_parse_addr(self, addr_in, &saddr_in);

    int ret = zsock_bind(self->ctx, &saddr_in, sizeof(saddr_in));
    if (ret < 0) {
        mp_raise_OSError(errno);
    }
    self->state = SOCK_STATE_CONNECTED;
    self->bound = true;

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(socket_bind_obj, socket_bind);

static mp_obj_t socket_connect(mp_obj_t self_in, mp_obj_t addr_in) {
    socket_obj_t *self = MP_OBJ_TO_PTR(self_in);
    socket_check_closed(self);

    struct net_sockaddr saddr_in;
    socket_parse_addr(self, addr_in, &saddr_in);

    int ret = zsock_connect(self->ctx, &saddr_in, sizeof(saddr_in));
    if (ret < 0) {
        if (self->timeout == 0 && errno == EINPROGRESS) {
            self->state = SOCK_STATE_CONNECTING;
            return mp_const_none;
        }
        mp_raise_OSError(errno);
    }

    self->state = SOCK_STATE_CONNECTED;

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(socket_connect_obj, socket_connect);

static mp_obj_t socket_listen(size_t n_args, const mp_obj_t *args) {
    socket_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    socket_check_closed(self);

    mp_int_t backlog = MICROPY_PY_SOCKET_LISTEN_BACKLOG_DEFAULT;
    if (n_args > 1) {
        backlog = mp_obj_get_int(args[1]);
        backlog = (backlog < 0) ? 0 : backlog;
    }

    int ret = zsock_listen(self->ctx, backlog);
    if (ret < 0) {
        mp_raise_OSError(errno);
    }

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(socket_listen_obj, 1, 2, socket_listen);

static mp_obj_t socket_accept(mp_obj_t self_in) {
    socket_obj_t *self = MP_OBJ_TO_PTR(self_in);
    socket_check_closed(self);

    if (!self->bound) {
        mp_raise_OSError(MP_EINVAL);
    }

    struct net_sockaddr saddr_in;
    socklen_t addrlen = sizeof(saddr_in);
    int ctx = zsock_accept(self->ctx, &saddr_in, &addrlen);

    socket_obj_t *socket = socket_new(self);
    socket->state = SOCK_STATE_CONNECTED;
    socket->ctx = ctx;

    mp_obj_tuple_t *client = MP_OBJ_TO_PTR(mp_obj_new_tuple(2, NULL));
    client->items[0] = MP_OBJ_FROM_PTR(socket);
    client->items[1] = socket_format_addr(&saddr_in);

    return MP_OBJ_FROM_PTR(client);
}
static MP_DEFINE_CONST_FUN_OBJ_1(socket_accept_obj, socket_accept);

static mp_obj_t socket_send(mp_obj_t self_in, mp_obj_t buf_in) {
    socket_obj_t *self = MP_OBJ_TO_PTR(self_in);
    socket_check_closed(self);

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_in, &bufinfo, MP_BUFFER_READ);

    ssize_t len = zsock_send(self->ctx, bufinfo.buf, bufinfo.len, 0);
    if (len < 0) {
        mp_raise_OSError(errno);
    }

    return mp_obj_new_int_from_uint(len);
}
static MP_DEFINE_CONST_FUN_OBJ_2(socket_send_obj, socket_send);

static mp_obj_t socket_recv(size_t n_args, const mp_obj_t *args) {
    socket_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    socket_check_closed(self);

    size_t len = mp_obj_get_uint(args[1]);
    vstr_t vstr;
    // +1 to accommodate for trailing \0
    vstr_init_len(&vstr, len + 1);

    int flags = n_args > 2 ? mp_obj_get_int(args[2]) : 0;

    int ret = socket_read_handler(self, vstr.buf, &len, flags, NULL, NULL);
    if (ret != 0) {
        vstr_clear(&vstr);
        mp_raise_OSError(ret);
    } else if (len == 0) {
        vstr_clear(&vstr);
        return mp_const_empty_bytes;
    }

    vstr.len = len;
    return mp_obj_new_bytes_from_vstr(&vstr);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(socket_recv_obj, 2, 3, socket_recv);

static mp_obj_t socket_recvfrom(size_t n_args, const mp_obj_t *args) {
    socket_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    socket_check_closed(self);

    size_t len = mp_obj_get_int(args[1]);
    vstr_t vstr;
    // +1 to accommodate for trailing \0
    vstr_init_len(&vstr, len + 1);

    int flags = n_args > 2 ? mp_obj_get_int(args[2]) : 0;

    struct net_sockaddr saddr;
    socklen_t slen = sizeof(saddr);

    int ret = socket_read_handler(self, vstr.buf, &len, flags, &saddr, &slen);
    if (ret != 0) {
        vstr_clear(&vstr);
        mp_raise_OSError(ret);
    }

    mp_obj_t items[2];
    if (len == 0) {
        vstr_clear(&vstr);
        items[0] = mp_const_empty_bytes;
    } else {
        vstr.len = len;
        items[0] = mp_obj_new_bytes_from_vstr(&vstr);
    }
    items[1] = socket_format_addr(&saddr);
    return mp_obj_new_tuple(2, items);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(socket_recvfrom_obj, 2, 3, socket_recvfrom);

static mp_obj_t socket_setsockopt(size_t n_args, const mp_obj_t *args) {
    (void)n_args; // always 4
    mp_warning(MP_WARN_CAT(RuntimeWarning), "setsockopt() not implemented");
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(socket_setsockopt_obj, 4, 4, socket_setsockopt);

static mp_obj_t socket_setblocking(const mp_obj_t arg0, const mp_obj_t arg1) {
    socket_obj_t *self = MP_OBJ_TO_PTR(arg0);

    if (!mp_obj_is_true(arg1)) {
        if (zsock_fcntl_impl(self->ctx, F_SETFL, O_NONBLOCK) != 0) {
            mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("could not set socket to nonblock"));
        }
        self->timeout = 0;
    } else {
        if (zsock_fcntl_impl(self->ctx, F_SETFL, 0) != 0) {
            mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("could not set socket to blocking"));
        }
        if (self->timeout == 0) {
            self->timeout = -1;
        }
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(socket_setblocking_obj, socket_setblocking);

static mp_obj_t socket_makefile(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    return args[0];
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(socket_makefile_obj, 1, 3, socket_makefile);

static const mp_rom_map_elem_t socket_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&mp_stream_close_obj) },
    { MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&mp_stream_close_obj) },
    { MP_ROM_QSTR(MP_QSTR_bind), MP_ROM_PTR(&socket_bind_obj) },
    { MP_ROM_QSTR(MP_QSTR_connect), MP_ROM_PTR(&socket_connect_obj) },
    { MP_ROM_QSTR(MP_QSTR_listen), MP_ROM_PTR(&socket_listen_obj) },
    { MP_ROM_QSTR(MP_QSTR_accept), MP_ROM_PTR(&socket_accept_obj) },
    { MP_ROM_QSTR(MP_QSTR_send), MP_ROM_PTR(&socket_send_obj) },
    { MP_ROM_QSTR(MP_QSTR_recv), MP_ROM_PTR(&socket_recv_obj) },
    { MP_ROM_QSTR(MP_QSTR_recvfrom), MP_ROM_PTR(&socket_recvfrom_obj) },
    { MP_ROM_QSTR(MP_QSTR_setsockopt), MP_ROM_PTR(&socket_setsockopt_obj) },
    { MP_ROM_QSTR(MP_QSTR_setblocking), MP_ROM_PTR(&socket_setblocking_obj) },

    { MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&mp_stream_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_readinto), MP_ROM_PTR(&mp_stream_readinto_obj) },
    { MP_ROM_QSTR(MP_QSTR_readline), MP_ROM_PTR(&mp_stream_unbuffered_readline_obj) },
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&mp_stream_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_makefile), MP_ROM_PTR(&socket_makefile_obj) },
};
static MP_DEFINE_CONST_DICT(socket_locals_dict, socket_locals_dict_table);

/* Stream Protocol */

static mp_uint_t sock_write(mp_obj_t self_in, const void *buf, mp_uint_t size, int *errcode) {
    socket_obj_t *self = MP_OBJ_TO_PTR(self_in);

    if (self->ctx < 0) {
        // already closed
        *errcode = EBADF;
        return MP_STREAM_ERROR;
    }

    ssize_t len = zsock_send(self->ctx, buf, size, 0);
    if (len == -1) {
        *errcode = errno;
        return MP_STREAM_ERROR;
    }

    return len;
}

static mp_uint_t sock_read(mp_obj_t self_in, void *buf, mp_uint_t max_len, int *errcode) {
    socket_obj_t *self = MP_OBJ_TO_PTR(self_in);

    if (self->ctx < 0) {
        // already closed
        *errcode = EBADF;
        return MP_STREAM_ERROR;
    }

    size_t len = max_len;
    int ret = socket_read_handler(self, buf, &len, 0, NULL, NULL);
    if (ret != 0) {
        *errcode = ret;
        return MP_STREAM_ERROR;
    }

    return len;
}

static mp_uint_t sock_ioctl(mp_obj_t self_in, mp_uint_t request, uintptr_t arg, int *errcode) {
    socket_obj_t *self = MP_OBJ_TO_PTR(self_in);

    switch (request) {
    case MP_STREAM_CLOSE:
        if (self->ctx >= 0) {
            int ret = zsock_close(self->ctx);
            if (ret < 0) {
                *errcode = errno;
                return MP_STREAM_ERROR;
            }
            self->ctx = -1;
        }
        return 0;
    case MP_STREAM_POLL: {
        if (self->ctx == -1) {
            return MP_STREAM_POLL_NVAL;
        }

        struct zsock_pollfd fds = {
            .fd = self->ctx,
            /* MPY poll tags match zephyr's */
            .events = arg,
            .revents = 0,
        };

        int ret = zsock_poll(&fds, 1, 0);
        if (ret < 0) {
            *errcode = MP_EIO;
            return MP_STREAM_ERROR;
        }

        // New (unconnected) sockets are writable and have HUP set.
        if (self->state == SOCK_STATE_NEW) {
            fds.revents |= (arg & MP_STREAM_POLL_WR) | MP_STREAM_POLL_HUP;
        }

        return fds.revents;
    }
    default:
        *errcode = MP_EINVAL;
        return MP_STREAM_ERROR;
    }
}

/* Ensure poll tags stay matching */
BUILD_ASSERT(MP_STREAM_POLL_RD == ZSOCK_POLLIN);
BUILD_ASSERT(MP_STREAM_POLL_WR == ZSOCK_POLLOUT);
BUILD_ASSERT(MP_STREAM_POLL_ERR == ZSOCK_POLLERR);
BUILD_ASSERT(MP_STREAM_POLL_HUP == ZSOCK_POLLHUP);
BUILD_ASSERT(MP_STREAM_POLL_NVAL == ZSOCK_POLLNVAL);

static const mp_stream_p_t socket_stream_p = {
    .read = sock_read,
    .write = sock_write,
    .ioctl = sock_ioctl,
};

static MP_DEFINE_CONST_OBJ_TYPE(
    socket_type,
    MP_QSTR_socket,
    MP_TYPE_FLAG_NONE,
    make_new, socket_make_new,
    print, socket_print,
    protocol, &socket_stream_p,
    locals_dict, &socket_locals_dict
    );

//
// getaddrinfo() implementation
//

typedef struct _getaddrinfo_state_t {
    mp_obj_t result;
    struct k_sem sem;
    mp_obj_t port;
    int status;
} getaddrinfo_state_t;

void dns_resolve_cb(enum dns_resolve_status status, struct dns_addrinfo *info, void *user_data) {
    getaddrinfo_state_t *state = user_data;
    DEBUG_printf("dns status: %d\n", status);

    if (info == NULL) {
        if (status == DNS_EAI_ALLDONE) {
            status = 0;
        }
        state->status = status;
        k_sem_give(&state->sem);
        return;
    }

    mp_obj_tuple_t *tuple = mp_obj_new_tuple(5, NULL);
    tuple->items[0] = MP_OBJ_NEW_SMALL_INT(info->ai_family);
    // info->ai_socktype not filled
    tuple->items[1] = MP_OBJ_NEW_SMALL_INT(SOCK_STREAM);
    // info->ai_protocol not filled
    tuple->items[2] = MP_OBJ_NEW_SMALL_INT(IPPROTO_TCP);
    tuple->items[3] = MP_OBJ_NEW_QSTR(MP_QSTR_);
    tuple->items[4] = format_inet_addr(&info->ai_addr, state->port);
    mp_obj_list_append(state->result, MP_OBJ_FROM_PTR(tuple));
}

static mp_obj_t mod_getaddrinfo(size_t n_args, const mp_obj_t *args) {
    mp_obj_t host_in = args[0], port_in = args[1];
    const char *host = mp_obj_str_get_str(host_in);
    mp_int_t family = 0;
    if (n_args > 2) {
        family = mp_obj_get_int(args[2]);
    }

    getaddrinfo_state_t state;
    // Just validate that it's int
    (void)mp_obj_get_int(port_in);
    state.port = port_in;
    state.result = mp_obj_new_list(0, NULL);
    k_sem_init(&state.sem, 0, UINT_MAX);

    for (int i = 2; i--;) {
        int type = (family != AF_INET6 ? DNS_QUERY_TYPE_A : DNS_QUERY_TYPE_AAAA);
        RAISE_ERRNO(dns_get_addr_info(host, type, NULL, dns_resolve_cb, &state, 3000));
        k_sem_take(&state.sem, K_FOREVER);
        if (family != 0) {
            break;
        }
        family = AF_INET6;
    }

    // Raise error only if there's nothing to return, otherwise
    // it may be IPv4 vs IPv6 differences.
    mp_int_t len = MP_OBJ_SMALL_INT_VALUE(mp_obj_len(state.result));
    if (state.status != 0 && len == 0) {
        mp_raise_OSError(state.status);
    }

    return state.result;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_getaddrinfo_obj, 2, 3, mod_getaddrinfo);


static mp_obj_t pkt_get_info(void) {
    struct k_mem_slab *rx, *tx;
    struct net_buf_pool *rx_data, *tx_data;
    net_pkt_get_info(&rx, &tx, &rx_data, &tx_data);
    mp_obj_tuple_t *t = MP_OBJ_TO_PTR(mp_obj_new_tuple(4, NULL));
    t->items[0] = MP_OBJ_NEW_SMALL_INT(k_mem_slab_num_free_get(rx));
    t->items[1] = MP_OBJ_NEW_SMALL_INT(k_mem_slab_num_free_get(tx));
    t->items[2] = MP_OBJ_NEW_SMALL_INT(rx_data->avail_count);
    t->items[3] = MP_OBJ_NEW_SMALL_INT(tx_data->avail_count);
    return MP_OBJ_FROM_PTR(t);
}
static MP_DEFINE_CONST_FUN_OBJ_0(pkt_get_info_obj, pkt_get_info);

static const mp_rom_map_elem_t mp_module_socket_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_socket) },
    // objects
    { MP_ROM_QSTR(MP_QSTR_socket), MP_ROM_PTR(&socket_type) },
    // class constants
    { MP_ROM_QSTR(MP_QSTR_AF_UNSPEC), MP_ROM_INT(NET_AF_UNSPEC) },
    { MP_ROM_QSTR(MP_QSTR_AF_INET), MP_ROM_INT(NET_AF_INET) },
    { MP_ROM_QSTR(MP_QSTR_AF_INET6), MP_ROM_INT(NET_AF_INET6) },
    { MP_ROM_QSTR(MP_QSTR_AF_PACKET), MP_ROM_INT(NET_AF_PACKET) },
    { MP_ROM_QSTR(MP_QSTR_AF_CAN), MP_ROM_INT(NET_AF_CAN) },
    { MP_ROM_QSTR(MP_QSTR_AF_NET_MGMT), MP_ROM_INT(NET_AF_NET_MGMT) },
    { MP_ROM_QSTR(MP_QSTR_AF_LOCAL), MP_ROM_INT(NET_AF_LOCAL) },

    { MP_ROM_QSTR(MP_QSTR_SOCK_STREAM), MP_ROM_INT(NET_SOCK_STREAM) },
    { MP_ROM_QSTR(MP_QSTR_SOCK_DGRAM), MP_ROM_INT(NET_SOCK_DGRAM) },
    { MP_ROM_QSTR(MP_QSTR_SOCK_RAW), MP_ROM_INT(NET_SOCK_RAW) },

    { MP_ROM_QSTR(MP_QSTR_IPPROTO_IP), MP_ROM_INT(NET_IPPROTO_IP) },
    { MP_ROM_QSTR(MP_QSTR_IPPROTO_ICMP), MP_ROM_INT(NET_IPPROTO_ICMP) },
    { MP_ROM_QSTR(MP_QSTR_IPPROTO_IGMP), MP_ROM_INT(NET_IPPROTO_IGMP) },
    { MP_ROM_QSTR(MP_QSTR_IPPROTO_ETH_P_ALL), MP_ROM_INT(NET_IPPROTO_ETH_P_ALL) },
    { MP_ROM_QSTR(MP_QSTR_IPPROTO_IPIP), MP_ROM_INT(NET_IPPROTO_IPIP) },
    { MP_ROM_QSTR(MP_QSTR_IPPROTO_TCP), MP_ROM_INT(NET_IPPROTO_TCP) },
    { MP_ROM_QSTR(MP_QSTR_IPPROTO_UDP), MP_ROM_INT(NET_IPPROTO_UDP) },
    { MP_ROM_QSTR(MP_QSTR_IPPROTO_IPV6), MP_ROM_INT(NET_IPPROTO_IPV6) },
    { MP_ROM_QSTR(MP_QSTR_IPPROTO_ICMPV6), MP_ROM_INT(NET_IPPROTO_ICMPV6) },
    { MP_ROM_QSTR(MP_QSTR_IPPROTO_RAW), MP_ROM_INT(NET_IPPROTO_RAW) },
    { MP_ROM_QSTR(MP_QSTR_IPPROTO_TLS_1_0), MP_ROM_INT(NET_IPPROTO_TLS_1_0) },
    { MP_ROM_QSTR(MP_QSTR_IPPROTO_TLS_1_1), MP_ROM_INT(NET_IPPROTO_TLS_1_1) },
    { MP_ROM_QSTR(MP_QSTR_IPPROTO_TLS_1_2), MP_ROM_INT(NET_IPPROTO_TLS_1_2) },
    { MP_ROM_QSTR(MP_QSTR_IPPROTO_TLS_1_3), MP_ROM_INT(NET_IPPROTO_TLS_1_3) },
    { MP_ROM_QSTR(MP_QSTR_IPPROTO_DTLS_1_0), MP_ROM_INT(NET_IPPROTO_DTLS_1_0) },
    { MP_ROM_QSTR(MP_QSTR_IPPROTO_DTLS_1_2), MP_ROM_INT(NET_IPPROTO_DTLS_1_2) },
    { MP_ROM_QSTR(MP_QSTR_IPPROTO_QUIC), MP_ROM_INT(NET_IPPROTO_QUIC) },

    { MP_ROM_QSTR(MP_QSTR_SOL_SOCKET), MP_ROM_INT(1) },
    { MP_ROM_QSTR(MP_QSTR_SO_REUSEADDR), MP_ROM_INT(2) },

    { MP_ROM_QSTR(MP_QSTR_getaddrinfo), MP_ROM_PTR(&mod_getaddrinfo_obj) },
    { MP_ROM_QSTR(MP_QSTR_pkt_get_info), MP_ROM_PTR(&pkt_get_info_obj) },
};

static MP_DEFINE_CONST_DICT(mp_module_socket_globals, mp_module_socket_globals_table);

const mp_obj_module_t mp_module_socket = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mp_module_socket_globals,
};

MP_REGISTER_EXTENSIBLE_MODULE(MP_QSTR_socket, mp_module_socket);

#endif // MICROPY_PY_SOCKET
