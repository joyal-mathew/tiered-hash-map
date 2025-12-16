set term svg
set output "plots/placement_mem_6000.svg"

set key off

set xlabel "% buckets allowed in RAM"
set ylabel "Memory Usage (MiB)"

set title "Memory usage at 200% load percent"

plot "data/results/placement_6000.dat" using 1:3 with linespoints
