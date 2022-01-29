#!/bin/sh

for i in $(seq 10); do
	touch "test/file-$i.txt"
	echo $((1 + i)) >>"test/file-$i.txt"
done
