#ifndef RENDERING_H
#define RENDERING_H

#include <stdbool.h>
#include <stdint.h>
#include <3ds/synchronization.h>
#include <citro2d.h>
#include "cJSON.h"
#include "networking.h"

#define MAX_CHAR_PER_MESSAGE_LINE 60
#define LQ_IDLENGTH 128 // 24 + 1 for null terminator.. i think. I think im right since with 24 something falls over //changed to 128 for lq 0.4.1 support //TODO: uhhh.. do this better

#define MAX_REND_MESSAGES 15 // the maximum amount of messages that should get displayed/rendered/saved per channel. This just determines the size of the "MessageStructure" Array
#define MAX_CHANNEL_QUARK 8 // the max channels or quarks that should get displayed on a *page*

#define MESSAGE_USERNAME_TEXT_SIZE 0.5f
#define QUARK_CHANNEL_TEXT_SIZE 0.5f

extern LightLock MessageWriterLock;

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

    float content_c2d_height; // the total height in pixels of the individual message text (multiline or not, including username). See ParseTextMessages. WITHOUT padding
    float content_totalpadding_height; // total height in pixels of the individual message including padding // I wonder if this is kinda uneccessary since i could try calculating the padding with the above variable
    float content_message_start; // the pixel number where each message starts. Basically the top of the message "block", including the username and padding (i think), relative to 0 (the bottom of the screen). Used for the auto message selection scrolling thing to figure out if a message is in view or not
    float username_c2d_height; // the height of the username. It should be only 1 line long, which means this is also the "default single character/line height for text that uses the same size and font as usernames and messages". Try having this as the variable name

    bool same_username_as_last; // if true, the username of this message is the same as the last message. Used to skip drawing the username if thats the case. Quarky developer(s?) might recognize this as isContinuation, though we can agree my variable names are superior
};

// --- [ Quark/Channel Structs ] ---
struct Channel {
    char channel_id[LQ_IDLENGTH];
    char *name;
    char *description;
    // --- --- ---

    struct MessageStructure messages[MAX_REND_MESSAGES];
    int message_index; // Tracks where the next message should be inserted
    int total_messages;
    float total_message_height; // how many pixels the entire message list takes up. Used for message auto selection
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

// --- [ Quark Functions ] ---
void freeQuarks(struct Quark **joined_quarks);

void addQuarksToArray(struct Quark **joined_quarks, char *json_response);

// --- [ Message Functions ] ---

char *WrappedMessage(const char *message);

void freeHELPMessageArrayAtIndex(struct MessageStructure *messages, int index);

void addMessageToArray(struct Channel *channel_struct, int array_size, cJSON *json_response);

// --- --- [ RENDERING ] --- ---

void ParseTextMessages(struct Channel *channel_struct);

void DrawTextMessages(struct Channel *channel_struct, float scrolling_offset, int selected_message);
    
void Buf_C2D_Cleanup();

void DrawStructuredQuarks(struct Quark *joined_quarks, bool channel_select, int selected_quark, int selected_channel, int entered_selected_channel);

// --- --- [ LOGIN SCREEN ] --- ---

void LQLoginScreen(loginState *loginState, struct Quark **joined_quarks, C3D_RenderTarget *topScreen);

#endif