#ifndef JSONPARSING_H
#define JSONPARSING_H

char *GW_LQAssembleGetMessages(char *usertoken, char *channelid, uint64_t *beforeTimestamp, uint64_t *afterTimestamp, uint16_t limit);

cJSON *GW_EventSorter(const char *json_response, uint16_t *eventnumber);

char *parseResponse(const char *json_string, const char* json_object);

#endif