#ifndef NETWORKING_H
#define NETWORKING_H

extern LightLock CurlGatewayLock;

#define CLIENT_NAME "Redshift3DS"

struct response {
    char *memory;
    size_t size;
};

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp)
{
  size_t realsize = size * nmemb;
  struct response *mem = (struct response *)userp;

  char *ptr = realloc(mem->memory, mem->size + realsize + 1);
  if(!ptr) {
    /* out of memory! */
    printf("not enough memory (realloc returned NULL)\n");
    return 0;
  }

  mem->memory = ptr;
  memcpy(&(mem->memory[mem->size]), contents, realsize);
  mem->size += realsize;
  mem->memory[mem->size] = 0;

  return realsize;
}

char *curlRequest(const char* url, const char* postdata, const char* token, long* httpcodeout) {
    //printf("curl_request URL: %s\n", url);
    CURL *curl;
    CURLcode res;
    struct response chunk= {.memory = malloc(0), .size = 0};

    curl = curl_easy_init();

    if (!curl) {
        free(chunk.memory);
        return NULL;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url); //set url 

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json"); //append the Content-Type header
    char agent_header[32];
    snprintf(agent_header, sizeof(agent_header), "lq-agent: %s", CLIENT_NAME);
    headers = curl_slist_append(headers, agent_header); //append lq-agent // do i even need to append this for things like this? o well
    
    if (token != NULL){
        char auth_header[512];
        snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", token); 
        headers = curl_slist_append(headers, auth_header); //append the Authorization header
    }

    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers); //set headers

    if (postdata != NULL){
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postdata); //sets the json post data
    }
    
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L); 
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 0L);

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);

    res = curl_easy_perform(curl);
    if(res != CURLE_OK) {
        printf("curl perform Error: %s\n", curl_easy_strerror(res));
        free(chunk.memory);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return NULL;
    }
    
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, httpcodeout);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return chunk.memory; //dont forget to free this dumbass
}

long lqSendmessage(const char* token, const char* channelid, const char* message, const char* replyto){
    char url[256];
    snprintf(url, sizeof(url), "https://lightquark.network/v4/channel/%s/messages", channelid);
    printf("Sending LQ Message: %s\n", message);
    
    CURL *curl;
    CURLcode res;
    struct response chunk= {.memory = malloc(0), .size = 0};

    curl = curl_easy_init();
    if (!curl) {
        free(chunk.memory);
        return -1;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: multipart/form-data");

    char agent_header[32];
    snprintf(agent_header, sizeof(agent_header), "lq-agent: %s", CLIENT_NAME);
    headers = curl_slist_append(headers, agent_header);

    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", token); 
    headers = curl_slist_append(headers, auth_header);

    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    // create message payload
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "content", message);
    cJSON *specialAttributes = cJSON_CreateArray();
    if (replyto != NULL){
        cJSON *replyAttr = cJSON_CreateObject();
        cJSON_AddStringToObject(replyAttr, "type", "reply");
        cJSON_AddStringToObject(replyAttr, "replyTo", replyto);
        cJSON_AddItemToArray(specialAttributes, replyAttr);
        cJSON_AddItemToObject(data, "specialAttributes", specialAttributes);
    }
    char *message_payload = cJSON_PrintUnformatted(data);
    cJSON_Delete(data);

    // mime/formdata
    curl_mime *mime;
    curl_mimepart *part;
    mime = curl_mime_init(curl);
    part = curl_mime_addpart(mime);

    curl_mime_name(part, "payload");
    curl_mime_type(part, "application/json");
    curl_mime_data(part, message_payload, CURL_ZERO_TERMINATED);
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);

    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L); 
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 0L);

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);

    res = curl_easy_perform(curl);
    if(res != CURLE_OK) {
        printf("curl perform error when sending lq message: %s\n", curl_easy_strerror(res));
        free(chunk.memory);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return -1;
    }
    
    long httpcodeout;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpcodeout);

    free(message_payload);
    free(chunk.memory);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return httpcodeout;
}

// --- [Gateway stuff] ---

void print_frame_binary(uint8_t *frame, size_t size) { //debugging function from printing a string (frame) as binary 
    printf("Frame in binary: ");
    for (size_t i = 0; i < size; i++) {
        for (int bit = 7; bit >= 0; bit--) {
        // Print each bit from the most significant to the least significant
        printf("%d", (frame[i] >> bit) & 1);
        }
        printf(" ");  // Space between each byte's binary representation
    }
    printf("\n");
}

CURL *curlUpgradeGateway(char *gateway_url){
    printf("Gateway URL for switching: %s\n", gateway_url);

    char *url_copy = malloc(strlen(gateway_url) + 1); 
    strcpy(url_copy, gateway_url);

    char *host_url = strtok(url_copy, "://");
    host_url = strtok(NULL, "://"); // Get the second part after "://"

    CURL *curl;
    CURLcode res;
    curl_socket_t sockfd;
    curl = curl_easy_init(); //init bruv

    
    curl_easy_setopt(curl, CURLOPT_URL, gateway_url); // Set the URL

    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);

    curl_easy_setopt(curl, CURLOPT_CONNECT_ONLY, 1L); 

    curl_easy_setopt(curl, CURLOPT_VERBOSE, 0L);  // debug on/off

    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        printf("Error when upgrading to socket: %s\n", curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        return NULL;
    }

    res = curl_easy_getinfo(curl, CURLINFO_ACTIVESOCKET, &sockfd);
    if (res != CURLE_OK || sockfd < 0) {
        printf("Failed to retrieve active socket!\n");
        curl_easy_cleanup(curl);
        return NULL;
    }

    char upgrade_request[256]; // hopefully this is large enough for the entire header todo: dynamically allocate this
    sprintf(upgrade_request,
        "GET / HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: QUo86XL2bHszCCpigvKqHg==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n",
        host_url);


    size_t nsent;    
    res = curl_easy_send(curl, upgrade_request, strlen(upgrade_request), &nsent);
    if (res != CURLE_OK || nsent != strlen(upgrade_request)) {
        perror("Failed to send WebSocket upgrade request");
        curl_easy_cleanup(curl);
        return NULL;
    }
    printf("Sent header size: %d\n", nsent);
    
    // Receive WebSocket response
    char response[1024];
    size_t bytes_received = 0;
    int max_retries = 5, retry_count = 0;

    res = curl_easy_getinfo(curl, CURLINFO_ACTIVESOCKET, &sockfd); //gets the socket
    if (res != CURLE_OK || sockfd < 0) {
        printf("Failed to retrieve active socket for receiving frame!\n");
        return NULL;
    }
    //printf("Receiving data on socket: %d\n", sockfd);

    while (retry_count < max_retries) { // for having a maximum retry count
        res = curl_easy_recv(curl, response, sizeof(response), &bytes_received);

        if (res == CURLE_OK && bytes_received > 0) {
            break;  // Successfully received data, exit loop
        } 
        else if (res == CURLE_AGAIN) {
            // **No data available, wait using poll**
            struct pollfd pfd;
            pfd.fd = sockfd;
            pfd.events = POLLIN;

            printf("Socket not ready, waiting...\n");
            int poll_res = poll(&pfd, 1, 1000);  // 1-second timeout

            if (poll_res == -1) {
                perror("poll() error");
                return NULL;
            } else if (poll_res == 0) {
                //printf("Timeout waiting for data, retrying...\n");
            } else if (pfd.revents & POLLIN) {
                //printf("Socket is ready for reading, retrying...\n");
            }
            retry_count++;

        } 
        else {
            printf("Error receiving WebSocket frame: %s\n", curl_easy_strerror(res));
            return NULL;
        }
        if (retry_count >= max_retries) {
            printf("Max retries reached, giving up on receiving data.\n");
            return NULL;
        }
    }

    // Check for 101 Switching Protocols
    if (strstr(response, "101 Switching Protocols")) {
        printf("WebSocket handshake successful!\n");
        return curl; //return curl handle 
    } else {
        printf("WebSocket handshake failed! Response:\n%s\n", response);
    }

    curl_easy_cleanup(curl);
    return NULL;
}

void generate_mask(uint8_t mask[4]) {
    for (int i = 0; i < 4; i++) {
        mask[i] = rand() % 256;
    }
}

void GW_SendLargeFrame(CURL *curl, const char *message){
    size_t msg_len = strlen(message);
    size_t frame_size = 8 + msg_len; // 1 byte for FIN + RSV + opcode, 1 byte for Mask set
    uint8_t *frame = (uint8_t *)malloc(frame_size);

    frame[0] = 0b10000001;
    frame[1] = 0b11111110;
    frame[2] = msg_len >> 8;
    frame[3] = msg_len & 0b11111111;

    uint8_t mask[4];
    generate_mask(mask);

    memcpy(&frame[4], mask, 4);

    for (size_t i = 0; i < msg_len; i++) {
        frame[8 + i] = message[i] ^ mask[i % 4];  //mask each byte of the message by cycling through the mask bytes, and XOR-ing with the message bytes
    }

    curl_socket_t sockfd;
    CURLcode res = curl_easy_getinfo(curl, CURLINFO_ACTIVESOCKET, &sockfd);
    if (res != CURLE_OK || sockfd < 0) {
        printf("Failed to retrieve active socket for sending frame!\n");
        free(frame);
        return;
    }

    // Send frame in chunks. 
    size_t nsent = 0;
    size_t sent_now = 0;
    while (nsent < frame_size) {
        size_t remaining = frame_size - nsent;
        size_t chunk_size = (remaining < 1024) ? remaining : 1024;
        res = curl_easy_send(curl, &frame[nsent], chunk_size, &sent_now);
        if (res != CURLE_OK || sent_now == 0) {
            printf("Failed to send WebSocket frame chunk!\n");
            break;
        }
        nsent += sent_now;
    }

    if (nsent == frame_size) {
        printf("Sent %zu bytes in total\n", nsent);
    }
    free(frame);
}

void GW_SendFrame(CURL *curl, const char *message) {
    printf("Sending message: %s\n", message);
    size_t msg_len = strlen(message);
    if ((msg_len > 125) && (msg_len <= 65535)){
        printf("Sending Extended Length message\n");
        GW_SendLargeFrame(curl, message);
        return;
    } else if(msg_len > 65535) {
        printf("Message too long. Im not sending this\n");
        return;
    }
    uint8_t mask[4];
    generate_mask(mask);

    size_t frame_size = 6 + msg_len; // 1 byte for FIN + RSV + opcode, 1 byte for MaskBit+PayloadLen, 4 bytes for Mask, + message length
    uint8_t *frame = (uint8_t *)malloc(frame_size);

    // WebSocket Header
    frame[0] = 0x81;  // FIN bit + Text frame (Opcode 0x1) = 0b1 000 0001
    frame[1] = 0x80 | msg_len;  // Mask bit set + payload length = 0b1 0000000

    memcpy(&frame[2], mask, 4);

    for (size_t i = 0; i < msg_len; i++) {
        frame[6 + i] = message[i] ^ mask[i % 4];  //mask each byte of the message by cycling through the mask bytes, and XOR-ing with the message bytes
    }

    curl_socket_t sockfd;
    CURLcode res = curl_easy_getinfo(curl, CURLINFO_ACTIVESOCKET, &sockfd);
    if (res != CURLE_OK || sockfd < 0) {
        printf("Failed to retrieve active socket for sending frame!\n");
        free(frame);
        return;
    }

    size_t nsent = 0;
    size_t sent_now = 0;
    while (nsent < frame_size) {
        size_t remaining = frame_size - nsent;
        size_t chunk_size = remaining < 1024 ? remaining : 1024;  // Send in chunks
        res = curl_easy_send(curl, &frame[nsent], chunk_size, &sent_now);
        if (res != CURLE_OK || sent_now == 0) {
            printf("Failed to send WebSocket frame chunk!\n");
            break;
        }
        nsent += sent_now;  // Update nsent with sent_now
    }

    if (nsent == frame_size) {
        printf("Sent %zu bytes in total\n", nsent);
    }
    free(frame);
}

void unmask_data(uint8_t *data, size_t data_len, uint8_t *mask) {
    for (size_t i = 0; i < data_len; i++) {
        data[i] ^= mask[i % 4];
    }
}

void curl_PollRecv(CURL *curl, void *buffer, size_t buflen){
    curl_socket_t sockfd;
    CURLcode res = curl_easy_getinfo(curl, CURLINFO_ACTIVESOCKET, &sockfd); //gets the socket

    struct pollfd pfd = { .fd = sockfd, .events = POLLIN };
    size_t bytes_received = 0;
    size_t total_received = 0;

    
    while (total_received < buflen) {
        // Attempt to receive data
        LightLock_Lock(&CurlGatewayLock);
        res = curl_easy_recv(curl, (uint8_t *)buffer + total_received, buflen - total_received, &bytes_received);
        LightLock_Unlock(&CurlGatewayLock);

        if (res == CURLE_OK && bytes_received > 0) {
            total_received += bytes_received;  // Accumulate received bytes
        } else if (res == CURLE_AGAIN) {
            // Poll the socket for readiness
            int poll_res = poll(&pfd, 1, 500); 
            if (poll_res == -1) {
                perror("poll() error");
                return;
            }
        } else {
            printf("Error during curl_easy_recv: %s\n", curl_easy_strerror(res));
            return;
        }
    }

    return;  // Success
}

char *GW_ReceiveFrame(CURL *curl){
    uint8_t header[2];
    curl_PollRecv(curl, header, sizeof(header));

    uint8_t opcode = header[0] & 0b00001111;
    uint8_t first_payload_len = header[1] & 0b01111111;

    uint64_t payload_len = first_payload_len;
    //printf("Payload length: %lld\n", payload_len);
    switch (payload_len){
    case 127:
        uint8_t xl_extended_header[8];
        curl_PollRecv(curl, xl_extended_header, sizeof(xl_extended_header));
        payload_len = 0;
        for (int i = 0; i < 8; i++) {
            payload_len |= ((uint64_t)xl_extended_header[i]) << (56 - 8 * i);
        }
        printf("Extra-Extended (actual) payload length: %lld", payload_len);
        break;
    case 126:
        uint8_t extended_header[2];
        curl_PollRecv(curl, extended_header, sizeof(extended_header));
        payload_len = (extended_header[0] << 8) | extended_header[1];
        printf("Extended (actual) payload length: %lld\n", payload_len);
        break;
    default: 
        if (payload_len <= 125){
            break;
        } else {
            return NULL;
        }
    }
    
    char * received_payload = malloc(payload_len+1);
    if (received_payload == NULL) {
        printf("Memory allocation for received_payload failed!\n");
        return NULL;
    }
    curl_PollRecv(curl, received_payload, payload_len);

    switch (opcode) {
    case 1:
        received_payload[payload_len] = '\0';
        printf("Text Payload received!\n");
        return received_payload;
        //do not forget to free payload outside
    case 2:
        printf("Warning: Binary Payload received!\n");
        break;
    case 8:
        printf("Server wants to close connection :(\n");
        free(received_payload);
        break;
    case 9:
        printf("Server pinged!\n"); //do i need to respond to the ping??
        free(received_payload);
        break;
    case 0xA:
        printf("Server ponged??\n"); //uhhh
        free(received_payload);
        break;
    default:
        printf("Warning: Fucked up opcode detected\n");
        free(received_payload);
        break;
    }
    
    //print_frame_binary(received_payload, payload_len);
    return NULL;
}


#endif