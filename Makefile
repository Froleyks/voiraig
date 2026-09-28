MAKEFLAGS += --no-print-directory
FUZZ_COUNT ?= 10000
FUZZ_JOBS ?= 8
GENERALIZED_TOOLS ?=
GENERALIZED_SECONDS ?= 10
MAPPINGS_TOOLS ?=
MAPPINGS_SECONDS ?= 10
all: build/Makefile
	@cmake --build build --parallel
	@cmake --install build --prefix .
verify-generalized: all
	@cmake --build build --target verify-liveness-trace --parallel
	./build/verify-liveness-trace
	python3 checks/generalized/verify.py --binary bin/voiraig --seconds $(GENERALIZED_SECONDS) $(if $(GENERALIZED_TOOLS),--tools-dir "$(GENERALIZED_TOOLS)")
verify-mappings: all
	python3 checks/mappings/verify.py --binary bin/voiraig --seconds $(MAPPINGS_SECONDS) $(if $(MAPPINGS_TOOLS),--tools-dir "$(MAPPINGS_TOOLS)")
tools: tools/Makefile
	@cmake --build tools --parallel
	@cmake --install tools --prefix .
fuzz: fuzz/Makefile
	@cmake --build fuzz --parallel
	@cmake --install fuzz --prefix fuzz
	cd fuzz && FUZZER_OPTIONS='-2 -f' ./bin/certifuzzer ./voiraig 8
fuzz-check: fuzz/Makefile
	@cmake --build fuzz --parallel
	@cmake --install fuzz --prefix fuzz
	python3 scripts/fuzz.py --count $(FUZZ_COUNT) --jobs $(FUZZ_JOBS)
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
	cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCHECK=ON -DLTO=OFF -B debug
docker: clean
	docker build -t voiraig .
	docker run --rm -it voiraig
clean:
	rm -rf build bin tools fuzz debug compile_commands.json
.PHONY: all tools fuzz fuzz-check clean docker debug verify-generalized verify-mappings
