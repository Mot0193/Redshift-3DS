#ifndef NETWORKING_H
#define NETWORKING_H

#include <stddef.h>
#include <stdint.h>
#include <curl/curl.h>
#include "cJSON.h"

#define CLIENT_NAME "Redshift3DS"
#define GATEWAY_URL "https://gw.rem.lightquark.network"

typedef enum LoginState {
    LOGIN_STATE_ATTEMPT,
    LOGIN_STATE_REFRESH,
    LOGIN_STATE_BLANK,
    LOGIN_STATE_DONE,
    LOGIN_STATE_FAILED,
    LOGIN_STATE_EXIT
} loginState;

struct Quark;

char *curlRequest(const char* url, const char* postdata, const char* token, long* httpcodeout);

long lqSendmessage(const char* token, const char* channelid, const char* message, const char* replyto);

// --- [Gateway stuff] ---

void print_frame_binary(uint8_t *frame, size_t size);

CURL *curlUpgradeGateway(char *gateway_url);

void generate_mask(uint8_t mask[4]);

void GW_SendLargeFrame(CURL *curl, const char *message);

void GW_SendFrame(CURL *curl, const char *message);

void unmask_data(uint8_t *data, size_t data_len, uint8_t *mask);

void curl_PollRecv(CURL *curl, void *buffer, size_t buflen);

char *GW_ReceiveFrame(CURL *curl);

loginState LightquarkLogin(loginState loginState, char *email, char *password, struct Quark **joined_quarks);

#endif