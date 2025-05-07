#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#include <unistd.h>

#include <citro2d.h>

#include <curl/curl.h>
#include "cJSON.h"

#include "MessageRendering.h"
#include "curlrequests.h"
#include "jsonparsing.h"
#include "socket3ds.h"

#define GATEWAY_URL "https://gw.ram.lightquark.network"
#define LOGIN_DATA "{\"email\": \"testuser@litdevs.org\",\"password\": \"wordpass\"}"

//struct MessageStructure recent_messages[MAX_REND_MESSAGES] = {0}; // array for storing message structs
struct Quark *joined_quarks = NULL; // dynamic array for storing joined quarks (and channels)
char selected_channel_id[LQ_IDLENGTH]; // for storing the selected channel id, used to filter websocket messages and stuff

size_t selected_quark = 0;
size_t selected_channel = 0;
size_t entered_selected_channel = 0;
    
LightLock MessageWriterLock;
LightLock CurlGatewayLock;

FILE *loginfile;
char accesstoken[89];
char refreshtoken[91];
CURL *curl_GW_handle;

int LightquarkLogin(){
    int login_retry = 0;
    long httpcodecurl = 0;

    if (mkdir("/3ds/redshift", 0775) == -1){
        if (errno != EEXIST) {
            perror("Failed to create /3ds/redshift");
            return -1;
        }
    }
    perror("/3ds/redshift Directory status");

    // --- ---- ---
    attempt_login:
    loginfile = fopen("/3ds/redshift/logindata.txt", "r");
    if (loginfile == NULL) {
        goto blank_login;
    }

    fgets(accesstoken, sizeof(accesstoken), loginfile);
    accesstoken[strcspn(accesstoken, "\n")] = '\0';
    printf("Access Token saved from file: %s\n", accesstoken);

    char *quarks_response = curlRequest("https://lightquark.network/v4/quark", NULL, accesstoken, &httpcodecurl); //get quarks
    printf("Quark Response HTTP code: %li\n", httpcodecurl);
    if (httpcodecurl != 200){
        goto refresh_login; // if code is not ok, aka most likely 401, refresh token
    }
    if (quarks_response == NULL) printf("Uh oh quarks response is null\n");

    addQuarksToArray(&joined_quarks, quarks_response);
    free(quarks_response);

    curl_GW_handle = curlUpgradeGateway(GATEWAY_URL); //upgrde to gateway (WebSocket)
    if (curl_GW_handle == NULL){
        printf("Gateway upgrade failed\n");
        goto refresh_login;
    }
    char gw_auth[256];
    sprintf(gw_auth,"{\"event\": \"authenticate\", \"token\": \"%s\", \"state\": \"\"}", accesstoken);
    GW_SendFrame(curl_GW_handle, gw_auth); //auth with gateway
    

    fclose(loginfile);
    return 0;

    // --- --- --- 
    refresh_login:

    printf("Refreshing Token...\n");

    fgets(refreshtoken, sizeof(refreshtoken), loginfile);
    fgets(refreshtoken, sizeof(refreshtoken), loginfile);
    refreshtoken[strcspn(refreshtoken, "\n")] = '\0';
    printf("Refresh Token saved from file: %s\n", refreshtoken);
    fclose(loginfile);
    

    char refreshtokenrequest[256];
    sprintf(refreshtokenrequest, "{\"accessToken\": \"%s\", \"refreshToken\": \"%s\"}", accesstoken, refreshtoken);
    
    char *refresh_response = curlRequest("https://lightquark.network/v4/auth/refresh", refreshtokenrequest, NULL, &httpcodecurl);
    if (httpcodecurl != 200 || refresh_response == NULL){
        printf("Failed to refresh tokens\n"); // if code is not ok, aka most likely 401, start blank login
        goto blank_login;
    }

    char *ACtoken_refresh = parseResponse(refresh_response, "accessToken");
    free(refresh_response);
    printf("Refreshed AC: %s\n", ACtoken_refresh);
    if (ACtoken_refresh == NULL){
        printf("Failed to refresh ACtoken. Starting blank re-login...\n");
        goto blank_login;
    }
    printf("Refreshed AC: %s\n", ACtoken_refresh);

    loginfile = fopen("/3ds/redshift/logindata.txt", "a");
    fseek(loginfile, 0, SEEK_SET);
    fprintf(loginfile,"%s\n", ACtoken_refresh);
    free(ACtoken_refresh);

    fclose(loginfile);
    goto attempt_login;

    // --- --- --- 
    blank_login:
    login_retry++;
    if (login_retry > 2){
        printf("Too many attempts to log in, bye bye\n");
        return -1;
    }

    printf("Creating logindata file...\n");

    loginfile = fopen("/3ds/redshift/logindata.txt", "w");
    if (loginfile == NULL) {
        perror("Error creating logindata file");
        return -1;
    }

    printf("Requesting Tokens...\n");
    char *auth_response = curlRequest("https://lightquark.network/v4/auth/token", LOGIN_DATA, NULL, &httpcodecurl); //request login

    printf("Parsing tokens...\n");
    char *ACtoken = parseResponse(auth_response, "access_token"); //get token(s)
    char *REtoken = parseResponse(auth_response, "refresh_token");
    free(auth_response);

    if (ACtoken && REtoken) {
        printf("AC: %s\n", ACtoken);
        printf("RE: %s\n", REtoken);

        fprintf(loginfile, "%s\n", ACtoken);
        fprintf(loginfile, "%s\n", REtoken);
        printf("Done writing tokens to file.\n");

        free(ACtoken);
        free(REtoken);
        fclose(loginfile);
        goto attempt_login;
    } else {
        printf("Failed to parse login tokens.\n");
        fclose(loginfile);
        return -1;
    }

    fclose(loginfile);
    return 0;
}

volatile bool runThreads = true;
void GW_reader_thread(void *ws_curl_handle)
{
    printf("Reader Thread started!\n");
    uint16_t eventnumber = 0;
	while (runThreads)
	{
        char * received_payload = GW_ReceiveFrame(ws_curl_handle);
        if (!received_payload) continue;

        cJSON *json_response = GW_EventReader(received_payload, &eventnumber);
        free(received_payload);

        if (!json_response) continue;

        switch (eventnumber)
        {
        case 0: //rpc --- --- ---
            //printf(cJSON_PrintUnformatted(json_response));
            cJSON *state = cJSON_GetObjectItem(json_response, "state");

            char *state_token;
            state_token = strtok(state->valuestring, ".");

            
            if (strcmp(state_token, "GetMessages") != 0){
                printf("Unknown State String for RPC message\n");
                goto rpc_abort;
            }
            state_token = strtok(NULL, ".");
            printf("State Thing Id: %s\n", state_token);

            cJSON *body = cJSON_GetObjectItem(json_response, "body");
            cJSON *response = cJSON_GetObjectItem(body, "response");
            cJSON *messages = cJSON_GetObjectItem(response, "messages");
            int messages_arraysize = cJSON_GetArraySize(messages);
            if (!messages || !cJSON_IsArray(messages) || messages_arraysize == 0) {
                printf("RPC Messages array doesn't exist or it's empty wtf\n");
                goto rpc_abort;
            }

            for (int i = 0; i < joined_quarks[selected_quark].channels_count; i++) {

                if (strcmp(state_token, joined_quarks[selected_quark].channels[i].channel_id) == 0) {

                    for (int j = messages_arraysize-1; j >= 0; j--){
                        // is this too overcomplicated i dont know. I feel like i could have a seperate addmessages function instead of trying to use the same function by making a compatible json obect
                        cJSON *message = cJSON_GetArrayItem(messages, j);
                        if (!message) {
                            printf("Message at index %d is NULL!\n", j);
                            continue;
                        }

                        cJSON *single_message = cJSON_CreateObject();
                        cJSON_AddItemToObject(single_message, "message", cJSON_Duplicate(message, 1));
                        cJSON *channelId = cJSON_CreateString(joined_quarks[selected_quark].channels[i].channel_id);
                        cJSON_AddItemToObject(single_message, "channelId", channelId);

                        LightLock_Lock(&MessageWriterLock);
                        addMessageToArray(&joined_quarks[selected_quark].channels[i], MAX_REND_MESSAGES, single_message);
                        LightLock_Unlock(&MessageWriterLock);

                        cJSON_Delete(single_message);
                    }
                    break;
                }
            }

            rpc_abort:
            break;

        case 1: //messageCreate --- --- ---
            cJSON *channelId = cJSON_GetObjectItemCaseSensitive(json_response, "channelId");
            for (int i = 0; i < joined_quarks[selected_quark].channels_count; i++) {
                if (strcmp(channelId->valuestring, joined_quarks[selected_quark].channels[i].channel_id) == 0) {
            
                    LightLock_Lock(&MessageWriterLock);
                    addMessageToArray(&joined_quarks[selected_quark].channels[i], MAX_REND_MESSAGES, json_response);
                    LightLock_Unlock(&MessageWriterLock);
                    break;
                }
            }
            break;
        default:
            break;
        }
        

        cJSON_Delete(json_response);
	}
}

void GW_heartbeat_thread(void *ws_curl_handle){
    printf("Heartbeat Thread started!\n");
	while (runThreads)
	{   
        LightLock_Lock(&CurlGatewayLock); //idk???
        GW_SendFrame(ws_curl_handle, "{\"event\": \"heartbeat\",\"state\": \"\"}");
        LightLock_Unlock(&CurlGatewayLock);
        printf("Sent heartbeat!\n");
        svcSleepThread(50*1000000000ULL); // delay 50 seconds x 1 sec in ns
	}
}

struct messageSendData {
    char *accesstoken;
    char *selectedchannelid;
    char *sendingmessage_buffer;
    char *replyto;
} threadsenddata;
Handle threadMessageSendRequest;
void messageSender_thread(void *arg){
    while(runThreads) {
		svcWaitSynchronization(threadMessageSendRequest, U64_MAX);
		svcClearEvent(threadMessageSendRequest);

        long messageSend_httpcode;
        const char *accesstoken = threadsenddata.accesstoken;
        const char *channelid = threadsenddata.selectedchannelid;
        char *message = threadsenddata.sendingmessage_buffer;
        const char *replyto = threadsenddata.replyto;

        if (message != NULL) {
            messageSend_httpcode = lqSendmessage(accesstoken, channelid, message, replyto);
            if (messageSend_httpcode != 201) {
                printf("Message sending failed... HTTP code: %li\n", messageSend_httpcode);
            } else {
                printf("Sent message: %s\n", message);
            }
            free(message);
            threadsenddata.sendingmessage_buffer = NULL;
        }
	}
}

bool touchingArea(touchPosition touch, touchPosition target1, touchPosition target2){
    if (touch.px >= target1.px && touch.px <= target2.px && touch.py >= target1.py && touch.py <= target2.py){
        return true;
    } else return false;
}

int main() {
    gfxInitDefault();
    //consoleInit(GFX_TOP, NULL);

    initSocketService();
	atexit(socShutdown);
    curl_global_init(CURL_GLOBAL_DEFAULT);

    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
	C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
	C2D_Prepare();

    LightLock_Init(&MessageWriterLock);
    LightLock_Init(&CurlGatewayLock);

    

    C3D_RenderTarget* top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    C3D_RenderTarget* bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    
    text_contentBuf  = C2D_TextBufNew(512 * MAX_REND_MESSAGES); // todo figure out dynamic allocation for c2d text buffers
    text_usernameBuf  = C2D_TextBufNew(64 * MAX_REND_MESSAGES); // LQ's max username char count is 64, so 64 x 10*, *MAX_REND_MESSAGES

    if (LightquarkLogin() != 0){
        usleep(1000 * 1000);
        goto exit_redshift;
    }

    svcCreateEvent(&threadMessageSendRequest,0);
    Thread thread_GW_reader = threadCreate(GW_reader_thread, curl_GW_handle, 4 * 1024, 0x2F, -2, false); //start the thread that reads incoming gateway messages
    Thread thread_GW_heartbeat = threadCreate(GW_heartbeat_thread, curl_GW_handle, 2 * 1024, 0x3F, -2, false); //start heartbeat thread
    Thread thread_messageSender = threadCreate(messageSender_thread, 0, 6 * 1024, 0x18, -2, true); //initialize message sender thread //todo, maybe check for new 3ds and banish this to another core, to hopefully increase performance?

    float scroll_offset = 0;
    bool channel_select = false;

    touchPosition target1; target1.px = 1; target1.py = 1;
    touchPosition target2; target2.px = 320; target2.py = 50;
    while (aptMainLoop()) {
        hidScanInput();
        u32 kDown = hidKeysDown();
        //u32 kDown = hidKeysHeld();
        //u32 kHeld = hidKeysHeld();
        circlePosition CPadPos;
        hidCircleRead(&CPadPos);

        if (kDown & KEY_START) break; // Exit on START button

        touchPosition touch;
        hidTouchRead(&touch);

        if (touchingArea(touch, target1, target2) == true){
            SwkbdState swkbd;
            char sendingmessage_buffer[1024];
            SwkbdButton button = SWKBD_BUTTON_NONE;

            swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, -1);
            swkbdSetHintText(&swkbd, "Send message in selected channel");
            swkbdSetButton(&swkbd, SWKBD_BUTTON_LEFT, "Cancel", false);
            swkbdSetButton(&swkbd, SWKBD_BUTTON_RIGHT, "Send Message", true);
            swkbdSetFeatures(&swkbd, SWKBD_PREDICTIVE_INPUT);
            swkbdSetValidation(&swkbd, SWKBD_ANYTHING, 1, sizeof(sendingmessage_buffer));
            button = swkbdInputText(&swkbd, sendingmessage_buffer, sizeof(sendingmessage_buffer)); 

            // if button = "ok" and the string is not empty
            if (button == SWKBD_BUTTON_RIGHT && sendingmessage_buffer[0] != '\0'){
                threadsenddata.accesstoken = accesstoken;
                threadsenddata.selectedchannelid = selected_channel_id;
                threadsenddata.sendingmessage_buffer = strdup(sendingmessage_buffer);
                //threadsenddata.replyto = NULL;
                svcSignalEvent(threadMessageSendRequest);
            }
        }

        if (kDown & KEY_A){
            // get recent messages in selected channel on A press
            char *getmessagerequest = GW_LQAssembleGetMessages(accesstoken, selected_channel_id, NULL, NULL, 10);
            GW_SendFrame(curl_GW_handle, getmessagerequest);
            free(getmessagerequest);
            
        }
        if (kDown & KEY_B){
        }
        if (kDown & KEY_Y){
        }


        if (abs(CPadPos.dy) >= 15){
            scroll_offset += (CPadPos.dy / 24);
        }

        
        // --- DPAD Controls (for channel/quark selection) ---
        if (kDown & KEY_DUP){
            if (channel_select){
                selected_channel = (selected_channel <= 0 ? 0 : selected_channel - 1);
            } else {
                selected_quark = (selected_quark <= 0 ? 0 : selected_quark - 1);
            }
        }
        if (kDown & KEY_DDOWN){
            if (channel_select){
                selected_channel = (selected_channel >= joined_quarks[selected_quark].channels_count-1 ? joined_quarks[selected_quark].channels_count-1 : selected_channel + 1);
            } else {
                selected_quark = (selected_quark >= joined_quark_count-1 ? joined_quark_count-1 : selected_quark + 1); //joined_quark_count-1 because joined qurk starts at 1 not 0
            }
        }
        if (kDown & KEY_DRIGHT){
            if (channel_select){
                entered_selected_channel = selected_channel;
            } else {
                channel_select = true;
            }
            if (joined_quarks[selected_quark].channels_count>0) strcpy(selected_channel_id, joined_quarks[selected_quark].channels[entered_selected_channel].channel_id);
        }
        if (kDown & KEY_DLEFT){
            channel_select = false;
            selected_channel = 0;
            entered_selected_channel = 0; 
        }
        
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

        //*
        C2D_TargetClear(top, C2D_Color32(0, 0, 0, 255));
        C2D_SceneBegin(top);

        LightLock_Lock(&MessageWriterLock);
        DrawStructuredMessage(&joined_quarks[selected_quark].channels[entered_selected_channel], MAX_REND_MESSAGES, scroll_offset);
        LightLock_Unlock(&MessageWriterLock);
        //*/

        //DrawStructuredQuarks(joined_quarks, channel_select, selected_quark, selected_channel, entered_selected_channel);

        C2D_TargetClear(bot, C2D_Color32(0, 0, 0, 255));
        C2D_SceneBegin(bot);
        DrawStructuredQuarks(joined_quarks, channel_select, selected_quark, selected_channel, entered_selected_channel);
        
        C3D_FrameEnd(0);
    }
    exit_redshift:
    printf("Exiting...");

    C2D_TextBufDelete(text_contentBuf);
    C2D_TextBufDelete(text_usernameBuf);

    runThreads = false;

    threadJoin(thread_GW_reader, 1000000000); // 1000000000 Nanoseconds = 1 Second // i mean... do i even need to wait for threads... clearly the timeout will always get reached because my threads take at most 40 seconds to finish
    
    threadFree(thread_GW_reader);

    threadJoin(thread_GW_heartbeat, 1000000000);
    threadFree(thread_GW_heartbeat);

    threadJoin(thread_messageSender, 1000000000);

    curl_easy_cleanup(curl_GW_handle);
    svcCloseHandle(threadMessageSendRequest);

    gfxExit();
    return 0;

}