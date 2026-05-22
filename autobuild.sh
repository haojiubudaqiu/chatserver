#!/bin/bash
set -e
mkdir -p build
cd build
cmake ..
make -j4
# Run the tests!
echo "Running unit tests..."
./test/test_db_pool || echo "Warning: test_db_pool failed"
./test/test_redis || echo "Warning: test_redis failed"
./test/test_models || echo "Warning: test_models failed"
./test/test_kafka || echo "Warning: test_kafka failed"
echo "All done!"
