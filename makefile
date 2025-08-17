CXX := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -pthread

all: echo-server echo-client

echo-server: echo-server.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

echo-client: echo-client.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

clean:
	rm -f echo-server echo-client

