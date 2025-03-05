#ifndef MESSAGEASSEMBLE_H
#define MESSAGEASSEMBLE_H

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <citro2d.h>

// Text buffers

C2D_TextBuf text_contentBuf, text_usernameBuf; 

//C2D_TextBuf quarkBuf, channelBuf;

// Text objects

C2D_Text contentText, usernameText;

#define MAX_CHAR_PER_MESSAGE_LINE 65
#define LQ_IDLENGTH 25 // 24 + 1 for null terminator.. i think. I think im right since with 24 something falls over

#define MAX_REND_MESSAGES 10 // the maximum amount of messages that should get displayed/rendered/saved per channel. This just determines the size of the "MessageStructure" Array

#define MAX_CHANNEL_QUARK 6 // the max channels or quarks that should get displayed on a *page*

#define QUARK_CHANNEL_TEXT_SIZE 0.5f

// --- [ Quark/Channel Structs ] ---
struct Channel {
    char channel_id[LQ_IDLENGTH];
    char *name;
    char *description;

    struct MessageStructure messages[MAX_REND_MESSAGES];
};

struct Quark {
    char quark_id[LQ_IDLENGTH];
    char *name;
    char *iconUri;
    char *invite;
    bool inviteEnabled;
    // --- --- ---
    char **owners; // NULL when thers no owners
    int owners_count; //number of owners
    
    struct Channel *channels;
    int channels_count;
    int channels_total_name_length; //each quark stores the total length of its channel names combined. Used for allocating C2D text buffers
};

// --- [ Message Structs ] ---
struct Attachment {
    char *url;
    int size;
    char *type;
    char *filename;
    // --- --- ---
    int height;
    int width;
};

struct SpecialAttribute {
    char *type;

    // --- botMessage ---
    char *username; 
    char *avatarUri; 
    // --- --------- ---

    // --- Reply ---
    char replyTo[LQ_IDLENGTH];
    // --- ----- ---

    // --- ClientAttributes ---
    uint64_t discordMessageId;
    bool quarkcord;

    char *plaintext;
    // --- --------- --- 
};

struct MessageStructure {
    // --- Message ---
    char message_id[LQ_IDLENGTH];
    char *content;
    char *ua;
    uint64_t timestamp;
    bool edited;
    
    struct Attachment *attachments;
    int attachment_count;

    
    struct SpecialAttribute *specialAttributes;
    int specialAttribute_count;
    // --- ------- ---

    // --- Author ---
    char author_id[LQ_IDLENGTH];
    char *username;
    bool admin; 
    bool isbot; 
    bool secretThirdThing; // whatever this is
    char *avatarUri;
    // --- ------ ---

    char channelId[LQ_IDLENGTH];

    int content_line_number;
};

// --- [ Quark/Channel Functions ] ---
static size_t joined_quark_count = 0;
size_t total_quarks_name_size = 0; // to get the length of all quark names, for allocating the C2D text buffer
//size_t totalChannelsNameSize = 0; // See channels_total_name_length; in Quark struct
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
            (*joined_quarks)[i].channels = malloc(channels_count * sizeof(struct Channel));
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

int countLines(const char *wrappedMessage) {
    int lines = 0;
    for (int i = 0; wrappedMessage[i] != '\0'; i++) {
        if (wrappedMessage[i] == '\n') {
            lines++; // Count each newline character as the end of a line
        }
    }
    return lines+1; //+1 because the message will always have at least one line
}

void freeMessageArrayAtIndex(struct MessageStructure *recent_messages, int index) {
    free(recent_messages[index].content);
    free(recent_messages[index].ua);

    free(recent_messages[index].username);
    free(recent_messages[index].avatarUri);

    for (int i = 0; i < recent_messages[index].attachment_count; i++) {
        free(recent_messages[index].attachments[i].url);
        free(recent_messages[index].attachments[i].type);
        free(recent_messages[index].attachments[i].filename);
    }
    free(recent_messages[index].attachments);

    for (int i = 0; i < recent_messages[index].specialAttribute_count; i++) {
        free(recent_messages[index].specialAttributes[i].type);

        free(recent_messages[index].specialAttributes[i].username);
        free(recent_messages[index].specialAttributes[i].avatarUri);

        //todo fix replyto here too i think i need to clear/remove it //what is past me yapping about i forgot??

        free(recent_messages[index].specialAttributes[i].plaintext);
    }
    free(recent_messages[index].specialAttributes);
}

void freeMessageArrayFull(struct MessageStructure *recent_messages, int array_size){
    //crazy function
    for (int i = 0; i < array_size; i++){
        freeMessageArrayAtIndex(recent_messages, i);
    }
}

int message_index = 0; // Tracks where the next message should be inserted
int total_messages = 0;
void addMessageToArray(struct MessageStructure *recent_messages, int array_size, cJSON *json_response){
    // dear god
    cJSON *message = cJSON_GetObjectItemCaseSensitive(json_response, "message");
    if (!message){
        printf("addMessage: No message found when trying to add to message array\n");
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

    if (strlen(content->valuestring) > MAX_CHAR_PER_MESSAGE_LINE){
        char *wrappedcontent = NULL;
        printf("Message too long, wrapping message...\n");
        wrappedcontent = WrappedMessage(content->valuestring);
        content->valuestring = wrappedcontent;
    }

    int content_lines = countLines(content->valuestring);

    freeMessageArrayAtIndex(recent_messages, message_index);

    snprintf(recent_messages[message_index].message_id, sizeof(recent_messages[message_index].message_id), "%s", message_id->valuestring);
    recent_messages[message_index].content = strdup(content->valuestring);
    recent_messages[message_index].ua = strdup(ua->valuestring);
    recent_messages[message_index].timestamp = timestamp->valuedouble;
    recent_messages[message_index].edited = cJSON_IsTrue(edited);

    snprintf(recent_messages[message_index].author_id, sizeof(recent_messages[message_index].author_id), "%s", author_id->valuestring);
    recent_messages[message_index].username = strdup(username->valuestring);
    recent_messages[message_index].admin = cJSON_IsTrue(admin);
    recent_messages[message_index].isbot = cJSON_IsTrue(isbot);
    recent_messages[message_index].secretThirdThing = cJSON_IsTrue(secretThirdThing);
    recent_messages[message_index].avatarUri = strdup(avatarUri->valuestring);

    snprintf(recent_messages[message_index].channelId, sizeof(recent_messages[message_index].channelId), "%s", channelId->valuestring);
    recent_messages[message_index].content_line_number = content_lines;


    // Attachments ----
    if (attachments && cJSON_IsArray(attachments) && cJSON_GetArraySize(attachments) > 0) {
        int count = cJSON_GetArraySize(attachments);
        recent_messages[message_index].attachments = malloc(count * sizeof(struct Attachment));
        recent_messages[message_index].attachment_count = count;

        for (int i = 0; i < count; i++) {
            cJSON *att = cJSON_GetArrayItem(attachments, i);
            recent_messages[message_index].attachments[i].url = strdup(cJSON_GetStringValue(cJSON_GetObjectItem(att, "url")));
            recent_messages[message_index].attachments[i].size = cJSON_GetNumberValue(cJSON_GetObjectItem(att, "size"));
            recent_messages[message_index].attachments[i].type = strdup(cJSON_GetStringValue(cJSON_GetObjectItem(att, "type")));
            recent_messages[message_index].attachments[i].filename = strdup(cJSON_GetStringValue(cJSON_GetObjectItem(att, "filename")));

            cJSON *att_height = cJSON_GetObjectItem(att, "height");
            att_height ? recent_messages[message_index].attachments[i].height = cJSON_GetNumberValue(att_height) : 0;

            cJSON *att_width = cJSON_GetObjectItem(att, "width");
            att_height ? recent_messages[message_index].attachments[i].width = cJSON_GetNumberValue(att_width) : 0;
        }
    } else {
        recent_messages[message_index].attachments = NULL;
        recent_messages[message_index].attachment_count = 0;
    }

    // Attributes ----
    if (specialAttributes && cJSON_IsArray(specialAttributes) && cJSON_GetArraySize(specialAttributes) > 0) {
        int count = cJSON_GetArraySize(specialAttributes);
        recent_messages[message_index].specialAttributes = malloc(count * sizeof(struct SpecialAttribute));
        recent_messages[message_index].specialAttribute_count = count;

        for (int i = 0; i < count; i++) {
            cJSON *attr = cJSON_GetArrayItem(specialAttributes, i);
            recent_messages[message_index].specialAttributes[i].type = strdup(cJSON_GetStringValue(cJSON_GetObjectItem(attr, "type")));

            cJSON *username = cJSON_GetObjectItem(attr, "username");
            if (username) recent_messages[message_index].specialAttributes[i].username = strdup(cJSON_GetStringValue(username));

            cJSON *avatarUri = cJSON_GetObjectItem(attr, "avatarUri");
            if (avatarUri) recent_messages[message_index].specialAttributes[i].avatarUri = strdup(cJSON_GetStringValue(avatarUri));
            
            cJSON *replyTo = cJSON_GetObjectItem(attr, "replyTo");
            if (replyTo) snprintf(recent_messages[message_index].specialAttributes[i].replyTo, sizeof(recent_messages[message_index].specialAttributes[i].replyTo), "%s", replyTo->valuestring);

            cJSON *discordMessageId = cJSON_GetObjectItem(attr, "discordMessageId");
            if (discordMessageId) recent_messages[message_index].specialAttributes[i].discordMessageId = cJSON_GetNumberValue(discordMessageId);

            cJSON *quarkcord = cJSON_GetObjectItem(attr, "quarkcord");
            if (quarkcord) recent_messages[message_index].specialAttributes[i].quarkcord = cJSON_IsTrue(quarkcord);
        }
    } else {
        recent_messages[message_index].specialAttributes = NULL;
        recent_messages[message_index].specialAttribute_count = 0;
    }

     
    message_index = (message_index + 1) % array_size;
    if (total_messages < array_size) total_messages++;

    cJSON_Delete(json_response); // all copied, nuke the json
}

// --- --- [ RENDERING ] --- ---

void DrawStructuredMessage(struct MessageStructure *messages_array, int array_size, float scrolling_offset) {
    float total_messages_height = 0.0f; //start at 0, the top of the screen
    C2D_TextBufClear(text_contentBuf);
    C2D_TextBufClear(text_usernameBuf);

    int start_index = (message_index - total_messages + array_size) % array_size;
    for (int i = 0; i <= array_size-1; i++) {
        int message_arr_index = (start_index + i) % array_size;
        if (messages_array[message_arr_index].content != NULL) {

            const char *username_to_render = messages_array[message_arr_index].username; // sets the default name to author username

            for (int j = 0; j < messages_array[message_arr_index].specialAttribute_count; j++) {
                if (messages_array[message_arr_index].specialAttributes[j].type && strcmp(messages_array[message_arr_index].specialAttributes[j].type, "botMessage") == 0 && messages_array[message_arr_index].specialAttributes[j].username) {
                    //ifthetypeisbotMessageandifitexists..
                    username_to_render = messages_array[message_arr_index].specialAttributes[j].username;
                    break; // use the botMessage username instead
                }
            }

            // Render the determined username
            C2D_TextParse(&usernameText, text_usernameBuf, username_to_render);
            C2D_TextOptimize(&usernameText);
            C2D_DrawText(&usernameText, C2D_WithColor, 0.0f, total_messages_height + scrolling_offset, 0.0f, 0.5f, 0.5f, C2D_Color32(255, 0, 0, 250));
            total_messages_height += 15; // 15 for username height
            

            // Render message content
            C2D_TextParse(&contentText, text_contentBuf, messages_array[message_arr_index].content);
            C2D_TextOptimize(&contentText);
            C2D_DrawText(&contentText, C2D_WithColor, 0.0f, total_messages_height + scrolling_offset, 0.0f, 0.5f, 0.5f, C2D_Color32(255, 255, 255, 255));

            float message_height = messages_array[message_arr_index].content_line_number * 15;
            total_messages_height += message_height;
        }
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

        //int total_pages = (joined_quark_count + MAX_CHANNEL_QUARK - 1) / MAX_CHANNEL_QUARK; // determine number of pages (starts at 0) //nvm i dont need this
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
    else {
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

        float message_line_height=0;
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

#endif