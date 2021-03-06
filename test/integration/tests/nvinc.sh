#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause
#;**********************************************************************;

source helpers.sh

nv_test_index=0x1500018

alg_pcr_policy=sha1
pcr_ids="0,1,2,3"
file_pcr_value=pcr.bin
file_policy=policy.data

cleanup() {
  tpm2_nvrelease -Q -x $nv_test_index -a o 2>/dev/null || true
  tpm2_nvrelease -Q -x 0x1500016 -a 0x40000001 2>/dev/null || true
  tpm2_nvrelease -Q -x 0x1500015 -a 0x40000001 -P owner 2>/dev/null || true

  rm -f policy.bin test.bin nv.test_inc nv.readlock foo.dat cmp.dat \
        $file_pcr_value $file_policy nv.out cap.out

  if [ "$1" != "no-shut-down" ]; then
     shut_down
  fi
}
trap cleanup EXIT

start_up

cleanup "no-shut-down"

tpm2_clear

tpm2_nvdefine -Q -x $nv_test_index -a o -s 8 -b "ownerread|policywrite|ownerwrite|nt=1"

tpm2_nvincrement -Q -x $nv_test_index -a o

tpm2_nvread -Q -x $nv_test_index -a o -s 8

tpm2_nvlist > nv.out
yaml_get_kv nv.out $nv_test_index > /dev/null

# Test writing to and reading from an offset by:
# 1. incrementing the nv counter
# 2. reading back the index
# 3. comparing the result.

echo -n -e '\x00\x00\x00\x00\x00\x00\x00\x02' > nv.test_inc

tpm2_nvincrement -Q -x $nv_test_index -a o

tpm2_nvread -x $nv_test_index -a o -s 8 > cmp.dat

cmp nv.test_inc cmp.dat

tpm2_nvrelease -x $nv_test_index -a o


tpm2_pcrlist -Q -L ${alg_pcr_policy}:${pcr_ids} -o $file_pcr_value

tpm2_createpolicy -Q --policy-pcr -L ${alg_pcr_policy}:${pcr_ids} -F $file_pcr_value -o $file_policy

tpm2_nvdefine -Q -x 0x1500016 -a 0x40000001 -s 8 -L $file_policy -b "policyread|policywrite|nt=1"

# Increment with index authorization for now, since tpm2_nvincrement does not support pcr policy.
echo -n -e '\x00\x00\x00\x00\x00\x00\x00\x03' > nv.test_inc

# Counter is initialised to highest value previously seen (in this case 2) then incremented
tpm2_nvincrement -Q -x 0x1500016 -a 0x1500016 -P pcr:${alg_pcr_policy}:${pcr_ids}+$file_pcr_value

tpm2_nvread -x 0x1500016 -a 0x1500016 -P pcr:${alg_pcr_policy}:${pcr_ids}+$file_pcr_value -s 8 > cmp.dat

cmp nv.test_inc cmp.dat

# this should fail because authread is not allowed
trap - ERR
tpm2_nvread -x 0x1500016 -a 0x1500016 -P "index" 2>/dev/null
trap onerror ERR

tpm2_nvrelease -Q -x 0x1500016 -a 0x40000001


#
# Test NV access locked
#
tpm2_nvdefine -Q -x $nv_test_index -a o -s 8 -b "ownerread|policywrite|ownerwrite|read_stclear|nt=1"

tpm2_nvincrement -Q -x $nv_test_index -a o

tpm2_nvread -Q -x $nv_test_index -a o -s 8

tpm2_nvreadlock -Q -x $nv_test_index -a o

# Reset ERR signal handler to test for expected nvread error
trap - ERR

tpm2_nvread -Q -x $nv_test_index -a o -s 8 2> /dev/null
if [ $? != 1 ];then
 echo "nvread didn't fail!"
 exit 1
fi

#
# Test that owner and index passwords work by
# 1. Setting up the owner password
# 2. Defining an nv index that can be satisfied by an:
#   a. Owner authorization
#   b. Index authorization
# 3. Using index and owner based auth during write/read operations
# 4. Testing that auth is needed or a failure occurs.
#
trap onerror ERR

tpm2_changeauth -w owner

tpm2_nvdefine -x 0x1500015 -a 0x40000001 -s 8 \
  -b "policyread|policywrite|authread|authwrite|ownerwrite|ownerread|nt=1" \
  -p "index" -P "owner"

# Use index password write/read, implicit -a
tpm2_nvincrement -Q -x 0x1500015 -P "index"
tpm2_nvread -Q -x 0x1500015 -P "index"

# Use index password write/read, explicit -a
tpm2_nvincrement -Q -x 0x1500015 -a 0x1500015 -P "index"
tpm2_nvread -Q -x 0x1500015 -a 0x1500015 -P "index"

# use owner password
tpm2_nvincrement -Q -x 0x1500015 -a 0x40000001 -P "owner"
tpm2_nvread -Q -x 0x1500015 -a 0x40000001 -P "owner"

# Check a bad password fails
trap - ERR
tpm2_nvincrement -Q -x 0x1500015 -a 0x1500015 -P "wrong" 2>/dev/null
if [ $? -eq 0 ];then
 echo "nvincrement with bad password should fail!"
 exit 1
fi

# Check using authorisation with tpm2_nvrelease
trap onerror ERR

tpm2_nvrelease -x 0x1500015 -a 0x40000001 -P "owner"

exit 0
