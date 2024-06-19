all: build/Makefile
	$(MAKE) -C build --no-print-directory
build/Makefile: CMakeLists.txt
	cmake -DCMAKE_BUILD_TYPE=Release -B build
clean:
	rm -rf build
.PHONY: all clean
