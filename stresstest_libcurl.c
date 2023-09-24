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
#include <pthread.h>
#include <curl/curl.h>
#include <unistd.h>

#define NUM_THREADS 20
#define MAX_REQUESTS 1000000
#define MAX_RETRY 3

unsigned long global_request_count = 0;
pthread_mutex_t count_mutex = PTHREAD_MUTEX_INITIALIZER;

size_t write_null(void* contents, size_t size, size_t nmemb, void* userp) {
    return size * nmemb;  // Must return number of bytes actually taken care of
}

size_t write_to_console(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t real_size = size * nmemb;
    fwrite(contents, size, nmemb, stdout);  // Write the content directly to the standard output
    return real_size;
}

void* performRequest(void* arg) {
    const char* url = "http://127.0.0.1:8888/proxy/proxy.php";  // Replace with the desired URL
    CURL* curl = curl_easy_init();

    if (curl) {
        CURLcode res;
        double total_time;
        int should_exit = 0;

        while (1) {
            curl_easy_setopt(curl, CURLOPT_URL, url);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 15000);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_console);

            int retry_count = 0;
            
            do {
                res = curl_easy_perform(curl);
                if (res != CURLE_OK) {
                    fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
                    retry_count++;
                    
                    if (retry_count < MAX_RETRY) {
                        usleep(10000);
                    } else {
                        fprintf(stderr, "E001: Max retries reached\n");
                        break;
                    }
                } else {
                    //printf("CURLE_OK\n");
                    // Get the total time of the request
                    curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &total_time);
                }
            } while (res != CURLE_OK && should_exit==0);
            
            pthread_mutex_lock(&count_mutex);
            global_request_count++;
            
            if (global_request_count>=MAX_REQUESTS)
                should_exit = 1;
            
            printf("    connection cycle %ld %.3f milliseconds\n", global_request_count, total_time * 1000);
            pthread_mutex_unlock(&count_mutex);
            if (should_exit==1) {
                printf("exiting thread\n");
                return NULL;
            }
            usleep(5000);
        }

        curl_easy_cleanup(curl);
    }

    return NULL;
}

int main() {
    pthread_t threads[NUM_THREADS];

    curl_global_init(CURL_GLOBAL_DEFAULT);

    for (int i = 0; i < NUM_THREADS; i++) {
        int result = pthread_create(&threads[i], NULL, performRequest, NULL);
        if (result != 0) {
            fprintf(stderr, "Failed to create thread: %d\n", result);
            exit(EXIT_FAILURE);
        }
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    printf("finished\n");

    curl_global_cleanup();

    return 0;
}
