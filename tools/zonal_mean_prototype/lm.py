import sys, numpy as np
from closure import load, a
for path in sys.argv[1:]:
    lat, N, f = load(path)
    phi = np.radians(lat); dy = a*(phi[1]-phi[0])
    sig = 1-(np.arange(N)+0.5)/N; free = sig < 0.7
    uv = f["eddy_momentum_flux_m2_s2"]; E = f["eddy_kinetic_m2_s2"].mean(0)
    Ey = np.gradient(E, dy)
    m = np.abs(lat) < 80
    uvf = uv[free].mean(0)              # free-troposphere mean flux
    c = np.sum(uvf[m]*Ey[m])/np.sum(Ey[m]**2)
    uvc = uv.mean(0)
    cc = np.sum(uvc[m]*Ey[m])/np.sum(Ey[m]**2)
    print(f"{path.split('/')[-1]:14s} free-trop l_m {c:.3e} corr {np.corrcoef(uvf[m],Ey[m])[0,1]:.2f} | column l {cc:.3e} corr {np.corrcoef(uvc[m],Ey[m])[0,1]:.2f}; free share of column flux {uv[free].sum()/max(1e-9,uv.sum()) if False else (np.sum(np.abs(uv[free].sum(0)))/np.sum(np.abs(uv.sum(0)))):.2f}")
