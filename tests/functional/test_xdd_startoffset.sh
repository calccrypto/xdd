#!/usr/bin/env bash
#
# Acceptance test for XDD
#
# Description - starts block transfer at n number of blocks from block 0
#
# Validate -startoffset by comparing the size of a file starting from block 0 to its size starting from the nth block
#
# Source the test configuration environment
source ../test_config
source ../common.sh

# Pre-test set-up
initialize_test
test_dir="${XDDTEST_LOCAL_MOUNT}/${TESTNAME}"

test_file="${test_dir}/data1"
touch "${test_file}"

#ratio of req_size to start_offset must be 2:1
req_size=10
start_offset=5
num_reqs=1
"${XDDTEST_XDD_EXE}" -target "${test_file}" -op write -reqsize "${req_size}" -numreqs "${num_reqs}" -startoffset "${start_offset}"

# Determine file requested transfer size and actual size
transfer_size=$((req_size*1024))
actual_size=$(stat -c '%s' "${test_file}")
calc_actual_size=$((actual_size-transfer_size))

# Verify results
if [[ "${calc_actual_size}" -eq "$((transfer_size/2))" ]]; then
  # test passed
  finalize_test 0
else
  # test failed
  finalize_test 1 "File size ${calc_actual_size} != $((transfer_size/2)) when doing -startoffset ${start_offset} -reqsize ${req_size} -numreqs $num_reqs"
fi
