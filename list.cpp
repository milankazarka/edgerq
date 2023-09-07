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
#include <limits.h>
#include "list.hpp"
#include "time.hpp"

void initLinkedList(LinkedList* list, bool createmutex) {
    list->head = NULL;
    list->lastId = 0;
    if (createmutex) {
        pthread_mutex_init(&list->mutex, NULL);
        list->bmutex = true;
    } else {
        list->bmutex = false;
    }
}

Node* insertNode(LinkedList* list) {
    Node* newNode = (Node*)malloc(sizeof(Node));
    if (!newNode) {
        perror("Memory allocation failed");
        exit(EXIT_FAILURE);
    }

    lockList(list);
    
    if (list->lastId == LLONG_MAX) {
        // Wrap around to 0 if lastId would exceed the maximum value
        list->lastId = 0;
    }
    
    newNode->id = list->lastId++;
    newNode->timestamp_ms = getCurrentTimeMillis(); // Set current timestamp in milliseconds
    newNode->next = NULL;
    newNode->data = NULL;

    if (list->head == NULL) {
        list->head = newNode;
    } else {
        Node* current = list->head;
        while (current->next != NULL) {
            current = current->next;
        }
        current->next = newNode;
    }
    unlockList(list);
    
    return newNode;
}

/** allocates a new node without adding it to any list
*/
Node *getNode() {
    Node* newNode = (Node*)malloc(sizeof(Node));
    if (!newNode) {
        perror("Memory allocation failed");
        exit(EXIT_FAILURE);
    }
    newNode->timestamp_ms = getCurrentTimeMillis(); // Set current timestamp in milliseconds
    newNode->next = NULL;
    newNode->data = NULL;
    return newNode;
}

/** add node to a list, pick if this needs to be locked or not
*/
void addNode(LinkedList* list, Node *node, bool lock) {
    if (!list || !node)
        return;
    if (lock)
        lockList(list);

    if (list->lastId == LLONG_MAX) {
        // Wrap around to 0 if lastId would exceed the maximum value
        list->lastId = 0;
    }
    
    node->id = list->lastId++;

    if (list->head == NULL) {
        list->head = node;
    } else {
        Node* current = list->head;
        while (current->next != NULL) {
            current = current->next;
        }
        current->next = node;
    }

    if (lock)
        unlockList(list);
}

Node* searchNode(LinkedList* list, long long id) {
    lockList(list);
    Node* current = list->head;
    while (current != NULL) {
        if (current->id == id) {
            pthread_mutex_unlock(&list->mutex);
            return current;
        }
        current = current->next;
    }
    unlockList(list);
    return NULL;
}

int nodesCount(LinkedList* list, bool lock) {
    if (lock)
        lockList(list);
    int count = 0;
    Node* current = list->head;
    while (current != NULL) {
        count++;
        current = current->next;
    }
    if (lock)
        unlockList(list);
    return count;
}

void removeNode(LinkedList* list, Node* target, bool lock) {
    if (list->head == NULL || target == NULL) {
        return;
    }
    if (lock)
        lockList(list);
    if (list->head == target) {
        list->head = target->next;
        free(target);
    } else {
        Node* current = list->head;
        while (current != NULL && current->next != target) {
            current = current->next;
        }
        if (current != NULL) {
            current->next = target->next;
            if (current->data) {
                free(current->data);
                current->data = NULL;
            }
            free(target);
        }
    }
    if (lock)
        unlockList(list);
}

void lockList(LinkedList* list) {
    pthread_mutex_lock(&list->mutex);
}

void unlockList(LinkedList* list) {
    pthread_mutex_unlock(&list->mutex);
}

void cleanupLinkedList(LinkedList* list) {
    Node* current = list->head;
    while (current != NULL) {
        Node* temp = current;
        current = current->next;
        free(temp);
    }
    if (list->bmutex)
        pthread_mutex_destroy(&list->mutex);
}

Node* getNodeById(LinkedList* list, long long id, bool lock) {
    if (lock)
        lockList(list);

    Node* current = list->head;
    while (current != NULL) {
        if (current->id == id) {
            if (lock)
                unlockList(list);
            return current;
        }
        current = current->next;
    }

    if (lock)
        unlockList(list);
    return NULL; // Node with the specified ID not found
}
