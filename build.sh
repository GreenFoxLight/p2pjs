#!/bin/sh

CFLAGS="-O0 -g -Wall -Wextra -Wpedantic -std=c11 -pthread -fsanitize=address"
OPTS=""

cc -o p2pjs p2pjs.c sha-256.c $CFLAGS $OPTS

