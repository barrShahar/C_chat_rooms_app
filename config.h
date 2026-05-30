#define CONF_RECV_BUF_SIZE 4096
#define CONF_SERVER_IP "127.0.0.1"
#define CONF_SERVER_PORT 8080
#define CONF_SERVER_NAME "Chat rooms server"

// GroupManager
#define CONF_GROUP_MANAGER_HASH_MAP_SIZE 10
#define CONF_GROUP_MANAGER_MAX_NAME_LENGTH 32

// UserManager
#define CONF_USER_MANAGER_HASH_MAP_SIZE 10
#define CONF_USER_VECTOR_INITIAL_CAPACITY 10
#define CONF_USER_VECTOR_BLOCK_SIZE 2
#define CONF_USER_GROUPS_LINE_BUF_SIZE 256

/* Multicast endpoint allocation for groups.
 * IPs are taken from the administratively scoped block 239.0.0.0/8.
 * Ports are allocated sequentially in [BASE, BASE + MAX_GROUPS).
 */
#define CONF_MULTICAST_BASE_IP   "239.0.0.1"
#define CONF_MULTICAST_PORT_BASE 5000
#define CONF_MULTICAST_MAX_GROUPS 1000

/* Buffer size for a formatted "ip:port" endpoint string, NUL included.
 * Derived from INET_ADDRSTRLEN (16: dotted-quad + NUL) + ':' + "65535" = 22. */
#define CONF_MULTICAST_ENDPOINT_STR_MAX 22
