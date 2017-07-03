/* This work is licensed under a Creative Commons CCZero 1.0 Universal License.
 * See http://creativecommons.org/publicdomain/zero/1.0/ for more information. */

#include <signal.h>

#ifdef UA_NO_AMALGAMATION
# include "ua_types.h"
# include "ua_client.h"
# include "ua_config_standard.h"
# include "ua_log_stdout.h"
# include "ua_client_highlevel.h"

#else
# include "open62541.h"
#endif
#include <stdio.h>

static void
testCallback(UA_Client *client, void *data) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "testcallback");
    printf("called back\n");
}

static void
testCallback2(UA_Client *client, void *data) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "testcallback");
    printf("called back again\n");
}

UA_Boolean running = true;
static void stopHandler(int sign) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_CLIENT, "received ctrl-c");
    running = false;
}

int main(void) {
    signal(SIGINT, stopHandler);
    signal(SIGTERM, stopHandler);

    UA_Client *client = UA_Client_new(UA_ClientConfig_standard);

	/* Listing endpoints */
	UA_EndpointDescription* endpointArray = NULL;
	size_t endpointArraySize = 0;
	UA_StatusCode retval = UA_Client_getEndpoints(client, "opc.tcp://localhost:16664",
												 &endpointArraySize, &endpointArray);
	if(retval != UA_STATUSCODE_GOOD) {
	   UA_Array_delete(endpointArray, endpointArraySize, &UA_TYPES[UA_TYPES_ENDPOINTDESCRIPTION]);
	   UA_Client_delete(client);
	   return (int)retval;
	}
	//printf("debug0");
	printf("%i endpoints found\n", (int)endpointArraySize);
	for(size_t i=0;i<endpointArraySize;i++){
	   printf("URL of endpoint %i is %.*s\n", (int)i,
			  (int)endpointArray[i].endpointUrl.length,
			  endpointArray[i].endpointUrl.data);
	}

	UA_Array_delete(endpointArray,endpointArraySize, &UA_TYPES[UA_TYPES_ENDPOINTDESCRIPTION]);

	/* Connect to a server */
	retval = UA_Client_connect(client, "opc.tcp://localhost:16664");
	if(retval != UA_STATUSCODE_GOOD) {
	        UA_Client_delete(client);
	        return (int)retval;
	}
    /* add requests to the client */
	UA_UInt32 rid1 = 1;
	UA_UInt32 rid2 = 2;
	UA_NodeId nodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER_SERVERSTATUS_CURRENTTIME);

    UA_ReadValueId item;
    UA_ReadValueId_init(&item);
    item.nodeId = nodeId;
    item.attributeId = UA_ATTRIBUTEID_ARRAYDIMENSIONS;
    UA_ReadRequest request;
    UA_ReadRequest_init(&request);
    request.nodesToRead = &item;
    request.nodesToReadSize = 1;

    UA_ReadResponse response;

    //response and request types


    UA_ClientRequest clientRequest1 = {.methodCall = {.method = testCallback, }, .requestId = rid1,
    		 .request = &request,  .response = &response,};


    UA_ClientRequest clientRequest2 = {.methodCall = {.method = testCallback2, }, .requestId = rid2,
    		 .request = &request,  .response = &response,};

    UA_ClientRequest clientRequest3 = {.methodCall = {.method = testCallback, },
        		 .request = &request,  .response = &response,};


    //UA_Request request1 = {.methodCall = {.method = testCallback, .data = NULL} };

    //while(running)
//incomplete sequence
    	UA_Client_addRequest(client, &clientRequest1);
    	//UA_Client_iterate(client);
		UA_Client_addRequest(client, &clientRequest2);
		//UA_Client_iterate(client);
		UA_Client_addRequest(client, &clientRequest3);
		UA_Client_iterate(client);

    // disconnect closes session & secure channel

    UA_Client_disconnect(client);
    UA_Client_delete(client);

    return 0;
}
