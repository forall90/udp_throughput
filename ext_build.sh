cmake -DCMAKE_BUILD_TYPE:STRING=Release -S$SRC_DIR -B./build -G Ninja
cmake --build ./build --config Release
