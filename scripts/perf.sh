perf record -e instructions,mem-loads,mem-stores --vmlinux=/lib/modules/4.17.0/build/vmlinux $1
perf report --sort=dso &> $2.out
mv perf.data $2.data
