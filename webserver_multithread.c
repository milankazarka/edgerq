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
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>

#define RESPONSE "HTTP/1.1 200 OK\nContent-Length: 12\n\nHello, world"

void *handle_connection(void *socket_desc) {
    int new_socket = *(int *)socket_desc;
    char buffer[1024] = {0};
    ssize_t bytesRead = read(new_socket, buffer, sizeof(buffer));
    if (bytesRead > 0) {
        //printf("Processing request in thread %lu\n", (unsigned long)pthread_self());
    }

    send(new_socket, RESPONSE, strlen(RESPONSE), 0);
    close(new_socket);

    // Free the socket descriptor and exit the thread
    free(socket_desc);
    pthread_exit(NULL);
}

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    // Create a socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // Set socket options
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt failed");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(3088);

    // Bind the socket
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    if (listen(server_fd, 5) < 0) {
        perror("listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Server is listening on port 8080...\n");

    while (1) {
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0) {
            perror("accept failed");
            exit(EXIT_FAILURE);
        }

        // Allocate memory for the socket descriptor to pass it to the thread
        int *socket_desc = malloc(sizeof(int));
        *socket_desc = new_socket;

        // Create a new thread to handle the connection
        pthread_t thread;
        if (pthread_create(&thread, NULL, handle_connection, (void *)socket_desc) != 0) {
            perror("pthread_create failed");
            exit(EXIT_FAILURE);
        }

        // Detach the thread to allow it to clean up after it exits
        pthread_detach(thread);
    }

    return 0;
}
