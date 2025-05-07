#ifndef jsonparsing_H
#define jsonparsing_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <cJSON.h>

char *GW_LQAssembleGetMessages(char *usertoken, char *channelid, uint64_t *beforeTimestamp, uint64_t *afterTimestamp, uint16_t limit){ //todo figure out search terms
    if (!usertoken) return NULL;
    if (!channelid) return NULL;
    
    char messageurl[128];
    int urlsize = sizeof(messageurl);
    snprintf(messageurl, urlsize,"/v4/channel/%s/messages?limit=%hu", channelid, limit);
    if (beforeTimestamp) snprintf(messageurl + strlen(messageurl), urlsize - strlen(messageurl), "&beforeTimestamp=%llu", *beforeTimestamp);
    if (afterTimestamp) snprintf(messageurl + strlen(messageurl), urlsize - strlen(messageurl), "&afterTimestamp=%llu", *afterTimestamp);

    char statestring[64];
    snprintf(statestring, sizeof(statestring), "GetMessages.%s", channelid);
    cJSON *rpcmessage = cJSON_CreateObject();
    cJSON *event = cJSON_CreateString("rpc");
    cJSON_AddItemToObject(rpcmessage, "event", event);

    cJSON *route = cJSON_CreateString(messageurl);
    cJSON_AddItemToObject(rpcmessage, "route", route); 

    cJSON *method = cJSON_CreateString("GET");
    cJSON_AddItemToObject(rpcmessage, "method", method);

    cJSON *body = cJSON_CreateNull();
    cJSON_AddItemToObject(rpcmessage, "body", body);

    cJSON *state = cJSON_CreateString(statestring);
    cJSON_AddItemToObject(rpcmessage, "state", state);

    cJSON *token = cJSON_CreateString(usertoken);
    cJSON_AddItemToObject(rpcmessage, "token", token);

    char *assembled = cJSON_PrintUnformatted(rpcmessage);
    //printf("Assembled for GetMessages: %s\n", assembled);

    cJSON_Delete(rpcmessage);
    return assembled; //YOU MORON YOU NEED TO FREE THIS :OTL:
} 

cJSON *GW_EventReader(const char *json_response, uint16_t *eventnumber){
    cJSON *json = cJSON_Parse(json_response);

    cJSON *event = cJSON_GetObjectItemCaseSensitive(json, "event");
    if (cJSON_IsString(event) && event->valuestring != NULL) {
        if (strcmp(event->valuestring, "rpc") == 0){
            *eventnumber = 0;
            return json;
        }
        if (strcmp(event->valuestring, "messageCreate") == 0){
            //printf("Found event: %s\n", event->valuestring);
            *eventnumber = 1;
            return json;
        }
        if (strcmp(event->valuestring, "heartbeat") == 0){
            printf("Found event: %s\n", event->valuestring);
        }
        else {
            printf("GatewayReader: Event unknown/unimplemented: %s\n", event->valuestring);
        }
    } 
    else {
        printf("GatewayReader: Event is NULL or not a string\n");
    }
    cJSON_Delete(json);
    return NULL;
}

char *parseResponse(const char *json_string, const char* json_object){
    if (!json_string) {
        printf("Invalid JSON input!\n");
        return NULL;
    }

    cJSON *json = cJSON_Parse(json_string);
    if (json == NULL){
        printf("cJSON_Parse failed!\n");
        return NULL;
    }

    cJSON *response = cJSON_GetObjectItem(json, "response");
    if (!cJSON_IsObject(response)) {
        printf("Failed to find object \"response\" in JSON.\n");
        cJSON_Delete(json);
        return NULL;
    }

    cJSON *value = cJSON_GetObjectItem(response, json_object);
    if (!cJSON_IsString(value) || value->valuestring == NULL) {
        printf("Failed to extract value %s from response.\n", json_object);
    }

    char *json_value = (char *)malloc(strlen(value->valuestring) + 1);
    if (!json_value) {
        printf("Memory allocation failed!\n");
        cJSON_Delete(json);
        return NULL;
    }

    strcpy(json_value, value->valuestring);
    cJSON_Delete(json);
    
    return json_value; //WHY ARE YOU NOT FREEING THIS????
}

#endif