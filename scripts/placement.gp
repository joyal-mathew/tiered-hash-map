set term svg
set output "plots/placement.svg"

plot "data/placement.dat" using 1:2 with linespoints
