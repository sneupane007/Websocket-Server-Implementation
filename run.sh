#!/bin/bash
g++ -std=c++17 \
    main.cpp server/http_router.cpp server/chat_api_handler.cpp server/chat_server.cpp \
    -o server_run \
    -I/opt/homebrew/include \
    -I/opt/homebrew/opt/openssl/include \
    -I/usr/local/include \
    -L/opt/homebrew/lib \
    -L/opt/homebrew/opt/openssl/lib \
    -L/usr/local/lib \
    -L/opt/homebrew/opt/libpq/lib \
    -lcrypto -pthread -lpqxx -lpq

