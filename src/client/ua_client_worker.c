//#include "ua_client_request.h"
#include "ua_client.h"
#include "ua_client_internal.h"

#include "ua_types_encoding_binary.h"
#include "ua_types_generated_encoding_binary.h"
#include "ua_transport_generated.h"
#include "ua_transport_generated_handling.h"
#include "ua_transport_generated_encoding_binary.h"

#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>


/**added definitions to ua_client.h
 *
 * */

/****************/
/* Raw Services */
/****************/
void *processRequest(UA_Client *client, UA_ClientRequest *clientRequest);
struct ResponseDescription {
    UA_Client *client;
    UA_Boolean processed;
    UA_UInt32 requestId;
    void *response;
    const UA_DataType *responseType;
};

static void
processServiceResponse(struct ResponseDescription *rd, UA_SecureChannel *channel,
                       UA_MessageType messageType, UA_UInt32 requestId,
                       UA_ByteString *message) {
    UA_StatusCode retval = UA_STATUSCODE_GOOD;
    const UA_NodeId expectedNodeId =
        UA_NODEID_NUMERIC(0, rd->responseType->binaryEncodingId);
    const UA_NodeId serviceFaultNodeId =
        UA_NODEID_NUMERIC(0, UA_TYPES[UA_TYPES_SERVICEFAULT].binaryEncodingId);

    UA_ResponseHeader *respHeader = (UA_ResponseHeader*)rd->response;
    rd->processed = true;

    if(messageType == UA_MESSAGETYPE_ERR) {
        UA_TcpErrorMessage *msg = (UA_TcpErrorMessage*)message;
        UA_LOG_ERROR(rd->client->config.logger, UA_LOGCATEGORY_CLIENT,
                     "Server replied with an error message: %s %.*s",
                     UA_StatusCode_name(msg->error), msg->reason.length, msg->reason.data);
        retval = msg->error;
        goto finish;
    } else if(messageType != UA_MESSAGETYPE_MSG) {
        UA_LOG_ERROR(rd->client->config.logger, UA_LOGCATEGORY_CLIENT,
                     "Server replied with the wrong message type");
        retval = UA_STATUSCODE_BADTCPMESSAGETYPEINVALID;
        goto finish;
    }

    /* Check that the response type matches */
    size_t offset = 0;
    UA_NodeId responseId;
    retval = UA_NodeId_decodeBinary(message, &offset, &responseId);
    if(retval != UA_STATUSCODE_GOOD)
        goto finish;
    if(!UA_NodeId_equal(&responseId, &expectedNodeId)) {
        if(UA_NodeId_equal(&responseId, &serviceFaultNodeId)) {
            /* Take the statuscode from the servicefault */
            retval = UA_decodeBinary(message, &offset, rd->response,
                                     &UA_TYPES[UA_TYPES_SERVICEFAULT]);
        } else {
            UA_LOG_ERROR(rd->client->config.logger, UA_LOGCATEGORY_CLIENT,
                         "Reply answers the wrong request. Expected ns=%i,i=%i."
                         "But retrieved ns=%i,i=%i", expectedNodeId.namespaceIndex,
                         expectedNodeId.identifier.numeric, responseId.namespaceIndex,
                         responseId.identifier.numeric);
            UA_NodeId_deleteMembers(&responseId);
            retval = UA_STATUSCODE_BADINTERNALERROR;
        }
        goto finish;
    }

    /* Decode the response */
    retval = UA_decodeBinary(message, &offset, rd->response, rd->responseType);

 finish:
    if(retval == UA_STATUSCODE_GOOD) {
        UA_LOG_DEBUG(rd->client->config.logger, UA_LOGCATEGORY_CLIENT,
                     "Received a response of type %i", responseId.identifier.numeric);
    } else {
        if(retval == UA_STATUSCODE_BADENCODINGLIMITSEXCEEDED)
            retval = UA_STATUSCODE_BADRESPONSETOOLARGE;
        UA_LOG_INFO(rd->client->config.logger, UA_LOGCATEGORY_CLIENT,
                    "Error receiving the response");
        respHeader->serviceResult = retval;
    }
}

//TODO: write wrapper for different types
void *processRequest(UA_Client *client, UA_ClientRequest *clientRequest){


	const UA_DataType *requestType = &UA_TYPES[UA_TYPES_READREQUEST];
	UA_RequestHeader *rr = (UA_RequestHeader*)(uintptr_t)clientRequest->request;
	rr->authenticationToken = client->authenticationToken; /* cleaned up at the end */
	rr->timestamp = UA_DateTime_now();
	rr->requestHandle = ++client->requestHandle;

	/* Send the request */
	UA_UInt32 requestId = clientRequest->requestId;
	UA_LOG_DEBUG(client->config.logger, UA_LOGCATEGORY_CLIENT,
				 "Sending a request of type %i", requestType->typeId.identifier.numeric);

	UA_StatusCode retval = UA_SecureChannel_sendBinaryMessage(&client->channel, requestId, rr, requestType);
	if(retval != UA_STATUSCODE_GOOD) {
		client->state = UA_CLIENTSTATE_FAULTED;
		UA_NodeId_init(&rr->authenticationToken);
		return NULL;
	}

	UA_NodeId_init(&rr->authenticationToken);


	UA_ReadResponse readResponse; //e.g. UA_ActivateSessionResponse
	void *response = &readResponse;
	const UA_DataType *responseType = &UA_TYPES[UA_TYPES_READRESPONSE];

	UA_init(response, responseType);//ok

	UA_ResponseHeader *respHeader = (UA_ResponseHeader*)response;
	requestId = clientRequest->requestId;

	struct ResponseDescription rd = {client, false, requestId, response, responseType};

	 /* Retrieve the response */
	UA_DateTime maxDate = UA_DateTime_nowMonotonic() + (client->config.timeout * UA_MSEC_TO_DATETIME);
	do {
		/* Retrieve complete chunks */
		UA_ByteString reply = UA_BYTESTRING_NULL;
		UA_Boolean realloced = false;
		UA_DateTime now = UA_DateTime_nowMonotonic();
		if(now < maxDate) {
			UA_UInt32 timeout = (UA_UInt32)((maxDate - now) / UA_MSEC_TO_DATETIME);
			retval = UA_Connection_receiveChunksBlocking(&client->connection, &reply, &realloced, timeout);
		} else {
			retval = UA_STATUSCODE_GOODNONCRITICALTIMEOUT;
		}
		if(retval != UA_STATUSCODE_GOOD) {
			respHeader->serviceResult = retval;
			break;
		}
		/* ProcessChunks and call processServiceResponse for complete messages */
		UA_SecureChannel_processChunks(&client->channel, &reply,
									   (UA_ProcessMessageCallback*)processServiceResponse, &rd);
		/* Free the received buffer */
		if(!realloced)
			client->connection.releaseRecvBuffer(&client->connection, &reply);
		else
			UA_ByteString_deleteMembers(&reply);
		//doesn't loop
	} while(!rd.processed);

	return response;
}


UA_StatusCode UA_Client_addRequest(UA_Client *client, UA_ClientRequest *clientRequest){
	UA_ClientRequest *req = realloc(client->requests,
	                 sizeof(struct UA_ClientRequest)*(client->requestSize+1));

	if(!req) {
		return UA_STATUSCODE_BADINTERNALERROR;
	}

    clientRequest->connection = &(client->connection);

    client->requests = req;
	client->requests[client->requestSize].connection = &(client->connection);
	client->requests[client->requestSize].methodCall.method = clientRequest->methodCall.method;
	client->requests[client->requestSize].methodCall.data = clientRequest->methodCall.data;
	client->requests[client->requestSize].requestId = ++client->requestId;
	client->requests[client->requestSize].request = clientRequest->request;
	client->requestSize += 1;

	UA_StatusCode retval = UA_Client_manuallyRenewSecureChannel(client);
	if(retval != UA_STATUSCODE_GOOD) {
		client->state = UA_CLIENTSTATE_ERRORED;
		return retval;
	}

	return retval;
}

UA_StatusCode UA_Client_iterate(UA_Client *client){
	UA_StatusCode retval = UA_STATUSCODE_GOOD;
	/* Get requests */
	UA_ClientRequest *requests = client->requests;

	UA_ReadResponse *response;
	size_t requestSize = client->requestSize;

    for(UA_UInt32 r = 0; r < requestSize; ++r) {
    	response = (UA_ReadResponse*) processRequest(client, &requests[r]);

    	for(UA_UInt32 j = 0; j < requestSize; ++j)
    		//compare requestId with the request handle of response
    		if(response->responseHeader.requestHandle == client->requests[r].requestId){
    			client->requests[r].methodCall.method(client, client->requests[r].methodCall.data);
    		}

    }

    return retval;
}
