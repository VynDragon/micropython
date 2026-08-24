/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
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

#if MICROPY_PY_ZEPHYR_TLS

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/net/tls_credentials.h>

#include "py/builtin.h"
#include "py/objstr.h"
#include "py/runtime.h"
#include "py/stream.h"

#ifndef NO_QSTR
#include <mbedtls/ssl.h>
#endif

#define MP_PROTOCOL_SERVER  (0x10)
#define MP_PROTOCOL_CLIENT  (0x0)
#define MP_PROTOCOL_TLS     (0x0)
#define MP_PROTOCOL_DTLS    (0x1)

#define MP_PROTOCOL_TLS_CLIENT  (MP_PROTOCOL_TLS | MP_PROTOCOL_CLIENT)
#define MP_PROTOCOL_TLS_SERVER  (MP_PROTOCOL_TLS | MP_PROTOCOL_SERVER)
#define MP_PROTOCOL_DTLS_CLIENT  (MP_PROTOCOL_DTLS | MP_PROTOCOL_CLIENT)
#define MP_PROTOCOL_DTLS_SERVER  (MP_PROTOCOL_DTLS | MP_PROTOCOL_SERVER)

typedef struct _ssl_context_sec_tag_t {
    atomic_t refcount;
    sec_tag_t tag;
} ssl_context_sec_tag_t;

// This corresponds to an SSLContext object.
typedef struct _ssl_context_obj_t {
    mp_obj_base_t base;
    uint32_t protocol;
    int *ciphersuites;
    sec_tag_t owned_tags[CONFIG_TLS_MAX_CREDENTIALS_NUMBER];
} ssl_context_obj_t;

static const mp_obj_type_t ssl_context_type;

/* Helper Functions ----------------------------------------------------------------------------- */

/* Global tag counter, 0 is always unused, negative are reserved */
static sec_tag_t sec_tag_cnt = 0;

static sec_tag_t ssl_context_get_tag(void) {
    sec_tag_cnt++;
    if (sec_tag_cnt >= INT_MAX) {
        sec_tag_cnt = 1;
    }
    return sec_tag_cnt;
}

/* Global security tags */
static ssl_context_sec_tag_t ssl_context_sec_tags[CONFIG_TLS_MAX_CREDENTIALS_NUMBER] = { 0 };

static int ssl_context_update_global_tags(sec_tag_t tag, bool add) {
    size_t last_free_entry = CONFIG_TLS_MAX_CREDENTIALS_NUMBER;

    for (size_t i = 0; i < CONFIG_TLS_MAX_CREDENTIALS_NUMBER; i++) {
        atomic_val_t val = atomic_get(&ssl_context_sec_tags[i].refcount);
        if (ssl_context_sec_tags[i].tag == tag && val > 0) {
            if (add) {
                atomic_inc(&ssl_context_sec_tags[i].refcount);
                return 0;
            } else {
                val = atomic_dec(&ssl_context_sec_tags[i].refcount);
                if (val == 0) {
                    /* Just try delete all possible for tag */
                    (void)tls_credential_delete(ssl_context_sec_tags[i].tag, TLS_CREDENTIAL_CA_CERTIFICATE);
                    (void)tls_credential_delete(ssl_context_sec_tags[i].tag, TLS_CREDENTIAL_PUBLIC_CERTIFICATE);
                    (void)tls_credential_delete(ssl_context_sec_tags[i].tag, TLS_CREDENTIAL_PRIVATE_KEY);
                    (void)tls_credential_delete(ssl_context_sec_tags[i].tag, TLS_CREDENTIAL_PSK);
                    (void)tls_credential_delete(ssl_context_sec_tags[i].tag, TLS_CREDENTIAL_PSK_ID);
                    (void)tls_credential_delete(ssl_context_sec_tags[i].tag, TLS_CREDENTIAL_PRIVATE_KEY_PSA);
                }
                return 0;
            }
        } else if (val == 0) {
            last_free_entry = i;
        }
    }

    if (add) {
        if (last_free_entry >= CONFIG_TLS_MAX_CREDENTIALS_NUMBER) {
            return -1;
        }
        ssl_context_sec_tags[last_free_entry].tag = tag;
        atomic_inc(&ssl_context_sec_tags[last_free_entry].refcount);
    }

    return 0;
}

static int ssl_context_update_local_tags(ssl_context_obj_t *self, sec_tag_t tag, bool add) {
    size_t last_free_entry = CONFIG_TLS_MAX_CREDENTIALS_NUMBER;

    for (size_t i = 0; i < CONFIG_TLS_MAX_CREDENTIALS_NUMBER; i++) {
        if (self->owned_tags[i] == tag) {
            if (!add) {
                self->owned_tags[i] = 0;
            }
            return 0;
        } else if (self->owned_tags[i] == 0) {
            last_free_entry = i;
        }
    }

    if (add) {
        if (last_free_entry >= CONFIG_TLS_MAX_CREDENTIALS_NUMBER) {
            return -1;
        }
        self->owned_tags[last_free_entry] = tag;
    }

    return 0;
}

static const char *ssl_context_protocol_get_str(const uint32_t protocol) {
    switch (protocol) {
    case MP_PROTOCOL_TLS_CLIENT:
        return STRINGIFY(MP_PROTOCOL_TLS_CLIENT);
    case MP_PROTOCOL_TLS_SERVER:
        return STRINGIFY(MP_PROTOCOL_TLS_SERVER);
    #if defined(CONFIG_NET_SOCKETS_ENABLE_DTLS)
    case MP_PROTOCOL_DTLS_CLIENT:
        return STRINGIFY(MP_PROTOCOL_DTLS_CLIENT);
    case MP_PROTOCOL_DTLS_SERVER:
        return STRINGIFY(MP_PROTOCOL_DTLS_SERVER);
    #endif
    }

    mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("%d is not a ssl context protocol"), protocol);
    return NULL;
}

/* Returns bytes */
static mp_obj_t ssl_context_file_or_bytes_get_data(mp_obj_t in) {
    if (mp_obj_is_str(in)) {
        mp_obj_t args[2] = {
            in,
            MP_OBJ_NEW_QSTR(MP_QSTR_rb),
        };
        mp_obj_t file = mp_call_function_n_kw(MP_OBJ_FROM_PTR(&mp_builtin_open_obj), 2, 0, args);
        mp_obj_t dest[2];
        mp_load_method(file, MP_QSTR_read, dest);
        mp_obj_t data = mp_call_method_n_kw(0, 0, dest);
        mp_stream_close(file);
        return data;
    }

    return in;
}

/* Methods -------------------------------------------------------------------------------------- */

static void ssl_context_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    ssl_context_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "<SSLContext %p protocol=%s>",
        self,
        ssl_context_protocol_get_str(self->protocol));
}

static mp_obj_t ssl_context_make_new(const mp_obj_type_t *type_in, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 1, 1, false);

    const uint32_t protocol = mp_obj_get_int(args[0]);
    (void)ssl_context_protocol_get_str(protocol);

    ssl_context_obj_t *self = mp_obj_malloc_with_finaliser(ssl_context_obj_t, type_in);
    self->protocol = protocol;
    self->ciphersuites = NULL;
    memset(self->owned_tags, 0, sizeof(self->owned_tags));

    return MP_OBJ_FROM_PTR(self);
}

/* Copy paste from modtls_mbedtls.c */
static mp_obj_t ssl_context_get_ciphers(mp_obj_t self_in) {
    mp_obj_t list = mp_obj_new_list(0, NULL);
    #ifndef NO_QSTR
    for (const int *cipher_list = mbedtls_ssl_list_ciphersuites(); *cipher_list; ++cipher_list) {
        const char *cipher_name = mbedtls_ssl_get_ciphersuite_name(*cipher_list);
        mp_obj_list_append(list, MP_OBJ_FROM_PTR(mp_obj_new_str_from_cstr(cipher_name)));
    }
    #endif
    return list;
}
static MP_DEFINE_CONST_FUN_OBJ_1(ssl_context_get_ciphers_obj, ssl_context_get_ciphers);

static mp_obj_t ssl_context_set_ciphers(mp_obj_t self_in, mp_obj_t ciphersuite) {
    ssl_context_obj_t *self = MP_OBJ_TO_PTR(self_in);

    // Check that ciphersuite is a list or tuple.
    size_t len = 0;
    mp_obj_t *ciphers;
    mp_obj_get_array(ciphersuite, &len, &ciphers);
    if (len == 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid cipher list"));
    }

    // Parse list of ciphers.
    self->ciphersuites = m_new(int, len + 1);
    for (size_t i = 0; i < len; ++i) {
        const char *ciphername = mp_obj_str_get_str(ciphers[i]);
        const int id = mbedtls_ssl_get_ciphersuite_id(ciphername);
        if (id == 0) {
            mp_raise_ValueError(MP_ERROR_TEXT("invalid cipher"));
        }
        self->ciphersuites[i] = id;
    }
    self->ciphersuites[len] = 0;

    /* Save list for socket creation */

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(ssl_context_set_ciphers_obj, ssl_context_set_ciphers);

static mp_obj_t ssl_context_load_cert_chain(mp_obj_t self_in, mp_obj_t cert, mp_obj_t key) {
    ssl_context_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_obj_t certdata = ssl_context_file_or_bytes_get_data(cert);
    mp_obj_t keydata = ssl_context_file_or_bytes_get_data(cert);
    mp_buffer_info_t certbuf, keybuf;
    int ret;
    sec_tag_t tag;

    mp_get_buffer_raise(certdata, &certbuf, MP_BUFFER_READ);
    mp_get_buffer_raise(keydata, &keybuf, MP_BUFFER_READ);

    do {
        tag = ssl_context_get_tag();
        ret = tls_credential_add(tag, TLS_CREDENTIAL_PUBLIC_CERTIFICATE, certbuf.buf, certbuf.len);
    } while (ret == -EEXIST);
    if (ret < 0) {
        if (ret == -EACCES) {
            mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("access denied"));
        } else if (ret == -ENOMEM) {
            mp_raise_type(&mp_type_MemoryError);
        } else {
            mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("invalid certificate data: %d"), ret);
        }
    }

    ret = tls_credential_add(tag, TLS_CREDENTIAL_PRIVATE_KEY, keybuf.buf, keybuf.len);
    if (ret < 0) {
        (void)tls_credential_delete(tag, TLS_CREDENTIAL_PUBLIC_CERTIFICATE);
        if (ret == -EACCES) {
            mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("access denied"));
        } else if (ret == -ENOMEM) {
            mp_raise_type(&mp_type_MemoryError);
        } else {
            mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("invalid key data: %d"), ret);
        }
    }

    ret = ssl_context_update_global_tags(tag, true);
    if (ret < 0) {
        (void)tls_credential_delete(tag, TLS_CREDENTIAL_PUBLIC_CERTIFICATE);
        (void)tls_credential_delete(tag, TLS_CREDENTIAL_PRIVATE_KEY);
        mp_raise_msg(&mp_type_MemoryError, MP_ERROR_TEXT("No free credential tag listing"));
    }
    ret = ssl_context_update_local_tags(self, tag, true);
    if (ret < 0) {
        ssl_context_update_global_tags(tag, false);
        mp_raise_msg(&mp_type_MemoryError, MP_ERROR_TEXT("No free credential tag in local listing"));
    }

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(ssl_context_load_cert_chain_obj, ssl_context_load_cert_chain);

static mp_obj_t ssl_context_load_verify_locations(mp_obj_t self_in, mp_obj_t cadata) {
    ssl_context_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_obj_t certdata = ssl_context_file_or_bytes_get_data(cadata);
    mp_buffer_info_t certbuf;
    int ret;
    sec_tag_t tag;

    mp_get_buffer_raise(certdata, &certbuf, MP_BUFFER_READ);

    do {
        tag = ssl_context_get_tag();
        ret = tls_credential_add(tag, TLS_CREDENTIAL_CA_CERTIFICATE, certbuf.buf, certbuf.len);
    } while (ret == -EEXIST);
    if (ret < 0) {
        if (ret == -EACCES) {
            mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("access denied"));
        } else if (ret == -ENOMEM) {
            mp_raise_type(&mp_type_MemoryError);
        } else {
            mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("invalid certificate data: %d"), ret);
        }
    }

    ret = ssl_context_update_global_tags(tag, true);
    if (ret < 0) {
        (void)tls_credential_delete(tag, TLS_CREDENTIAL_CA_CERTIFICATE);
        mp_raise_msg(&mp_type_MemoryError, MP_ERROR_TEXT("No free credential tag listing"));
    }
    ret = ssl_context_update_local_tags(self, tag, true);
    if (ret < 0) {
        ssl_context_update_global_tags(tag, false);
        mp_raise_msg(&mp_type_MemoryError, MP_ERROR_TEXT("No free credential tag in local listing"));
    }

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(ssl_context_load_verify_locations_obj, ssl_context_load_verify_locations);

static mp_obj_t ssl_context___del__(mp_obj_t self_in) {
    ssl_context_obj_t *self = MP_OBJ_TO_PTR(self_in);
    for (size_t i = 0; i < CONFIG_TLS_MAX_CREDENTIALS_NUMBER; i++) {
        if (self->owned_tags[i] != 0) {
            ssl_context_update_global_tags(self->owned_tags[i], false);
        }
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(ssl_context___del___obj, ssl_context___del__);

static const mp_rom_map_elem_t ssl_context_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&ssl_context___del___obj) },
    { MP_ROM_QSTR(MP_QSTR_get_ciphers), MP_ROM_PTR(&ssl_context_get_ciphers_obj)},
    { MP_ROM_QSTR(MP_QSTR_set_ciphers), MP_ROM_PTR(&ssl_context_set_ciphers_obj)},
    { MP_ROM_QSTR(MP_QSTR_load_cert_chain), MP_ROM_PTR(&ssl_context_load_cert_chain_obj)},
    { MP_ROM_QSTR(MP_QSTR_load_verify_locations), MP_ROM_PTR(&ssl_context_load_verify_locations_obj)},
    // { MP_ROM_QSTR(MP_QSTR_wrap_socket), MP_ROM_PTR(&ssl_context_wrap_socket_obj) },
};
static MP_DEFINE_CONST_DICT(ssl_context_locals_dict, ssl_context_locals_dict_table);

static MP_DEFINE_CONST_OBJ_TYPE(
    ssl_context_type,
    MP_QSTR_SSLContext,
    MP_TYPE_FLAG_NONE,
    make_new, ssl_context_make_new,
    print, ssl_context_print,
    locals_dict, &ssl_context_locals_dict
    );

static const mp_rom_map_elem_t mp_module_tls_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_tls) },

    // Classes.
    { MP_ROM_QSTR(MP_QSTR_SSLContext), MP_ROM_PTR(&ssl_context_type) },

    // Constants.
    //{ MP_ROM_QSTR(MP_QSTR_MBEDTLS_VERSION), MP_ROM_PTR(&mbedtls_version_obj)},
    { MP_ROM_QSTR(MP_QSTR_PROTOCOL_TLS_CLIENT), MP_ROM_INT(MP_PROTOCOL_TLS_CLIENT) },
    { MP_ROM_QSTR(MP_QSTR_PROTOCOL_TLS_SERVER), MP_ROM_INT(MP_PROTOCOL_TLS_SERVER) },
    #ifdef CONFIG_NET_SOCKETS_ENABLE_DTLS
    { MP_ROM_QSTR(MP_QSTR_PROTOCOL_DTLS_CLIENT), MP_ROM_INT(MP_PROTOCOL_DTLS_CLIENT) },
    { MP_ROM_QSTR(MP_QSTR_PROTOCOL_DTLS_SERVER), MP_ROM_INT(MP_PROTOCOL_DTLS_SERVER) },
    #endif
    // { MP_ROM_QSTR(MP_QSTR_CERT_NONE), MP_ROM_INT(MBEDTLS_SSL_VERIFY_NONE) },
    // { MP_ROM_QSTR(MP_QSTR_CERT_OPTIONAL), MP_ROM_INT(MBEDTLS_SSL_VERIFY_OPTIONAL) },
    // { MP_ROM_QSTR(MP_QSTR_CERT_REQUIRED), MP_ROM_INT(MBEDTLS_SSL_VERIFY_REQUIRED) },
};
static MP_DEFINE_CONST_DICT(mp_module_tls_globals, mp_module_tls_globals_table);

const mp_obj_module_t mp_module_tls = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mp_module_tls_globals,
};

MP_REGISTER_MODULE(MP_QSTR_tls, mp_module_tls);

#endif // MICROPY_PY_ZEPHYR_TLS
