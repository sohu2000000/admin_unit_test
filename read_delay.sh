#!/bin/bash
#set -x

MAX_LOOP=5

#JIT_FILE="/proc/jitbusy"
#JIT_FILE="/proc/jitsched"
#JIT_FILE="/proc/jitqueue"
JIT_FILE="/proc/jitschedto"

for (( i=0; i < MAX_LOOP ; i++ ))
do
    cat $JIT_FILE
done