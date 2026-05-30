#pragma once
#include <stdint.h>
#include <arpa/inet.h>
#include <stdbool.h>
/**
 * Converts a uint32_t IP address into a dotted-decimal string (A.B.C.D).
 * @param a_ip_addr The 32-bit IP address in network byte order (as stored in
 *                  struct in_addr / returned by network_convert_ip_p_to_n);
 * @param a_output_buffer Pointer to a user-provided buffer, NULL is not allowed!
 * @return Pointer to the resulting string.
 */
char *network_convert_ip_n_to_p(uint32_t a_ip_addr, char a_output_buffer[INET_ADDRSTRLEN]);

/**
 * @brief 
 * 
 * @param a_ip_addr 
 * @return uint32_t 
 */
uint32_t network_convert_ip_p_to_n(const char *a_ip_addr);

/**
 * @brief 
 * 
 * @param ip_str 
 * @return true 
 * @return false 
 */
bool is_valid_ip_address(const char* ip_str);


char* networkCopyString(const char* a_string);
