#!/bin/bash

while read fl; do
	file=$(echo $fl | cut -d " " -f1)
	func=$(echo $fl | cut -d " " -f2)

	grep -A 30 $func $file | grep wake > /dev/null

	if [ $? -eq 0 ]; then
		echo "keyword wake found after function reference $func in $file"
		echo "Output:"
		grep -A 30 $func $file 
		echo "==========================================================="
	fi
done < func-list-sorted
