#pragma once
#include <stdint.h>
#include <arpa/inet.h>

/**
 * Converts a uint32_t IP address into a dotted-decimal string (A.B.C.D).
 * @param a_ip_addr The 32-bit IP address in host byte order (big-endian); 
 * @param a_output_buffer Pointer to a user-provided buffer, NULL is not allowed!
 * @return Pointer to the resulting string.
 */
char *network_convert_ip_n_to_p(uint32_t a_ip_addr, char a_output_buffer[INET_ADDRSTRLEN]);

uint32_t network_convert_ip_p_to_n(const char *a_ip_addr);