/* This work is licensed under a Creative Commons CCZero 1.0 Universal License.
 * See http://creativecommons.org/publicdomain/zero/1.0/ for more information. */

#include <stdio.h>
#include "open62541.h"
#include <unistd.h>

/*async connection callback*/
static void onConnect(UA_Client *client, void *connected, UA_UInt32 requestId,
		void *response) {
	if (UA_Client_getState(client) == UA_CLIENTSTATE_SESSION)
		*(UA_Boolean *) connected = true;
	printf("%s\n", "Client is connected!");
}

/*raw service callbacks*/
static void valueWritten(UA_Client *client, void *userdata, UA_UInt32 requestId,
		UA_WriteResponse *response) {
	printf("%-50s%i\n", "Received WriteResponse for request ", requestId);
}

static
void valueRead(UA_Client *client, void *userdata, UA_UInt32 requestId,
		UA_ReadResponse *response) {
	printf("%-50s%i\n", "Received ReadResponse for request ", requestId);
}

static
void fileBrowsed(UA_Client *client, void *userdata, UA_UInt32 requestId,
		UA_BrowseResponse *response) {
	printf("%-50s%i\n", "Received BrowseResponse for request ", requestId);
	UA_String us = *(UA_String *) userdata;
	printf("---%.*s is also passed safely \n", (int) us.length, us.data);
}

/*high-level function callbacks*/
static
void readValueAttributeCallback(UA_Client *client, void *userdata,
		UA_UInt32 requestId, UA_Variant *var) {
	printf("%-50s%i\n", "Read value attribute for request", requestId);

	if (UA_Variant_hasScalarType(var, &UA_TYPES[UA_TYPES_INT32])) {
		UA_Int32 int_val = *(UA_Int32*) var->data;
		printf("---%-40s%-8i\n",
				"Reading the value of node (1, \"the.answer\"):", int_val);
	}

	/*more type distinctions possible*/
	return;
}

static
void attrWritten(UA_Client *client, void *userdata, UA_UInt32 requestId,
		UA_WriteResponse *response) {
	/*assuming no data to be retrieved by writing attributes*/
	printf("%-50s%i\n", "Wrote value attribute for request ", requestId);
}

static void methodCalled(UA_Client *client, void *userdata, UA_UInt32 requestId,
		UA_CallResponse *response) {

	printf("%-50s%i\n", "Called method for request ", requestId);
	size_t outputSize;
	UA_Variant *output;
	UA_StatusCode retval = response->responseHeader.serviceResult;
	if (retval == UA_STATUSCODE_GOOD) {
		if (response->resultsSize == 1)
			retval = response->results[0].statusCode;
		else
			retval = UA_STATUSCODE_BADUNEXPECTEDERROR;
	}
	if (retval != UA_STATUSCODE_GOOD) {
		UA_CallResponse_deleteMembers(response);
	}

	/* Move the output arguments */
	output = response->results[0].outputArguments;
	outputSize = response->results[0].outputArgumentsSize;
	response->results[0].outputArguments = NULL;
	response->results[0].outputArgumentsSize = 0;

	if (retval == UA_STATUSCODE_GOOD) {
		printf("---Method call was successful, returned %lu values.\n",
				(unsigned long) outputSize);
		UA_Array_delete(output, outputSize, &UA_TYPES[UA_TYPES_VARIANT]);
	} else {
		printf("---Method call was unsuccessful, returned %x values.\n",
				retval);
	}
}

static void translateCalled(UA_Client *client, void *userdata,
		UA_UInt32 requestId, UA_TranslateBrowsePathsToNodeIdsResponse *response) {
	printf("%-50s%i\n", "Translated path for request ", requestId);

	if (response->results[0].targetsSize == 1) {
		return;
	}
}


int main(int argc, char *argv[]) {
	UA_Client *client = UA_Client_new(UA_ClientConfig_default);
	UA_UInt32 reqId = 0;
	UA_String userdata = UA_STRING("userdata");
	UA_Boolean connected = false;

	/*preparing requests*/
	UA_String sValue;
	sValue.data = (UA_Byte *) malloc(90000);
	memset(sValue.data, 'a', 90000);
	sValue.length = 90000;

	UA_WriteRequest wReq;
	UA_WriteRequest_init(&wReq);
	wReq.nodesToWrite = UA_WriteValue_new();
	wReq.nodesToWriteSize = 1;
	wReq.nodesToWrite[0].nodeId = UA_NODEID_NUMERIC(1, 51034);
	wReq.nodesToWrite[0].attributeId = UA_ATTRIBUTEID_VALUE;
	wReq.nodesToWrite[0].value.hasValue = true;
	wReq.nodesToWrite[0].value.value.type = &UA_TYPES[UA_TYPES_STRING];
	wReq.nodesToWrite[0].value.value.storageType = UA_VARIANT_DATA_NODELETE; /* do not free the integer on deletion */
	wReq.nodesToWrite[0].value.value.data = &sValue;

	UA_ReadRequest rReq;
	UA_ReadRequest_init(&rReq);
	rReq.nodesToRead = UA_ReadValueId_new();
	rReq.nodesToReadSize = 1;
	rReq.nodesToRead[0].nodeId = UA_NODEID_STRING(1, "the.answer");
	rReq.nodesToRead[0].attributeId = UA_ATTRIBUTEID_VALUE;

	UA_BrowseRequest bReq;
	UA_BrowseRequest_init(&bReq);
	bReq.requestedMaxReferencesPerNode = 0;
	bReq.nodesToBrowse = UA_BrowseDescription_new();
	bReq.nodesToBrowseSize = 1;
	bReq.nodesToBrowse[0].nodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER); /* browse objects folder */
	bReq.nodesToBrowse[0].resultMask = UA_BROWSERESULTMASK_ALL; /* return everything */

	/*connected updated when client is connected*/
	UA_Client_connect_async(client, "opc.tcp://localhost:4840", onConnect,
			&connected);

	/*Demo: raw services*/
	do {
		if (connected) {
			/*if not connected requests are not sent*/
			UA_Client_sendAsyncBrowseRequest(client, &bReq, fileBrowsed,
					&userdata, &reqId);

			UA_Client_sendAsyncWriteRequest(client, &wReq, valueWritten, NULL,
					&reqId);

			UA_Client_sendAsyncReadRequest(client, &rReq, valueRead, NULL,
					&reqId);
		}
		/*requests are processed*/
		UA_Client_run_iterate(client, 10);
	} while (reqId < 10);

	/*Demo: high-level functions*/
	UA_Int32 value = 0;
	UA_Variant *myVariant = UA_Variant_new();

	for (UA_UInt16 i = 0; i < 5; i++) {
		/*writing and reading value 1 to 5*/
		UA_Variant_setScalarCopy(myVariant, &value, &UA_TYPES[UA_TYPES_INT32]);
		value++;
		UA_Client_writeValueAttribute_async(client,
				UA_NODEID_STRING(1, "the.answer"), myVariant, attrWritten, NULL,
				&reqId);

		UA_Client_readValueAttribute_async(client,
				UA_NODEID_STRING(1, "the.answer"), readValueAttributeCallback,
				NULL, &reqId);

		/*the following two functions takes more arguments*/
		UA_String stringValue = UA_String_fromChars("World");
		UA_Variant input;
		UA_Variant_init(&input);
		UA_Variant_setScalar(&input, &stringValue, &UA_TYPES[UA_TYPES_STRING]);

		UA_Client_call_async(client,
				UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER),
				UA_NODEID_NUMERIC(1, 62541), 1, &input, methodCalled, NULL,
				&reqId);

#define pathSize 3
		char *paths[pathSize] = { "Server", "ServerStatus", "State" };
		UA_UInt32 ids[pathSize] = { UA_NS0ID_ORGANIZES,
		UA_NS0ID_HASCOMPONENT, UA_NS0ID_HASCOMPONENT };

		UA_Cient_translateBrowsePathsToNodeIds_async(client, paths, ids,
		pathSize, translateCalled, NULL, &reqId);
	}

	/*process high-level requests*/
	for (int i = 0; i < 10; i++)
		UA_Client_run_iterate(client, 10);

	UA_Variant_delete(myVariant);
	UA_Client_disconnect(client);
	UA_Client_delete(client);
	return (int) UA_STATUSCODE_GOOD;
}
