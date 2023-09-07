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

#ifndef __LIST_HPP__
#define __LIST_HPP__

#include <stdlib.h>
#include <pthread.h>

#define LIST_USEMUTEX true
#define LIST_NOMUTEX false

typedef struct Node {
    long long id; // long long is probably overkill #todo - change to int everywhere
    long long timestamp_ms; // Store timestamp in milliseconds
    void *data;
    struct Node* next;
} Node;

typedef struct LinkedList {
    Node* head;
    long long lastId; // #todo - should be tha same as atoi
    bool bmutex;
    pthread_mutex_t mutex;
} LinkedList;

void initLinkedList(LinkedList* list, bool createmutex);
Node* insertNode(LinkedList* list);
Node* searchNode(LinkedList* list, long long id);
void removeNode(LinkedList* list, Node* target, bool lock);
void cleanupLinkedList(LinkedList* list);
Node *getNode();
void addNode(LinkedList* list, Node *node, bool lock);
void lockList(LinkedList* list);
void unlockList(LinkedList* list);
int nodesCount(LinkedList* list, bool lock);
Node* getNodeById(LinkedList* list, long long id, bool lock);

#endif
