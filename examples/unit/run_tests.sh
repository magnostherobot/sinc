#!/bin/sh

make --silent clean
make --silent test_framework.o

for t in test_*.sin ; do
    exe="$(basename -s .sin $t)"
    echo -n "$exe... "
    make --silent "$exe"
    if [ $? -ne 0 ] ; then
        echo "cannot build"
        continue
    fi
    echo -n "built "
    ./"$exe"
    if [ $? -ne 0 ] ; then
        echo "failed"
        continue
    fi
    echo "passed"
done
