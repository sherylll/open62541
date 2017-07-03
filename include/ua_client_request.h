
#ifndef UA_CLIENT_REQUEST_H
#define UA_CLIENT_REQUEST_H
#include "ua_connection.h"

struct UA_Client;
typedef struct UA_Client UA_Client;
typedef void (*UA_ClientCallback)(UA_Client *client, void *data);
typedef struct UA_ClientRequest UA_ClientRequest;

struct UA_ClientRequest {
	UA_Connection *connection;
    struct {
		void *data;
        UA_ClientCallback method;
    }methodCall;
    void *request; //real request, e.g., UA_ActivateSessionRequest
    UA_DataType *requestType;
    void *response;
    const UA_DataType *responseType;

    UA_NodeId nodeId;
    UA_UInt32 requestId;
};

#endif





