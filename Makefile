MAKEFLAGS += --no-print-directory
all: build/Makefile
	@cmake --build build --parallel
	@cmake --install build --prefix .
tools: tools/Makefile
	@cmake --build tools --parallel
	@cmake --install tools --prefix .
fuzz: fuzz/Makefile
	@cmake --build fuzz --parallel
	@cmake --install fuzz --prefix fuzz
	cd fuzz && FUZZER_OPTIONS='-2 -m -s -j -L' ./bin/certifuzzer ./voiraig 8
debug: debug/Makefile
	@cmake --build debug --parallel
	@cmake --install debug --prefix .
	ln -sf debug/compile_commands.json .
	./bin/certified 'valgrind ./bin/voiraig --verbosity=5' fuzz/bug.aag fuzz/wit.aag
build/Makefile: CMakeLists.txt
	cmake -DCMAKE_BUILD_TYPE=Release -DSTATIC=ON -B build
tools/Makefile: CMakeLists.txt
	cmake -DCMAKE_BUILD_TYPE=Release -DSTATIC=ON -DCHECK=ON -B tools
fuzz/Makefile: CMakeLists.txt
	cmake -DCMAKE_BUILD_TYPE=Fuzzing -B fuzz -DCHECK=ON
debug/Makefile: CMakeLists.txt
	cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCHECK=ON -B debug
docker: clean
	docker build -t voiraig .
	docker run --rm -it voiraig
clean:
	rm -rf build bin tools fuzz debug compile_commands.json
.PHONY: all fuzz clean docker debug
