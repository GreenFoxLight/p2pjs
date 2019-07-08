#!/bin/sh

CFLAGS="-O0 -g -Wall -Wextra -Wpedantic -std=c11 -pthread -fsanitize=address -Iwren/src/include -Lwren/lib"
OPTS=""

CODE_HOME=`pwd`

if [ -e wren ];
then
    if [ ! -e wren/lib/libwren.a ];
    then
        cd wren && make -j 4;
        rm $CODE_HOME/wren/lib/libwren.dylib
        cd $CODE_HOME
    fi
else
    echo "Make sure that you have the wren submodule!.";
fi

cc -o p2pjs p2pjs.c sha-256.c $CFLAGS $OPTS -lwren

