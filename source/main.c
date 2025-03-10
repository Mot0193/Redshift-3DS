#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

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

void printMessageAtIndex(struct MessageStructure *recent_messages, int index) {
    // quick ai-made function for debugging, bite me. Printing is boring
    printf("=== Message at Index %d ===\n", index);
    
    // Message details
    printf("Message ID: %s\n", recent_messages[index].message_id);
    printf("Content: %s\n", recent_messages[index].content ? recent_messages[index].content : "(NULL)");
    printf("User-Agent: %s\n", recent_messages[index].ua ? recent_messages[index].ua : "(NULL)");
    printf("Timestamp: %llu\n", recent_messages[index].timestamp);
    printf("Edited: %s\n", recent_messages[index].edited ? "true" : "false");

    // Author details
    printf("Author ID: %s\n", recent_messages[index].author_id);
    printf("Username: %s\n", recent_messages[index].username ? recent_messages[index].username : "(NULL)");
    printf("Admin: %s\n", recent_messages[index].admin ? "true" : "false");
    printf("Bot: %s\n", recent_messages[index].isbot ? "true" : "false");
    printf("SecretThirdThing: %s\n", recent_messages[index].secretThirdThing ? "true" : "false");
    printf("Avatar URI: %s\n", recent_messages[index].avatarUri ? recent_messages[index].avatarUri : "(NULL)");
    
    // Channel ID
    printf("Channel ID: %s\n", recent_messages[index].channelId);

     // Attachments (only print if they exist)
     if (recent_messages[index].attachment_count > 0) {
        printf("Attachments (%d):\n", recent_messages[index].attachment_count);
        for (int i = 0; i < recent_messages[index].attachment_count; i++) {
            if (recent_messages[index].attachments[i].url) 
                printf("  [%d] URL: %s\n", i, recent_messages[index].attachments[i].url);
            if (recent_messages[index].attachments[i].type) 
                printf("      Type: %s\n", recent_messages[index].attachments[i].type);
            if (recent_messages[index].attachments[i].filename) 
                printf("      Filename: %s\n", recent_messages[index].attachments[i].filename);
            if (recent_messages[index].attachments[i].size > 0) 
                printf("      Size: %d bytes\n", recent_messages[index].attachments[i].size);
            if (recent_messages[index].attachments[i].width > 0 && recent_messages[index].attachments[i].height > 0) 
                printf("      Dimensions: %dx%d\n", recent_messages[index].attachments[i].width, recent_messages[index].attachments[i].height);
        }
    }

    // Special Attributes (only print if they exist)
    if (recent_messages[index].specialAttribute_count > 0) {
        printf("Special Attributes (%d):\n", recent_messages[index].specialAttribute_count);
        for (int i = 0; i < recent_messages[index].specialAttribute_count; i++) {
            if (recent_messages[index].specialAttributes[i].type) 
                printf("  [%d] Type: %s\n", i, recent_messages[index].specialAttributes[i].type);
            if (recent_messages[index].specialAttributes[i].username) 
                printf("      Username: %s\n", recent_messages[index].specialAttributes[i].username);
            if (recent_messages[index].specialAttributes[i].avatarUri) 
                printf("      Avatar URI: %s\n", recent_messages[index].specialAttributes[i].avatarUri);
            if (recent_messages[index].specialAttributes[i].replyTo[0] != '\0') 
                printf("      Reply To: %s\n", recent_messages[index].specialAttributes[i].replyTo);
            if (recent_messages[index].specialAttributes[i].discordMessageId != 0) 
                printf("      Discord Message ID: %llu\n", recent_messages[index].specialAttributes[i].discordMessageId);
            if (recent_messages[index].specialAttributes[i].quarkcord) 
                printf("      Quarkcord: true\n");
            if (recent_messages[index].specialAttributes[i].plaintext) 
                printf("      Plaintext: %s\n", recent_messages[index].specialAttributes[i].plaintext);
        }
    }

    printf("Content Line Number: %d\n", recent_messages[index].content_line_number);
    printf("===========================\n");
}

void printQuarkStruct(struct Quark *quarks, int quark_count) {
    printf("Displaying Quark Information:\n");
    
    for (int i = 0; i < quark_count; i++) {
        struct Quark *q = &quarks[i];
        printf("\nQuark #%d:\n", i + 1);
        printf("  ID: %s\n", q->quark_id);
        printf("  Name: %s\n", q->name);
        printf("  Icon URI: %s\n", q->iconUri ? q->iconUri : "None");
        printf("  Invite Link: %s\n", q->inviteEnabled ? q->invite : "Invites Disabled");

        // Display Owners
        if (q->owners_count > 0 && q->owners != NULL) {
            printf("  Owners (%d): ", q->owners_count);
            for (int j = 0; j < q->owners_count; j++) {
                printf("%s%s", q->owners[j], (j < q->owners_count - 1) ? ", " : "");
            }
            printf("\n");
        } else {
            printf("  Owners: None\n");
        }

        // Display Channels
        printf("  Channels (%d):\n", q->channels_count);
        if (q->channels_count > 0 && q->channels != NULL) {
            for (int k = 0; k < q->channels_count; k++) {
                struct Channel *c = &q->channels[k];
                printf("    - [%s] %s\n", c->channel_id, c->name);
                printf("      Description: %s\n", c->description ? c->description : "No description");
            }
        } else {
            printf("    No channels available.\n");
        }
    }
}

volatile bool runThreads = true;
void WS_reader_thread(void *ws_curl_handle)
{
    printf("Reader Thread started!\n");
	while (runThreads)
	{
        char * received_payload = receive_websocket_frame(ws_curl_handle);
        if (received_payload != NULL){
            cJSON *json_response = jsonGatewayReader(received_payload);

            if (json_response != NULL){
                cJSON *channelId = cJSON_GetObjectItemCaseSensitive(json_response, "channelId");

                for (int i = 0; i < joined_quarks[selected_quark].channels_count; i++){

                    if (strcmp(channelId->valuestring, joined_quarks[selected_quark].channels[i].channel_id) == 0){

                        //if (!joined_quarks[selected_quark].channels[i].message_index) joined_quarks[selected_quark].channels[i].message_index = 0;
                        // changed to using calloc when allocating memory for channels instead of doing this

                        addMessageToArray(&joined_quarks[selected_quark].channels[i], MAX_REND_MESSAGES, json_response);
                        break;
                    }
                }
            }
            
            free(received_payload);
        } else {
            printf("Something happened when receiving websocket frame.\n"); //very decriptive error message
        }
	}
}

void WS_heartbeat_thread(void *ws_curl_handle){
    printf("Heartbeat Thread started!\n");
	while (runThreads)
	{
        send_websocket_frame(ws_curl_handle, "{\"event\": \"heartbeat\",\"state\": \"\"}");
        printf("Sent heartbeat!\n");
        svcSleepThread(50*1000000000ULL); // delay 50 seconds x 1 sec in ns
	}
}

int main() {
    gfxInitDefault();
    //consoleInit(GFX_TOP, NULL);

    initSocketSerive();
	atexit(socShutdown);
    curl_global_init(CURL_GLOBAL_DEFAULT);

    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
	C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
	C2D_Prepare();

    C3D_RenderTarget* top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    C3D_RenderTarget* bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    
    text_contentBuf  = C2D_TextBufNew(512 * MAX_REND_MESSAGES); // todo figure out dynamic allocation for c2d text buffers
    text_usernameBuf  = C2D_TextBufNew(64 * MAX_REND_MESSAGES); // LQ's max username char count is 64, so 64 x 10*, *MAX_REND_MESSAGES

    
    char *auth_response = curlRequest("https://lightquark.network/v4/auth/token", LOGIN_DATA, NULL); //login
    char *token = parse_response(auth_response, "access_token"); //get token

    printf("Requesting quarks...\n");
    char *quarks_response = curlRequest("https://lightquark.network/v4/quark", NULL, token);
    addQuarksToArray(&joined_quarks, quarks_response);

    CURL *curl_WS_handle = curlUpgradeGateway(GATEWAY_URL); //upgrde to gateway (WebSocket)
    char ws_auth[256];
    sprintf(ws_auth,"{\"event\": \"authenticate\", \"token\": \"%s\", \"state\": \"\"}", token);
    send_websocket_frame(curl_WS_handle, ws_auth); //auth with gateway

    Thread thread_WS_reader = threadCreate(WS_reader_thread, curl_WS_handle, 4096, 0x2E, -2, false); //start the thread that reads incoming gateway messages
    Thread thread_WS_heartbeat = threadCreate(WS_heartbeat_thread, curl_WS_handle, 1024, 0x2F, -2, false); //start heartbeat thread

    float scroll_offset = 0;

    bool channel_select = false;
    while (aptMainLoop()) {
        hidScanInput();
        u32 kDown = hidKeysDown();
        //u32 kHeld = hidKeysHeld();
        circlePosition CPadPos;
        hidCircleRead(&CPadPos);

        if (kDown & KEY_START) break; // Exit on START button
        if (kDown & KEY_A){
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
        DrawStructuredMessage(&joined_quarks[selected_quark].channels[entered_selected_channel], MAX_REND_MESSAGES, scroll_offset);

        C2D_TargetClear(bot, C2D_Color32(0, 0, 0, 255));
        C2D_SceneBegin(bot);
        DrawStructuredQuarks(joined_quarks, channel_select, selected_quark, selected_channel, entered_selected_channel);
        
        C3D_FrameEnd(0);
    }
    printf("Exiting...");

    C2D_TextBufDelete(text_contentBuf);
    C2D_TextBufDelete(text_usernameBuf);

    runThreads = false;
    curl_easy_cleanup(curl_WS_handle);
    curl_WS_handle = NULL;

    threadJoin(thread_WS_reader, 1000000000);
    threadFree(thread_WS_reader);

    threadJoin(thread_WS_heartbeat, 1000000000);
    threadFree(thread_WS_heartbeat);

    gfxExit();
    return 0;

}