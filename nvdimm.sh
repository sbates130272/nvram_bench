#!/bin/bash

if [ "$(id -u)" != "0" ]; then
    echo "This script must be run as root or via sudo (preferred)" 1>&2
    exit 1
fi

if [[ $# -ne 1 ]] ; then
    echo 'nvdimm.sh requires one argument (a file on a mounted file system)'
    exit 1
fi

p=64
while [ $p -lt 17000 ]
do
	echo "P = $p"
	mkdir -p P$p
	t=1
	while [ $t -lt 17 ]
	do
	    echo "	T = $t"
	    mkdir -p P$p/'t_'$t'_P_'$p
	    for try in 1 2 3
	    do
                ./build/nvram_bench -n 100 -f -t $t -p $p -rsl $1 > P$p/'t_'$t'_P_'$p/'try_'$try
            done
	    t=$[$t+1]
	done
	p=$[$p*2]
done
