#!/bin/bash
set -e
mkdir -p build
cd build
cmake ..
make -j4
# Run the tests!
echo "Running unit tests..."
cd ..
./bin/test_db_pool || echo "Warning: test_db_pool failed"
./bin/test_redis || echo "Warning: test_redis failed"
./bin/test_models || echo "Warning: test_models failed"
./bin/test_kafka || echo "Warning: test_kafka failed"
echo "All done!"
