X_SOURCES := $(shell find `pwd`/nhttp -type f -regex ".*\.cpp")
C_SOURCES := $(shell find `pwd`/nhttp -type f -regex ".*\.c")

X_CVG_OBJECTS := $(patsubst %.cpp,%.cpp.co, $(X_SOURCES))
C_CVG_OBJECTS := $(patsubst %.c,%.c.co, $(C_SOURCES))

X_STATIC_OBJECTS := $(patsubst %.cpp,%.cpp.ao, $(X_SOURCES))
C_STATIC_OBJECTS := $(patsubst %.c,%.c.ao, $(C_SOURCES))

X_SHARED_OBJECTS := $(patsubst %.cpp,%.cpp.po, $(X_SOURCES))
C_SHARED_OBJECTS := $(patsubst %.c,%.c.po, $(C_SOURCES))

CFLAGS=-O3
CXXFLAGS=-O3 -std=c++17 -D__NHTTP_BUILD__

CFLAGS_CVG=-O0 -lgcov -fprofile-arcs -ftest-coverage
CXXFLAGS_CVG=-O0 -std=c++17 -D__NHTTP_BUILD__ -lgcov --coverage -fprofile-arcs -ftest-coverage

AR=ar
CC=gcc
CXX=g++

SHARED_FLAGS=-fPIC -Wl,--whole-archive -lpthread -lrt -Wl,--no-whole-archive
STATIC_FLAGS=-D__NHTTP_STATIC__ -Wl,--whole-archive -lpthread -lrt -Wl,--no-whole-archive

all: libnhttp.so libnhttp.a

mk-deb: libnhttp.so
	rm -rf deb; true
	mkdir -pv deb
	mkdir -pv deb/{DEBIAN,usr/{lib,local/include}}
	# generate control information.
	echo "Package: libnhttp" > deb/DEBIAN/control
	echo "Version: 1.0.0"	>> deb/DEBIAN/control
	echo "Maintainer: jay94ks <jay94ks@gmail.com>" >> deb/DEBIAN/control
	echo "Description: libnhttp for micro http server apps." >> deb/DEBIAN/control
	echo "Homepage: https://github.com/jay94ks/libnhttp" >> deb/DEBIAN/control
	echo "Architecture: amd64" >> deb/DEBIAN/control
	# generate configuration file list. (currently nothing)
	# echo "" > deb/DEBIAN/conffiles
	# generate pre-inst script. (currently nothing)
	echo "#!/bin/bash" > deb/DEBIAN/preinst
	echo "" >> deb/DEBIAN/preinst
	# generate post-inst script.
	echo "#!/bin/bash" > deb/DEBIAN/postinst
	echo "rm -rf /usr/lib/libnhttp.so" >> deb/DEBIAN/postinst
	echo "rm -rf /usr/local/include/nhttp" >> deb/DEBIAN/postinst
	echo "ln -s /usr/lib/libnhttp.so.1.0.0 /usr/lib/libnhttp.so" >> deb/DEBIAN/postinst
	echo "ln -s /usr/local/include/nhttp.1.0.0 /usr/local/include/nhttp" >> deb/DEBIAN/postinst
	# generate pre-remove script. (currently nothing)
	echo "#!/bin/bash" > deb/DEBIAN/prerm
	echo "" >> deb/DEBIAN/prerm
	# generate post-remove script.
	echo "#!/bin/bash" > deb/DEBIAN/postrm
	echo "rm -rf /usr/lib/libnhttp.so" >> deb/DEBIAN/postrm
	echo "rm -rf /usr/local/include/nhttp" >> deb/DEBIAN/postrm
	# copy entire source codes and remove *.cpp and *.c files.
	cp -R ./nhttp deb/usr/local/include/nhttp.1.0.0
	find ./deb/usr/local/include -type f -regex ".*\.cpp" -exec rm {} \;
	find ./deb/usr/local/include -type f -regex ".*\.c" -exec rm {} \;
	# copy .so file with version extension.
	cp libnhttp.so deb/usr/lib/libnhttp.so.1.0.0
	# build deb package.
	chmod +x deb/DEBIAN/{preinst,postinst,prerm,postrm}
	dpkg-deb -b deb/ libnhttp-1.0.0.deb

clean:
	rm -rf $(X_STATIC_OBJECTS)
	rm -rf $(C_STATIC_OBJECTS)
	rm -rf $(X_SHARED_OBJECTS)
	rm -rf $(C_SHARED_OBJECTS)
	rm -rf $(X_CVG_OBJECTS)
	rm -rf $(C_CVG_OBJECTS)
	rm -rf libnhttp.so libnhttp.a
	rm -rf $(shell find . -type f -regex ".*\.gcda")
	rm -rf coverage.info
	rm -rf test-app
	rm -rf test-main.cpp

bench-app: libnhttp.so
	g++ -O3 -o bench-app ../nhttpd/main.cpp -lpthread -lrt -lnhttp.so
	
test-app: $(X_CVG_OBJECTS) $(C_CVG_OBJECTS)
	cp ../libnhttp-tests/main.cpp test-main.cpp
	mkdir -pv ./tests; true
	cp -R ../libnhttp-tests/tests/* ./tests
	g++ -o test-app test-main.cpp $(shell find `pwd`/tests -type f -regex ".*\.cpp") \
		$(X_CVG_OBJECTS) $(C_CVG_OBJECTS) -I. \
		-lgcov --coverage -fprofile-arcs -ftest-coverage -std=c++17 -lpthread

run-test-app: test-app
	./test-app &
	sleep 30
	curl -v "http://127.0.0.1:8080/?dummy=query-string&a=1,b=c&d=3"; true
	curl -i "http://127.0.0.1:8080/" -H "Range: bytes=0-5"; true
	curl -i -v -H "Range: bytes=0-" "http://127.0.0.1:8080/"; true
	curl -i -v -H "Range: bytes=0-" -H 'If-None-Match: "NH-0000150560759eed"' "http://127.0.0.1:8080/"; true
	curl -i -v -H "Range: bytes=0-" -H 'If-Modified-Since: Tue, 13 Apr 2021 13:38:53 GMT' "http://127.0.0.1:8080/"; true
	curl -v "http://127.0.0.1:8080/whoami?dummy=query-string&a=1,b=c&d=3"; true
	curl -v "http://127.0.0.1:8080/always-501?dummy=query-string&a=1,b=c&d=3"; true
	curl -d "HELLO=WORLD~&whowhowho=11" -v "http://127.0.0.1:8080/jay/set?dummy=query-string&a=1,b=c&d=3"; true
	curl -d "HELLO=WORLD~&whowhowho=11" -v "http://127.0.0.1:8080/jay/greetings?dummy=query-string&a=1,b=c&d=3"; true
	curl -d "HELLO=WORLD~&whowhowho=11" -v "http://127.0.0.1:8080/jay/profile?dummy=query-string&a=1,b=c&d=3"; true
	curl -d "HELLO=WORLD~&whowhowho=11" -v "http://127.0.0.1:8080/always-501?dummy=query-string&a=1,b=c&d=3"; true
	curl -d "HELLO=WORLD~&whowhowho=11" -v -X PUT "http://127.0.0.1:8080/jay/set?dummy=query-string&a=1,b=c&d=3"; true
	curl -d "HELLO=WORLD~&whowhowho=11" -v -X PUT "http://127.0.0.1:8080/always-501?dummy=query-string&a=1,b=c&d=3"; true
	curl -d "HELLO=WORLD~&whowhowho=11" -v -X PATCH "http://127.0.0.1:8080/jay/set?this=should&be=405"; true
	curl -v -X DELETE "http://127.0.0.1:8080/jay?dummy=query-string&a=1,b=c&d=3"; true
	curl -d "HELLO=WORLD~&whowhowho=11" -v -X DELETE "http://127.0.0.1:8080/jay?this=should&be=400"; true
	curl -d "HELLO=WORLD~&whowhowho=11" -H 'Transfer-Encoding: chunked' -v -X POST "http://127.0.0.1:8080/jayset?this=can&be&handled=1"; true
	curl -d @index.html -v -H "Content-Type: application/octet-steam" -X POST "http://127.0.0.1:8080/jay/set?dummy=query-string&a=1,b=c&d=3"; true
	curl -d @index.html -v -H "Content-Type: application/octet-steam" -X PUT "http://127.0.0.1:8080/always-501?dummy=query-string&a=1,b=c&d=3"; true
	dd if=/dev/zero of=test.bin bs=102400 count=1024; true
	wget -O temp.bin "http://127.0.0.1:8080/test.bin"
	rm -rf test.bin temp.bin
	curl -v "http://[::1]:8080/?dummy=query-string&a=1,b=c&d=3"; true
	curl -i "http://[::1]:8080/" -H "Range: bytes=0-5"; true
	curl -i -v -H "Range: bytes=0-" "http://[::1]:8080/"; true
	curl -i -v -H "Range: bytes=0-" -H 'If-None-Match: "NH-0000150560759eed"' "http://[::1]:8080/"; true
	curl -i -v -H "Range: bytes=0-" -H 'If-Modified-Since: Tue, 13 Apr 2021 13:38:53 GMT' "http://[::1]:8080/"; true
	curl -v "http://[::1]:8080/whoami?dummy=query-string&a=1,b=c&d=3"; true
	curl -v "http://[::1]:8080/always-501?dummy=query-string&a=1,b=c&d=3"; true
	curl -d "HELLO=WORLD~&whowhowho=11" -v "http://[::1]:8080/jay/set?dummy=query-string&a=1,b=c&d=3"; true
	curl -d "HELLO=WORLD~&whowhowho=11" -v "http://[::1]:8080/jay/greetings?dummy=query-string&a=1,b=c&d=3"; true
	curl -d "HELLO=WORLD~&whowhowho=11" -v "http://[::1]:8080/jay/profile?dummy=query-string&a=1,b=c&d=3"; true
	curl -d "HELLO=WORLD~&whowhowho=11" -v "http://[::1]:8080/always-501?dummy=query-string&a=1,b=c&d=3"; true
	curl -d "HELLO=WORLD~&whowhowho=11" -v -X PUT "http://[::1]:8080/jay/set?dummy=query-string&a=1,b=c&d=3"; true
	curl -d "HELLO=WORLD~&whowhowho=11" -v -X PUT "http://[::1]:8080/always-501?dummy=query-string&a=1,b=c&d=3"; true
	curl -d "HELLO=WORLD~&whowhowho=11" -v -X PATCH "http://[::1]:8080/jay/set?this=should&be=405"; true
	curl -v -X DELETE "http://[::1]:8080/jay?dummy=query-string&a=1,b=c&d=3"; true
	curl -d "HELLO=WORLD~&whowhowho=11" -v -X DELETE "http://[::1]:8080/jay?this=should&be=400"; true
	curl -d "HELLO=WORLD~&whowhowho=11" -H 'Transfer-Encoding: chunked' -v -X POST "http://[::1]:8080/jayset?this=can&be&handled=1"; true
	curl -d @index.html -v -H "Content-Type: application/octet-steam" -X POST "http://[::1]:8080/jay/set?dummy=query-string&a=1,b=c&d=3"; true
	curl -d @index.html -v -H "Content-Type: application/octet-steam" -X PUT "http://[::1]:8080/always-501?dummy=query-string&a=1,b=c&d=3"; true
	dd if=/dev/zero of=test.bin bs=102400 count=1024; true
	wget -O temp.bin "http://[::1]:8080/test.bin"
	rm -rf test.bin temp.bin
	curl -d @index.html -v -H "Content-Type: application/octet-steam" -X POST "http://127.0.0.1:8080/exit"; true

coverage.info:
	lcov --capture --output-file coverage.info --directory . --base-directory "." --no-external
	genhtml coverage.info --output-directory coverage
	rm -rf ~/coverage/* | true
	mv coverage/* ~/coverage/

libnhttp.a: $(X_STATIC_OBJECTS) $(C_STATIC_OBJECTS)
	$(AR) crf libnhttp.a $(X_STATIC_OBJECTS) $(C_STATIC_OBJECTS)

libnhttp.so: $(X_SHARED_OBJECTS) $(C_SHARED_OBJECTS)
	$(CXX) -o libnhttp.so -shared $(X_SHARED_OBJECTS) $(C_SHARED_OBJECTS) $(SHARED_FLAGS) $(CXXFLAGS)

%.cpp.po: %.cpp
	$(CXX) -c $< -o $@ $(SHARED_FLAGS) $(CXXFLAGS)
	
%.cpp.ao: %.cpp
	$(CXX) -c $< -o $@ $(STATIC_FLAGS) $(CXXFLAGS)
	
%.cpp.co: %.cpp
	$(CXX) -c $< -o $@ $(STATIC_FLAGS) $(CXXFLAGS_CVG)
	
%.c.po: %.c
	$(CC) -c $< -o $@ $(SHARED_FLAGS) $(CFLAGS)
	
%.c.ao: %.c
	$(CC) -c $< -o $@ $(STATIC_FLAGS) $(CFLAGS)

%.c.co: %.c
	$(CC) -c $< -o $@ $(STATIC_FLAGS) $(CFLAGS_CVG)
