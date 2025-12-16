set term svg
set output "plots/placement_mem_60000.svg"

set key off

set xlabel "% buckets allowed in RAM"
set ylabel "Memory Usage (MiB)"

set title "Memory usage at 20% load percent"

plot "data/results/placement_60000.dat" using 1:3 with linespoints
