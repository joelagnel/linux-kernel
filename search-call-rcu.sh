#!/bin/bash

rm func-list
touch func-list

for f in $(find . \( -name "*.c" -o -name "*.h" \) | grep -v rcu); do

	funcs=$(perl -0777 -ne 'while(m/call_rcu\([&]?.+,\s?(.+)\).*;/g){print "$1\n";}' $f)

	if [ "x$funcs" != "x" ]; then
		for func in $funcs; do
			echo "$f $func" >> func-list
			echo "$f $func"
		done
	fi

done

cat func-list | sort | uniq | tee func-list-sorted
