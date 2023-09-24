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
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>

#define SERVER_PORT 80
#define SERVER_IP "127.0.0.1"
#define MAX_BUFFER 4096
#define MAX_RETRY 5
#define NUM_THREADS 10
#define MAX_REQUESTS 100000
#define REQUEST_TIMEOUT 3

unsigned long global_request_count = 0;
pthread_mutex_t count_mutex = PTHREAD_MUTEX_INITIALIZER;

int check_http_200(const char* response) {
    return strstr(response, "200 OK") != NULL;
}

void* perform_request(void* arg) {
    int client_socket;
    struct sockaddr_in server_addr;
    char send_data[MAX_BUFFER];
    char recv_data[MAX_BUFFER];
    int should_exit = 0;

    struct timeval timeout;
    timeout.tv_sec = REQUEST_TIMEOUT;
    timeout.tv_usec = 0;

    while (1) {
        client_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (client_socket == -1) {
            perror("Failed to create socket");
            return NULL;
        }

        // Set socket option for timeout
        setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(SERVER_PORT);
        inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

        if (connect(client_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            perror("Connection failed");
            return NULL;
        }

        sprintf(send_data, "GET /proxy/proxy.php HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");

        int retries = 0;
        int valid_response = 0;
        while (retries < MAX_RETRY) {
            if (send(client_socket, send_data, strlen(send_data), 0) < 0) {
                perror("Failed to send data");
                retries++;
                continue;
            }

            int bytes_received = recv(client_socket, recv_data, MAX_BUFFER, 0);
            if (bytes_received <= 0) {
                perror("Failed to receive data or timed out");
                retries++;
                continue;
            } else {
                recv_data[bytes_received] = '\0';
                if (check_http_200(recv_data)) {
                    valid_response = 1;
                    printf("Received valid response:\n%s\n", recv_data);
                    break;
                } else {
                    printf("Received non-200 response:\n%s\n", recv_data);
                    retries++;
                }
            }
        }

        close(client_socket);

        if (!valid_response) {
            printf("Max retries reached without a valid 200 response.\n");
        }

        pthread_mutex_lock(&count_mutex);
        global_request_count++;

        if (global_request_count >= MAX_REQUESTS)
            should_exit = 1;

        printf("Request number: %lu\n", global_request_count);
        pthread_mutex_unlock(&count_mutex);

        if (should_exit == 1) {
            printf("Exiting thread\n");
            return NULL;
        }
    }
}

int main() {
    pthread_t threads[NUM_THREADS];

    for (int i = 0; i < NUM_THREADS; i++) {
        int result = pthread_create(&threads[i], NULL, perform_request, NULL);
        if (result != 0) {
            fprintf(stderr, "Failed to create thread: %d\n", result);
            exit(EXIT_FAILURE);
        }
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    printf("Finished all requests\n");

    return 0;
}
