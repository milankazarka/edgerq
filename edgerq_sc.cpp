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
#include <uuid/uuid.h>
#include <string.h>
#include "base64.hpp"
#include <uuid/uuid.h>
#include <signal.h>
#include "hex.hpp"
#include "list.hpp"
#include "time.hpp"
#include "common.hpp"
#include <arpa/inet.h>

#define SC_MAX_REQUESTS 100 // maximum number of requests each service can hold at any time
#define SC_TERMINATE_CHILD_PROCESSES

#define SEMAPHORE_PROTECTION

pid_t parentPid;

// #todo - should be per service
sem_t *binarySemaphore; // Pointer to the semaphore

// simulation
#define MAX_UDP_MSG_SIZE 1024*64 // buffer for UDP read #todo - rename
#define NREQUESTS 20 // maximum requests we are constructing out of segments at any given time
int sockfd;
struct sockaddr_in server_addr, client_addr;
socklen_t addr_len;
RQMSG rqmsgs[NREQUESTS];
// \simulation

// this effectively limits the size of the 'id' in Request to int. We leave the Request id as 'long long'
// in case we switch from defining these as sig_atomic_t
//
volatile sig_atomic_t msgid = 1; // itterated in parent before spawning, only used to identify outgoing message by child process
volatile sig_atomic_t pipemsgid = 1;

typedef struct Service {
    const char *id;
    int port;
    bool inaddrAny;

    int maxConnections;
    int requestBuffer;
    int requestTtl;

    LinkedList requests;
} Service;

typedef struct Setup {
    int listenerPort;
    bool inaddrAny;
    int maxConnections;
    int requestBuffer;
    int requestTtl;
    LinkedList services;
    // #todo - this would be a good place for pipes
} Setup;

// #todo - we need to create a structure passed down to the child_process that would have both
// the Service and Request - to be able to do things like lock the binary semaphore
//
typedef struct Request {
    long long id; // we just copy our node's id
    volatile sig_atomic_t socket;
    int pId;
    volatile sig_atomic_t pipe_fd[2]; // Pipe for parent<->child process communication
    volatile sig_atomic_t pipe_fd_rev[2];
    pthread_t threadId;
} Request;

typedef struct Child_ConnectionThreadData {
    Service *service;
    Request *request;
} Child_ConnectionThreadData;

typedef struct ServiceDef { // #todo #refactoring
    char *id; // #todo - char or const char
} ServiceDef;

typedef struct Pipe {
    const char *id; // Id or id?
    LinkedList serviceDefs;
} Pipe;

Setup globalSetup;
LinkedList pipes;

void processRequestList(LinkedList* list, bool lock);
void closeSocket(int socket);
int isSocketOpen(int socket_fd);
void onNewConsumerConnection(int new_socket);
void removeRequestsWithDuplicateSocket(LinkedList* list,int socket,bool lock);
void invalidateRequestsWithDuplicateSocket(LinkedList* list,int socket,bool lock);
void initializePipe(Pipe *pipe);
void initializeService(ServiceDef *service);
Pipe *pipeById(const char *id);
char* GenerateUUID();
ServiceDef *serviceDefByIdInPipe(Pipe *pipe,const char *id); // #todo - evaluate if to only run on the one Pipe or check pipes - if there can be multiples
Service *serviceByServiceDef(LinkedList* services, ServiceDef *serviceDef, bool lock);
void debugRequests(LinkedList* list, bool lock);
bool loadConfigurationFile(const char *filename);
bool runSetup(Setup *setup);
bool runService(Service *service);
bool initService(Service *service, const char *uuid, const char *name, int port);
void *watchdog(void *data);
void *pipeListener(void *data);

// Helper function to generate a new UUID
char* GenerateUUID() {
    uuid_t uuid;
    uuid_generate(uuid);

    char *uuidStr = (char*)malloc(37);
    uuid_unparse(uuid, uuidStr);
    return uuidStr;
}

// https://codereview.stackexchange.com/questions/29198/random-string-generator-in-c
char *mkrndstr(size_t length) { // const size_t length, supra

    static char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789,.-#'?!"; // could be const
    char *randomString;

    if (length) {
        randomString = (char*)malloc(length +1); // sizeof(char) == 1, cf. C99

        if (randomString) {
            int l = (int) (sizeof(charset) -1); // (static/global, could be const or #define SZ, would be even better)
            int key;  // one-time instantiation (static/global would be even better)
            for (int n = 0;n < length;n++) {        
                key = rand() % l;   // no instantiation, just assignment, no overhead from sizeof
                randomString[n] = charset[key];
            }

            randomString[length] = '\0';
        }
    }

    return randomString;
}

void initializePipe(Pipe *pipe) {
    initLinkedList(&pipe->serviceDefs,LIST_USEMUTEX);
    pipe->id = GenerateUUID();
}

void initializeService(ServiceDef *service,const char *id) {
    service->id = (char*)malloc(strlen(id)+1);
    strcpy(service->id,id); // #todo - check if the const is needed in service
}

Pipe *pipeById(const char *id) {
    verbose("pipeById\n");
    lockList(&pipes);
    Node* current = pipes.head;
    while (current != NULL) {
        Pipe *pipe = (Pipe*)current->data;
        verbose("    pipe id(%s)\n",pipe->id);
        if (strcmp(pipe->id,id)==0) {
            unlockList(&pipes);
            return pipe;
        }
        current = current->next;
    }
    unlockList(&pipes);
    return NULL;
}

ServiceDef *serviceDefByIdInPipe(Pipe *pipe,const char *id) {
    lockList(&pipe->serviceDefs);
    Node* current = pipe->serviceDefs.head;
    while (current != NULL) {
        ServiceDef *service = (ServiceDef*)current->data;
        if (strcmp(service->id,id)==0) {
            unlockList(&pipe->serviceDefs);
            return service;
        }
        current = current->next;
    }
    unlockList(&pipe->serviceDefs);
    return NULL;
}

Service *serviceByServiceDef(LinkedList* services, ServiceDef *serviceDef, bool lock) {
    if (lock)
        lockList(services);
    
    Node* current = services->head;
    while (current != NULL) {
        Service *service = (Service*)current->data;
        if (strcmp(service->id,serviceDef->id)==0) {
            if (lock)
                unlockList(services);
            return service;
        }
        current = current->next;
    }

    if (lock)
        unlockList(services);
    
    return NULL;
}

void debugRequests(LinkedList* list, bool lock) {
    verbose("debugRequests\n");
    if (lock)
        lockList(list);
    int index = 0;
    Node* current = list->head;
    while (current != NULL) {
        if (current->data) {
            Request *request = (Request*)current->data;
            verbose("    node(%d) ts_ms(%lld) diff(%lld) pipe_fd[1]='%d'\n",index,current->timestamp_ms,getCurrentTimeMillis()-current->timestamp_ms,request->pipe_fd[1]);

        }
        current = current->next;
        index++;
    }
    if (lock)
        unlockList(list);
}

/** always either lock here or upstream
*/
void processRequestList(LinkedList* list, bool lock) {

    if (lock)
        lockList(list);

    long long currentTimeMs = getCurrentTimeMillis();
    Node* current = list->head;
    Node* previous = NULL;
    bool remove = false;

    while (current != NULL) {
        remove = false;
        // calculate the time elapsed since the node's timestamp
        long long diffTimeMs = currentTimeMs - current->timestamp_ms;
        long long threshold = globalSetup.requestTtl*1000; // 1s

        if (diffTimeMs > threshold) {
            verbose("WARNING: removing Request node due to timeout\n");
            remove = true;
        }

        Request *request = (Request*)current->data;
        if (request) {
            if (request->pipe_fd[1]==-1) {
                verbose("request->pipe_fd[1]==-1 <- flag to remove\n");
                remove = true;
            }
        }

        if (remove) {
            // remove the node from the list
            Node* nodeToRemove = current;
            
            if (previous == NULL) {
                list->head = current->next;
                current = list->head;
            } else {
                previous->next = current->next;
                current = current->next;
            }
            
            verbose("removing node(%lld) due to timeout or pipe closure\n",nodeToRemove->id);

            if (request) {
#ifdef SC_TERMINATE_CHILD_PROCESSES
                if (request->pId!=-1) {
                    verbose("sending SIGTERM to process %d\n",request->pId);
                    if (request->pipe_fd[1]!=-1) {
                        write(request->pipe_fd[1], " ", 1);
                        close(request->pipe_fd[1]);
                        request->pipe_fd[1] = -1;
                    }
                    kill(request->pId, SIGTERM); // Terminate the child process
                    verbose("waiting for SIGTERM to complete\n");
                    wait(NULL); // Wait for the child process to finish
                    verbose("SIGTERM completed\n");
                }
#endif
                if (request->pipe_fd[1]!=-1) {
                    write(request->pipe_fd[1], " ", 1);
                    close(request->pipe_fd[1]);
                    request->pipe_fd[1] = -1; // #todo - create separate function to invalidate a Request
                }
            } else {
                verbose("WARNING: found node without request\n");
            }

            free(request);
            free(nodeToRemove);

        } else {
            previous = current;
            current = current->next;
        }
    }

    if (lock)
        unlockList(list);
}

int isSocketOpen(int socket_fd) {
    int optval;
    socklen_t optlen = sizeof(optval);

    if (getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, &optval, &optlen) == -1) {
        perror("getsockopt");
        return 0; // Error occurred, assume socket is not open
    }

    return (optval == 0);
}

void closeSocket(int socket) {
    verbose("close socket %d\n",socket);
    if (socket<0)
        return;
    if (isSocketOpen(socket)) {
        close(socket);
    }
}

void invalidateRequest(Request *request) {
    closeSocket(request->socket);
    request->socket = -1;
}

/** We should make sure that we don't have the same socket descriptor in multiple requests.
*   This should be done right after we receive a new connection & before we do any work on it.
*/
void invalidateRequestsWithDuplicateSocket(LinkedList* list,int socket,bool lock) {
    if (lock)
        lockList(list);

    Node* current = list->head;
    while (current != NULL) {
        Request *request = (Request*)current->data;
        if (request) {
            if (request->socket==socket) {
                // we assume that the one passed is the open one
                request->socket = -1;
            }
        }
        current = current->next;
    }

    if (lock)
        unlockList(list);
}

void removeRequestsWithDuplicateSocket(LinkedList* list,int socket,bool lock) {
    if (lock)
        lockList(list);

    Node* current = list->head;
    Node* previous = NULL;

    while (current != NULL) {
        
        Request *request = (Request*)current->data;
        if (request->socket==socket) {
            // remove the node from the list
            Node* nodeToRemove = current;
            
            if (previous == NULL) {
                list->head = current->next;
                current = list->head;
            } else {
                previous->next = current->next;
                current = current->next;
            }
            
            free(request);
            free(nodeToRemove);
        } else {
            previous = current;
            current = current->next;
        }
    }

    if (lock)
        unlockList(list);
}

/**
* #todo - change from using a binary semaphore for locking to posix mutex as we should
* not be calling this function from child processes anyway
*/
void udpsend(const char *message, const struct sockaddr_in *addr, int addrlen) {
    verbose("udpsend message(%s)\n",message);

    if (getpid()!=parentPid) {
        verbose("warning: do not call udpsend from child process\n");
        return;
    }

    int msgidcopy = msgid;

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

        sem_wait(binarySemaphore);
        
        if (sendto(sockfd, rqmsgraw, index, 0, (struct sockaddr *)addr, addrlen) == -1) {
            perror("sendto");

            sem_post(binarySemaphore);

            verbose("failed to send data\n");
            exit(EXIT_SUCCESS); // #todo - evaluate
        } else {
            
        }

        sem_post(binarySemaphore);

        free(rqmsgraw);

        countdown-=size;
        gramindex++;
        dataindex+=size;
    }
    verbose("udpsend finish\n");
}

// do the same kind of segmentation as we do for UDP just for a pipe
//
void writePipe(int fd, char *message) {
    verbose("writePipe\n");

    int pipemsgidcopy = pipemsgid;

    LinkedList *grams = splitRawDataIntoGrams(message,pipemsgidcopy);
    if (grams) {
        Node* current = grams->head;
        while (current != NULL) {
            RQGRAM_HEADERDATA *headerdata = (RQGRAM_HEADERDATA*)current->data;
            RQGRAM_HEADER *header = (RQGRAM_HEADER*)headerdata->data;
            verbose("SENDING MSG GRAM SIZE(%d) MSGID(%lld) INDEX(%d) NGRAMS(%d) \n",
                headerdata->datalen,header->msgid,header->index,header->ngrams);
            write(fd,headerdata->data,headerdata->datalen);
            free(headerdata->data);
            free(headerdata);
            current->data = NULL;
            current = current->next;
        }
        cleanupLinkedList(grams);
        free(grams);
    } else {
        verbose("COULDN'T SPLIT MSG INTO GRAMS\n");
    }

    verbose("udpsend finish\n");
}

/** child process
*/
void* child_ConnectionThread(void *arg) {
    //char buffer[SC_REQUEST_BUFFER];
    int valread;

    verbose("child_ConnectionThread\n");

    Child_ConnectionThreadData *ctdata = (Child_ConnectionThreadData*)arg;

    Service *service = ctdata->service;
    Request *request = ctdata->request;

    char buffer[service->requestBuffer];

    valread = read(request->socket, buffer, service->requestBuffer-1);
    buffer[valread] = 0x00;
    verbose("read(%d) data(%s) from socket(%d)\n",valread,buffer,request->socket);

    // the child process needs to forward the data through the UDP socket

    Pipe *selectedPipe = NULL;
    ServiceDef *selectedService = NULL;
    Node* current = pipes.head;
        while (current != NULL) {
            selectedPipe = (Pipe*)current->data;
            current = current->next;
        }
    // #todo
    if (selectedPipe) {
        current = selectedPipe->serviceDefs.head;
        while (current != NULL) {
            selectedService = (ServiceDef*)current->data;
            current = current->next;
        }
    }
    verbose("pipe(%s) service(%s)\n",selectedPipe->id,selectedService->id);

    char *b64 = base64Encode(buffer);
    if (!b64) {
        // #todo - error
        printf("error: base64Encode didn't return encoded data\n");
        return NULL;
    }

    unsigned int responseLen = strlen(b64)+1024;
    char *response = (char*)malloc(responseLen); // #todo - 1024 is just an arbitrary number

    snprintf(response,responseLen,"<?xml version=\"1.0\" encoding=\"UTF-8\"?><message><pipe_id>\"%s\"</pipe_id><services><service uuid=\"%s\"><request id=\"%lld\"><payload>%s</payload></request></service></services></message>\n",selectedPipe->id,selectedService->id,request->id,b64);
    verbose("sending to pipe(%s)\n",response);
    
    writePipe(request->pipe_fd_rev[1],response);

    free(b64);
    free(response);

    verbose("child_ConnectionThread finish\n");

    return NULL;
}

// just a silly watchdog
// WARNING:
// - not sure that this is safe if we don't take action / sync it up with creation of child processes
void *watchdog(void *data) {
    while(getpid()==parentPid) {
        lockList(&globalSetup.services);

        Node* current = globalSetup.services.head;
        while (current != NULL) {
            Service *service = (Service*)current->data;
            processRequestList(&service->requests,true);
            current = current->next;
        }

        unlockList(&globalSetup.services);
        usleep(50000);
    }
    return NULL;
}

typedef struct PipeListener {
    int pipeFd;
    // #todo - another verification mechanism & info to check if we're processing data for the right request
} PipeListenerData;

/** we just forward data that comes through this pipe through UDP
*/
void *pipeListener(void *data) { // #todo - make sure it is understood that this is a thread
    if (!data)
        return NULL;

    char buffer[MAX_UDP_MSG_SIZE];
    char *completemsg = NULL;
    PipeListener *listener = (PipeListener*)data;
    RQMSG rqmsg;
    initializeRQMSG(&rqmsg);
    
    rqmsg.timestamp = time(NULL);

    printf("listen on pipeFd(%d) in pipeListener\n",listener->pipeFd);
    while(1) {
        ssize_t bytes_read = read(listener->pipeFd, buffer, sizeof(buffer));
        printf("    got data\n");
        
        // this should never happen as we should execute this only in the parent process
        //
        //if (getpid()!=parentPid) {
        //    // #todo - more cleanup
        //    printf("    returning, since we are not the parent\n");
        //    return NULL;
        //}
        
        if (bytes_read>0) {
            buffer[bytes_read] = '\0';

            // #todo - make this into a separate function

            // put the data in the right format
            RQMSGRAW *rqmsgraw = (RQMSGRAW*)malloc(sizeof(RQMSGRAW));
            unsigned int index = 0;
            memcpy(&rqmsgraw->msgid,buffer,sizeof(unsigned long long));
            index+=sizeof(unsigned long long);
            memcpy(&rqmsgraw->ngrams,buffer+index,sizeof(unsigned int));
            index+=sizeof(unsigned int);
            memcpy(&rqmsgraw->index,buffer+index,sizeof(unsigned int));
            index+=sizeof(unsigned int);
            unsigned int chunksize = bytes_read-index;
            rqmsgraw->data = (char*)malloc(chunksize+1);
            memcpy(rqmsgraw->data,buffer+index,chunksize);
            rqmsgraw->data[chunksize]=0x00;
            printf("in(%s) size(%d)\n",rqmsgraw->data,chunksize);

            completemsg = NULL;
            
            if (rqmsg.ngrams==0) { // #todo - make this nicer, this is horrible
                rqmsg.ngrams = rqmsgraw->ngrams;
                rqmsg.msgid = rqmsgraw->msgid;
            }

            printf("rqmsg ngrams(%d) rqmsgraw->index(%d)\n",rqmsg.ngrams,rqmsgraw->index);
            if (rqmsg.grams[rqmsgraw->index].data) {
                // something went wrong - there shouldn't be data at this index
                //rqmsg = NULL; // #todo - what's the alternative
                free(rqmsgraw->data);
                free(rqmsgraw);
                rqmsgraw->data = NULL;
                printf("    E1\n");
                break;
            } else {
                rqmsg.grams[rqmsgraw->index].size = chunksize;
                rqmsg.grams[rqmsgraw->index].data = rqmsgraw->data; // freed in invalidateRQMSG
                completemsg = dataFromRQMSG(&rqmsg);
            }
            
            printf("2\n");

            //free(rqmsgraw->data); // this is correct
            free(rqmsgraw); // only this and not ->data, since that is held in rqmsg nodes

            if (!completemsg) {
                printf("    msg not complete\n");
                continue;
            }

            printf("sending response through pipeListener\n");
            //printHex(completemsg,index);
            udpsend(completemsg,&client_addr,addr_len);
            printf("data forwarded through UDP\n");

            printf("3\n");
            close(listener->pipeFd); // we only transmit a single complete message here
            listener->pipeFd = -1;
            free(completemsg);
            break;
        } else {
            printf("    no more data\n");
            break;
        }
    }
    invalidateRQMSG(&rqmsg);
    if (listener->pipeFd>-1) {
        close(listener->pipeFd);
    }
    free(listener);
    printf("4\n");
    return NULL;
}

void *serviceListener(void *arg) {

    printf("consumerListener\n");

    Service *service = (Service*)arg;

    int server_fd, new_socket, valread;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    bool processNodes = true;
    //const char *response = "Hello from server"; // was somehow responsible for corrupting memmory when spawning new child processes

    // Creating socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket() command failed while creating listener");
        exit(EXIT_FAILURE);
    }

    // Set socket options to reuse address and port
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt() command failed while creating listener");
        exit(EXIT_FAILURE);
    }

    // Set the receive timeout
    struct timeval timeout;
    timeout.tv_sec = service->requestTtl; // TTL
    timeout.tv_usec = 0;
    setsockopt(server_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

    address.sin_family = AF_INET;
    if (service->inaddrAny)
        address.sin_addr.s_addr = INADDR_ANY;
    else
        address.sin_addr.s_addr = inet_addr("127.0.0.1"); // INADDR_ANY
    address.sin_port = htons(service->port);

    // Bind socket to address and port
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind() command failed while creating listener");
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    if (listen(server_fd, service->maxConnections) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    while(1) {
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0) {
            perror("accept");
        } else {
            
            printf("accept\n");

             // helps load balancing - but would be better done in a different way
            while (nodesCount(&service->requests,true)>SC_MAX_REQUESTS) {
                    printf("    too many running nodes, waiting\n");
                    processRequestList(&service->requests,true);
                    usleep(1000); 
            }
            usleep(50*nodesCount(&service->requests,true)); // #todo - dynamic throttling
            //usleep(1000);

            // this is used by the child process to identify the outgoing response when it is sent segmented
            // and must only be set here in the parent process
            if (msgid==INT_MAX) {
                msgid = 1;
            } else {
                msgid++;
            }

            if (pipemsgid==INT_MAX) {
                pipemsgid = 1;
            } else {
                pipemsgid++;
            }

            int pipe_fd[2];
            int pipe_fd_rev[2]; // reverse ? (this is to receive the request to forward to the service provider)

            processNodes = true;
            while( pipe(pipe_fd) == -1 ) {
                printf("    Warning: maximum amount of pipes reached\n");
                processRequestList(&service->requests,true);
                processNodes = false;
                usleep(20000);
            }

            // #todo - evaluate if this has an effect
            struct timeval timeout;
            timeout.tv_sec = globalSetup.requestTtl; // TTL
            timeout.tv_usec = 0;
            setsockopt(pipe_fd[0], SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
            setsockopt(pipe_fd[1], SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));

            if (processNodes)
                processRequestList(&service->requests,true);

            processNodes = true;
            while( pipe(pipe_fd_rev) == -1 ) {
                printf("    Warning: maximum amount of pipes reached (b)\n");
                processRequestList(&service->requests,true);
                processNodes = false;
                usleep(20000);
            }

            setsockopt(pipe_fd_rev[0], SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
            setsockopt(pipe_fd_rev[1], SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));

            if (processNodes)
                processRequestList(&service->requests,true);

            Node* node;
            node = getNode();
            //pthread_t thread_id;
            Request *request = (Request*)malloc(sizeof(Request));
            request->socket = -1;
            request->pipe_fd[0] = pipe_fd[0];
            request->pipe_fd[1] = pipe_fd[1];
            request->pipe_fd_rev[0] = pipe_fd_rev[0];
            request->pipe_fd_rev[1] = pipe_fd_rev[1];
            printf("    new pipes [0]=%d [1]=%d /rev/ [0]=%d [1]=%d\n",pipe_fd[0],pipe_fd[1],pipe_fd_rev[0],pipe_fd_rev[1]);
            request->pId = -1;
            node->data = request;
            
            // we launch this before forking
            //PipeListener *listener = (PipeListener*)malloc(sizeof(PipeListener));
            //listener->pipeFd = pipe_fd_rev[0];
            //pthread_t pipeThread;
            //pthread_create(&pipeThread, NULL, pipeListener, listener);
            
            //
            // #todo - this is only here (and not just in the parent), because 
            // we're copying the node's id into the request id
            lockList(&service->requests);
            addNode(&service->requests,node,false);
            int nc = nodesCount(&service->requests,false);
            printf("nodes count(%d)\n",nc);
            debugRequests(&service->requests,false);
            //unlockList(&list); // only unlock at the end in parent
            
            //usleep(nc*100); // #todo this might be contraproductive - a large part of the work on this is that more requests don't add to call-time linearly
            request->id = node->id;

            // Spawn a new process
            pid_t pid = fork();
            time_t start = time(NULL);
            while(pid==-1 && start+globalSetup.requestTtl>time(NULL)) {
                printf("warning: retrying to fork\n");
                usleep(10000);
                pid = fork();
                // #todo - add a maximum number of cycles (and/or other condition)
            }

            if (pid == -1) {
                perror("fork");
                //exit(EXIT_FAILURE);

                //lockList(&list); // assumed locked
                close(new_socket);
                close(pipe_fd[0]);
                close(pipe_fd[1]);
                removeNode(&service->requests,node,false);
                unlockList(&service->requests);

            } else if (pid == 0) {

                printf("###CHILD PROCESS fd[%d]\n",pipe_fd[0]);

                unlockList(&service->requests);

                // Child process
                //close(sockfd);
                close(pipe_fd[1]);
                close(pipe_fd_rev[0]);
                close(server_fd); // Close the listening socket in the child process

                request->socket = new_socket;

                //pthread_mutex_init(&child_mutex, NULL);

                Child_ConnectionThreadData *ctdata = (Child_ConnectionThreadData*)malloc(sizeof(Child_ConnectionThreadData));
                ctdata->service = service;
                ctdata->request = request;

                // this might not need to be run in a separate thread, but we would have to guarantee
                // that the read on the pipe returns data after we do the write on the UDP in the thread
                if (pthread_create(&request->threadId, NULL, child_ConnectionThread, ctdata) != 0) {
                    perror("pthread_create");
                    close(new_socket);
                    //close(pipe_fd[0]);
                    printf("Child process EXIT_FAILURE\n");
                    exit(EXIT_FAILURE);
                }

                char buffer[100];

                //while(1) { usleep(1000); }

                // we keep the process up and running until we receive a response to forward from the server
                ssize_t bytes_read = read(pipe_fd[0], buffer, sizeof(buffer));
                if (bytes_read == -1) {
                    perror("read");
                    close(new_socket);
                    pthread_join(request->threadId,NULL);
                    printf("Child process EXIT_FAILURE\n");
                    exit(EXIT_FAILURE); // #todo - evaluate
                }
                printf("Child process received: (%s) size(%ld)\n", buffer,bytes_read);
                
                send(new_socket, buffer, strlen(buffer), 0);
                close(new_socket);

                //close(pipe_fd[0]); // for some reason this might create problems ?

                pthread_join(request->threadId,NULL);

                printf("Child process EXIT_SUCCESS\n");

                exit(EXIT_SUCCESS);
            } else {
                // Parent process

                PipeListener *listener = (PipeListener*)malloc(sizeof(PipeListener));
                listener->pipeFd = pipe_fd_rev[0];
                pthread_t pipeThread;
                pthread_create(&pipeThread, NULL, pipeListener, listener);
                pthread_detach(pipeThread);

                request->pId = pid;
                request->pipe_fd[0] = -1;
                request->pipe_fd_rev[1] = -1;
                request->socket = -1; //new_socket;

                close(pipe_fd[0]); // Close the read end of the pipe in the parent
                close(pipe_fd_rev[1]); // reverse, since with this we will be reading
                // the response should also be handled by the child - us forwarding it the response through a pipe
                // - for demo/testing we can also not do that & just respond here
                close(new_socket); // Close the socket in the parent process
                unlockList(&service->requests); // assumed locked

                //close(pipe_fd_rev[1]); // reverse, since with this we will be reading

                //lockList(&list);
                //addNode(&list,node,false);
                //unlockList(&list); // assumed locked
            }

        }
    }

    //close(server_fd);

    return NULL;
}

/**
* #todo - too long, too complicated, too many levels
*/
char* parseUDPXmlMessage(const char* xmlMessage) {
        
        verbose("ParseXmlMessage\n");
        
        Pipe *assignedPipe = NULL;
        
        char *result = (char*)malloc(4096); // #todo
        result[0] = 0x00;

        // Parse the XML document
        tinyxml2::XMLDocument xmlDoc;
        tinyxml2::XMLError error = xmlDoc.Parse(xmlMessage);
        if (error != tinyxml2::XML_SUCCESS) {

            //std::cerr << "Error parsing XML message: " << xmlDoc.ErrorStr() << std::endl;
            verbose("error parsing XML\n");

            // #todo
            //result.response = CreateErrorResponse(xmlDoc.ErrorStr());
            return result;
        }

        strcat(result,"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
        strcat(result,"<message>\n");
        strcat(result,"<response>\n");
        strcat(result,"<pipe_id>");

        // Check if the message has a pipe ID
        const char* pipeId = NULL;
        if (xmlDoc.FirstChildElement("message")->FirstChildElement("pipe_id"))
            pipeId = xmlDoc.FirstChildElement("message")->FirstChildElement("pipe_id")->GetText();

        if (pipeId) {

            verbose("have pipeId\n");
            
            assignedPipe = pipeById(pipeId);
            if (!assignedPipe) {
                verbose("didn't find pipe\n");
            }

            // #todo - error response on pipeId not found
            /**
            // If no matching pipe route was found, return an error response
            if (!assignedPipeRoute) {
                std::string errorMessage = "Unknown pipe ID: ";
                errorMessage += pipeId;
                result.response = CreateErrorResponse(errorMessage);
                return result;
            }
            */
        } else {
            // Create a new pipe route if necessary
            if (xmlDoc.FirstChildElement("message")->FirstChildElement("request_pipe_id")) {
                /**
                CMyPipeRoute* pipeRoute = new CMyPipeRoute();
                pipeRoute->consumer = this; // #todo - make consumer private, add handling methods
                pipeRoute->CreateUUID();

                m_pipeRoutes.push_back(pipeRoute);
                */
                
                verbose("create new pipe\n");

                assignedPipe = (Pipe*)malloc(sizeof(Pipe));
                initializePipe(assignedPipe);
                Node *node = getNode(); // #todo - rename to initializeNode()
                node->data = assignedPipe;
                addNode(&pipes,node,true);

                //assignedPipeRoute = pipeRoute;
            }
        }
        if (assignedPipe) {

            verbose("have assigned pipe\n");

            strcat(result,assignedPipe->id);
            strcat(result,"</pipe_id>\n");

            tinyxml2::XMLElement* servicesElement = xmlDoc.FirstChildElement("message")->FirstChildElement("services");
            if (servicesElement) {
                for (tinyxml2::XMLElement* serviceElement = servicesElement->FirstChildElement("service");
                    serviceElement; serviceElement = serviceElement->NextSiblingElement("service")) {

                        verbose("process service\n");

                        const char* service_uuid = serviceElement->Attribute("uuid");
                        const char* service_name = serviceElement->Attribute("name");
                        const char* service_type = serviceElement->Attribute("type");
                        /**
                        if (!service_uuid || !service_name || !service_type) {
                            response += "  <error reason=\"Missing attribute(s) in <service> element\"/>\n";
                            continue;
                        } else {
                            //printf("parsing service uuid(%s) name(%s) type(%s)\n",service_uuid,service_name,service_type);
                        } */
                    
                        ServiceDef *serviceDef = serviceDefByIdInPipe(assignedPipe,service_uuid);
                        if (!serviceDef) {

                            serviceDef = (ServiceDef*)malloc(sizeof(ServiceDef));
                            serviceDef->id = (char*)malloc(strlen(service_uuid)+1);
                            strcpy(serviceDef->id,service_uuid);
                            Node *snode = getNode();
                            snode->data = serviceDef;
                            addNode(&assignedPipe->serviceDefs,snode,true);

                            strcat(result,"  <service uuid=\"");
                            strcat(result,service_uuid);
                            strcat(result,"\" status=\"ServiceRouteRegistered\">\n");
                        } else {
                            // #todo - do we acknowledge that we received response? (circular logic?)

                            strcat(result,"  <service uuid=\"");
                            strcat(result,serviceDef->id);
                            strcat(result,"\">");

                            // #todo - #refactoring
                            tinyxml2::XMLElement* responseElement = serviceElement->FirstChildElement("response");
                            if (responseElement) {
                                verbose("got response - try to forward to consumer request_id(%d)\n",atoi(responseElement->Attribute("request_id")));
                                
                                tinyxml2::XMLElement* payloadElement = responseElement->FirstChildElement("payload");
                                if (payloadElement) {

                                    const char *payloadData = payloadElement->GetText();
                                    const char *httpResponse = NULL;
                                    char *payloadDataDecoded = NULL;
                                    if (payloadData) {
                                        payloadDataDecoded = base64Decode(payloadData);
                                    }
                                    if (payloadDataDecoded) {
                                        httpResponse = payloadDataDecoded;
                                    } else {
                                        // #todo - evaluate if this should be 502, 500 or other
                                        //httpResponse = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nContent-Type: text/plain\r\n\r\n";
                                        httpResponse = "HTTP/1.1 502 Bad Gateway\r\nContent-Length: 25\r\nContent-Type: text/plain\r\n\r\nBad Gateway: Routing Error.";
                                    }

                                    lockList(&globalSetup.services);

                                    Service *service = serviceByServiceDef(&globalSetup.services,serviceDef,false);
                                    lockList(&service->requests);

                                    // atol
                                    Node *node = getNodeById(&service->requests,atol(responseElement->Attribute("request_id")),false);
                                    if (node) {
                                        Request *request = (Request*)node->data;
                                        
                                        if (request) {

                                            if (request->pipe_fd[1]>-1) {
                                                verbose("attempting to write to pipe to send child process response\n");
                                                write(request->pipe_fd[1], httpResponse, strlen(httpResponse));
                                                // to close the pipe we need to make sure that access to other pipes is synced
                                                verbose("write to pipe finished\n");

                                                close(request->pipe_fd[1]); // close pipe immediately so we don't reach the limit
                                                request->pipe_fd[1] = -1;
                                                // theoretically there should only be one place to handle this
                                            }
                                        } else {
                                            verbose("Warning: Request node without data\n");
                                        }
                                    } else {
                                        verbose("Warning: got response for Request node that is no longer registered\n");
                                    }
                                    
                                    unlockList(&service->requests);

                                    unlockList(&globalSetup.services);

                                    if (payloadDataDecoded)
                                        free(payloadDataDecoded);

                                } else {
                                    verbose("warning: <response> didn't include <payload>\n");
                                }
                            }
                        }

                        strcat(result,"  </service>\n");
                }
            } else {
                verbose("missing data <services>\n");
            }

        }

        strcat(result,"</response>\n");
        strcat(result,"</message>\n");

        return result;
    }

void *udpserver_thread(void *arg) {

    Setup *setup = (Setup*)arg;

    //int sockfd;
    //struct sockaddr_in server_addr, client_addr;
    //socklen_t addr_len;
    char buffer[MAX_UDP_MSG_SIZE];

    // Create a UDP socket
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }

    // Set up the server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    if (setup->inaddrAny)
        server_addr.sin_addr.s_addr = INADDR_ANY;
    else
        server_addr.sin_addr.s_addr = inet_addr("127.0.0.1"); // INADDR_ANY;
    server_addr.sin_port = htons(setup->listenerPort);
    
    // Bind the socket to the server address
    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind");
        exit(1);
    }

    // Allocate memory for the buffer dynamically
    //buffer = (char *)malloc(MAX_UDP_MSG_SIZE * sizeof(char));
    //if (buffer == NULL) {
    //    perror("malloc");
    //    exit(1);
    //}

    printf("UDP server is listening on port %d...\n", setup->listenerPort);

    for(int n = 0; n < NREQUESTS; n++) {
        initializeRQMSG(&rqmsgs[n]);
    }

    //setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout)); // doesn't work
    struct timeval timeout;
    timeout.tv_sec = globalSetup.requestTtl;
    timeout.tv_usec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == -1) {
        perror("setsockopt");
        exit(1);
    }
    
    char *completemsg = NULL;
    addr_len = sizeof(client_addr); // #todo
    while (1) {
        // Receive a message from a client
        //addr_len = sizeof(client_addr);
        unsigned int num_bytes = (unsigned int)recvfrom(sockfd, buffer, MAX_UDP_MSG_SIZE - 1, 0, (struct sockaddr *)&client_addr, &addr_len);
        if (getpid()!=parentPid) {
            continue;
        }
        if (num_bytes == -1) {
            perror("recvfrom"); // #todo
            //exit(1); // or break; // #todo
            //break;
            continue; // in case of non blocking
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

        RQMSG *rqmsg = NULL;
        for(int n = 0; n < NREQUESTS; n++) {
            if (rqmsgs[n].timestamp+globalSetup.requestTtl<time(NULL)) {
                invalidateRQMSG(&rqmsgs[n]);
                continue;
            }
            if (rqmsgs[n].msgid==rqmsgraw->msgid && !rqmsg) {
                rqmsg = &rqmsgs[n];
                break;
            }
        }
        if (!rqmsg) {
            for(int n = 0; n < NREQUESTS; n++) {
                if (rqmsgs[n].msgid==0) {
                    rqmsg = &rqmsgs[n];
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

        if (!completemsg) {
            printf("    msg not complete\n");
            continue;
        }

        printf("Received message from client: (%s)\n", completemsg);

        char *result = parseUDPXmlMessage(completemsg);
        printf("response using(%s)\n",result);

        printf("run udpsend from parent thread\n");
        if (result) {
            udpsend(result,&client_addr,addr_len);
        }
        printf("run udpsend from parent thread after\n");

        free(completemsg);
        invalidateRQMSG(rqmsg);
        free(result);
        
    }

    //free(buffer);
    //close(sockfd);

    return NULL;
}

bool loadConfigurationFile(const char *filename, Setup *setup) {

        tinyxml2::XMLDocument doc;
        doc.LoadFile(filename);

        tinyxml2::XMLElement* sc_elem = doc.FirstChildElement("service_consumer");
        if (!sc_elem) {
            printf("Error: could not find service_consumer element\n");
            return false;
        }

        tinyxml2::XMLElement* max_connections_elem = sc_elem->FirstChildElement("max_connections");
        if (!max_connections_elem) {
            setup->maxConnections = 20; // default if no setup
        } else {
            setup->maxConnections = max_connections_elem->IntText();
        }

        tinyxml2::XMLElement* request_buffer_elem = sc_elem->FirstChildElement("request_buffer");
        if (!request_buffer_elem) {
            setup->requestBuffer = 1024; // default if no setup
        } else {
            setup->requestBuffer = request_buffer_elem->IntText();
        }

        tinyxml2::XMLElement* request_ttl_elem = sc_elem->FirstChildElement("request_ttl");
        if (!request_ttl_elem) {
            setup->requestTtl = 5; // default if no setup
        } else {
            setup->requestTtl = request_ttl_elem->IntText();
        }

        tinyxml2::XMLElement* listener_elem = sc_elem->FirstChildElement("listener");
        if (!listener_elem) {
            printf("Error: could not find listener element\n");
            return false;
        }

        tinyxml2::XMLElement* port_elem = listener_elem->FirstChildElement("port");
        if (!port_elem) {
            printf("Error: could not find port element\n");
            return false;
        }

        setup->listenerPort = port_elem->IntText();

        // optional
        setup->inaddrAny = false;
        tinyxml2::XMLElement* inaddr_elem = listener_elem->FirstChildElement("inaddr_any");
        if (inaddr_elem) {
            if (strcmp(inaddr_elem->GetText(),"yes")==0) {
                setup->inaddrAny = true;
            }
        }

        tinyxml2::XMLElement* services_elem = sc_elem->FirstChildElement("services");
        if (!services_elem) {
            printf("Error: could not find services element\n");
            return false;
        }

        lockList(&setup->services);
        for (tinyxml2::XMLElement* service_elem = services_elem->FirstChildElement("service"); service_elem; service_elem = service_elem->NextSiblingElement("service")) {
            tinyxml2::XMLElement* uuid_elem = service_elem->FirstChildElement("uuid");
            if (!uuid_elem) {
                printf("Error: could not find uuid element\n");
                continue;
            }

            tinyxml2::XMLElement* name_elem = service_elem->FirstChildElement("name");
            if (!name_elem) {
                printf("Error: could not find name element\n");
                continue;
            }

            tinyxml2::XMLElement* service_port_elem = service_elem->FirstChildElement("port");
            if (!service_port_elem) {
                printf("Error: could not find port element\n");
                continue;
            }

            const char* uuid = uuid_elem->GetText();
            if (strlen(uuid)!=36) {
                printf("invalid service id(%s)\n",uuid);
                continue;
            }
            const char* name = name_elem->GetText(); // #todo - use for service name
            int service_port = service_port_elem->IntText(); // add checks

            // optionals:
            //

            int service_max_connections = 0;
            int service_request_buffer = 0;
            int service_request_ttl = 0;

            tinyxml2::XMLElement* service_max_connections_elem = services_elem->FirstChildElement("max_connections");
            if (!service_max_connections_elem) {
                service_max_connections = setup->maxConnections; // default if no setup
            } else {
                service_max_connections = service_max_connections_elem->IntText();
            }

            tinyxml2::XMLElement* service_request_buffer_elem = services_elem->FirstChildElement("request_buffer");
            if (!service_request_buffer_elem) {
                service_request_buffer = setup->requestBuffer; // default if no setup
            } else {
                service_request_buffer = service_request_buffer_elem->IntText();
            }

            tinyxml2::XMLElement* service_request_ttl_elem = services_elem->FirstChildElement("request_ttl");
            if (!service_request_ttl_elem) {
                service_request_ttl = setup->requestTtl; // default if no setup
            } else {
                service_request_ttl = service_request_ttl_elem->IntText();
            }

            // #todo - implement initService() with arguments

            Service *service = (Service*)malloc(sizeof(Service));
            
            initService(service,uuid,name,service_port);

            // optionals? #todo - include in service initialization
            service->maxConnections = service_max_connections;
            service->requestBuffer = service_request_buffer;
            service->requestTtl = service_request_ttl;
            // todo
            service->inaddrAny = false;
            tinyxml2::XMLElement* service_inaddr_elem = service_elem->FirstChildElement("inaddr_any");
            if (service_inaddr_elem) {
                if (strcmp(service_inaddr_elem->GetText(),"yes")==0) {
                    service->inaddrAny = true;
                }
            }

            Node *node = (Node*)malloc(sizeof(Node));
            node->data = service;
            addNode(&setup->services,node,false); // list locked around loop
        }
        unlockList(&setup->services);

    return true;
}

bool initService(Service *service, const char *uuid, const char *name, int port) {
    if (!service || !uuid || !name) {
        verbose("error: missing attributes to register service\n");
        return false;
    }
    
    service->id = (const char*)malloc(37);
    strcpy((char*)service->id,uuid);
    service->port = port; // todo - add checks prior

    initLinkedList(&service->requests,true);

    return true;
}

bool runService(Service *service) {
    if (!service)
        return false;

    pthread_t server_thread;
    if (pthread_create(&server_thread, NULL, serviceListener, service) != 0) {
        perror("pthread_create");
        return false;
    }

    return true;
}

bool runSetup(Setup *setup) {
    if (!setup)
        return false;

    pthread_t watchdog_thread;
    if (pthread_create(&watchdog_thread, NULL, watchdog, NULL) != 0) {
        perror("pthread_create");
        return false;
    }

    pthread_t server_thread;
    if (pthread_create(&server_thread, NULL, udpserver_thread, setup) != 0) {
        perror("pthread_create");
        return false;
    }

    Node* current = setup->services.head;
    while (current != NULL) {
        Service *service = (Service*)current->data;
        runService(service);
        current = current->next;
    }

    return true;
}

// Global variable to control the loop

volatile sig_atomic_t keepRunning = 1;

// Signal handler for termination signals (e.g., SIGINT)
void handleSignal(int sig) {
    printf("Received signal %d. Terminating...\n", sig);
    keepRunning = 0;

    // #todo - cleanup the child processes
}

int main(int argc, char *argv[]) {

    if (argc < 2) {
        printf("Usage: %s <filename>\n",argv[0]);
        //return 1;
    }

    initLinkedList(&globalSetup.services,LIST_USEMUTEX);
    //if (!loadConfigurationFile(argv[1],&globalSetup)) {
    if (!loadConfigurationFile("/Users/milankazarka/Desktop/quasirq/consumer.xml",&globalSetup)) {
        printf("Error: failed to load configuration file\n");
        return 1;
    }

    srand(time(NULL));

    parentPid = getpid();

    //initLinkedList(&list,LIST_USEMUTEX);
    initLinkedList(&pipes,LIST_USEMUTEX);

    // Create and open the named semaphore
    char *randomStr = mkrndstr(8);
    char *semName = (char*)malloc(14);
    snprintf(semName,13,"/sem_%s",randomStr);
    free(randomStr);
    free(semName);

    binarySemaphore = sem_open(semName, O_CREAT, 0644, 1);
    if (binarySemaphore == SEM_FAILED) {
        perror("sem_open");
        exit(EXIT_FAILURE);
    }

    // Set up the signal handler for SIGINT (Ctrl+C)
    //signal(SIGTERM, handleSignal);

    runSetup(&globalSetup);

    // Your main program logic here
    printf("Starting the main loop...\n");

    //while (keepRunning) {
        // Perform your ongoing tasks here

        // Introduce a small delay or sleep to avoid busy-waiting
    //    usleep(100000); // Sleep for 100 ms (adjust as needed)
    //}

    while(1)
        sleep(1);
    
    //printf("return from process\n");

    return 0;
}
