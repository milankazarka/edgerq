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

#ifndef __HEX_HPP__
#define __HEX_HPP__

void printHex(const char* data, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        printf("%02X ", (unsigned char)data[i]); // Print the byte in hexadecimal format

        if ((i + 1) % 16 == 0 || i == length - 1) {
            // Print ASCII representation
            for (size_t j = i - (i % 16); j <= i; ++j) {
                if (data[j] >= 32 && data[j] <= 126) {
                    printf("%c", data[j]); // Printable character
                } else {
                    printf("."); // Non-printable character
                }
            }
            printf("\n");
        }
    }
}

#endif
