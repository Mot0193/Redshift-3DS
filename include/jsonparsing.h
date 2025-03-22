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
    return assembled;
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
        else {
            printf("GatewayReader: Event unknown/unimplemented\n");
            cJSON_Delete(json);
            return NULL;
        }
    } 
    else {
        printf("GatewayReader: Event is NULL or not a string\n");
        cJSON_Delete(json);
        return NULL;
    }
}

char *parse_response(const char *json_string, const char* json_object){
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
    
    return json_value;

}

char **parse_quark(const char *json_string, int *num_quarks) {
    if (!json_string) {
        printf("Invalid JSON input!\n");
        return NULL; // Return if the JSON input is invalid
    }

    // Parse the JSON string
    cJSON *json = cJSON_Parse(json_string);
    if (json == NULL) {
        printf("cJSON_Parse in parse_quark failed! \n");
        return NULL;
    }

    // Access the "response" object
    cJSON *response = cJSON_GetObjectItem(json, "response");
    if (!cJSON_IsObject(response)) {
        printf("Failed to find object \"response\" in JSON.\n");
        cJSON_Delete(json);
        *num_quarks = 0;
        return NULL;
    }

    cJSON *quarks = cJSON_GetObjectItem(response, "quarks");
    if (!cJSON_IsArray(quarks)) {
        printf("Failed to find \"quarks\" array.\n");
        cJSON_Delete(json);
        *num_quarks = 0;
        return NULL;
    }
    // Get the number of quarks
    *num_quarks = cJSON_GetArraySize(quarks);

    // Dynamically allocate memory for an array of string pointers
    char **quark_names = (char **)malloc(*num_quarks * sizeof(char *));
    if (quark_names == NULL) {
        printf("Failed to allocate memory for quark names.\n");
        cJSON_Delete(json);
        *num_quarks = 0;
        return NULL;
    }

    // Iterate through each quark in the "quarks" array
    for (int i = 0; i < *num_quarks; i++) {
        cJSON *quark = cJSON_GetArrayItem(quarks, i);
        if (!cJSON_IsObject(quark)) {
            printf("Failed to access quark object at index %d.\n", i);
            continue;
        }

        // Access the "name" of each quark
        cJSON *name = cJSON_GetObjectItem(quark, "name");
        if (cJSON_IsString(name) && name->valuestring != NULL) {
            // Allocate memory for the quark name and copy it
            quark_names[i] = (char *)malloc(strlen(name->valuestring) + 1);
            if (quark_names[i]) {
                strcpy(quark_names[i], name->valuestring);
            }
            else {
                printf("Failed to allocate memory for quark name at index %d.\n", i);
                // Free previously allocated memory before returning
                for (int j = 0; j < i; j++) {
                    free(quark_names[j]);
                }
                free(quark_names);
                cJSON_Delete(json);
                *num_quarks = 0;
                return NULL;
            }
        } else {
            printf("Failed to get name from quark at index %d.\n", i);
            quark_names[i] = NULL;
        }
    }

    // Cleanup and return the array of quark names
    cJSON_Delete(json);
    return quark_names;
}

char **parse_channel(const char *json_string, const char *quark_name, int *num_channels) {
    if (!json_string) {
        printf("Invalid JSON input!\n");
        *num_channels = 0;
        return NULL;
    }

    // Parse the JSON string
    cJSON *json = cJSON_Parse(json_string);
    if (json == NULL) {
        printf("Error parsing JSON!\n");
        *num_channels = 0;
        return NULL;
    }

    // Access the "response" object
    cJSON *response = cJSON_GetObjectItem(json, "response");
    if (!cJSON_IsObject(response)) {
        printf("Failed to find \"response\" object in JSON.\n");
        cJSON_Delete(json);
        *num_channels = 0;
        return NULL;
    }

    // Access the "quarks" array
    cJSON *quarks = cJSON_GetObjectItem(response, "quarks");
    if (!cJSON_IsArray(quarks)) {
        printf("Failed to find \"quarks\" array.\n");
        cJSON_Delete(json);
        *num_channels = 0;
        return NULL;
    }

    // Find the quark with the matching name
    cJSON *quark = NULL;
    cJSON_ArrayForEach(quark, quarks) {
        cJSON *name = cJSON_GetObjectItem(quark, "name");
        if (cJSON_IsString(name) && strcmp(name->valuestring, quark_name) == 0) {
            // Found the quark with the matching name
            // Get the "channels" array
            cJSON *channels = cJSON_GetObjectItem(quark, "channels");
            if (!cJSON_IsArray(channels)) {
                printf("No channels found for quark \"%s\".\n", quark_name);
                cJSON_Delete(json);
                *num_channels = 0;
                return NULL;
            }

            // Get the number of channels
            *num_channels = cJSON_GetArraySize(channels);

            // Dynamically allocate memory for an array of string pointers
            char **channel_names = (char **)malloc(*num_channels * sizeof(char *));
            if (channel_names == NULL) {
                printf("Failed to allocate memory for channel names.\n");
                cJSON_Delete(json);
                *num_channels = 0;
                return NULL;
            }

            // Iterate through the "channels" array to extract channel names
            for (int i = 0; i < *num_channels; i++) {
                cJSON *channel = cJSON_GetArrayItem(channels, i);
                if (!cJSON_IsObject(channel)) {
                    printf("Failed to access channel object at index %d.\n", i);
                    continue;
                }

                // Access the "name" of each channel
                cJSON *channel_name = cJSON_GetObjectItem(channel, "name");
                if (cJSON_IsString(channel_name) && channel_name->valuestring != NULL) {
                    // Allocate memory for the channel name and copy it
                    channel_names[i] = strdup(channel_name->valuestring);
                    if (channel_names[i] == NULL) {
                        printf("Failed to copy channel name at index %d.\n", i);
                        // Free previously allocated memory before returning
                        for (int j = 0; j < i; j++) {
                            free(channel_names[j]);
                        }
                        free(channel_names);
                        cJSON_Delete(json);
                        *num_channels = 0;
                        return NULL;
                    }
                } else {
                    printf("Failed to get name from channel at index %d.\n", i);
                    channel_names[i] = NULL;
                }
            }

            // Cleanup and return the array of channel names
            cJSON_Delete(json);
            return channel_names;
        }
    }

    cJSON_Delete(json);
    return NULL;
}

void free_quark_names(char **quark_names, int num_quarks) {
    // Free the memory for each quark name
    for (int i = 0; i < num_quarks; i++) {
        free(quark_names[i]);
    }
    // Free the array of pointers itself
    free(quark_names);
}
#endif