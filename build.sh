#!/bin/sh

gcc -o stresstest_libcurl stresstest_libcurl.c -lcurl -lpthread
gcc -o stresstest stresstest.c -lpthread
gcc -o webserver webserver.c
gcc -o webserver_multithread webserver_multithread.c -lpthread

g++ -o edgerq_sc edgerq_sc.cpp base64.cpp msggram.cpp time.cpp list.cpp common.cpp \
    -lpthread -Wc++11-compat-deprecated-writable-strings \
    -ltinyxml2

g++ -o edgerq_sp edgerq_sp.cpp base64.cpp msggram.cpp time.cpp list.cpp common.cpp \
    -lpthread -Wc++11-compat-deprecated-writable-strings \
    -ltinyxml2
