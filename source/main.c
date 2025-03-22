#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

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
        case 0: //rpc
            printf(cJSON_PrintUnformatted(json_response));
            break;
        case 1: //messageCreate
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
        GW_SendFrame(ws_curl_handle, "{\"event\": \"heartbeat\",\"state\": \"\"}");
        printf("Sent heartbeat!\n");
        svcSleepThread(50*1000000000ULL); // delay 50 seconds x 1 sec in ns
	}
}

int main() {
    gfxInitDefault();
    consoleInit(GFX_BOTTOM, NULL);

    initSocketService();
	atexit(socShutdown);
    curl_global_init(CURL_GLOBAL_DEFAULT);

    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
	C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
	C2D_Prepare();

    LightLock_Init(&MessageWriterLock);

    C3D_RenderTarget* top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    C3D_RenderTarget* bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    
    text_contentBuf  = C2D_TextBufNew(512 * MAX_REND_MESSAGES); // todo figure out dynamic allocation for c2d text buffers
    text_usernameBuf  = C2D_TextBufNew(64 * MAX_REND_MESSAGES); // LQ's max username char count is 64, so 64 x 10*, *MAX_REND_MESSAGES

    
    char *auth_response = curlRequest("https://lightquark.network/v4/auth/token", LOGIN_DATA, NULL); //login
    char *token = parse_response(auth_response, "access_token"); //get token

    printf("Requesting quarks...\n");
    char *quarks_response = curlRequest("https://lightquark.network/v4/quark", NULL, token); //get quarks 
    addQuarksToArray(&joined_quarks, quarks_response);

    CURL *curl_GW_handle = curlUpgradeGateway(GATEWAY_URL); //upgrde to gateway (WebSocket)
    char gw_auth[256];
    sprintf(gw_auth,"{\"event\": \"authenticate\", \"token\": \"%s\", \"state\": \"\"}", token);
    GW_SendFrame(curl_GW_handle, gw_auth); //auth with gateway

    Thread thread_GW_reader = threadCreate(GW_reader_thread, curl_GW_handle, 4 * 1024, 0x2E, -2, false); //start the thread that reads incoming gateway messages
    Thread thread_GW_heartbeat = threadCreate(GW_heartbeat_thread, curl_GW_handle, 1024, 0x2F, -2, false); //start heartbeat thread
    
    float scroll_offset = 0;

    bool channel_select = false;
    while (aptMainLoop()) {
        hidScanInput();
        u32 kDown = hidKeysDown();
        //u32 kDown = hidKeysHeld();
        //u32 kHeld = hidKeysHeld();
        circlePosition CPadPos;
        hidCircleRead(&CPadPos);

        if (kDown & KEY_START) break; // Exit on START button
        if (kDown & KEY_A){
            //for testing
            GW_SendFrame(curl_GW_handle, GW_LQAssembleGetMessages(token, "638b815b4d55b470d9d6fa19", NULL, NULL, 1));
        }
        if (kDown & KEY_B){
        }
        if (kDown & KEY_Y){
        }


        if (abs(CPadPos.dy) >= 15){
            scroll_offset += -1 * (CPadPos.dy / 30);
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

        C2D_TargetClear(top, C2D_Color32(0, 0, 0, 255));
        C2D_SceneBegin(top);

        LightLock_Lock(&MessageWriterLock);
        DrawStructuredMessage(&joined_quarks[selected_quark].channels[entered_selected_channel], MAX_REND_MESSAGES, scroll_offset);
        LightLock_Unlock(&MessageWriterLock);

        DrawStructuredQuarks(joined_quarks, channel_select, selected_quark, selected_channel, entered_selected_channel);

        //C2D_TargetClear(bot, C2D_Color32(0, 0, 0, 255));
        //C2D_SceneBegin(bot);
        //DrawStructuredQuarks(joined_quarks, channel_select, selected_quark, selected_channel, entered_selected_channel);
        
        C3D_FrameEnd(0);
    }
    printf("Exiting...");

    C2D_TextBufDelete(text_contentBuf);
    C2D_TextBufDelete(text_usernameBuf);

    runThreads = false;

    threadJoin(thread_GW_reader, 1000000000);
    threadFree(thread_GW_reader);

    threadJoin(thread_GW_heartbeat, 1000000000);
    threadFree(thread_GW_heartbeat);

    curl_easy_cleanup(curl_GW_handle);

    gfxExit();
    return 0;

}