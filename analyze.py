import numpy as np
import matplotlib.pyplot as plt

with open(0, "r") as f:
    for line in f:
        kind, values = map(str.strip, line.split("|"))

        data = np.array(values.split(), dtype=np.float64)
        print(kind + "\t", data.mean(), 1.96 * data.std() / np.sqrt(len(data)))
