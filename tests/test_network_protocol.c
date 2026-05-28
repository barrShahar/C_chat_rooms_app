#include <stdio.h>
#include <string.h>
#include "../NetworkProtocol.h"

static int s_failures = 0;

#define ASSERT(cond, name) \
    do { \
        if (cond) { fprintf(stream, "[PASS] %s\n", (name)); } \
        else      { fprintf(stream, "[FAIL] %s\n", (name)); s_failures++; } \
    } while(0)

void
PrintChatMessage(FILE* a_stream, const ChatMessage* a_message)
{
    fprintf(a_stream, "ChatMessage: opcode=%d, length=%d, status=%d, value=%s\n",
            a_message->m_opcode, a_message->m_length, a_message->m_status, a_message->m_value);
}

static void TestSerialize(FILE* stream)
{
    fprintf(stream, "--- Serialize ---\n");
    char buf[CHAT_MAX_TOTAL];

    ChatMessage msg;
    msg.m_opcode = OPCODE_REGISTER;
    msg.m_length = 5;
    msg.m_status = CHAT_OK;
    memcpy(msg.m_value, "hello", 5);

    int n = SerializeChatMessage(&msg, buf, sizeof(buf));
    ASSERT(n == CHAT_HEADER_SIZE + 5,          "serialize: total bytes written");
    ASSERT((uint8_t)buf[0] == OPCODE_REGISTER, "serialize: opcode byte");
    ASSERT((uint8_t)buf[1] == 5,               "serialize: length lo byte");
    ASSERT((uint8_t)buf[2] == 0,               "serialize: length hi byte");
    ASSERT((uint8_t)buf[3] == CHAT_OK,         "serialize: status byte");
    ASSERT(memcmp(buf + CHAT_HEADER_SIZE, "hello", 5) == 0, "serialize: value bytes");

    ASSERT(SerializeChatMessage(NULL, buf, sizeof(buf)) == -1, "serialize: NULL msg");
    ASSERT(SerializeChatMessage(&msg, NULL, sizeof(buf)) == -1, "serialize: NULL buf");
    ASSERT(SerializeChatMessage(&msg, buf, 2) == -1,           "serialize: buf too small");
}

static void TestDeserialize(FILE* stream)
{
    fprintf(stream, "--- Deserialize ---\n");
    char buf[CHAT_MAX_TOTAL];
    buf[0] = (char)OPCODE_LOGIN;
    buf[1] = 5; buf[2] = 0;
    buf[3] = (char)CHAT_OK;
    memcpy(buf + CHAT_HEADER_SIZE, "world", 5);

    ChatMessage msg;
    ASSERT(DeserializeChatMessage(buf, CHAT_HEADER_SIZE + 5, &msg) == CHAT_OK, "deserialize: returns CHAT_OK");
    ASSERT(msg.m_opcode == OPCODE_LOGIN,                              "deserialize: opcode");
    ASSERT(msg.m_length == 5,                                         "deserialize: length");
    ASSERT(msg.m_status == CHAT_OK,                                   "deserialize: status");
    ASSERT(memcmp(msg.m_value, "world", 5) == 0,                     "deserialize: value");

    ASSERT(DeserializeChatMessage(NULL, 10, &msg) != CHAT_OK,              "deserialize: NULL buf");
    ASSERT(DeserializeChatMessage(buf, 10, NULL) != CHAT_OK,               "deserialize: NULL out");
    ASSERT(DeserializeChatMessage(buf, CHAT_HEADER_SIZE - 1, &msg) != CHAT_OK, "deserialize: buf < header");

    /* value_len field says 65535 — must be rejected even with a large buffer */
    buf[1] = 0xFF; buf[2] = 0xFF;
    ASSERT(DeserializeChatMessage(buf, sizeof(buf), &msg) != CHAT_OK, "deserialize: value_len > MAX");
}

static void TestRoundtrip(FILE* stream)
{
    fprintf(stream, "--- Roundtrip ---\n");
    const char* name = "alice";
    const char* pass = "s3cr3t";
    size_t value_len = strlen(name) + 1 + strlen(pass) + 1;

    ChatMessage orig;
    orig.m_opcode = OPCODE_REGISTER;
    orig.m_length = (uint16_t)value_len;
    orig.m_status = CHAT_OK;
    strcpy(orig.m_value, name);
    strcpy(orig.m_value + strlen(name) + 1, pass);

    char buf[CHAT_MAX_TOTAL];
    int n = SerializeChatMessage(&orig, buf, sizeof(buf));
    ASSERT(n > 0, "roundtrip: serialize succeeds");

    ChatMessage decoded;
    ASSERT(DeserializeChatMessage(buf, (size_t)n, &decoded) == CHAT_OK, "roundtrip: deserialize succeeds");
    ASSERT(decoded.m_opcode == orig.m_opcode,  "roundtrip: opcode");
    ASSERT(decoded.m_length == orig.m_length,  "roundtrip: length");
    ASSERT(decoded.m_status == orig.m_status,  "roundtrip: status");

    /* Verify the name\0password\0 split is intact */
    ASSERT(strcmp(decoded.m_value, name) == 0,                       "roundtrip: name");
    ASSERT(strcmp(decoded.m_value + strlen(name) + 1, pass) == 0,    "roundtrip: password");
}

static void TestOpcodeSign(FILE* stream)
{
    fprintf(stream, "--- Opcode sign (0x81) ---\n");
    /* OPCODE_RESPONSE = 0x81. On signed-char platforms a naïve cast gives -127,
     * which would not compare equal to OPCODE_RESPONSE (129). Verify the fix. */
    char buf[CHAT_MAX_TOTAL];
    ChatMessage msg;
    msg.m_opcode = OPCODE_RESPONSE;
    msg.m_length = 0;
    msg.m_status = CHAT_OK;
    SerializeChatMessage(&msg, buf, sizeof(buf));

    ASSERT(Chat_GetOpcode(buf) == OPCODE_RESPONSE, "opcode sign: 0x81 reads back as OPCODE_RESPONSE");
}

static void TestValidate(FILE* stream)
{
    fprintf(stream, "--- Chat_Validate ---\n");
    ASSERT(Chat_Validate(CHAT_HEADER_SIZE + 10, 10) == CHAT_OK,           "validate: exact fit");
    ASSERT(Chat_Validate(CHAT_MAX_TOTAL, CHAT_MAX_VALUE) == CHAT_OK,      "validate: max payload");
    ASSERT(Chat_Validate(CHAT_HEADER_SIZE + 10, CHAT_MAX_VALUE + 1) != CHAT_OK, "validate: value_len > MAX");
    ASSERT(Chat_Validate(CHAT_HEADER_SIZE + 5,  10) != CHAT_OK,           "validate: buf too small for value");
}

int main(void)
{
    FILE* stream = stdout;

    TestSerialize(stream);
    fprintf(stream, "\n");
    TestDeserialize(stream);
    fprintf(stream, "\n");
    TestRoundtrip(stream);
    fprintf(stream, "\n");
    TestOpcodeSign(stream);
    fprintf(stream, "\n");
    TestValidate(stream);

    fprintf(stream, "\n%s (%d failure%s)\n",
            s_failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED",
            s_failures, s_failures == 1 ? "" : "s");
    return s_failures == 0 ? 0 : 1;
}
