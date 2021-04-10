X_SOURCES := $(shell find . -type f -regex ".*\.cpp")
C_SOURCES := $(shell find . -type f -regex ".*\.c")

X_STATIC_OBJECTS := $(patsubst %.cpp,%.cpp.ao, $(X_SOURCES))
C_STATIC_OBJECTS := $(patsubst %.c,%.c.ao, $(C_SOURCES))

X_SHARED_OBJECTS := $(patsubst %.cpp,%.cpp.po, $(X_SOURCES))
C_SHARED_OBJECTS := $(patsubst %.c,%.c.po, $(C_SOURCES))

CFLAGS=-O2
CXXFLAGS=-O2 -std=c++17 -D__NHTTP_BUILD__

AR=ar
CC=gcc
CXX=g++

SHARED_FLAGS=-fPIC -Wl,--whole-archive -lpthread -lrt -Wl,--no-whole-archive
STATIC_FLAGS=-D__NHTTP_STATIC__ -Wl,--whole-archive -lpthread -lrt -Wl,--no-whole-archive

all: libnhttp.so libnhttp.a

clean:
	rm -rf $(X_STATIC_OBJECTS)
	rm -rf $(C_STATIC_OBJECTS)
	rm -rf $(X_SHARED_OBJECTS)
	rm -rf $(C_SHARED_OBJECTS)
	rm -rf libnhttp.so libnhttp.a

libnhttp.a: $(X_STATIC_OBJECTS) $(C_STATIC_OBJECTS)
	$(AR) crf libnhttp.a $(X_STATIC_OBJECTS) $(C_STATIC_OBJECTS)

libnhttp.so: $(X_SHARED_OBJECTS) $(C_SHARED_OBJECTS)
	$(CXX) -o libnhttp.so -shared $(X_SHARED_OBJECTS) $(C_SHARED_OBJECTS) $(SHARED_FLAGS) $(CXXFLAGS)

%.cpp.po: %.cpp
	$(CXX) -c $< -o $@ $(SHARED_FLAGS) $(CXXFLAGS)
	
%.cpp.ao: %.cpp
	$(CXX) -c $< -o $@ $(STATIC_FLAGS) $(CXXFLAGS)
	
%.c.po: %.c
	$(CC) -c $< -o $@ $(SHARED_FLAGS) $(CFLAGS)
	
%.c.ao: %.c
	$(CC) -c $< -o $@ $(STATIC_FLAGS) $(CFLAGS)
