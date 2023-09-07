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

#ifndef __MSGGRAM_HPP__
#define __MSGGRAM_HPP__

#include <stdlib.h>
#include <time.h>

struct Node;
struct LinkedList;

#define MAXGRAMS 200 // 64kB*MAXGRAMS

typedef struct {
    unsigned int size;
    char *data;
} RQGRAM;

typedef struct {
    unsigned long long msgid;
    unsigned int ngrams;
    time_t timestamp;
    RQGRAM grams[MAXGRAMS];
} RQMSG;

typedef struct {
    unsigned long long msgid;
    unsigned int ngrams;
    unsigned int index;
    char *data;
} RQMSGRAW;

// #todo - there should be better management of these

typedef struct {
    unsigned long long msgid;
    unsigned int ngrams;
    unsigned int index;
} RQGRAM_HEADER;

typedef struct {
    char *data; // have the RQMSGRAW_HEADER part of the data
    int datalen;
} RQGRAM_HEADERDATA;

void invalidateRQGRAM( RQGRAM *rqgram );
void initializeRQMSG( RQMSG *rqmsg );
void invalidateRQMSG( RQMSG *rqmsg );
char *dataFromRQMSG( RQMSG *rqmsg ); // reconstruct data from grams in a message
LinkedList *splitRawDataIntoGrams( const char *message, int mymsgid );

#endif
