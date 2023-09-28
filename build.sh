#!/bin/sh

gcc -o stresstest_libcurl stresstest_libcurl.c -lcurl -lpthread
gcc -o stresstest stresstest.c -lpthread
gcc -o webserver webserver.c
gcc -o webserver_multithread webserver_multithread.c -lpthread

gcc -c 3rdparty/uuid4/src/uuid4.c -I3rdparty/uuid4/src/
g++ -c 3rdparty/tinyxml2-9.0.0/tinyxml2.cpp -I3rdparty/tinyxml2-9.0.0/

g++ -o edgerq_sc edgerq_sc.cpp base64.cpp msggram.cpp time.cpp list.cpp common.cpp \
    -lpthread -Wc++11-compat-deprecated-writable-strings \
    -Wdeprecated \
    -I3rdparty/tinyxml2-9.0.0/ tinyxml2.o \
    -I3rdparty/uuid4/src/ uuid4.o

g++ -o edgerq_sp edgerq_sp.cpp base64.cpp msggram.cpp time.cpp list.cpp common.cpp \
    -lpthread -Wc++11-compat-deprecated-writable-strings \
    -Wdeprecated \
    -I3rdparty/tinyxml2-9.0.0/ tinyxml2.o \
    -I3rdparty/uuid4/src/ uuid4.o
