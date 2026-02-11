g++ -std=c++17 test.cpp -o db_test \
-I/opt/homebrew/include \
-L/opt/homebrew/lib \
-L/opt/homebrew/opt/libpq/lib \
-lpqxx -lpq