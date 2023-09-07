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
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include "msggram.hpp"
#include "list.hpp"

void invalidateRQGRAM( RQGRAM *rqgram ) {
    if (!rqgram)
        return;
    if (rqgram->data)
        free(rqgram->data);
    rqgram->data = NULL;
    rqgram->size = 0;
}

void initializeRQMSG( RQMSG *rqmsg ) {
    if (!rqmsg)
        return;
    
    rqmsg->msgid = 0;
    rqmsg->ngrams = 0;
    rqmsg->timestamp = 0;
    for(int n = 0; n < MAXGRAMS; n++) {
        rqmsg->grams[n].data = NULL;
        rqmsg->grams[n].size = 0;
    }
}

void invalidateRQMSG( RQMSG *rqmsg ) {
    printf("invalidateRQMSG\n");
    if (!rqmsg)
        return;
    rqmsg->msgid = 0;
    rqmsg->ngrams = 0;
    rqmsg->timestamp = 0;
    for(int n = 0; n < MAXGRAMS; n++) {
        invalidateRQGRAM(&rqmsg->grams[n]);
    }
}

// reconstruct data from grams in a message
//
char *dataFromRQMSG( RQMSG *rqmsg ) {
    printf("dataFromRQMSG\n");
    if (!rqmsg)
        return NULL;
    if (rqmsg->timestamp+3<=time(NULL)) {
        printf("    invalidate\n");
        invalidateRQMSG(rqmsg);
        return NULL;
    }
    unsigned int totalsize = 0;
    for(int n = 0; n < rqmsg->ngrams; n++) {
        if (!rqmsg->grams[n].data || rqmsg->grams[n].size==0) {
            return NULL;
        }
        totalsize += rqmsg->grams[n].size;
    }
    unsigned int index = 0;
    char *data = (char*)malloc(totalsize+1);
    for(int n = 0; n < rqmsg->ngrams; n++) {
        memcpy(data+index,rqmsg->grams[n].data,rqmsg->grams[n].size);
        index+=rqmsg->grams[n].size;
    }
    data[totalsize]=0x00;
    printf("dataFromRQMSG returning size(%d)\n",totalsize);
    return data; // free upstream
}

// #todo - check that we don't surpass MAXGRAMS
// #todo - separate functions for handling header & headerdata
//
LinkedList *splitRawDataIntoGrams( const char *message, int mymsgid ) {
    
    if (!message)
        return NULL;
    
    LinkedList *list = (LinkedList*)malloc(sizeof(LinkedList));
    initLinkedList(list,LIST_NOMUTEX);

    unsigned short gramsize = (64*1024)-1024; // very rough, we just assume max 1024 for header by default
    unsigned int msglen = (unsigned int)strlen(message);
    unsigned int countdown = msglen;
    unsigned int size = gramsize;
    unsigned int gramindex = 0;
    unsigned int dataindex = 0;
    unsigned int ngrams = 0;

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

        RQGRAM_HEADERDATA *headerdata = (RQGRAM_HEADERDATA*)malloc(sizeof(RQGRAM_HEADERDATA));
        // we store the header inside of the data as well
        headerdata->data = (char*)malloc(sizeof(RQGRAM_HEADER)+size+1);
        RQGRAM_HEADER *header = (RQGRAM_HEADER*)headerdata->data;
        header->msgid = mymsgid;
        header->ngrams = ngrams;
        header->index = gramindex;

        memcpy(headerdata->data+sizeof(RQGRAM_HEADER),message+dataindex,size);
        headerdata->data[sizeof(RQGRAM_HEADER)+size] = 0x00;
        headerdata->datalen = sizeof(RQGRAM_HEADER)+size;

        Node *node = (Node*)malloc(sizeof(Node));
        node->data = headerdata;
        addNode(list,node,false);

        countdown-=size;
        gramindex++;
        dataindex+=size;
    }

    return list;
}
