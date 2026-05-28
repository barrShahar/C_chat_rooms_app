#ifndef NETWORK_PROTOCOL_H
#define NETWORK_PROTOCOL_H

#include <inttypes.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "logger.h"
#include "config.h"
/*
 * Wire format: [Opcode: char][Length: uint16_t LE][Value: Length bytes]
 *
 * Value encoding per opcode (all strings are null-terminated):
 *   OPCODE_REGISTER, OPCODE_LOGIN                    -> "name\0password\0"
 *   OPCODE_CREATE_GROUP, OPCODE_JOIN_GROUP/LEAVE_GROUP -> "group_name\0"
 *   OPCODE_LOGOUT, OPCODE_EXIT                       -> (empty, Length = 0)
 *   OPCODE_RESPONSE                                  -> status_byte + "message\0"
 */

#define CHAT_HEADER_SIZE  4   /* 1 (type) + 2 (length) + 1 (status) */
#define CHAT_MAX_VALUE   256  /* maximum value payload in bytes */
#define CHAT_MAX_TOTAL   (CHAT_HEADER_SIZE + CHAT_MAX_VALUE)

typedef enum {
    OPCODE_REGISTER     = 0x01,
    OPCODE_LOGIN        = 0x02,
    OPCODE_LOGOUT       = 0x03,
    OPCODE_EXIT         = 0x04,
    OPCODE_CREATE_GROUP = 0x05,
    OPCODE_JOIN_GROUP   = 0x06,
    OPCODE_LEAVE_GROUP  = 0x07,

    OPCODE_RESPONSE     = 0x81,
} MessageOpcode;

typedef enum {
    CHAT_OK              = 0,
    CHAT_ERR_GENERIC     = 1,
    CHAT_ERR_BAD_CREDS   = 2,
    CHAT_ERR_NAME_TAKEN  = 3,
    CHAT_ERR_NOT_FOUND   = 4,
    CHAT_ERR_ALREADY_IN  = 5,
    CHAT_ERR_NOT_IN      = 6,
    CHAT_ERR_NOT_LOGGED  = 7,
    CHAT_ERR_ALREADY_LOG = 8,
    CHAT_ERR_MALFORMED   = 9,
} ChatStatus;

typedef struct {
    MessageOpcode m_opcode;
    uint16_t      m_length;
    ChatStatus    m_status;
    char       m_value[CHAT_MAX_VALUE];
} ChatMessage;

static inline MessageOpcode Chat_GetOpcode(const char* buf);
static inline uint16_t Chat_GetLength(const char* buf);
static inline const char* Chat_GetValue(const char* buf);
static inline ChatStatus DeserializeChatMessage(const char* serialized_chat_message_buf, size_t serialized_chat_message_buf_size, ChatMessage* out_deserialized_message);
static inline int SerializeChatMessage(const ChatMessage* a_messageToSerialize, char* bufTarget, size_t buf_size);
static inline ChatStatus Chat_GetStatus(const char* buf);
static inline ChatStatus Chat_Validate(size_t buf_size, size_t value_len);

/* -------------------------------------------------------------------------
 * Encode helpers — write a complete TLV message into buf (caller-supplied).
 * Return the total number of bytes written, or -1 if buf is too small.
 * ------------------------------------------------------------------------- */

static inline ChatStatus DeserializeChatMessage(const char* serialized_chat_message_buf, size_t serialized_chat_message_buf_size, ChatMessage* out_deserialized_message)
{
    if (serialized_chat_message_buf == NULL || out_deserialized_message == NULL) return false;
    if (serialized_chat_message_buf_size < CHAT_HEADER_SIZE) return false;

    MessageOpcode opcode    = Chat_GetOpcode(serialized_chat_message_buf);
    uint16_t      value_len = Chat_GetLength((const char *)serialized_chat_message_buf);
    ChatStatus    status    = Chat_GetStatus(serialized_chat_message_buf);

    // Safety gaurd: if the serialized_chat_message_buf_size is too small, return false
    ChatStatus validate_status = Chat_Validate(serialized_chat_message_buf_size, value_len);
    if (validate_status != CHAT_OK) return validate_status; // loged from Chat_Validate
    
    out_deserialized_message->m_opcode = opcode;
    out_deserialized_message->m_status = status;
    out_deserialized_message->m_length = value_len;
    memcpy(out_deserialized_message->m_value, serialized_chat_message_buf + CHAT_HEADER_SIZE, value_len);
    

    LOG_DEBUG("DeserializeChatMessage: deserialized message: %s", out_deserialized_message->m_value);
    return true;
}

/* Returns total bytes written, or -1 if buf is too small. */
static inline int SerializeChatMessage(const ChatMessage* a_messageToSerialize, char* bufTarget, size_t buf_size)
{
    if (a_messageToSerialize == NULL || bufTarget == NULL)
    {
        LOG_ERROR("SerializeChatMessage: a_messageToSerialize or bufTarget is NULL");
        return -1;
    }
  
    size_t total = (size_t)(CHAT_HEADER_SIZE + a_messageToSerialize->m_length);
    if (buf_size < total)
    {
        LOG_ERROR("SerializeChatMessage: buf_size is too small");
        return -1;
    }
    bufTarget[0] = (char)a_messageToSerialize->m_opcode;
    bufTarget[1] = (char)(a_messageToSerialize->m_length & 0xFF);
    bufTarget[2] = (char)(a_messageToSerialize->m_length >> 8);
    bufTarget[3] = (char)a_messageToSerialize->m_status;
    memcpy(bufTarget + CHAT_HEADER_SIZE, a_messageToSerialize->m_value, a_messageToSerialize->m_length);
    return (int)total;

}

/* -------------------------------------------------------------------------
 * Decode helpers — read header fields from a raw buffer.
 * ------------------------------------------------------------------------- */

static inline MessageOpcode Chat_GetOpcode(const char* buf)
{
    return (MessageOpcode)buf[0];
}

static inline uint16_t Chat_GetLength(const char* buf)
{
    return (uint16_t)(buf[1] | (buf[2] << 8));
}

static inline const char* Chat_GetValue(const char* buf)
{
    return buf + CHAT_HEADER_SIZE;
}

static inline ChatStatus Chat_GetStatus(const char* buf)
{
    return (ChatStatus)buf[3];
}

/* Returns 0 if the header is valid and the full message is present in len bytes. */
static inline ChatStatus Chat_Validate(size_t buf_size, size_t value_len)
{
    if (value_len > CHAT_MAX_VALUE)
    {
        LOG_ERROR("Chat_Validate: value_len is too large");
        return CHAT_ERR_MALFORMED;
    }

    if (buf_size < (size_t)(CHAT_HEADER_SIZE + value_len))
    {
        LOG_ERROR("Chat_Validate: buf_size is too small");
        return CHAT_ERR_MALFORMED;
    }
    return CHAT_OK;
}

#endif /* NETWORK_PROTOCOL_H */
