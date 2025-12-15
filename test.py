import re
from collections import Counter

with open("data/sample.txt", "r") as f:
    words = re.split("[^A-Za-z]", f.read())

counter = Counter(filter(None, words))

table = {}
with open("data/debug.txt", "r") as f:
    for line in f:
        key, value = line.split("->")
        table[key.strip()] = int(value)

for w, c in counter.most_common(100):
    print(w, c)

print(len(counter))
assert dict(counter) == table
