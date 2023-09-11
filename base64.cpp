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

static const char base64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

char* base64Encode(const char* input) {
    size_t input_len = strlen(input);
    size_t output_len = 4 * ((input_len + 2) / 3) + 1;
    char* encoded = (char*)malloc(output_len);
    if (!encoded) {
        perror("malloc");
        return NULL;
    }

    size_t i, j = 0;
    for (i = 0; i < input_len; i += 3) {
        unsigned char a = input[i];
        unsigned char b = (i + 1 < input_len) ? input[i + 1] : 0;
        unsigned char c = (i + 2 < input_len) ? input[i + 2] : 0;

        encoded[j++] = base64_table[(a >> 2) & 0x3F];
        encoded[j++] = base64_table[((a << 4) | (b >> 4)) & 0x3F];
        encoded[j++] = base64_table[((b << 2) | (c >> 6)) & 0x3F];
        encoded[j++] = base64_table[c & 0x3F];
    }

    while (j > 0 && input_len % 3 != 0) {
        encoded[--j] = '=';
        input_len++;
    }

    encoded[output_len - 1] = '\0';
    return encoded;
}

char* base64Decode(const char* input) {
    size_t input_len = strlen(input);
    size_t output_len = input_len / 4 * 3;
    if (input[input_len - 1] == '=') {
        output_len--;
        if (input[input_len - 2] == '=') {
            output_len--;
        }
    }

    char* decoded = (char*)malloc(output_len + 1);
    if (!decoded) {
        perror("malloc");
        return NULL;
    }

    size_t i, j = 0;
    for (i = 0; i < input_len; i += 4) {
        unsigned char a = strchr(base64_table, input[i]) - base64_table;
        unsigned char b = strchr(base64_table, input[i + 1]) - base64_table;
        unsigned char c = strchr(base64_table, input[i + 2]) - base64_table;
        unsigned char d = strchr(base64_table, input[i + 3]) - base64_table;

        decoded[j++] = (a << 2) | (b >> 4);
        decoded[j++] = (b << 4) | (c >> 2);
        decoded[j++] = (c << 6) | d;
    }

    decoded[output_len] = '\0';
    return decoded;
}
