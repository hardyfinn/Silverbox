#!/usr/bin/env python3
"""Resonant frequency and critical feedback of the coupled diode ladder (docs/DECISIONS.md items 1 and 4). Needs numpy."""
import numpy as np
c1=1.8; W180=1.3775
A=np.array([[-c1,c1,0,0],[1,-2,1,0],[0,1,-2,1],[0,0,1,-2]],float); B=np.array([c1,0,0,0.])
def H(w): return np.linalg.solve(1j*w*np.eye(4)-A,B)[3]
for r in [1,1.5,2,3,4,6,8,12,16,24,32,64,128]:
    # fc = r*fhp ; system scale w = fc/W180 ; HP corner in system units = fhp/(fc/W180) = W180/r
    h=W180/r
    ws=np.logspace(-1,1.5,20000)
    L=np.array([H(w)*(1j*w/(1j*w+h)) for w in ws])
    ph=np.unwrap(np.angle(L)); i=np.argmin(abs(ph+np.pi))
    print(r, round(ws[i]/W180,4), round(1/abs(L[i]),3))
# Without the feedback high-pass (very high cutoffs):
w=np.logspace(-1,1.5,20000); ph=np.unwrap(np.angle([H(x) for x in w])); i=np.argmin(abs(ph+np.pi))
print("no HP: resonant frequency", w[i], "critical feedback", 1/abs(H(w[i])))
