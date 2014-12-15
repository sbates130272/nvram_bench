#!/bin/bash

if [[ $# -neq 1 ]] ; then
    echo 'nvdimm.sh requires one argument (a file on a mounted file system)'
    exit 1
fi


p=64
while [ $p -lt 17000 ]
do
	echo "P = $p"
	mkdir P$p
	cd P$p
	t=1
	while [ $t -lt 17 ]
	do
		echo "	T = $t"
		mkdir 't_'$t'_P_'$p
		cd 't_'$t'_P_'$p
			#for loop
			for try in 1 2 3
			do
                    sudo nvram_bench -n 100 -t $t -p $p -rsl $1 > try_$try
            done
            #end of for loop.

		t=$[$t+1]
		cd ..
	done
	p=$[$p*2]
	cd ..
done
