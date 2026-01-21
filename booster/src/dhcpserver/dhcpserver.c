/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2018-2019 Damien P. George
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

// For DHCP specs see:
//  https://www.ietf.org/rfc/rfc2131.txt
//  https://tools.ietf.org/html/rfc2132 -- DHCP Options and BOOTP Vendor Extensions

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "cyw43_config.h"
#include "dhcpserver.h"
#include "lwip/udp.h"

static dhcp_server_t dhcp_server_data = {0};

static dhcp_server_cb_t dhcp_server_cb = NULL;

static void print_dhcp_server_data(dhcp_server_t *d)
{
    // Display the content of dhcp_server_t
    DPRINTF("dhcp_server_t: ip=%u.%u.%u.%u nm=%u.%u.%u.%u\n",
            ip4_addr1(&d->ip), ip4_addr2(&d->ip), ip4_addr3(&d->ip), ip4_addr4(&d->ip),
            ip4_addr1(&d->nm), ip4_addr2(&d->nm), ip4_addr3(&d->nm), ip4_addr4(&d->nm));
    // Show the leases
    for (int i = 0; i < DHCPS_MAX_IP; ++i)
    {
        DPRINTF("lease[%d]: mac=%02x:%02x:%02x:%02x:%02x:%02x expiry=%u\n",
                i, d->lease[i].mac[0], d->lease[i].mac[1], d->lease[i].mac[2], d->lease[i].mac[3], d->lease[i].mac[4], d->lease[i].mac[5],
                d->lease[i].expiry);
    }
}


static int dhcp_socket_new_dgram(struct udp_pcb **udp, void *cb_data, udp_recv_fn cb_udp_recv) {
    // family is AF_INET
    // type is SOCK_DGRAM

    if (udp == NULL || cb_udp_recv == NULL) {
        return -EINVAL; // Invalid argument
    }

    *udp = udp_new();
    if (*udp == NULL) {
        return -ENOMEM;
    }

    // Register callback
    udp_recv(*udp, cb_udp_recv, (void *)cb_data);

    return 0; // success
}

static void dhcp_socket_free(struct udp_pcb **udp) {
    if (*udp != NULL) {
        udp_remove(*udp);
        *udp = NULL;
    }
}

static int dhcp_socket_bind(struct udp_pcb **udp, uint16_t port) {
    // TODO convert lwIP errors to errno
    return udp_bind(*udp, IP_ADDR_ANY, port);
}

static int dhcp_socket_sendto(struct udp_pcb **udp, struct netif *nif, const void *buf, size_t len, uint32_t ip, uint16_t port) {
    if (len > 0xffff) {
        len = 0xffff;
    }

    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, len, PBUF_RAM);
    if (p == NULL) {
        return -ENOMEM;
    }

    memcpy(p->payload, buf, len);

    ip_addr_t dest;
    IP4_ADDR(ip_2_ip4(&dest), ip >> 24 & 0xff, ip >> 16 & 0xff, ip >> 8 & 0xff, ip & 0xff);
    err_t err;
    if (nif != NULL) {
        err = udp_sendto_if(*udp, p, &dest, port, nif);
    } else {
        err = udp_sendto(*udp, p, &dest, port);
    }

    pbuf_free(p);

    if (err != ERR_OK) {
        return err;
    }

    return len;
}

static uint8_t *opt_find(uint8_t *opt, uint8_t cmd) {
    for (int i = 0; i < 308 && opt[i] != DHCP_OPT_END;) {
        if (opt[i] == cmd) {
            return &opt[i];
        }
        i += 2 + opt[i + 1];
    }
    return NULL;
}

static void opt_write_n(uint8_t **opt, uint8_t cmd, size_t n, const void *data) {
    uint8_t *o = *opt;
    *o++ = cmd;
    *o++ = n;
    memcpy(o, data, n);
    *opt = o + n;
}

static void opt_write_u8(uint8_t **opt, uint8_t cmd, uint8_t val) {
    uint8_t *o = *opt;
    *o++ = cmd;
    *o++ = 1;
    *o++ = val;
    *opt = o;
}

static void opt_write_u32(uint8_t **opt, uint8_t cmd, uint32_t val) {
    uint8_t *o = *opt;
    *o++ = cmd;
    *o++ = 4;
    *o++ = val >> 24;
    *o++ = val >> 16;
    *o++ = val >> 8;
    *o++ = val;
    *opt = o;
}

static void dhcp_server_process(void *arg, struct udp_pcb *upcb, struct pbuf *p, const ip_addr_t *src_addr, u16_t src_port) {
    // Ignore arg
    dhcp_server_t *d = arg;
    (void)upcb;
    (void)src_addr;
    (void)src_port;


    // This is around 548 bytes
    dhcp_msg_t dhcp_msg;

    #define DHCP_MIN_SIZE (240 + 3)
    if (p->tot_len < DHCP_MIN_SIZE) {
        goto ignore_request;
    }

    size_t len = pbuf_copy_partial(p, &dhcp_msg, sizeof(dhcp_msg), 0);
    if (len < DHCP_MIN_SIZE) {
        goto ignore_request;
    }

    dhcp_msg.op = DHCPOFFER;
    memcpy(&dhcp_msg.yiaddr, &ip4_addr_get_u32(ip_2_ip4(&d->ip)), 4);

    uint8_t *opt = (uint8_t *)&dhcp_msg.options;
    opt += 4; // assume magic cookie: 99, 130, 83, 99

    uint8_t *msgtype = opt_find(opt, DHCP_OPT_MSG_TYPE);
    if (msgtype == NULL) {
        // A DHCP package without MSG_TYPE?
        goto ignore_request;
    }

    switch (msgtype[2]) {
        case DHCPDISCOVER: {
            DPRINTF("DHCP DISCOVER[1]\n");
            int yi = DHCPS_MAX_IP;
            for (int i = 0; i < DHCPS_MAX_IP; ++i) {
                if (memcmp(d->lease[i].mac, dhcp_msg.chaddr, MAC_LEN) == 0) {
                    // MAC match, use this IP address
                    DPRINTF("MAC match, use this IP address\n");
                    yi = i;
                    break;
                }
                if (yi == DHCPS_MAX_IP) {
                    // Look for a free IP address
                    if (memcmp(d->lease[i].mac, "\x00\x00\x00\x00\x00\x00", MAC_LEN) == 0) {
                        // IP available
                        yi = i;
                        DPRINTF("IP available %d\n", yi);
                    }
                    uint32_t expiry = d->lease[i].expiry << 16 | 0xffff;
                    if ((int32_t)(expiry - cyw43_hal_ticks_ms()) < 0) {
                        // IP expired, reuse it
                        memset(d->lease[i].mac, 0, MAC_LEN);
                        yi = i;
                        DPRINTF("IP expired, reuse it %d\n", yi);
                    }
                }
            }
            if (yi == DHCPS_MAX_IP) {
                // No more IP addresses left
                DPRINTF("No more IP addresses left\n");
                goto ignore_request;
            }
            DPRINTF("Send DHCPOFFER\n");
            dhcp_msg.yiaddr[3] = DHCPS_BASE_IP + yi;
            opt_write_u8(&opt, DHCP_OPT_MSG_TYPE, DHCPOFFER);
            break;
        }

        case DHCPREQUEST: {
            DPRINTF("DHCP DISCOVER[3]\n");
            uint8_t *o = opt_find(opt, DHCP_OPT_REQUESTED_IP);
            if (o == NULL) {
                // Should be NACK
                DPRINTF("DHCP REQUEST: no requested IP\n");
                goto ignore_request;
            }
            if (memcmp(o + 2, &ip4_addr_get_u32(ip_2_ip4(&d->ip)), 3) != 0) {
                // Should be NACK
                DPRINTF("DHCP REQUEST: requested IP does not match: %u.%u.%u.%u\n",
                    o[2], o[3], o[4], o[5]);
                goto ignore_request;
            }
            uint8_t yi = o[5] - DHCPS_BASE_IP;
            if (yi >= DHCPS_MAX_IP) {
                // Should be NACK
                DPRINTF("DHCP REQUEST: requested IP out of range: %u\n", yi);
                goto ignore_request;
            }
            if (memcmp(d->lease[yi].mac, dhcp_msg.chaddr, MAC_LEN) == 0) {
                // MAC match, ok to use this IP address
                DPRINTF("MAC match, ok to use this IP address\n");
            } else if (memcmp(d->lease[yi].mac, "\x00\x00\x00\x00\x00\x00", MAC_LEN) == 0) {
                // IP unused, ok to use this IP address
                memcpy(d->lease[yi].mac, dhcp_msg.chaddr, MAC_LEN);
                DPRINTF("IP unused, ok to use this IP address\n");
            } else {
                // IP already in use
                // Should be NACK
                DPRINTF("IP already in use\n");
                goto ignore_request;
            }
            d->lease[yi].expiry = (cyw43_hal_ticks_ms() + DEFAULT_LEASE_TIME_S * 1000) >> 16;
            dhcp_msg.yiaddr[3] = DHCPS_BASE_IP + yi;
            opt_write_u8(&opt, DHCP_OPT_MSG_TYPE, DHCPACK);
            DPRINTF("DHCPS: client connected: MAC=%02x:%02x:%02x:%02x:%02x:%02x IP=%u.%u.%u.%u\n",
                dhcp_msg.chaddr[0], dhcp_msg.chaddr[1], dhcp_msg.chaddr[2], dhcp_msg.chaddr[3], dhcp_msg.chaddr[4], dhcp_msg.chaddr[5],
                dhcp_msg.yiaddr[0], dhcp_msg.yiaddr[1], dhcp_msg.yiaddr[2], dhcp_msg.yiaddr[3]);
            break;
        }

        default:
            goto ignore_request;
    }

    opt_write_n(&opt, DHCP_OPT_SERVER_ID, 4, &ip4_addr_get_u32(ip_2_ip4(&d->ip)));
    DPRINTF("DHCP_OPT_SERVER_ID: %u.%u.%u.%u\n", opt[-4], opt[-3], opt[-2], opt[-1]);
    opt_write_n(&opt, DHCP_OPT_SUBNET_MASK, 4, &ip4_addr_get_u32(ip_2_ip4(&d->nm)));
    DPRINTF("DHCP_OPT_SUBNET_MASK: %u.%u.%u.%u\n", opt[-4], opt[-3], opt[-2], opt[-1]);
    opt_write_n(&opt, DHCP_OPT_ROUTER, 4, &ip4_addr_get_u32(ip_2_ip4(&d->ip))); // aka gateway; can have multiple addresses
    DPRINTF("DHCP_OPT_ROUTER: %u.%u.%u.%u\n", opt[-4], opt[-3], opt[-2], opt[-1]);
    opt_write_n(&opt, DHCP_OPT_DNS, 4, &ip4_addr_get_u32(ip_2_ip4(&d->ip))); // this server is the dns
    DPRINTF("DHCP_OPT_DNS: %u.%u.%u.%u\n", opt[-4], opt[-3], opt[-2], opt[-1]);
    opt_write_u32(&opt, DHCP_OPT_IP_LEASE_TIME, DEFAULT_LEASE_TIME_S);
    DPRINTF("DHCP_OPT_IP_LEASE_TIME: %u\n", opt[-4] << 24 | opt[-3] << 16 | opt[-2] << 8 | opt[-1]);

    // Define your local domain name
    const char *local_domain = "local";

    // Write Domain Name (Option 15)
    size_t domain_len = strlen(local_domain);
    *opt++ = DHCP_OPT_DOMAIN_NAME; // Option 15
    *opt++ = (uint8_t)domain_len;  // Length of the domain name
    memcpy(opt, local_domain, domain_len);
    opt += domain_len;
    DPRINTF("DHCP_OPT_DOMAIN_NAME: %s\n", local_domain);

    // Define your domain search list
    const char *domain_search_list[] = {"local"};
    size_t num_domains = sizeof(domain_search_list) / sizeof(domain_search_list[0]);

    // Initialize the option buffer
    *opt++ = DHCP_OPT_DOMAIN_SEARCH; // Option 119

    // Placeholder for total length
    uint8_t *len_ptr = opt++;
    size_t total_len = 0;

    // Encode each domain in the search list
    for (size_t i = 0; i < num_domains; i++) {
        const char *domain = domain_search_list[i];
        const char *label = domain;
        while (*label) {
            const char *next_dot = strchr(label, '.');
            size_t label_len = next_dot ? (size_t)(next_dot - label) : strlen(label);
            *opt++ = (uint8_t)label_len; // Length byte
            memcpy(opt, label, label_len);
            opt += label_len;
            total_len += label_len + 1;
            label += label_len;
            if (*label == '.') {
                label++;
            }
        }
        *opt++ = 0x00; // Null terminator for the domain
        total_len += 1;
        DPRINTF("DHCP_OPT_DOMAIN_SEARCH: %s\n", domain_search_list[i]);
    }

    // Set the total length
    *len_ptr = (uint8_t)total_len;

    // Define the captive portal URI
    char ip_str[IP4ADDR_STRLEN_MAX];
    if (ip4addr_ntoa_r(&d->ip, ip_str, sizeof(ip_str)) == NULL) {
        // Handle error: conversion failed
        DPRINTF("Failed to convert IP address to string.\n");
        return;
    }
    const char *scheme = "https://";
    size_t uri_len = strlen(scheme) + strlen(ip_str) + 1; // +1 for null terminator
    char *captive_portal_uri = malloc(uri_len);
    if (captive_portal_uri == NULL) {
        DPRINTF("Memory allocation failed.\n");
        return;
    }

    // Construct the full URI
    snprintf(captive_portal_uri, uri_len, "%s%s", scheme, ip_str);
    uint8_t captive_portal_option[2 + sizeof(captive_portal_uri) - 1];

    // Set the option code (114) and length
    captive_portal_option[0] = DHCP_CAPTIVE_PORTAL_URL; // Option code for Captive Portal
    captive_portal_option[1] = sizeof(captive_portal_uri) - 1; // Length of the URI string

    // Copy the URI into the option data
    memcpy(&captive_portal_option[2], captive_portal_uri, sizeof(captive_portal_uri) - 1);

    // Append the captive portal option to the DHCP options
    opt_write_n(&opt, captive_portal_option[0], captive_portal_option[1], &captive_portal_option[2]);
    DPRINTF("DHCP Captive Portal Option: %s\n", captive_portal_uri);

    *opt++ = DHCP_OPT_END;
    struct netif *nif = ip_current_input_netif();
    DPRINTF("Current ip address: %s\n", ip4addr_ntoa(ip_2_ip4(&nif->ip_addr)));
    dhcp_socket_sendto(&d->udp, nif, &dhcp_msg, opt - (uint8_t *)&dhcp_msg, 0xffffffff, PORT_DHCP_CLIENT);
    DPRINTF("DHCPS: sent DHCP message type %d\n", dhcp_msg.options[2]);

    if (dhcp_server_cb != NULL) {
        dhcp_server_cb(d, &dhcp_msg, nif);
    }

ignore_request:
    pbuf_free(p);
}

void dhcp_server_init(ip_addr_t *ip, ip_addr_t *nm) {
    ip_addr_copy(dhcp_server_data.ip, *ip);
    ip_addr_copy(dhcp_server_data.nm, *nm);
    memset(dhcp_server_data.lease, 0, sizeof(dhcp_server_data.lease));

    if (dhcp_socket_new_dgram(&dhcp_server_data.udp, &dhcp_server_data, dhcp_server_process) != 0) {
        return;
    }
    dhcp_socket_bind(&dhcp_server_data.udp, PORT_DHCP_SERVER);
}

void dhcp_server_deinit() {
    dhcp_socket_free(&dhcp_server_data.udp);
}

void set_dhcp_server_cb(dhcp_server_cb_t cb) {
    dhcp_server_cb = cb;
}