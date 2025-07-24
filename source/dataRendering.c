#include <stdlib.h>
#include <errno.h>
#include <citro2d.h>
#include <curl/curl.h>
#include <unistd.h>

#include "cJSON.h"
#include "networking.h"
#include "dataRendering.h"

// Text buffers
C2D_TextBuf buftxt_messageContent[MAX_REND_MESSAGES], buftxt_messageUsername[MAX_REND_MESSAGES], buftxt_attachments[MAX_REND_MESSAGES];

// Text objects
C2D_Text txt_messageContent[MAX_REND_MESSAGES], txt_messageUsername[MAX_REND_MESSAGES], txt_attachments[MAX_REND_MESSAGES];

extern LightLock MessageWriterLock;

size_t joined_quark_count = 0;
size_t total_quarks_name_size = 0; // to get the length of all quark names, for allocating the C2D text buffer


// --- [ Quark/Channel Functions ] ---
void freeQuarks(struct Quark **joined_quarks){
    if (joined_quarks == NULL || *joined_quarks == NULL) {
        printf("QuarkArray is NULL, wont free\n");
        return;
    }
    for (int i = 0; i < joined_quark_count; i++){
        free((*joined_quarks)[i].name);
        free((*joined_quarks)[i].iconUri);
        free((*joined_quarks)[i].invite);

        for (int j = 0; j < (*joined_quarks)[i].owners_count; j++){
            free((*joined_quarks)[i].owners[j]);
        }
        free((*joined_quarks)[i].owners);

        for(int j = 0; j < (*joined_quarks)[i].channels_count; j++){
            free((*joined_quarks)[i].channels[j].name);
            free((*joined_quarks)[i].channels[j].description);
        }

        free((*joined_quarks)[i].channels);
    }

    free(*joined_quarks);
    *joined_quarks = NULL;
}

void addQuarksToArray(struct Quark **joined_quarks, char *json_response){
    printf("Adding quarks to array...\n");
    cJSON *quarklist_response = cJSON_Parse(json_response);
    if (!quarklist_response) {
        printf("Quark JSON parsing failed!\n");
        return;
    }
    cJSON *response = cJSON_GetObjectItemCaseSensitive(quarklist_response, "response");
    cJSON *quarks = cJSON_GetObjectItemCaseSensitive(response, "quarks");
    if (!quarks){
        printf("addQuarks: No quarks found in json message wtf\n");
        cJSON_Delete(quarklist_response);
        return;
    }

    freeQuarks(joined_quarks);
    joined_quark_count = 0;
    total_quarks_name_size = 0;

    joined_quark_count = cJSON_GetArraySize(quarks);
    *joined_quarks = malloc(joined_quark_count * sizeof(struct Quark));

    for (int i = 0; i < joined_quark_count; i++){
        cJSON *qrk = cJSON_GetArrayItem(quarks, i);
        snprintf((*joined_quarks)[i].quark_id, sizeof((*joined_quarks)[i].quark_id), "%s", cJSON_GetStringValue(cJSON_GetObjectItem(qrk, "_id")));

        char *quark_name = cJSON_GetStringValue(cJSON_GetObjectItem(qrk, "name"));
        (*joined_quarks)[i].name = strdup(quark_name);
        total_quarks_name_size += strlen(quark_name) + 1; //increment name size. +1 for null terminator
        
        (*joined_quarks)[i].iconUri = strdup(cJSON_GetStringValue(cJSON_GetObjectItem(qrk, "iconUri")));
        (*joined_quarks)[i].invite = strdup(cJSON_GetStringValue(cJSON_GetObjectItem(qrk, "invite")));
        (*joined_quarks)[i].inviteEnabled = cJSON_IsTrue(cJSON_GetObjectItem(qrk, "inviteEnabled"));

        cJSON *owners = cJSON_GetObjectItem(qrk, "owners");
        int owners_count = cJSON_GetArraySize(owners);
        (*joined_quarks)[i].owners_count = owners_count;

        if (owners_count > 0) {
            (*joined_quarks)[i].owners = malloc(owners_count * sizeof(char *));
            for (int j = 0; j < owners_count; j++) {
                (*joined_quarks)[i].owners[j] = strdup(cJSON_GetStringValue(cJSON_GetArrayItem(owners, j)));
            }
        } else {
            (*joined_quarks)[i].owners = NULL;
        }

        cJSON *channels = cJSON_GetObjectItem(qrk, "channels");
        int channels_count = cJSON_GetArraySize(channels);
        (*joined_quarks)[i].channels_count = channels_count;

        if (channels_count > 0){
            (*joined_quarks)[i].channels = calloc(channels_count, sizeof(struct Channel));
            for (int j = 0; j < channels_count; j++){
                cJSON *chnl = cJSON_GetArrayItem(channels, j);
                snprintf((*joined_quarks)[i].channels[j].channel_id, sizeof((*joined_quarks)[i].channels[j].channel_id), "%s", cJSON_GetStringValue(cJSON_GetObjectItem(chnl, "_id")));
                
                char *channel_name = cJSON_GetStringValue(cJSON_GetObjectItem(chnl, "name"));
                (*joined_quarks)[i].channels[j].name = strdup(channel_name);
                (*joined_quarks)[i].channels_total_name_length += strlen(channel_name) + 1; // increment channel size

                (*joined_quarks)[i].channels[j].description = strdup(cJSON_GetStringValue(cJSON_GetObjectItem(chnl, "description")));
            } 
        } else {
            (*joined_quarks)[i].channels = NULL;
            (*joined_quarks)[i].channels_total_name_length = 0;
        }
    }
    
    printf("Added quark data to quark array!\n");
    cJSON_Delete(quarklist_response);
}

// --- [ Message Functions ] ---

char *WrappedMessage(const char *message){ //i give up, Mr GeePeeTee wrote this one im sorry
    // im not sure if this is a very good way to handle text wrapping, but i did not like C2D's wrapping 
    int msg_length = strlen(message);
    int max_char = MAX_CHAR_PER_MESSAGE_LINE, line_start = 0, result_index = 0; 
    char *result = (char*)malloc(msg_length + msg_length / max_char + 2);
    if (!result) return NULL;

    while (line_start < msg_length){ 
        int line_end = line_start + max_char; 

        if (line_end >= msg_length){
            strcpy(result + result_index, message + line_start);
            result_index += msg_length - line_start;  
            break;
        }

        int break_point = line_end; 
        while (break_point > line_start && message[break_point] != ' ' && message[break_point] != '\n') {
            break_point--;
        }

        if (break_point == line_start) {
            break_point = line_end; // If no space is found, force break at max length
        }

        // Copy the current line to result
        strncpy(result + result_index, message + line_start, break_point - line_start);
        result_index += break_point - line_start;

        // Insert '\n'
        result[result_index++] = '\n';

        // Move to the next segment, skipping the space if we broke at one
        line_start = (message[break_point] == ' ' || message[break_point] == '\n') ? break_point + 1 : break_point;
    }

    result[result_index] = '\0'; // Null-terminate the string
    return result;
}

void freeMessageArrayAtIndex(struct MessageStructure *messages, int index) {
    if (!messages) {
        return;
    }

    free(messages[index].content);
    messages[index].content = NULL;

    free(messages[index].ua);
    messages[index].ua = NULL;

    free(messages[index].username);
    messages[index].username = NULL;

    free(messages[index].avatarUri);
    messages[index].avatarUri = NULL;

    for (int i = 0; i < messages[index].attachment_count; i++) {

        free(messages[index].attachments[i].url);
        messages[index].attachments[i].url = NULL;

        free(messages[index].attachments[i].type);
        messages[index].attachments[i].type = NULL;

        free(messages[index].attachments[i].filename);
        messages[index].attachments[i].filename = NULL;

    }

    free(messages[index].attachments);
    messages[index].attachments = NULL;

    

    for (int i = 0; i < messages[index].specialAttribute_count; i++) {

        free(messages[index].specialAttributes[i].type);
        messages[index].specialAttributes[i].type = NULL;

        
        free(messages[index].specialAttributes[i].username);
        messages[index].specialAttributes[i].username = NULL;

        
        free(messages[index].specialAttributes[i].avatarUri);
        messages[index].specialAttributes[i].avatarUri = NULL;

        
        free(messages[index].specialAttributes[i].plaintext);
        messages[index].specialAttributes[i].plaintext = NULL;
    }

    free(messages[index].specialAttributes);
    messages[index].specialAttributes = NULL;
}

void addMessageToArray(struct Channel *channel_struct, int array_size, cJSON *json_response){

    cJSON *message = cJSON_GetObjectItemCaseSensitive(json_response, "message");
    if (!message){
        printf("No message found when trying to add to message array\n");
        return;
    }

    cJSON *message_id = cJSON_GetObjectItemCaseSensitive(message, "_id");
    cJSON *content = cJSON_GetObjectItemCaseSensitive(message, "content");
    cJSON *ua = cJSON_GetObjectItemCaseSensitive(message, "ua");
    cJSON *timestamp = cJSON_GetObjectItemCaseSensitive(message, "timestamp");
    cJSON *edited = cJSON_GetObjectItemCaseSensitive(message, "edited");
    cJSON *attachments = cJSON_GetObjectItem(message, "attachments");
    cJSON *specialAttributes = cJSON_GetObjectItem(message, "specialAttributes");

    cJSON *author = cJSON_GetObjectItemCaseSensitive(message, "author");
    cJSON *author_id = cJSON_GetObjectItemCaseSensitive(author, "_id");
    cJSON *username = cJSON_GetObjectItemCaseSensitive(author, "username");
    cJSON *admin = cJSON_GetObjectItemCaseSensitive(author, "admin");
    cJSON *isbot = cJSON_GetObjectItemCaseSensitive(author, "isBot");
    cJSON *secretThirdThing = cJSON_GetObjectItemCaseSensitive(author, "secretThirdThing");
    cJSON *avatarUri = cJSON_GetObjectItemCaseSensitive(author, "avatarUri");

    cJSON *channelId = cJSON_GetObjectItemCaseSensitive(json_response, "channelId");

    char *wrappedContent = NULL;
    if (strlen(content->valuestring) > MAX_CHAR_PER_MESSAGE_LINE) {
        wrappedContent = strdup(WrappedMessage(content->valuestring));
    } else {
        wrappedContent = strdup(content->valuestring);
    }


    int message_index = channel_struct->message_index;

    freeMessageArrayAtIndex(channel_struct->messages, message_index);


    snprintf(channel_struct->messages[message_index].message_id, sizeof(channel_struct->messages[message_index].message_id), "%s", message_id->valuestring);
    channel_struct->messages[message_index].content = wrappedContent;
    channel_struct->messages[message_index].ua = strdup(ua->valuestring);
    channel_struct->messages[message_index].timestamp = (uint64_t)timestamp->valuedouble;
    channel_struct->messages[message_index].edited = cJSON_IsTrue(edited);

    snprintf(channel_struct->messages[message_index].author_id, sizeof(channel_struct->messages[message_index].author_id), "%s", author_id->valuestring);
    channel_struct->messages[message_index].username = strdup(username->valuestring);
    channel_struct->messages[message_index].admin = cJSON_IsTrue(admin);
    channel_struct->messages[message_index].isbot = cJSON_IsTrue(isbot);
    channel_struct->messages[message_index].secretThirdThing = cJSON_IsTrue(secretThirdThing);
    channel_struct->messages[message_index].avatarUri = strdup(avatarUri->valuestring);

    snprintf(channel_struct->messages[message_index].channelId, sizeof(channel_struct->messages[message_index].channelId), "%s", channelId->valuestring);

    
    // Attachments ----
    if (attachments && cJSON_IsArray(attachments) && cJSON_GetArraySize(attachments) > 0) {
        int count = cJSON_GetArraySize(attachments);
        channel_struct->messages[message_index].attachments = malloc(count * sizeof(struct Attachment));
        channel_struct->messages[message_index].attachment_count = count;

        for (int i = 0; i < count; i++) {
            cJSON *att = cJSON_GetArrayItem(attachments, i);
            channel_struct->messages[message_index].attachments[i].url = strdup(cJSON_GetStringValue(cJSON_GetObjectItem(att, "url")));
            channel_struct->messages[message_index].attachments[i].size = cJSON_GetNumberValue(cJSON_GetObjectItem(att, "size"));
            channel_struct->messages[message_index].attachments[i].type = strdup(cJSON_GetStringValue(cJSON_GetObjectItem(att, "type")));
            channel_struct->messages[message_index].attachments[i].filename = strdup(cJSON_GetStringValue(cJSON_GetObjectItem(att, "filename")));

            cJSON *att_height = cJSON_GetObjectItem(att, "height");
            att_height ? channel_struct->messages[message_index].attachments[i].height = cJSON_GetNumberValue(att_height) : 0;

            cJSON *att_width = cJSON_GetObjectItem(att, "width");
            att_width ? channel_struct->messages[message_index].attachments[i].width = cJSON_GetNumberValue(att_width) : 0;
        }
    } else {
        channel_struct->messages[message_index].attachments = NULL;
        channel_struct->messages[message_index].attachment_count = 0;
    }

    // Attributes ----
    
    if (specialAttributes && cJSON_IsArray(specialAttributes) && cJSON_GetArraySize(specialAttributes) > 0) {
        int count = cJSON_GetArraySize(specialAttributes);
        channel_struct->messages[message_index].specialAttributes = malloc(count * sizeof(struct SpecialAttribute));
        channel_struct->messages[message_index].specialAttribute_count = count;

        for (int i = 0; i < count; i++) {
            cJSON *attr = cJSON_GetArrayItem(specialAttributes, i);
            channel_struct->messages[message_index].specialAttributes[i].type = strdup(cJSON_GetStringValue(cJSON_GetObjectItem(attr, "type")));

            cJSON *username = cJSON_GetObjectItem(attr, "username");
            if (username) channel_struct->messages[message_index].specialAttributes[i].username = strdup(cJSON_GetStringValue(username));
            else channel_struct->messages[message_index].specialAttributes[i].username = NULL;

            cJSON *avatarUri = cJSON_GetObjectItem(attr, "avatarUri");
            if (avatarUri) channel_struct->messages[message_index].specialAttributes[i].avatarUri = strdup(cJSON_GetStringValue(avatarUri));
            else channel_struct->messages[message_index].specialAttributes[i].avatarUri = NULL;
            
            cJSON *replyTo = cJSON_GetObjectItem(attr, "replyTo");
            if (replyTo) {
                snprintf(channel_struct->messages[message_index].specialAttributes[i].replyTo, sizeof(channel_struct->messages[message_index].specialAttributes[i].replyTo), "%s", replyTo->valuestring);
            } else channel_struct->messages[message_index].specialAttributes[i].replyTo[0] = '\0';

            cJSON *discordMessageId = cJSON_GetObjectItem(attr, "discordMessageId");
            if (discordMessageId) channel_struct->messages[message_index].specialAttributes[i].discordMessageId = cJSON_GetNumberValue(discordMessageId);
            else channel_struct->messages[message_index].specialAttributes[i].discordMessageId = 0;

            cJSON *quarkcord = cJSON_GetObjectItem(attr, "quarkcord");
            if (quarkcord) channel_struct->messages[message_index].specialAttributes[i].quarkcord = cJSON_IsTrue(quarkcord);
            else channel_struct->messages[message_index].specialAttributes[i].quarkcord = NULL;

            cJSON *plaintext = cJSON_GetObjectItem(attr, "plaintext");
            if (plaintext) channel_struct->messages[message_index].specialAttributes[i].plaintext = strdup(cJSON_GetStringValue(plaintext));
            else channel_struct->messages[message_index].specialAttributes[i].plaintext = NULL;
        }
    } else {
        channel_struct->messages[message_index].specialAttributes = NULL;
        channel_struct->messages[message_index].specialAttribute_count = 0;
    }
    

     
    channel_struct->message_index = (channel_struct->message_index + 1) % array_size;
    if (channel_struct->total_messages < array_size) {
        channel_struct->total_messages++;
    }

    //printf("!!! Done adding message\n");
}

// --- --- [ RENDERING ] --- ---

void ParseTextMessages(struct Channel *channel_struct){
    // This should in theory only run once, when the message structure updates (e.g getting messages in a channel, getting a new message, message gets edited).
    // Treating the messages as dynamic text and parsing on each frame like the previous rendering function did sounds rather inneficient, so i plan on splitting the parsing and rendering functions.
    if (!channel_struct) return;

    //Im taking the opportunity to also split each message and username in its own text buffer and C2D object
    //Only create buffers if they DONT exist. We assume that if the 0th buffer exists, all of them have been created (which should obviously be true)
    if (!buftxt_messageContent[0] || !buftxt_messageUsername[0]){
        for (int i = 0; i < MAX_REND_MESSAGES; i++){
            buftxt_messageContent[i] = C2D_TextBufNew(4); //TODO: This is the maximum glyphs per message when its rendered in a message list. I want to cut long messages so everything would be cleaner, and to be able to skip scrolling through walls of text basically. Im thinking about being able to select a message to fully read it, in case it longer than this character limit. 
            //NVM for now lets not do this
            //Im setting the buffers to some low arbritary value because below they will get resized to (almost) perfectly fit the usernames/messages
            buftxt_messageUsername[i] = C2D_TextBufNew(4);
        }
    }
    

    int start_index = (channel_struct->message_index - channel_struct->total_messages + MAX_REND_MESSAGES) % MAX_REND_MESSAGES;
    char *lastusername = NULL;
    for (int i = 0; i < MAX_REND_MESSAGES; i++){
        int message_arr_index = (start_index + i) % MAX_REND_MESSAGES;

        if (channel_struct->messages[message_arr_index].content != NULL) {
            char *username = channel_struct->messages[message_arr_index].username; // sets the default name to author username

            for (int j = 0; j < channel_struct->messages[message_arr_index].specialAttribute_count; j++) {
                if (channel_struct->messages[message_arr_index].specialAttributes[j].type && strcmp(channel_struct->messages[message_arr_index].specialAttributes[j].type, "botMessage") == 0 && channel_struct->messages[message_arr_index].specialAttributes[j].username) {
                    //ifthetypeisbotMessageandifBotUsernameexists..
                    username = channel_struct->messages[message_arr_index].specialAttributes[j].username; 
                    break; // use the botMessage username instead
                }
            }
            if (lastusername && strcmp(lastusername, username) == 0){
                channel_struct->messages[message_arr_index].same_username_as_last = true;
            } else channel_struct->messages[message_arr_index].same_username_as_last = false;
            lastusername = username;

            //printf("Parsing for buffer %i\n", i);
            /*/
            //lala more debugging
            printf("Usr ptr: %p | Cont ptr: %p\n", username, channel_struct->messages[message_arr_index].content);
            printf("Usr len: %u | Con len: %u\n", strlen(username), strlen(channel_struct->messages[message_arr_index].content));
            printf("Username value: %s\n", username);
            printf("Content value: %s\n", channel_struct->messages[message_arr_index].content);
            usleep(300 * 1000);
            //*/

            C2D_TextBufClear(buftxt_messageUsername[i]);
            C2D_TextBuf resizedUsernameBuf = C2D_TextBufResize(buftxt_messageUsername[i], strlen(username)*1.2+1); //resize the buffer to be able to hold the exact length of the username (+a little headroom i guess)
            //TODO: consider skipping resizing if the buffer is already big enough. Though im not sure if thats worth doing
            if (!resizedUsernameBuf) {
                printf("TextBufResize failed when parsing username for message %i\n", i);
                continue;
            } else {
                buftxt_messageUsername[i] = resizedUsernameBuf;
            }
            // Parse Usernames
            C2D_TextParse(&txt_messageUsername[i], buftxt_messageUsername[i], username);
            C2D_TextOptimize(&txt_messageUsername[i]);
            C2D_TextGetDimensions(&txt_messageUsername[i], MESSAGE_USERNAME_TEXT_SIZE, MESSAGE_USERNAME_TEXT_SIZE, NULL, &channel_struct->messages[message_arr_index].username_c2d_height);
            //printf("User Height: %f\n", channel_struct->messages[message_arr_index].username_c2d_height);


            C2D_TextBufClear(buftxt_messageContent[i]);
            C2D_TextBuf resizedContentBuf = C2D_TextBufResize(buftxt_messageContent[i], strlen(channel_struct->messages[message_arr_index].content)*1.2+1); // resize buffer. I know that for C2D spaces dont count as "glyphs", but i using strlen should be close enough, even though i technically dont need to account for spaces. Realistically the buffer will be slightly bigger than needed if the message contains spaces // you fool...
            if (!resizedContentBuf) {
                printf("TextBufResize failed when parsing content for message %i\n", i);
                continue;
            } else {
                buftxt_messageContent[i] = resizedContentBuf;
            }
            // Parse Contents
            C2D_TextParse(&txt_messageContent[i], buftxt_messageContent[i], channel_struct->messages[message_arr_index].content); 
            C2D_TextOptimize(&txt_messageContent[i]);
            C2D_TextGetDimensions(&txt_messageContent[i], MESSAGE_USERNAME_TEXT_SIZE, MESSAGE_USERNAME_TEXT_SIZE, NULL, &channel_struct->messages[message_arr_index].content_c2d_height);
            //printf("Content Height: %f\n", channel_struct->messages[message_arr_index].content_c2d_height);
            //saves the message height so i dont have to run getdimensions on every frame later. This is used to properly position multiline messages

            // Create buffers and parse for "attachment" indicators
            if (channel_struct->messages[message_arr_index].attachment_count > 1){
                if (!buftxt_attachments[i]) buftxt_attachments[i] = C2D_TextBufNew(24);
                char attachment_text[24];
                snprintf(attachment_text, sizeof(attachment_text), "[%i attachments]", channel_struct->messages[message_arr_index].attachment_count);

                C2D_TextBufClear(buftxt_attachments[i]);
                // i should probably just check if the buffer is already big enough... Im resizing here because if the buffer has previously held a single file name thats shorter than 24 glypth, the [attachments] text would be cut off. 
                // Unsure if C2D_TextBufGetNumGlyphs returns the buffer _size_ OR how many characters/glypths it has from previously parsed text.. well either way it would be useful, but for now im lazy so im doing this.
                C2D_TextBuf resizedAttachmentBuf = C2D_TextBufResize(buftxt_attachments[i], 24);
                buftxt_attachments[i] = resizedAttachmentBuf;

                C2D_TextParse(&txt_attachments[i], buftxt_attachments[i], attachment_text);
                C2D_TextOptimize(&txt_attachments[i]);
                printf("Multiple attachment text: %s\n", attachment_text);
            } else if (channel_struct->messages[message_arr_index].attachment_count == 1){
                if (!buftxt_attachments[i]) buftxt_attachments[i] = C2D_TextBufNew(strlen(channel_struct->messages[message_arr_index].attachments[0].filename));
                else {
                    C2D_TextBufClear(buftxt_attachments[i]);
                    C2D_TextBuf resizedAttachmentBuf = C2D_TextBufResize(buftxt_attachments[i], strlen(channel_struct->messages[message_arr_index].attachments[0].filename)*1.2+1);
                    buftxt_attachments[i] = resizedAttachmentBuf;
                }
                char attachment_text[strlen(channel_struct->messages[message_arr_index].attachments[0].filename) + 4];
                snprintf(attachment_text, sizeof(attachment_text), "[%s]", channel_struct->messages[message_arr_index].attachments[0].filename);
                C2D_TextParse(&txt_attachments[i], buftxt_attachments[i], channel_struct->messages[message_arr_index].attachments[0].filename);
                C2D_TextOptimize(&txt_attachments[i]);
                printf("Single attachment text: %s\n", channel_struct->messages[message_arr_index].attachments[0].filename);
            } 
        }
    }

    // This calculates each message's "start", basically its height in relation to the bottom of the screen, as well as the total message height. Used for auto mesasge selection when scrolling.
    // I originally thought about doing this in the rendering function, but then i realized calculating this each frame as the messages gets rendered is unnecessary.
    // The for loop goes backwards beacuse of the order messages are stored. The most recent message is the last in the array, but it would get rendered first.
    channel_struct->total_message_height = 0; // reset this first...
    for (int i = MAX_REND_MESSAGES-1; i >= 0; i--){
        int message_arr_index = (start_index + i) % MAX_REND_MESSAGES;
        if (channel_struct->messages[message_arr_index].attachment_count <= 0 && channel_struct->messages[message_arr_index].content_c2d_height <= 0) continue; //C2D_TextGetDimensions might output 0 if the message content is empty. Normally messages cant be empty, BUT if it has an attachment they can be empty. If both are 0 then that means its a non-existing message and should get skipped
        float this_message_height = 0;
        
        if (channel_struct->messages[message_arr_index].attachment_count >= 1)  this_message_height += channel_struct->messages[message_arr_index].username_c2d_height; // if theres attachments i want to leave space to render an [attachment] indicator
        
        this_message_height += channel_struct->messages[message_arr_index].content_c2d_height;

        if (channel_struct->messages[message_arr_index].same_username_as_last == false) {
            this_message_height += channel_struct->messages[message_arr_index].username_c2d_height;
            this_message_height += channel_struct->messages[message_arr_index].username_c2d_height / 4;
        } else {
            this_message_height += channel_struct->messages[message_arr_index].username_c2d_height / 6;
        }

        channel_struct->messages[message_arr_index].content_totalpadding_height = this_message_height;

        channel_struct->total_message_height += this_message_height;

        channel_struct->messages[message_arr_index].content_message_start = channel_struct->total_message_height;

        //printf("Message %i height: %f\n", i, channel_struct->messages[message_arr_index].content_message_start);
    }
    //printf("Total message height: %f\n", channel_struct->total_message_height);
}

void DrawTextMessages(struct Channel *channel_struct, float scrolling_offset, int selected_message){
    if (!channel_struct) return;

    int start_index = (channel_struct->message_index - channel_struct->total_messages + MAX_REND_MESSAGES) % MAX_REND_MESSAGES;
    float y = 240.0f + scrolling_offset;
    for (int i = MAX_REND_MESSAGES-1; i >= 0; i--){
        int message_arr_index = (start_index + i) % MAX_REND_MESSAGES;
        if (channel_struct->messages[message_arr_index].content == NULL) continue;
        u32 content_color = C2D_Color32(255, 0, 0, 255);
        if (selected_message == message_arr_index) content_color = C2D_Color32(255, 0, 0, 127);
        u32 username_color = C2D_Color32(255, 255, 255, 255);
        if (selected_message == message_arr_index) username_color = C2D_Color32(255, 255, 255, 127);
        u32 attachment_color = C2D_Color32(255, 0, 255, 255);
        if (selected_message == message_arr_index) attachment_color = C2D_Color32(255, 0, 255, 127);

        if (channel_struct->messages[message_arr_index].attachment_count > 0){
            y -= channel_struct->messages[message_arr_index].username_c2d_height;
            C2D_DrawText(&txt_attachments[i], C2D_WithColor, 0.0f, y, 0.0f, MESSAGE_USERNAME_TEXT_SIZE, MESSAGE_USERNAME_TEXT_SIZE, attachment_color);
        }

        if (channel_struct->messages[message_arr_index].content[0] != '\0'){
            y -= channel_struct->messages[message_arr_index].content_c2d_height;
            C2D_DrawText(&txt_messageContent[i], C2D_WithColor, 0.0f, y, 0.0f, MESSAGE_USERNAME_TEXT_SIZE, MESSAGE_USERNAME_TEXT_SIZE, content_color);
        }
        
        if (channel_struct->messages[message_arr_index].same_username_as_last){
            y -= channel_struct->messages[message_arr_index].username_c2d_height / 6;
        } else {
            y -= channel_struct->messages[message_arr_index].username_c2d_height;
            C2D_DrawText(&txt_messageUsername[i], C2D_WithColor, 0.0f, y, 0.0f, MESSAGE_USERNAME_TEXT_SIZE, MESSAGE_USERNAME_TEXT_SIZE, username_color);
            y -= channel_struct->messages[message_arr_index].username_c2d_height / 4; // add some padding in between messages
        }
    }
}
    
void Buf_C2D_Cleanup(){
    printf("Deleting C2D Buffers...\n");
    for (int i = 0; i < MAX_REND_MESSAGES; i++){
        if (buftxt_messageContent[i])  C2D_TextBufDelete(buftxt_messageContent[i]);
        if (buftxt_messageUsername[i]) C2D_TextBufDelete(buftxt_messageUsername[i]);
        if (buftxt_attachments[i]) C2D_TextBufDelete(buftxt_attachments[i]);
    }
}

void DrawStructuredQuarks(struct Quark *joined_quarks, bool channel_select, int selected_quark, int selected_channel, int entered_selected_channel){
    float total_quark_channel_height = 50.0f;

    if (!channel_select){
        C2D_TextBuf quarkBuf = NULL;
        C2D_Text quarkText[MAX_CHANNEL_QUARK];
        if (!quarkBuf) {
            quarkBuf = C2D_TextBufNew(total_quarks_name_size);
        }

        //int total_pages = (joined_quark_count + MAX_CHANNEL_QUARK - 1) / MAX_CHANNEL_QUARK; // determine number of pages (starts at 0) //nvm i dont need this //nvm i might need this if i want to display pages
        int current_page = selected_quark / MAX_CHANNEL_QUARK; // the current page the selected quark is on

        int start_index = current_page * MAX_CHANNEL_QUARK;
        int end_index = start_index + MAX_CHANNEL_QUARK;
        if (end_index > joined_quark_count) end_index = joined_quark_count;

        C2D_TextBufClear(quarkBuf);

        float quark_line_height=0;
        for (int i = start_index; i < end_index; i++){
            C2D_TextParse(&quarkText[i - start_index], quarkBuf, joined_quarks[i].name);
            C2D_TextOptimize(&quarkText[i - start_index]);

            u32 color = (i == selected_quark) ? C2D_Color32(255, 0, 0, 255) : C2D_Color32(255, 255, 255, 255);

            C2D_TextGetDimensions(&quarkText[i - start_index], QUARK_CHANNEL_TEXT_SIZE, QUARK_CHANNEL_TEXT_SIZE, NULL, &quark_line_height);

            C2D_DrawText(&quarkText[i - start_index], C2D_WithColor, 0.0f, total_quark_channel_height, 0.0f, QUARK_CHANNEL_TEXT_SIZE, QUARK_CHANNEL_TEXT_SIZE, color);
            total_quark_channel_height += quark_line_height;
        }
    } 
    // displaying channels instead of quarks 
    else if (joined_quarks[selected_quark].channels_count > 0){
        C2D_TextBuf channelBuf = NULL;
        C2D_Text channelText[joined_quarks[selected_quark].channels_count];
        if (!channelBuf) {
            channelBuf = C2D_TextBufNew(joined_quarks[selected_quark].channels_total_name_length);
        } else {
            channelBuf = C2D_TextBufResize(channelBuf, joined_quarks[selected_quark].channels_total_name_length);
        }

        int current_page = selected_channel / MAX_CHANNEL_QUARK; // the current page the selected <channel> is on

        int start_index = current_page * MAX_CHANNEL_QUARK;
        int end_index = start_index + MAX_CHANNEL_QUARK;
        if (end_index > joined_quarks[selected_quark].channels_count) end_index = joined_quarks[selected_quark].channels_count;

        C2D_TextBufClear(channelBuf);

        float message_line_height = 0;

        for (int i = start_index; i < end_index; i++){
            C2D_TextParse(&channelText[i - start_index], channelBuf, joined_quarks[selected_quark].channels[i].name);
            C2D_TextOptimize(&channelText[i - start_index]);

            u32 color;
            if (i == entered_selected_channel){
                color = C2D_Color32(255, 0, 0, 255);
            } else if (i == selected_channel){
                color = C2D_Color32(128, 128, 128, 255);
            } else color = C2D_Color32(255, 255, 255, 255);

            C2D_TextGetDimensions(&channelText[i - start_index], QUARK_CHANNEL_TEXT_SIZE, QUARK_CHANNEL_TEXT_SIZE, NULL, &message_line_height);

            C2D_DrawText(&channelText[i - start_index], C2D_WithColor, 0.0f, total_quark_channel_height, 0.0f, QUARK_CHANNEL_TEXT_SIZE, QUARK_CHANNEL_TEXT_SIZE, color);
            total_quark_channel_height += message_line_height;
        }
    }
}

// --- [ Login Screen ] ---

void LQLoginScreen(loginState *loginState, struct Quark **joined_quarks, C3D_RenderTarget *topScreen) {
  switch (*loginState) {
    case LOGIN_STATE_BLANK:
      char loginEmail[128] = {0};
      char loginPassword[128] = {0};

      C2D_TextBuf bufEmail = C2D_TextBufNew(sizeof(loginEmail));
      C2D_TextBuf bufPassword = C2D_TextBufNew(sizeof(loginPassword) + 1);

      const char buttons[] =" Input Email\n  Input Password\n  Hold to reveal Password and Email\n  Login\n START Exit";
      C2D_TextBuf bufLoginButtons = C2D_TextBufNew(sizeof(buttons));
      C2D_Text txtLoginButtons;

      C2D_TextParse(&txtLoginButtons, bufLoginButtons, buttons);
      C2D_TextOptimize(&txtLoginButtons);

      while (*loginState != LOGIN_STATE_ATTEMPT) {
        hidScanInput();
        u32 kDown = hidKeysDown();
        u32 kHeld = hidKeysHeld();

        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C2D_TargetClear(topScreen, C2D_Color32(0, 0, 0, 255));
        C2D_SceneBegin(topScreen);

        C2D_DrawText(&txtLoginButtons, C2D_AlignCenter | C2D_WithColor, 200.0f, 10.0f, 0.0f, 0.5f, 0.5f, C2D_Color32(255, 255, 255, 255));

        if (kDown & KEY_X) {
          SwkbdState swkbd;

          swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 1, -1);
          swkbdSetInitialText(&swkbd, loginEmail);
          swkbdSetHintText(&swkbd, "Input login email");
          swkbdSetButton(&swkbd, SWKBD_BUTTON_RIGHT, "OK", true);
          swkbdSetFeatures(&swkbd, SWKBD_PREDICTIVE_INPUT);
          swkbdSetValidation(&swkbd, SWKBD_ANYTHING, 8, sizeof(loginEmail));
          swkbdInputText(&swkbd, loginEmail, sizeof(loginEmail));
        }
        if (kDown & KEY_B) {
          SwkbdState swkbd;

          swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 1, -1);
          swkbdSetInitialText(&swkbd, loginPassword);
          swkbdSetHintText(&swkbd, "Input password");
          swkbdSetButton(&swkbd, SWKBD_BUTTON_RIGHT, "OK", true);
          swkbdSetFeatures(&swkbd, SWKBD_PREDICTIVE_INPUT);
          swkbdSetValidation(&swkbd, SWKBD_ANYTHING, 8, sizeof(loginPassword));
          swkbdInputText(&swkbd, loginPassword, sizeof(loginPassword));
        }
        if (kHeld & KEY_Y) {
          C2D_Text txtEmail;
          C2D_Text txtPassword;

          C2D_TextParse(&txtEmail, bufEmail, loginEmail);
          C2D_TextOptimize(&txtEmail);
          C2D_DrawText(&txtEmail, C2D_AlignCenter | C2D_WithColor, 200.0f, 100.0f, 0.0f, 0.4f, 0.4f, C2D_Color32(255, 255, 255, 255));
          C2D_TextBufClear(bufEmail);

          C2D_TextParse(&txtPassword, bufPassword, loginPassword);
          C2D_TextOptimize(&txtPassword);
          C2D_DrawText(&txtPassword, C2D_AlignCenter | C2D_WithColor, 200.0f, 120.0f, 0.0f, 0.4f, 0.4f, C2D_Color32(255, 255, 255, 255));
          C2D_TextBufClear(bufPassword);
        }
        if (kDown & KEY_A) {
          *loginState = LightquarkLogin(LOGIN_STATE_BLANK, loginEmail, loginPassword, joined_quarks);
          if (bufLoginButtons) C2D_TextBufDelete(bufLoginButtons);
          if (bufEmail) C2D_TextBufDelete(bufEmail);
          if (bufPassword) C2D_TextBufDelete(bufPassword);
          break;
        }
        if (kDown & KEY_START) {
            *loginState = LOGIN_STATE_EXIT;
            if (bufLoginButtons) C2D_TextBufDelete(bufLoginButtons);
            if (bufEmail) C2D_TextBufDelete(bufEmail);
            if (bufPassword) C2D_TextBufDelete(bufPassword);
            C3D_FrameEnd(0);
            break;
        }
        C3D_FrameEnd(0);
      }
    case LOGIN_STATE_ATTEMPT:
      *loginState = LightquarkLogin(LOGIN_STATE_ATTEMPT, NULL, NULL, joined_quarks);
      break;
    case LOGIN_STATE_REFRESH:
      *loginState = LightquarkLogin(LOGIN_STATE_REFRESH, NULL, NULL, joined_quarks);
      break;
    case LOGIN_STATE_DONE:
      printf("Login Successful! :)\n");
      break;
    default:
      printf("Login Failed :(\n");
      break;
    }
}