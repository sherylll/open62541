#include "ua_services.h"
#include "ua_server_internal.h"
#include "ua_statuscodes.h"
#include "ua_util.h"
#include "ua_nodestore.h"
#include "ua_nodes.h"

static const UA_VariableNode *getArgumentsVariableNode(UA_Server *server, const UA_MethodNode *ofMethod,
                                                       UA_String withBrowseName) {
    const UA_Node *refTarget;
    UA_NodeId hasProperty = UA_NODEID_NUMERIC(0, UA_NS0ID_HASPROPERTY);
    
    for(UA_Int32 i = 0; i < ofMethod->referencesSize; i++) {
        if(ofMethod->references[i].isInverse == UA_FALSE && 
            UA_NodeId_equal(&hasProperty, &ofMethod->references[i].referenceTypeId)) {
            refTarget = UA_NodeStore_get(server->nodestore, &ofMethod->references[i].targetId.nodeId);
            if(!refTarget)
                continue;
            if(refTarget->nodeClass == UA_NODECLASS_VARIABLE && 
                refTarget->browseName.namespaceIndex == 0 &&
                UA_String_equal(&withBrowseName, &refTarget->browseName.name)) {
                return (const UA_VariableNode*) refTarget;
            }
            UA_NodeStore_release(refTarget);
        }
    }
    return UA_NULL;
}

static UA_StatusCode statisfySignature(UA_Variant *var, UA_Argument arg) {
    if(!UA_NodeId_equal(&var->type->typeId, &arg.dataType) )
        return UA_STATUSCODE_BADINVALIDARGUMENT;
    
    // Note: The namespace compiler will compile nodes with their actual array dimensions, never -1
    if(arg.arrayDimensionsSize > 0 && var->arrayDimensionsSize > 0)
        if(var->arrayDimensionsSize != arg.arrayDimensionsSize) 
            return UA_STATUSCODE_BADINVALIDARGUMENT;
        
        // Continue with jpfr's statisfySignature from here on
        /* ValueRank Semantics
         *  n >= 1: the value is an array with the specified number of dimens*ions.
         *  n = 0: the value is an array with one or more dimensions.
         *  n = -1: the value is a scalar.
         *  n = -2: the value can be a scalar or an array with any number of dimensions.
         *  n = -3:  the value can be a scalar or a one dimensional array. */
        UA_Boolean scalar = UA_Variant_isScalar(var);
        if(arg.valueRank == 0 && scalar)
            return UA_STATUSCODE_BADINVALIDARGUMENT;
        if(arg.valueRank == -1 && !scalar)
            return UA_STATUSCODE_BADINVALIDARGUMENT;
        if(arg.valueRank == -3 && var->arrayDimensionsSize > 1)
            return UA_STATUSCODE_BADINVALIDARGUMENT;
        if(arg.valueRank >= 1 && var->arrayDimensionsSize != arg.arrayDimensionsSize)
            return UA_STATUSCODE_BADINVALIDARGUMENT;
        
        if(arg.arrayDimensionsSize >= 1) {
            if(arg.arrayDimensionsSize != var->arrayDimensionsSize)
                return UA_STATUSCODE_BADINVALIDARGUMENT;
            for(UA_Int32 i = 0; i < arg.arrayDimensionsSize; i++) {
                if(arg.arrayDimensions[i] != (UA_UInt32) var->arrayDimensions[i])
                    return UA_STATUSCODE_BADINVALIDARGUMENT;
            }
        }
   return UA_STATUSCODE_GOOD;
}

static UA_StatusCode argConformsToDefinition(UA_CallMethodRequest *rs, const UA_VariableNode *argDefinition) {
    if(argDefinition->value.variant.type != &UA_TYPES[UA_TYPES_ARGUMENT] &&
        argDefinition->value.variant.type != &UA_TYPES[UA_TYPES_EXTENSIONOBJECT])
        return UA_STATUSCODE_BADINTERNALERROR;
    if(rs->inputArgumentsSize < argDefinition->value.variant.arrayLength) 
        return UA_STATUSCODE_BADARGUMENTSMISSING;
    if(rs->inputArgumentsSize > argDefinition->value.variant.arrayLength)
        return UA_STATUSCODE_BADINVALIDARGUMENT;
    
    const UA_ExtensionObject *thisArgDefExtObj;
    UA_Variant *var;
    UA_Argument arg;
    size_t decodingOffset = 0;
    UA_StatusCode retval = UA_STATUSCODE_GOOD;
    UA_NodeId ArgumentNodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_ARGUMENT + UA_ENCODINGOFFSET_BINARY);
    for(int i = 0; i<rs->inputArgumentsSize; i++) {
        var = &rs->inputArguments[i];
        if(argDefinition->value.variant.type == &UA_TYPES[UA_TYPES_EXTENSIONOBJECT]) {
            thisArgDefExtObj = &((const UA_ExtensionObject *) (argDefinition->value.variant.data))[i];
            decodingOffset = 0;
            
            if(!UA_NodeId_equal(&ArgumentNodeId, &thisArgDefExtObj->typeId))
                return UA_STATUSCODE_BADINTERNALERROR;
                
            UA_decodeBinary(&thisArgDefExtObj->body, &decodingOffset, &arg, &UA_TYPES[UA_TYPES_ARGUMENT]);
        } else if(argDefinition->value.variant.type == &UA_TYPES[UA_TYPES_ARGUMENT])
            arg = ((UA_Argument *) argDefinition->value.variant.data)[i];
        retval |= statisfySignature(var, arg);
    }
    return retval;
}

void Service_Call(UA_Server *server, UA_Session *session, const UA_CallRequest *request,
                  UA_CallResponse *response) {
    if(request->methodsToCallSize <= 0) {
        response->responseHeader.serviceResult = UA_STATUSCODE_BADNOTHINGTODO;
        return;
    }

    response->results = UA_Array_new(&UA_TYPES[UA_TYPES_CALLMETHODRESULT], request->methodsToCallSize);
    if(!response->results) {
        response->responseHeader.serviceResult = UA_STATUSCODE_BADOUTOFMEMORY;
        return;
    }
    response->resultsSize = request->methodsToCallSize;
    
    for(UA_Int32 i = 0; i < request->methodsToCallSize;i++) {
        UA_CallMethodRequest *rq = &request->methodsToCall[i];
        UA_CallMethodResult  *rs = &response->results[i];
        
        /* Get/Check Nodes */
        const UA_MethodNode *methodCalled =
            (const UA_MethodNode*) UA_NodeStore_get(server->nodestore, &rq->methodId);
        if(methodCalled == UA_NULL) {
            rs->statusCode = UA_STATUSCODE_BADMETHODINVALID;
            continue;
        }
        const UA_ObjectNode *withObject =
            (const UA_ObjectNode *) UA_NodeStore_get(server->nodestore, &rq->objectId);
        if(withObject == UA_NULL) {
            rs->statusCode = UA_STATUSCODE_BADNODEIDINVALID;
            printf("Obj not found\n");
            continue;
        }
        
        if(methodCalled->nodeClass != UA_NODECLASS_METHOD) {
            rs->statusCode = UA_STATUSCODE_BADNODECLASSINVALID;
            continue;
        }
        if(withObject->nodeClass != UA_NODECLASS_OBJECT && withObject->nodeClass != UA_NODECLASS_OBJECTTYPE) {
            rs->statusCode = UA_STATUSCODE_BADNODECLASSINVALID;
            printf("Obj not found 1\n");
            continue;
        }
        
        /* Verify method/object relations */
        // Object must have a hasComponent reference (or any inherited referenceType from sayd reference) 
        // to be valid for a methodCall...
        for(UA_Int32 i = 0; i < withObject->referencesSize; i++) {
            if(withObject->references[i].referenceTypeId.identifier.numeric == UA_NS0ID_HASCOMPONENT) {
                // FIXME: Not checking any subtypes of HasComponent at the moment
                if(UA_NodeId_equal(&withObject->references[i].targetId.nodeId, &methodCalled->nodeId)) {
                    rs->statusCode = UA_STATUSCODE_GOOD;
                    break;
                }
                
            }
        }
        if(rs->statusCode != UA_STATUSCODE_GOOD)
            continue;
        
        /* Verify method executable */
        if(((const UA_MethodNode *) methodCalled)->executable == UA_FALSE ||
           ((const UA_MethodNode *) methodCalled)->userExecutable == UA_FALSE ) {
            rs->statusCode = UA_STATUSCODE_BADNOTWRITABLE; // There is no NOTEXECUTABLE?
            continue;
        }

        /* Verify Input Argument count, types and sizes */
        const UA_VariableNode *inputArguments = getArgumentsVariableNode(server, methodCalled,
                                                                         UA_STRING("InputArguments"));
        if(inputArguments) {
            // Expects arguments
            rs->statusCode = argConformsToDefinition(rq, inputArguments);
            UA_NodeStore_release((const UA_Node*)inputArguments);
            if(rs->statusCode != UA_STATUSCODE_GOOD)
                continue;
        } else if(rq->inputArgumentsSize > 0) {
            // Expects no arguments, but got some
            rs->statusCode = UA_STATUSCODE_BADINVALIDARGUMENT;
            UA_NodeStore_release((const UA_Node*)inputArguments);
            continue;
        }

        const UA_VariableNode *outputArguments = getArgumentsVariableNode(server, methodCalled,
                                                                          UA_STRING("OutputArguments"));
        if(!outputArguments) {
            // A MethodNode must have an OutputArguments variable (which may be empty)
            rs->statusCode = UA_STATUSCODE_BADINTERNALERROR;
            continue;
        }
        
        // Call method if available
        if(methodCalled->attachedMethod) {
            rs->outputArguments = UA_Array_new(&UA_TYPES[UA_TYPES_VARIANT],
                                               outputArguments->value.variant.arrayLength);
            rs->outputArgumentsSize = outputArguments->value.variant.arrayLength;
            rs->statusCode = methodCalled->attachedMethod(withObject->nodeId, rq->inputArguments,
                                                          rs->outputArguments);
        }
        else
            rs->statusCode = UA_STATUSCODE_BADNOTWRITABLE; // There is no NOTEXECUTABLE?
            
        /* FIXME: Verify Output Argument count, types and sizes */
        if(outputArguments) {
            UA_NodeStore_release((const UA_Node*)outputArguments);
        }
        UA_NodeStore_release((const UA_Node *)withObject);
        UA_NodeStore_release((const UA_Node *)methodCalled);
    }
}
