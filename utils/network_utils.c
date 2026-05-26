#include "network_utils.h"
#include <arpa/inet.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>

#define IP_OCTET(ip, n)         (((ip) >> ((n) * 8)) & 0xFF)
#define APPEND_PART(ip, byte)   (((ip) << 8) | (uint8_t)(byte))
#define IS_VALID_PART(v)        ((v) <= 255)
#define IPV4_PART_COUNT         (sizeof(uint32_t))

char *network_convert_ip_n_to_p(uint32_t a_ip_addr, char a_output_buffer[INET_ADDRSTRLEN])
{

    assert(a_output_buffer != NULL);

    if (inet_ntop(AF_INET, &a_ip_addr, a_output_buffer, INET_ADDRSTRLEN) == NULL)
    {
        a_output_buffer[0] = '\0';
        return NULL;
    }


    return a_output_buffer;;
}

uint32_t network_convert_ip_p_to_n(const char *a_ip_addr)
{
    if (a_ip_addr == NULL) { return 0; }

    uint32_t binary_prefix = 0;
    inet_pton(AF_INET, a_ip_addr, &binary_prefix);
    binary_prefix = htonl(binary_prefix);
    return binary_prefix;
}

