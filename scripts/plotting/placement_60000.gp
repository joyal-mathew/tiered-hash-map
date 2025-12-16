set term svg
set output "plots/placement_60000.svg"

set key off

set xlabel "% buckets allowed in RAM"
set ylabel "Throughput (MiB/s)"

set title "Throughput at 20% load percent"

plot "data/results/placement_60000.dat" using 1:2 with linespoints
