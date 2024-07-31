all: build/Makefile
	$(MAKE) -C build --no-print-directory
build/Makefile: CMakeLists.txt
	cmake -DCMAKE_BUILD_TYPE=Release -B build
clean:
	rm -rf build
fuzz: all
	./build/certifaiger/certifuzzer ./build/voiraig
.PHONY: all clean
