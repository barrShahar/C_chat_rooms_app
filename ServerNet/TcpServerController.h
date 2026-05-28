#ifndef __TCP_SERVER_CONTROLLER_H__
#define __TCP_SERVER_CONTROLLER_H__

/**
 * Threading contract
 *
 * - TcpServerController_Start spawns an acceptor thread and a handler thread.
 * - Callbacks registered via TcpServerController_SetCallbacks are invoked on
 *   those worker threads (not on the thread that called Start). Implementations
 *   must be thread-safe if they share state with the main thread or with each other.
 * - Call TcpServerController_SetCallbacks before TcpServerController_Start.
 *   SetCallbacks while the server is running returns TCP_RESULT_INVALID_ARGUMENT.
 * - Start, Stop, Destroy, and SetCallbacks are serialized internally; they may be
 *   called from the main thread only (not concurrently with each other).
 * - Stop and Destroy are idempotent.
 */

#include <stddef.h>  /* size_t */
#include <stdint.h>  /* uint16_t, uint32_t */

#include "TcpConnectionRecord.h"

typedef struct TcpServerController TcpServerController;

typedef enum TcpResult
{
    TCP_RESULT_SUCCESS = 0,
    TCP_RESULT_NULL_PTR,
    TCP_RESULT_INVALID_ARGUMENT,
    TCP_RESULT_ALLOCATION_FAILED,
    TCP_RESULT_SOCKET_ERROR,
    TCP_RESULT_BIND_ERROR,
    TCP_RESULT_LISTEN_ERROR,
    TCP_RESULT_ACCEPT_ERROR,
    TCP_RESULT_CONNECTION_CLOSED,
    TCP_RESULT_THREAD_CREATION_FAILED
} TcpResult;

/**
 * @brief Convert a TcpResult to a string representation
 * 
 * @param a_result Instance of tcpResult
 * @return const char* 
 */
const char* 
TcpResult_ToString(TcpResult a_result);

/**
 * @brief Create a TCP server controller
 *
 * @params a_name : Human-readable name of the controller
 * @params a_ip   : IP address to bind to (dotted-quad string)
 * @params a_port : Port to listen on
 * @return a pointer to the created controller
 * @retval NULL on failure due to allocation or initialization error
 */
TcpServerController* 
TcpServerController_Create(const char* a_name, const char* a_ip, const uint16_t a_port);

/**
 * @brief Destroy a TCP server controller
 * @details Releases the acceptor, handler, and all owned resources.
 *          On completion *a_controller will be NULL.
 *
 * @params[in] a_controller : Address of a previously created controller pointer
 */
void 
TcpServerController_Destroy(TcpServerController** a_controller);

/**
 * @brief Register the callbacks invoked by the controller
 * @details Each callback may be NULL to disable that notification.
 *          Must be called before Start. Callbacks run on worker threads; see
 *          threading contract at the top of this header.
 *
 * @params a_controller              : A previously created TcpServerController
 * @params a_context                 : Opaque pointer passed as the first argument to every callback
 * @params a_callbackNewConnection   : Called on new client connection
 * @params a_callbackDisconnect      : Called on client disconnect
 * @params a_callbackMessageReceived : Called on incoming message
 * @return TCP_RESULT_SUCCESS on success, TCP_RESULT_INVALID_ARGUMENT if the
 *         server is already running, or another error code on failure
 */
TcpResult
TcpServerController_SetCallbacks(TcpServerController* a_controller,
    void* a_context,
    void (*a_callbackNewConnection)(void* a_context, const TcpConnectionRecord* a_record),
    void (*a_callbackDisconnect)(void* a_context, const TcpConnectionRecord* a_record),
    void (*a_callbackMessageReceived)(void* a_context, const TcpConnectionRecord* a_record, const char* a_message, size_t a_length));

/**
 * @brief Start the controller
 * @details Spawns the acceptor and connection-handler threads.
 *
 * @params a_controller : A previously created TcpServerController
 * @return TCP_RESULT_SUCCESS on success or an error code on failure
 */
TcpResult 
TcpServerController_Start(TcpServerController* a_controller);

/**
 * @brief Stop the controller
 *
 * @params a_controller : A previously created TcpServerController
 */
void 
TcpServerController_Stop(TcpServerController* a_controller);

/**
 * @brief Process a newly accepted connection
 *
 * @params a_controller : A previously created TcpServerController
 * @params a_record     : Connection record (caller must create via TcpConnectionRecord_Create)
 *
 * On success, ownership of @a_record transfers to the connection handler.
 * On failure, ownership remains with the caller, which must destroy @a_record.
 *
 * @return TCP_RESULT_SUCCESS on success or an error code on failure
 */
TcpResult 
TcpServerController_ProcessConnection(TcpServerController* a_controller,
    TcpConnectionRecord* a_record);

/**
 * @brief Fire the new-connection callback for a registered connection
 * @details Invoked by the connection handler on its worker thread once the
 *          record has been registered, so the new-connection callback runs on
 *          the same thread as the message/disconnect callbacks and the record
 *          is guaranteed to be alive. Must not be called from the acceptor
 *          thread (the record may already be owned/freed by the handler).
 *
 * @params a_controller : A previously created TcpServerController
 * @params a_record     : Connection record of the newly registered client
 */
void 
TcpServerController_NotifyNewConnection(TcpServerController* a_controller,
    const TcpConnectionRecord* a_record);

/**
 * @brief Process a client disconnection
 *
 * @params a_controller : A previously created TcpServerController
 * @params a_record     : Connection record of the disconnected client
 * @return TCP_RESULT_SUCCESS on success or an error code on failure
 */
TcpResult 
TcpServerController_ProcessDisconnect(TcpServerController* a_controller, TcpConnectionRecord* a_record);

/**
 * @brief Process an incoming message from a client
 *
 * @params a_controller : A previously created TcpServerController
 * @params a_record     : Connection record of the sending client
 * @params a_message    : Message buffer
 * @params a_length     : Length of the message in bytes
 * @return TCP_RESULT_SUCCESS on success or an error code on failure
 */
TcpResult 
TcpServerController_ProcessMessage(TcpServerController* a_controller, const TcpConnectionRecord* a_record, const char* a_message, size_t a_length);

/**
 * @brief Send a message to a client
 *
 * @params a_controller : A previously created TcpServerController
 * @params a_fd         : File descriptor of the client
 * @params a_message    : Message buffer
 * @params a_length     : Length of the message in bytes
 * @return TCP_RESULT_SUCCESS on success or an error code on failure
 */
TcpResult
TcpServerController_SendMessage(TcpServerController* a_controller,
    const int a_fd,
    const char* a_message, 
    size_t a_length);

/**
 * @brief Get the controller's listening port
 *
 * @params a_controller : A previously created TcpServerController
 * @return the port in host byte order
 */
uint16_t 
TcpServerController_GetPort(TcpServerController* a_controller);

/**
 * @brief Get the controller's bound IP address
 *
 * @params a_controller : A previously created TcpServerController
 * @return the IP address in network byte order
 */
uint32_t 
TcpServerController_GetIp(TcpServerController* a_controller);


#endif /* __TCP_SERVER_CONTROLLER_H__ */
