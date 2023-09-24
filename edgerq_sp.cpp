/*
 * Copyright (C) [2023] Milan Kazarka
 * Email: milan.kazarka.office@gmail.com
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#include <limits.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <unistd.h>
#include <signal.h>
#include <semaphore.h>
#include <tinyxml2.h>
#include "msggram.hpp"
#include <string.h>
#include "base64.hpp"
#include <signal.h>
#include "hex.hpp"
#include "list.hpp"
#include "time.hpp"
#include "common.hpp"
#include <arpa/inet.h>
#include <stdarg.h>

#define NMSG_CONSTRUCTS 100
#define UDP_BUFFER_SIZE 1024*64
#define TCP_READ_BUFFER_SIZE 4096 // 64*1024

#define _DYNAMIC_TIMEOUT_DEFAULT_SEC 3 // in seconds
#define _DYNAMIC_TIMEOUT_DEFAULT_USEC 0 // in milliseconds

RQMSG inrqmsgs[NMSG_CONSTRUCTS]; // max number of messages we reconstruct at any given time

typedef struct SpService {
    const char id[37]; // UUID
    bool registered; // is the service registered at SC?

    int port;
    char *address;
} SpService;

typedef struct SpPipe {
    const char id[37]; // UUID
    int sockfd;
    struct sockaddr_in consumerAddr;
    socklen_t addrLen;
    LinkedList services;
    pthread_mutex_t sendMutex;
    bool initialized; // initialized
} SpPipe;

typedef struct SpSetup {
    LinkedList pipes;
} SpSetup;

// #todo - we shouldn't be holding another instance of the XML
typedef struct SpRequest {
    SpService *service;
    SpPipe *pipe;
    pthread_t thread_id;
    char *request_id;
    char *payload;
} SpRequest;

SpSetup globalSpSetup;

char* dynamic_sprintf(const char* format, ...);
void udpsend(SpPipe *pipe, const char *message);
void *udpreceive_thread(void *arg);
bool runPipe(SpPipe *pipe);
bool loadConfigurationFile(const char *filename, SpSetup *setup);
void runServiceRequest(SpRequest *sprequest);
void* processRequest_thread(void* requestptr);
void onMsg(SpPipe *pipe, const char *payload, int pl_len);

char* dynamic_sprintf(const char* format, ...) {
    printf("dynamic_sprintf\n");
    va_list args;
    int len;

    va_start(args, format);

    // compute the complete length
    len = vsnprintf(NULL, 0, format, args);

    // allocate memmory+1
    char* buffer = (char*)malloc(len + 1);
    if (!buffer) {
        va_end(args);
        return NULL;
    }

    // write to the allocated buffer
    vsnprintf(buffer, len + 1, format, args);

    va_end(args);

    printf("dynamic_sprintf end\n");
    return buffer; // free upstream
}

void udpsend(SpPipe *pipe, const char *message) {
    verbose("udpsend message(%s)\n",message);

    pthread_mutex_lock(&pipe->sendMutex);

    int msgidcopy = 1;

    unsigned int gramsize = (64*1024)-1024;
    unsigned int msglen = (unsigned int)strlen(message);
    unsigned int countdown = msglen;
    unsigned int size = gramsize;
    unsigned int gramindex = 0;
    unsigned int dataindex = 0;
    unsigned int ngrams = 0;
    unsigned int dataoffset = 0;

    while(countdown!=0) {
        if (countdown<size)
            size = countdown;
        countdown-=size;
        ngrams++;
    }
    size = gramsize;
    countdown = msglen;

    while(countdown!=0) {
        if (countdown<size)
            size = countdown;

        // #todo - change from just raw pointing to a position in data to using msggram.hpp - RQMSGRAW
        unsigned int index = 0;
        char *rqmsgraw = (char*)malloc(size+1024);
        memcpy(rqmsgraw,&msgidcopy,sizeof(unsigned long long));
        index+=sizeof(unsigned long long);
        memcpy(rqmsgraw+index,&ngrams,sizeof(unsigned int));
        index+=sizeof(unsigned int);
        unsigned int thisindex = gramindex;
        memcpy(rqmsgraw+index,&thisindex,sizeof(unsigned int));
        index+=sizeof(unsigned int);
        dataoffset = index;
        memcpy(rqmsgraw+index,message+dataindex,size);
        index+=size;
        rqmsgraw[index]=0x00;

        verbose("    sending msgid(%d) ngrams(%d) index(%d) data(%s) size(%d)\n",msgidcopy,ngrams,thisindex,rqmsgraw+dataoffset,index);
        
        if (sendto(pipe->sockfd, rqmsgraw, index, 0, (struct sockaddr *)&pipe->consumerAddr, pipe->addrLen) == -1) {
            perror("sendto");

            verbose("failed to send data\n");
            exit(EXIT_SUCCESS); // #todo - evaluate
        } else {
            
        }

        free(rqmsgraw);

        countdown-=size;
        gramindex++;
        dataindex+=size;
    }
    pthread_mutex_unlock(&pipe->sendMutex);

    verbose("udpsend finish\n");
}

void runServiceRequest(SpRequest *sprequest) {
    if (!sprequest)
        return;

    char *decoded_request_payload = base64Decode(sprequest->payload);

    if (!decoded_request_payload) {    
        printf("error parsing XML message & payload payload(%s)\n",sprequest->payload);
        return;
    }

    //
    struct sockaddr_in server_addr;
    const char *service_address = sprequest->service->address;

    int sock = 0, valread;
        
    // Create socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket creation error");
        //exit(EXIT_FAILURE);
        free(decoded_request_payload);
        return;
    }
    struct timeval timeout;
    timeout.tv_sec =    _DYNAMIC_TIMEOUT_DEFAULT_SEC;  // Set the timeout in seconds // #todo - should be a parameter
    timeout.tv_usec =   _DYNAMIC_TIMEOUT_DEFAULT_USEC;  // Set the timeout in microseconds
        
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout)) < 0) {
        perror("setsockopt error");
        // Handle the error
        free(decoded_request_payload);
        close(sock);
        return;
    }
    if (setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout)) < 0) {
        perror("setsockopt error");
        // Handle the error
        free(decoded_request_payload);
        close(sock);
        return;
    }
    // Setup server address
    memset(&server_addr, '0', sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(sprequest->service->port);
        
    if (inet_pton(AF_INET, service_address, &server_addr.sin_addr) <= 0) {
        perror("inet_pton error");
        //exit(EXIT_FAILURE); // #todo - error instead of exit
        free(decoded_request_payload);
        close(sock);
        return;
    }
        
    // Connect to server
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect error");
        free(decoded_request_payload);
        close(sock);
        return;
    }
        
    // Send payload to server
    if (send(sock, decoded_request_payload, strlen(decoded_request_payload), 0) < 0) {
        perror("send error");
        free(decoded_request_payload);
        close(sock);
        return;
    }

    char* buffer = new char[TCP_READ_BUFFER_SIZE + 1]; // +1 for null terminator
    ssize_t bytesRead = 0;
    int totalBytesRead = 0;
    int bufferSize = TCP_READ_BUFFER_SIZE + 1; // +1 for null terminator
    int cycle = 0; // #todo - limit by number of cycles too

    while ((bytesRead = read(sock, buffer + totalBytesRead, bufferSize - totalBytesRead - 1)) > 0) { // -1 to account for null terminator
        totalBytesRead += bytesRead;

        // Null-terminate the buffer at the current end of data
        buffer[totalBytesRead] = '\0'; 

        // Check if the buffer is full (excluding the null terminator)
        if (totalBytesRead == bufferSize - 1) {
            // #todo - make max size consistent - this is just a very simple calculation
            if (totalBytesRead + TCP_READ_BUFFER_SIZE > TCP_READ_BUFFER_SIZE * (NMSG_CONSTRUCTS-1)) {
                break;
            }
            // Resize the buffer to make it larger
            bufferSize += TCP_READ_BUFFER_SIZE;
            char* newBuffer = new char[bufferSize + 1]; // +1 for null terminator
            memcpy(newBuffer, buffer, totalBytesRead);
            delete[] buffer;
            buffer = newBuffer;
        } else {
            // In case the bytes read is smaller than the initial buffer, assume that we got everything
            break;
        }
        cycle++;
    }

    char *b64 = base64Encode(buffer);
    if (b64) {
        //const char *request_id = sprequest->service_elem->FirstChildElement("request")->Attribute("id");
        char *responsePayload = dynamic_sprintf("<?xml version=\"1.0\" encoding=\"UTF-8\"?><message><pipe_id>%s</pipe_id><services><service uuid=\"%s\" name=\"service_name\" type=\"tcp\"><response request_id=\"%s\"><payload>%s</payload></response></service></services></message>\n",sprequest->pipe->id,sprequest->service->id,sprequest->request_id,b64);
        if (responsePayload) {
            printf("respond:(%s)\n",responsePayload);
            udpsend(sprequest->pipe,responsePayload);
            free(responsePayload);
        } else {
            // #todo - add message if needed
            printf("error: could not construct response\n");
        }
        free(b64);
    }

    free(decoded_request_payload);
    delete[] buffer;
    close(sock);
}

void* processRequest_thread(void* requestptr) {

    if (!requestptr)
        return NULL;

    SpRequest *sprequest = (SpRequest*)requestptr;
    runServiceRequest(sprequest);

    free(sprequest->request_id);
    free(sprequest->payload);
    free(sprequest);

    return NULL;
}

void onMsg(SpPipe *pipe, const char *payload, int pl_len) {
    if (!payload)
        return;
    
    // #todo - do not do this double, just once
    tinyxml2::XMLDocument doc; // = new tinyxml2::XMLDocument(); // #todo #refactoring - parsing is done twice
    
    if (doc.Parse((const char*)payload, pl_len) != tinyxml2::XML_SUCCESS) {
        printf("failed to parse payload(%s)\n",payload);
        return;
    }

    if (pipe->initialized) {
            
        printf("have response %ld\n",time(NULL));
            
        tinyxml2::XMLElement* message_elem = doc.FirstChildElement("message");
        if (!message_elem) {
                printf("could not find message element\n");
        } else {
            
            tinyxml2::XMLElement* servicesElement = message_elem->FirstChildElement("services");
            if (servicesElement) {

                for (tinyxml2::XMLElement* service_elem = servicesElement->FirstChildElement("service"); service_elem; service_elem = service_elem->NextSiblingElement("service")) {
                    
                    const char *uuid_attr = service_elem->Attribute("uuid");
                    if (uuid_attr) {
                        if (strlen(uuid_attr)!=36) {
                            // #todo - message
                            continue;
                        }
                    } else {
                        // #todo - message
                        continue;
                    }

                    int index = 0; // #todo #dumb
                    lockList(&pipe->services);
                    Node* current = pipe->services.head;
                    SpService *service = NULL;
                    while (current != NULL) {
                        service = (SpService*)current->data;

                        if (strncmp((char*)service->id,uuid_attr,36)==0) {
                            break;
                        } else {
                            service = NULL;
                        }
                        current = current->next;
                    }
                    unlockList(&pipe->services);

                    if (service) {

                        // found service

                        printf("found service\n");

                        char *request_id = NULL;
                        char *payload = NULL;
                        tinyxml2::XMLElement *request_elem = service_elem->FirstChildElement("request");
                        if (request_elem) {
                            request_id = (char*)malloc(sizeof(request_elem->Attribute("id"))+1);
                            strcpy(request_id,request_elem->Attribute("id"));
                            tinyxml2::XMLElement *payload_elem = request_elem->FirstChildElement("payload");
                            if (payload_elem) {
                                if (payload_elem->GetText()) {
                                    payload = (char*)malloc(strlen(payload_elem->GetText())+1);
                                    strcpy(payload,payload_elem->GetText());
                                }
                            }
                        }

                        if (request_id && payload) {
                            SpRequest *sprequest = (SpRequest*)malloc(sizeof(SpRequest));
                            sprequest->service = service;
                            sprequest->pipe = pipe;
                            sprequest->request_id = request_id;
                            sprequest->payload = payload;

                            pthread_attr_t attr;
                            int rc;
                            rc = pthread_attr_init(&attr);
                            rc = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
                                                    
                            if (pthread_create(&sprequest->thread_id, &attr, processRequest_thread, sprequest) != 0) {
                                perror("pthread_create");
                                free(sprequest);
                                return;
                            } else {
                                //msg->thread_id = thread_id;
                            }

                            pthread_attr_destroy(&attr);
                        }

                    }

                }
            }

        }
        
        return;
    }

    tinyxml2::XMLElement* pipe_id_element = doc.FirstChildElement("message")->FirstChildElement("response")->FirstChildElement("pipe_id");
    if (!pipe_id_element) {
        printf("Failed to find pipe_id element in payload");
        //unlock();
        return;
    }

    if (strlen(pipe_id_element->GetText())!=36) {
        printf("invalid uuid length\n");
        return;
    }
    strcpy((char*)pipe->id,pipe_id_element->GetText());

    printf("Received pipe_id(%s)\n",pipe->id);

    // register our services
    Node* current = pipe->services.head;
    while (current != NULL) {
        SpService *service = (SpService*)current->data;
        
        char *registerServicePayload = dynamic_sprintf("<?xml version=\"1.0\" encoding=\"UTF-8\"?><message><pipe_id>%s</pipe_id><services><service uuid=\"%s\" name=\"service_name\" type=\"tcp\"></service></services></message>\n",pipe->id,service->id);
        if (registerServicePayload) {
            udpsend(pipe,registerServicePayload);
            free(registerServicePayload);
        } else {
            // #todo - add message if needed
            printf("error: could not construct response\n");
        }

        current = current->next;
    }

    pipe->initialized = true;
    
}

void *udpreceive_thread(void *arg) {
    SpPipe *pipe = (SpPipe*)arg;

    socklen_t addr_len;
    char buffer[UDP_BUFFER_SIZE+1];

    char *completemsg = NULL;

    while (1) {
        // Receive a message
        addr_len = sizeof(pipe->consumerAddr);
        ssize_t num_bytes = recvfrom(pipe->sockfd, &buffer, UDP_BUFFER_SIZE, 0, (struct sockaddr *)&pipe->consumerAddr, &addr_len);
        if (num_bytes == -1) {
            perror("recvfrom");
            break;
        }

        buffer[num_bytes] = '\0';

        // put the data in the right format
        RQMSGRAW *rqmsgraw = (RQMSGRAW*)malloc(sizeof(RQMSGRAW));
        unsigned int index = 0;
        memcpy(&rqmsgraw->msgid,buffer,sizeof(unsigned long long));
        index+=sizeof(unsigned long long);
        memcpy(&rqmsgraw->ngrams,buffer+index,sizeof(unsigned int));
        index+=sizeof(unsigned int);
        memcpy(&rqmsgraw->index,buffer+index,sizeof(unsigned int));
        index+=sizeof(unsigned int);
        unsigned int chunksize = num_bytes-index;
        rqmsgraw->data = (char*)malloc(chunksize+1);
        memcpy(rqmsgraw->data,buffer+index,chunksize);
        rqmsgraw->data[chunksize]=0x00;
        printf("in(%s) size(%d) ngrams(%d)\n",rqmsgraw->data,chunksize,rqmsgraw->ngrams);

        int TTL = 3; // #todo - add a TTL param into SP as is in SC

        RQMSG *rqmsg = NULL;
        for(int n = 0; n < NMSG_CONSTRUCTS; n++) {
            
            if (inrqmsgs[n].timestamp+TTL<time(NULL)) {
                invalidateRQMSG(&inrqmsgs[n]);
                continue;
            }
            if (!rqmsg) {
                if (inrqmsgs[n].msgid==rqmsgraw->msgid && !rqmsg) {
                    rqmsg = &inrqmsgs[n];
                    //break; // we do not break so that the TTL check continues in this loop
                }
            }
        }
        if (!rqmsg) {
            for(int n = 0; n < NMSG_CONSTRUCTS; n++) {
                if (inrqmsgs[n].msgid==0) {
                    rqmsg = &inrqmsgs[n];
                    rqmsg->timestamp = time(NULL);
                    rqmsg->ngrams = rqmsgraw->ngrams;
                    rqmsg->msgid = rqmsgraw->msgid;
                    break;
                }
            }
        }
        completemsg = NULL;
        if (rqmsg) {
            printf("rqmsg ngrams(%d)\n",rqmsg->ngrams);
            if (rqmsg->grams[rqmsgraw->index].data) {
                // something went wrong - there shouldn't be data at this index
                rqmsg = NULL;
                free(rqmsgraw->data);
                rqmsgraw->data = NULL;
            } else {
                rqmsg->grams[rqmsgraw->index].size = chunksize;
                rqmsg->grams[rqmsgraw->index].data = rqmsgraw->data; // freed in invalidateRQMSG
                completemsg = dataFromRQMSG(rqmsg);
            }
        } else {
            printf("warning: didn't find an rqmsg slot\n");
            free(rqmsgraw->data);
            rqmsgraw->data = NULL;
        }
        //free(rqmsgraw->data);
        free(rqmsgraw);

        if (!completemsg)
            continue;

        printf("Received message from server: length(%ld)\n", num_bytes);
        //printHex(completemsg,num_bytes);
        // process message

        onMsg(pipe,completemsg,strlen(completemsg)); // #todo - just send the number of bytes we read, don't count again

        free(completemsg);
        invalidateRQMSG(rqmsg);

    }

    return NULL;
}

bool runPipe(SpPipe *pipe) {
    printf("runPipe\n");
    if (!pipe)
        return false;
    
    // #todo - add pipe initialization
    pipe->initialized = false;

    if ((pipe->sockfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }

    // Set up the server address
    memset(&pipe->consumerAddr, 0, sizeof(pipe->consumerAddr));
    pipe->consumerAddr.sin_family = AF_INET;
    pipe->consumerAddr.sin_port = htons(12345);
    if (inet_pton(AF_INET, "127.0.0.1", &(pipe->consumerAddr.sin_addr)) <= 0) {
        perror("inet_pton");
        exit(1);
    }
    pipe->addrLen = sizeof(pipe->consumerAddr);
    
    pthread_t receiveThread;

    pthread_attr_t attr;
    int rc; // #todo (should not be any problems)
    rc = pthread_attr_init(&attr);
    rc = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    // Start the UDP client
    if (pthread_create(&receiveThread, &attr, udpreceive_thread, (void *)pipe) != 0) {
        perror("pthread_create");
        exit(1);
    }

    pthread_attr_destroy(&attr);

    const char *msg = "<?xml version=\"1.0\" encoding=\"UTF-8\"?><message><request_pipe_id></request_pipe_id></message>";
    udpsend(pipe,msg);

    printf("finished\n");

    return true;
}

bool loadConfigurationFile(const char *filename, SpSetup *setup) {
    printf("loadConfigurationFile\n");

    tinyxml2::XMLDocument doc;
    doc.LoadFile(filename);

    tinyxml2::XMLElement* sp_elem = doc.FirstChildElement("service_provider");
    if (!sp_elem) {
        printf("Error: could not find <service_provider> element\n");
        return false;
    }

    tinyxml2::XMLElement* pipes_elem = sp_elem->FirstChildElement("pipes");
    if (!pipes_elem) {
        printf("Error: could not find <pipes> element\n");
        return false;
    }

    for (tinyxml2::XMLElement* pipe_elem = pipes_elem->FirstChildElement("pipe"); pipe_elem; pipe_elem = pipes_elem->NextSiblingElement("pipe")) {
        SpPipe *pipe = (SpPipe*)malloc(sizeof(SpPipe));
        // #todo - add pipe initialization
        initLinkedList(&pipe->services,LIST_USEMUTEX);
        pthread_mutex_init(&pipe->sendMutex, NULL);

        // parse services
		tinyxml2::XMLElement* services_elem = pipe_elem->FirstChildElement("services");
		if (!services_elem) {
			printf("Failed to find 'services' element in configuration file\n");
			return false;
		}

        // #todo - possibly don't do inline here, but in pipe initialization
        for (tinyxml2::XMLElement* service_elem = services_elem->FirstChildElement("service"); service_elem != NULL; service_elem = service_elem->NextSiblingElement("service")) {
            
            tinyxml2::XMLElement* uuid_elem = service_elem->FirstChildElement("uuid");
            if (!uuid_elem) {
                continue;
            }

            if (strlen(uuid_elem->GetText())!=36) {
                printf("Invalid service id (invalid uuid)\n");
                continue;
            }

            tinyxml2::XMLElement* service_port_elem = service_elem->FirstChildElement("port");
            if (!service_port_elem) {
                continue;
            }

            tinyxml2::XMLElement* service_hostname_elem = service_elem->FirstChildElement("hostname");
            if (!service_hostname_elem) {
                continue;
            }

            const char* hostname = service_hostname_elem->GetText(); // #todo
            int service_port = service_port_elem->IntText(); // #todo - add checks

            // #todo - add SpService initialize function
            SpService *service = (SpService*)malloc(sizeof(SpService));
            service->registered = true; // #todo - check for SC getting back to us that service has been registered
            service->address = (char*)malloc(strlen(hostname+1));
            strcpy(service->address,hostname);
            service->port = service_port;

            strcpy((char*)service->id,uuid_elem->GetText());
            Node *node = (Node*)malloc(sizeof(Node));
            node->data = service;
            addNode(&pipe->services,node,LIST_USEMUTEX);
        }
        runPipe(pipe);
    }

    return true;
}

int main(int argc, char *argv[]) {
    
    if (argc < 2) {
        printf("Usage: %s <filename>\n",argv[0]);
        return 1;
    }
    
    initLinkedList(&globalSpSetup.pipes,LIST_USEMUTEX);
    if (!loadConfigurationFile(argv[1],&globalSpSetup)) {
        printf("Error: failed to load configuration file\n");
        return 1;
    }

    while(1)
        sleep(1);

    return 0;
}
