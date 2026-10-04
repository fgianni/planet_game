import sys, numpy as np

from closure import load, structure, a
lat, N, f = load(sys.argv[1])
phi = np.radians(lat); dy = a*(phi[1]-phi[0])
uv = f["eddy_momentum_flux_m2_s2"]; E = f["eddy_kinetic_m2_s2"]
T = f["temperature_K"]; pmid, theta, pibar = structure(f["ps"], T, N)
thy = np.gradient(theta, dy, axis=1); col = np.abs(thy.mean(0))
Ey = np.gradient(E, dy, axis=1)
G = col**2; Gy = np.gradient(G, dy)
print("lat  " + " ".join(f"uv{k}" for k in range(N)) + " | " + " ".join(f"Ey{k}*1e6" for k in range(N)) + " | E_k ... | |thy|^2 ")
for j in range(len(lat)):
    print(f"{lat[j]:6.1f} " + " ".join(f"{uv[k,j]:6.2f}" for k in range(N)) + " | " +
          " ".join(f"{Ey[k,j]*1e6:6.2f}" for k in range(N)) + " | " + " ".join(f"{E[k,j]:6.1f}" for k in range(N)) + f" | {G[j]*1e12:6.2f}")
m = np.abs(lat) < 75
for k in range(N):
    c = np.sum(uv[k,m]*Ey[k,m])/np.sum(Ey[k,m]**2)
    r = np.corrcoef(uv[k,m], Ey[k,m])[0,1]
    print(f"layer {k}: uv ~ c dE/dy: c = {c:.3e} m, corr {r:.2f}")
uvc = uv.mean(0); Ec = E.mean(0); Ecy = np.gradient(Ec, dy)
print("column uv vs dEcol/dy corr", np.corrcoef(uvc[m], Ecy[m])[0,1], " vs d|thy|^2/dy corr", np.corrcoef(uvc[m], Gy[m])[0,1])
print("column E vs |thy|^2 corr", np.corrcoef(Ec[m], G[m])[0,1], " E/|thy|^2 =", np.sum(Ec[m]*G[m])/np.sum(G[m]**2))
