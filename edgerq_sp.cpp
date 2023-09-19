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

#define NMSG_CONSTRUCTS 100
#define UDP_BUFFER_SIZE 1024*64

RQMSG inrqmsgs[NMSG_CONSTRUCTS]; // max number of messages we reconstruct at any given time

typedef struct SpService {
    const char id[37]; // UUID
    bool registered; // is the service registered at SC?
} SpService;

typedef struct SpPipe {
    const char id[37]; // UUID
    int sockfd;
    struct sockaddr_in consumerAddr;
    socklen_t addrLen;
    LinkedList services;
    pthread_mutex_t sendMutex;
    bool response_sent;
} SpPipe;

typedef struct SpSetup {
    LinkedList pipes;
} SpSetup;

typedef struct SpRequest {

} SpRequest;

SpSetup globalSpSetup;

void udpsend(SpPipe *pipe, const char *message);
void *udpreceive_thread(void *arg);
bool runPipe(SpPipe *pipe);
bool loadConfigurationFile(const char *filename, SpSetup *setup);
void* processMsg_thread(void* msgptr);
void onMsg(SpPipe *pipe, const char *payload, int pl_len);

void udpsend(SpPipe *pipe, const char *message) {
    verbose("udpsend message(%s)\n",message);

    //if (getpid()!=parentPid) {
    //    verbose("warning: do not call udpsend from child process\n");
    //    return;
    //}

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

        //sem_wait(binarySemaphore);
        
        if (sendto(pipe->sockfd, rqmsgraw, index, 0, (struct sockaddr *)&pipe->consumerAddr, pipe->addrLen) == -1) {
            perror("sendto");

            //sem_post(binarySemaphore);

            verbose("failed to send data\n");
            exit(EXIT_SUCCESS); // #todo - evaluate
        } else {
            
        }

        //sem_post(binarySemaphore);

        free(rqmsgraw);

        countdown-=size;
        gramindex++;
        dataindex+=size;
    }
    verbose("udpsend finish\n");
}

void* processMsg_thread(void* msgptr) {
    return NULL;
}

void onMsg(SpPipe *pipe, const char *payload, int pl_len) {
    if (!payload)
        return;

    tinyxml2::XMLDocument doc; // = new tinyxml2::XMLDocument(); // #todo #refactoring - parsing is done twice
    
    if (doc.Parse((const char*)payload, pl_len) != tinyxml2::XML_SUCCESS) {
        printf("failed to parse payload(%s)\n",payload);
        return;
    }

    if (pipe->response_sent) {
            
        printf("have response\n");
            
        //std::cerr << "Initialization already done, parsing request" << std::endl;

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

                    Node* current = pipe->services.head;
                    while (current != NULL) {
                        SpService *service = (SpService*)current->data;

                        if (strncmp((char*)service->id,uuid_attr,36)==0) {
                            
                            // found service

                            const char *request_id = service_elem->FirstChildElement("request")->Attribute("id");

                            // response << "<?xml version=\"1.0\" encoding=\"UTF-8\"?><message><pipe_id>" << m_pipe_id << "</pipe_id><services><service uuid=\"" << service->GetUUID() << "\" name=\"" << service->GetName() << "\" type=\"tcp\"><response request_id=\"" << msg->service_elem->FirstChildElement("request")->Attribute("id") << "\"><payload>" << base64payload << "</payload></response></service></services></message>\n";
                            char registerServicePayload[1024];
                            strcpy((char*)registerServicePayload,"<?xml version=\"1.0\" encoding=\"UTF-8\"?><message><pipe_id>");
                            strcat((char*)registerServicePayload,(char*)pipe->id);
                            strcat((char*)registerServicePayload,"</pipe_id><services><service uuid=\"");
                            strcat((char*)registerServicePayload,(char*)service->id);
                            strcat((char*)registerServicePayload,"\" name=\"service_name\" type=\"tcp\"><response request_id=\"");
                            strcat((char*)registerServicePayload,request_id);
                            strcat((char*)registerServicePayload,"\"><payload>SFRUUC8xLjEgMjAwIE9LCkRhdGU6IFR1ZSwgMTkgU2VwIDIwMjMgMTQ6NTg6MDAgR01UClNlcnZlcjogQXBhY2hlLzIuNC4zOCAoRGViaWFuKQpDb250ZW50LUxlbmd0aDogMTMKQ29udGVudC1UeXBlOiB0ZXh0L3BsYWluCkNvbm5lY3Rpb246IGNsb3NlCgpIZWxsbywgV29ybGQh</payload></response></service></services></message>\n");
                            //
                            udpsend(pipe,(char*)registerServicePayload);
                        }

                        current = current->next;
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
        
        char registerServicePayload[1024]; // #todo - use propper size
        // #todo - use the service name we set in the configuration file
        // #todo - make this nicer:
        strcpy((char*)registerServicePayload,"<?xml version=\"1.0\" encoding=\"UTF-8\"?><message><pipe_id>");
        strcat((char*)registerServicePayload,(char*)pipe->id);
        strcat((char*)registerServicePayload,"</pipe_id><services><service uuid=\"");
        strcat((char*)registerServicePayload,(char*)(char*)service->id);
        strcat((char*)registerServicePayload,"\" name=\"service_name\" type=\"tcp\"></service></services></message>\n");
        udpsend(pipe,(char*)registerServicePayload);

        current = current->next;
    }

    pipe->response_sent = true;
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
            if (inrqmsgs[n].msgid==rqmsgraw->msgid && !rqmsg) {
                rqmsg = &inrqmsgs[n];
                break;
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
        printHex(completemsg,num_bytes);
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
    pipe->response_sent = false;

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

    // Start the UDP client
    if (pthread_create(&receiveThread, NULL, udpreceive_thread, (void *)pipe) != 0) {
        perror("pthread_create");
        exit(1);
    }

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
        initLinkedList(&pipe->services,LIST_USEMUTEX);

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

            // #todo - add SpService initialize function
            SpService *service = (SpService*)malloc(sizeof(SpService));
            service->registered = true; // #todo - check for SC getting back to us that service has been registered
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
