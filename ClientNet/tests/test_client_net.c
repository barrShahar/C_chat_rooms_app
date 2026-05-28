#include <unistd.h>
#include "../ClientController.h"
#include "logger.h"
#include "../config.h"
#include "../NetworkProtocol.h"

void TestRegisterProcess(ClientController* a_clientController)
{
    char* name = "test_user_register";
    char* password = "test_password";

    char valueBuffer[CONF_RECV_BUF_SIZE];
    char messageBuffer[CONF_RECV_BUF_SIZE];

    ChatMessage message;
    message.m_opcode = OPCODE_REGISTER;
    message.m_length = strlen(name) + strlen(password) + 2;
    message.m_status = CHAT_OK;
    

    strcpy((char*)valueBuffer, name);
    strcpy((char*)valueBuffer + strlen(name) + 1, password);
    memcpy(message.m_value, valueBuffer, message.m_length);
    

    SerializeChatMessage(&message, messageBuffer, CONF_RECV_BUF_SIZE);

    LOG_INFO("Sending register message to server, message: %s", (char*)valueBuffer);
    ClientResult result = ClientController_Send(a_clientController, (char*)messageBuffer, CONF_RECV_BUF_SIZE);
    if (result != CLIENT_RESULT_SUCCESS)
    {
        LOG_ERROR("ClientController_Send failed: %d", result);
        return;
    }

    result = ClientController_Receive(a_clientController, (char*)messageBuffer, CONF_RECV_BUF_SIZE);
    LOG_DEBUG("Received message from server, message: %s", (char*)messageBuffer);
    if (result != CLIENT_RESULT_SUCCESS)
    {
        LOG_ERROR("ClientController_Receive failed: %d", result);
        return;
    }
    // decode the response

    ChatMessage responseMessage;
    if (DeserializeChatMessage((char*)messageBuffer, CONF_RECV_BUF_SIZE, &responseMessage) != CHAT_OK)
    {
        LOG_ERROR("DeserializeChatMessage failed");
        return;
    }

    MessageOpcode responseOpcode = Chat_GetOpcode(messageBuffer);
    if (responseOpcode != OPCODE_RESPONSE)
    {
        LOG_ERROR("Invalid opcode: %d", responseOpcode);
        return;
    }


    LOG_INFO("Response opcode: %d", responseOpcode);
    ChatStatus responseStatus = (ChatStatus)messageBuffer[3];
    if (responseStatus != CHAT_OK)
    {
        LOG_ERROR("Response status: %d", responseStatus);
        return;
    }
    else {
        LOG_INFO("Response status: OK");
        LOG_INFO("Response message: %s", messageBuffer + 4);
    }
    
}

int main(void)
{

    LOG_INFO("Starting test client");
    char buf[CONF_RECV_BUF_SIZE];
    ClientController* clientController = ClientController_Create(CONF_SERVER_IP, CONF_SERVER_PORT);
    if (clientController == NULL)
    {
        LOG_ERROR("ClientController_Create failed");
        return 1;
    }
    LOG_INFO("Starting client controller");
    ClientController_Start(clientController);
    LOG_INFO("Sending message to server, message: Hello, World!");
    ClientController_Send(clientController, "Hello, World!", 13);

    ClientController_Receive(clientController, buf, CONF_RECV_BUF_SIZE);
    LOG_INFO("Received message: %s", buf);

    sleep(1);
    LOG_INFO("Testing register process");
    TestRegisterProcess(clientController);
    LOG_DEBUG("Stopping client controller");
    ClientController_Stop(clientController);
    ClientController_Destroy(&clientController);

    LOG_INFO("Test client manager finished");
    return 0;
}