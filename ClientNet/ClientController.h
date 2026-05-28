#ifndef __CLIENT_CONTROLLER_H__
#define __CLIENT_CONTROLLER_H__

/**
 * Threading contract
 *
 * - ClientController_Start spawns an IO worker thread that owns the socket.
 * - Callbacks registered via ClientController_SetCallbacks are invoked on
 *   that worker thread (not on the thread that called Start). Implementations
 *   must be thread-safe if they share state with the main thread.
 * - Call ClientController_SetCallbacks before ClientController_Start.
 *   SetCallbacks while the client is running returns CLIENT_RESULT_INVALID_ARGUMENT.
 * - Start, Stop, Destroy, SetCallbacks, and Send are serialized internally; they
 *   may be called from the main thread only (not concurrently with each other).
 * - Stop and Destroy are idempotent.
 */

#include <stddef.h>  /* size_t */
#include <stdint.h>  /* uint16_t, uint32_t */

typedef struct ClientController ClientController;

typedef enum ClientResult
{
    CLIENT_RESULT_SUCCESS = 0,
    CLIENT_RESULT_NULL_PTR,
    CLIENT_RESULT_INVALID_ARGUMENT,
    CLIENT_RESULT_ALLOCATION_FAILED,
    CLIENT_RESULT_SOCKET_ERROR,
    CLIENT_RESULT_CONNECT_ERROR,
    CLIENT_RESULT_NOT_CONNECTED,
    CLIENT_RESULT_RECEIVE_ERROR,
    CLIENT_RESULT_CONNECTION_CLOSED,
    CLIENT_RESULT_SEND_ERROR,
} ClientResult;

/**
 * @brief Convert a ClientResult to a string representation
 *
 * @param a_result Instance of ClientResult
 * @return const char*
 */
const char* ClientResult_ToString(ClientResult a_result);

/**
 * @brief Create a TCP client controller
 *
 * @param a_ip   : Server IP address to connect to (dotted-quad string)
 * @param a_port : Server port in host byte order
 * @return a pointer to the created controller
 * @retval NULL on failure due to allocation or initialization error
 */
ClientController* ClientController_Create(const char* a_ip, uint16_t a_port);

/**
 * @brief Destroy a TCP client controller
 * @details Stops the worker thread if running and releases all owned resources.
 *          On completion *a_controller will be NULL.
 *
 * @param[in] a_controller : Address of a previously created controller pointer
 */
void ClientController_Destroy(ClientController** a_controller);


/**
 * @brief Start the controller
 * @details Connects to the server and spawns the IO worker thread.
 *
 * @param a_controller : A previously created ClientController
 * @return CLIENT_RESULT_SUCCESS on success or an error code on failure
 */
ClientResult ClientController_Start(ClientController* a_controller);

/**
 * @brief Stop the controller
 * @details Signals the worker thread to exit and closes the socket.
 *
 * @param a_controller : A previously created ClientController
 * @return CLIENT_RESULT_SUCCESS on success or an error code on failure
 */
 ClientResult ClientController_Stop(ClientController* a_controller);

/**
 * @brief Send a message to the server
 * @details May be called from the main thread while the client is running.
 *
 * @param a_controller : A previously created ClientController
 * @param a_message    : Message buffer
 * @param a_length     : Length of the message in bytes
 * @return CLIENT_RESULT_SUCCESS on success or an error code on failure
 */
ClientResult ClientController_Send(ClientController* a_controller,
    const char* a_message, size_t a_length);

ClientResult ClientController_Receive(ClientController* a_controller, char* a_buffer, size_t a_length);

/**
 * @brief Get the configured server port
 *
 * @param a_controller : A previously created ClientController
 * @return the port in host byte order
 */
uint16_t ClientController_GetPort(ClientController* a_controller);

/**
 * @brief Get the configured server IP address
 *
 * @param a_controller : A previously created ClientController
 * @return the IP address in network byte order
 */
uint32_t ClientController_GetIp(ClientController* a_controller);

#endif /* __CLIENT_CONTROLLER_H__ */
