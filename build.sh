#!/bin/sh

CFLAGS="-O0 -g -Wall -Wextra -Wno-multichar -std=c11 -pthread -fsanitize=address"
OPTS="-DP2PJS_FORKTOBACKGROUND=0"

cc -o p2pjs p2pjs.c $CFLAGS $OPTS

