#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <poll.h>
#include <malloc.h>

#include <unistd.h>

#include <citro2d.h>

#include <curl/curl.h>
#include "cJSON.h"

#include "dataRendering.h"
#include "networking.h"
#include "jsonParsing.h"
#include "socket3ds.h"

#define GATEWAY_URL "https://gw.rem.lightquark.network"

struct Quark *joined_quarks = NULL; // dynamic array for storing joined quarks (and channels) // maybe i should make this global so my functions dont have to play hot potato with it
char selected_channel_id[LQ_IDLENGTH]; // for storing the selected channel id, used to filter websocket messages and stuff

extern char accesstoken[89];

extern size_t joined_quark_count;
extern size_t total_quarks_name_size;

size_t selected_quark = 0;
size_t selected_channel = 0;
int entered_selected_channel = -1;
bool channel_select = false;

int selected_message = -1;
char selected_message_id[LQ_IDLENGTH];

static bool refresh_message_array_parsing = false;

float scroll_offset = 0;

LightLock MessageWriterLock;
LightLock CurlGatewayLock;

CURL *curl_GW_handle;

volatile bool runThreads = false;
void GW_reader_thread(void *ws_curl_handle)
{
    printf("Reader Thread started!\n");
    uint16_t eventnumber = 0;
	while (runThreads)
	{
        char * received_payload = GW_ReceiveFrame(ws_curl_handle);
        if (!received_payload) continue;

        cJSON *json_response = GW_EventSorter(received_payload, &eventnumber);
        free(received_payload);

        if (!json_response) continue;

        switch (eventnumber)
        {
        case 0: //rpc --- --- ---
            //printf(cJSON_PrintUnformatted(json_response));
            cJSON *state = cJSON_GetObjectItem(json_response, "state");
            char *state_token;
            state_token = strtok(state->valuestring, "_");

            
            if (strcmp(state_token, "GetMessages") == 0){ // --- GetMessages ---
                state_token = strtok(NULL, "_");
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
                            refresh_message_array_parsing = true;
                            LightLock_Unlock(&MessageWriterLock);

                            cJSON_Delete(single_message);
                        }
                        break;
                    }
                }
            } else {
                printf("Unknown State String for RPC message\n");
            }

            rpc_abort:
            break;

        case 1: //messageCreate --- --- ---
            cJSON *channelId = cJSON_GetObjectItemCaseSensitive(json_response, "channelId");
            for (int i = 0; i < joined_quarks[selected_quark].channels_count; i++) {
                if (strcmp(channelId->valuestring, joined_quarks[selected_quark].channels[i].channel_id) == 0) {
            
                    LightLock_Lock(&MessageWriterLock);
                    addMessageToArray(&joined_quarks[selected_quark].channels[i], MAX_REND_MESSAGES, json_response);
                    refresh_message_array_parsing = true;
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
            selected_message_id[0] = '\0';
        }
	}
}

bool touchingArea(touchPosition touch, touchPosition target1, touchPosition target2){
    if (touch.px >= target1.px && touch.px <= target2.px && touch.py >= target1.py && touch.py <= target2.py){
        return true;
    } else return false;
}

void log_heap_usage(void) {
    struct mallinfo mi = mallinfo();
    printf("[MEM] arena: %d, ordblks: %d, uordblks: %d, fordblks: %d\n", mi.arena, mi.ordblks, mi.uordblks, mi.fordblks);
}

int main() {
    gfxInitDefault();
    consoleInit(GFX_BOTTOM, NULL);
    printf("Console Initialized!\n");

    initSocketService();
	atexit(socShutdown);
    curl_global_init(CURL_GLOBAL_DEFAULT);

    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
	C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
	C2D_Prepare();

    LightLock_Init(&MessageWriterLock);
    LightLock_Init(&CurlGatewayLock);

    C3D_RenderTarget* topScreen = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    C3D_RenderTarget* botScreen = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

    Thread thread_GW_reader = NULL;
    Thread thread_GW_heartbeat = NULL;
    Thread thread_messageSender = NULL;

    touchPosition target1; target1.px = 1; target1.py = 1;
    touchPosition target2; target2.px = 320; target2.py = 50;

    loginState loginState = LOGIN_STATE_ATTEMPT; 
    while (aptMainLoop()) {
        hidScanInput();
        u32 kDown = hidKeysDown();
        //u32 kDown = hidKeysHeld();
        u32 kHeld = hidKeysHeld();
        circlePosition CPadPos;
        hidCircleRead(&CPadPos);
        
        if (kHeld & KEY_SELECT && !runThreads) { //idk runthreads is used as a way to detect if the user logged in already. To prevent funkiness only hold select to log out/blank login as soon as the app starts
            printf("Manual Blank Login requested\n");
            loginState = LOGIN_STATE_BLANK;
        }
        while (loginState != LOGIN_STATE_DONE){ // --- LOGIN SCREEN ---
            LQLoginScreen(&loginState, &joined_quarks, topScreen);
            printf("LOGIN_STATE: %i\n", loginState);
            if (loginState == LOGIN_STATE_EXIT) goto exit_redshift;
        }
        
        if (!runThreads){
            runThreads = true;
            svcCreateEvent(&threadMessageSendRequest,0);
            thread_GW_reader = threadCreate(GW_reader_thread, curl_GW_handle, 4 * 1024, 0x2F, -2, false); //start the thread that reads incoming gateway messages
            thread_GW_heartbeat = threadCreate(GW_heartbeat_thread, curl_GW_handle, 2 * 1024, 0x3F, -2, false); //start heartbeat thread
            thread_messageSender = threadCreate(messageSender_thread, 0, 6 * 1024, 0x18, -2, true); //initialize message sender thread //todo, maybe check for new 3ds and banish this to another core, to hopefully increase performance?
        }

        if (kDown & KEY_START) break; // Exit on START button

        touchPosition touch;
        hidTouchRead(&touch);

        //its way too overcomplicated to use structures for the touch positions, idk what i was thinking
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
                if (selected_message_id[0] != '\0'){
                    threadsenddata.replyto = selected_message_id;
                } else threadsenddata.replyto = NULL;
                svcSignalEvent(threadMessageSendRequest);
            }
        }

        if (kDown & KEY_A){
            log_heap_usage();
        }
        if (kDown & KEY_B){
        }
        if (kDown & KEY_X){
            if (selected_message > -1) strcpy(selected_message_id, joined_quarks[selected_quark].channels[entered_selected_channel].messages[selected_message].message_id);
        }
        if (kDown & KEY_Y){
        }

        // Message Scrolling
        if (abs(CPadPos.dy) >= 15 && entered_selected_channel >= 0){ 
            struct Channel *channel = &joined_quarks[selected_quark].channels[entered_selected_channel];
            if (channel->total_message_height > 0){
                scroll_offset += (CPadPos.dy / 20.0f);
                if (scroll_offset < 0.0f) scroll_offset = 0.0f;
                int start_index = (channel->message_index - channel->total_messages + MAX_REND_MESSAGES) % MAX_REND_MESSAGES; //the oldest message

                float max_scroll = channel->total_message_height - channel->messages[start_index].content_totalpadding_height;
                if (scroll_offset > max_scroll) scroll_offset = max_scroll;

                for (int i = 0; i <= channel->total_messages; ++i) {
                    int msg_index = (start_index + i + MAX_REND_MESSAGES) % MAX_REND_MESSAGES;
                    float start = channel->messages[msg_index].content_message_start;
                    float end = start - channel->messages[msg_index].content_totalpadding_height;

                    if (scroll_offset <= start && scroll_offset >= end) {
                        selected_message = msg_index;
                    }
                }
            }
        }

        // Message selection nudging
        if (kDown & KEY_ZL){
            selected_message = (selected_message + 1 + MAX_REND_MESSAGES) % MAX_REND_MESSAGES;
        }
        if (kDown & KEY_ZR){
            selected_message = (selected_message - 1 + MAX_REND_MESSAGES) % MAX_REND_MESSAGES;
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
                // if user is in channel select mode, which means they just selected a channel to be "entered"
                entered_selected_channel = selected_channel;
                if (joined_quarks[selected_quark].channels_count > 0){
                    // get recent messages in selected channel if the quark has channels
                    strcpy(selected_channel_id, joined_quarks[selected_quark].channels[entered_selected_channel].channel_id);
                    char *getmessagerequest = GW_LQAssembleGetMessages(accesstoken, selected_channel_id, NULL, NULL, MAX_REND_MESSAGES);
                    GW_SendFrame(curl_GW_handle, getmessagerequest);
                    free(getmessagerequest); 
                }
                refresh_message_array_parsing = true; // tells the message rendering to parse the new messages below
                scroll_offset = 0; // reset scrolling
                selected_message = -1; // un-select a selected message if one has been selected previously
                selected_message_id[0] = '\0'; // clear selected message id
            } else {
                // the user is NOT selecting a channel, so they just selected a quark instead
                channel_select = true;
                entered_selected_channel = -1;
            }
        }
        if (kDown & KEY_DLEFT){
            // this exits "channel select" mode returning to quark select mode
            channel_select = false;
            selected_channel = 0;
            entered_selected_channel = -1;
        }

        // --- Rendering and messsage handling things ---

        if (refresh_message_array_parsing == true){
            LightLock_Lock(&MessageWriterLock);
            ParseTextMessages(&joined_quarks[selected_quark].channels[entered_selected_channel]);
            LightLock_Unlock(&MessageWriterLock);
            refresh_message_array_parsing = false;
        }
        
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

        //*
        C2D_TargetClear(topScreen, C2D_Color32(0, 0, 0, 255));
        C2D_SceneBegin(topScreen);

        LightLock_Lock(&MessageWriterLock);
        if (entered_selected_channel > -1) DrawTextMessages(&joined_quarks[selected_quark].channels[entered_selected_channel], scroll_offset, selected_message);
        LightLock_Unlock(&MessageWriterLock);

        //*/

        DrawStructuredQuarks(joined_quarks, channel_select, selected_quark, selected_channel, entered_selected_channel);

        /*
        C2D_TargetClear(botScreen, C2D_Color32(0, 0, 0, 255));
        C2D_SceneBegin(botScreen);
        DrawStructuredQuarks(joined_quarks, channel_select, selected_quark, selected_channel, entered_selected_channel);
        //*/
        C3D_FrameEnd(0);
    }
    exit_redshift:
    printf("Exiting...\n");

    Buf_C2D_Cleanup();

    runThreads = false;

    threadJoin(thread_GW_reader, 0.5 * 1000000000); // 1000000000 Nanoseconds = 1 Second // i mean... do i even need to wait for threads... clearly the timeout will always get reached because my threads take at most 40 seconds to finish
    if (thread_GW_reader) threadFree(thread_GW_reader);

    threadJoin(thread_GW_heartbeat, 0.5 * 1000000000);
    if (thread_GW_heartbeat) threadFree(thread_GW_heartbeat);

    threadJoin(thread_messageSender, 0.5 * 1000000000);
    if (thread_messageSender) threadFree(thread_messageSender);

    curl_easy_cleanup(curl_GW_handle);
    svcCloseHandle(threadMessageSendRequest);

    gfxExit();
    return 0;

}