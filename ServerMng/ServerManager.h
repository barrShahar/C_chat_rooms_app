#ifndef __SERVER_MANAGER_H__
#define __SERVER_MANAGER_H__

/**
 * Threading contract
 *
 * - ServerManager_Start starts the underlying TcpServerController, which spawns
 *   acceptor and handler threads.  All network callbacks run on those worker
 *   threads, not on the thread that called Start.
 * - Create, Start, Stop, and Destroy must be called from a single controlling
 *   thread and must not be called concurrently with each other.
 * - Stop and Destroy are idempotent.
 */

typedef struct ServerManager ServerManager;

typedef enum ServerResult
{
    SERVER_RESULT_SUCCESS = 0,
    SERVER_RESULT_NULL_PTR,
    SERVER_RESULT_INVALID_ARGUMENT,
    SERVER_RESULT_ALLOCATION_FAILED,
    SERVER_RESULT_ALREADY_RUNNING,
    SERVER_RESULT_NOT_RUNNING,
    SERVER_RESULT_NETWORK_ERROR,
    SERVER_RESULT_INTERNAL_ERROR
} ServerResult;

/**
 * @brief Convert a ServerResult to a string representation
 *
 * @param a_result Instance of ServerResult
 * @return const char*
 */
const char* ServerResult_ToString(ServerResult a_result);

/**
 * @brief Create a server manager
 * @details Initializes the user manager, group manager, and TCP server
 *          controller.  Network configuration is read from config.h.
 *
 * @return A pointer to the created server manager
 * @retval NULL on allocation or initialization failure
 */
ServerManager* ServerManager_Create(void);

/**
 * @brief Destroy a server manager
 * @details Stops the server if running, then releases all owned resources.
 *          On completion *a_manager will be NULL.
 *
 * @param[in,out] a_manager Address of a previously created ServerManager pointer
 */
void ServerManager_Destroy(ServerManager** a_manager);

/**
 * @brief Start the server manager
 * @details Starts the TCP server controller, which begins accepting client
 *          connections.  Must be called after ServerManager_Create and before
 *          any client traffic is expected.
 *
 * @param a_manager A previously created ServerManager
 * @return SERVER_RESULT_SUCCESS on success or an error code on failure
 */
ServerResult ServerManager_Start(ServerManager* a_manager);

/**
 * @brief Stop the server manager
 * @details Stops accepting new connections and shuts down worker threads.
 *          Existing connections are closed.  Safe to call on an already-stopped
 *          manager.
 *
 * @param a_manager A previously created ServerManager
 */
void ServerManager_Stop(ServerManager* a_manager);

#endif /* __SERVER_MANAGER_H__ */
