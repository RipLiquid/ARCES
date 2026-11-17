"""Independent standard-library reference for the C++ regression tests.

Uses exact rational arithmetic for OLS and Newey-West covariance, and numerical
integration of the Student-t density for inference. Not used by the application
or CI. Run with Python 3.10+ to reproduce the constants in analysis_test.cpp.
"""
from fractions import Fraction as F
from math import atan, cos, gamma, pi, sqrt

values = [10, 11, 10, 13, 12, 15, 14, 16, 15, 18, 17, 20]
n = len(values)
x = [F(i) for i in range(n)]
y = [F(v) for v in values]
xbar, ybar = sum(x) / n, sum(y) / n
sxx = sum((v - xbar) ** 2 for v in x)
slope = sum((a - xbar) * (b - ybar) for a, b in zip(x, y)) / sxx
residual = [b - ybar - slope * (a - xbar) for a, b in zip(x, y)]
score = [(a - xbar) * r for a, r in zip(x, residual)]
lag = 2
meat = sum(v * v for v in score)
for k in range(1, lag + 1):
    meat += 2 * (1 - F(k, lag + 1)) * sum(score[i] * score[i - k] for i in range(k, n))
variance = meat * F(n, n - 2) / sxx ** 2
se = sqrt(float(variance))

def two_sided_p(t, df):
    # t = sqrt(df) tan(theta); integrate transformed density over the tail.
    a, b, intervals = atan(abs(t) / sqrt(df)), pi / 2, 20000
    h = (b - a) / intervals
    total = cos(a) ** (df - 1) + cos(b) ** (df - 1)
    for i in range(1, intervals):
        total += (4 if i % 2 else 2) * cos(a + i * h) ** (df - 1)
    return 2 * gamma((df + 1) / 2) / (sqrt(pi) * gamma(df / 2)) * total * h / 3

lo, hi = 0., 10.
for _ in range(55):
    mid = (lo + hi) / 2
    if two_sided_p(mid, n - 2) > .05:
        lo = mid
    else:
        hi = mid
critical = (lo + hi) / 2
print(f"slope={float(slope):.16g}")
print(f"hac_se={se:.16g}")
print(f"lower={float(slope) - critical * se:.16g}")
print(f"upper={float(slope) + critical * se:.16g}")
print(f"p={two_sided_p(float(slope) / se, n - 2):.16g}")
print(f"r_squared={float(1-sum(v*v for v in residual)/sum((v-ybar)**2 for v in y)):.16g}")

# Same measurements, with an absent calendar year after the fifth observation.
gx = [F(v) for v in [0, 1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 12]]
gbar = sum(gx) / n
gss = sum((a - gbar) ** 2 for a in gx)
gb = sum((a - gbar) * (b - ybar) for a, b in zip(gx, y)) / gss
gz = [(a - gbar) * (b - ybar - gb * (a - gbar)) for a, b in zip(gx, y)]
gm = sum(z*z for z in gz)
gm += 2 * sum((1 - (gx[i] - gx[j]) / 3) * gz[i] * gz[j]
              for i in range(n) for j in range(i) if gx[i] - gx[j] <= 2)
print(f"gap_slope={float(gb):.16g}")
print(f"gap_hac_se={sqrt(float(gm * F(n, n-2) / gss**2)):.16g}")
