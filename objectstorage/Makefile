CXX := g++
CXXFLAGS := -std=c++20 -Wall -Wextra -pthread
INCLUDES := -I. -ICrow/include -Iasio/include -DASIO_STANDALONE
LDLIBS := -lcrypto

SRCS := cmd/main.cpp $(shell find internal -name '*.cpp')
BIN := bin/objectstorage

.PHONY: all run clean

all: $(BIN)

$(BIN): $(SRCS)
	@mkdir -p $(dir $(BIN))
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SRCS) -o $(BIN) $(LDLIBS)

run: all
	./$(BIN)

clean:
	rm -rf bin
