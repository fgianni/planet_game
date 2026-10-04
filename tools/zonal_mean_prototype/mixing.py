import sys, numpy as np
from closure import load, structure, a
for path in sys.argv[1:]:
    lat, N, f = load(path)
    phi = np.radians(lat); dy = a*(phi[1]-phi[0])
    _, theta, pibar = structure(f["ps"], f["temperature_K"], N)
    vth = f["eddy_heat_flux_K_m_s"]/pibar
    thy = np.gradient(theta, dy, axis=1)
    E = f["eddy_kinetic_m2_s2"].mean(0)
    sig = 1-(np.arange(N)+0.5)/N; s = sig**2/np.mean(sig**2)
    m = (np.abs(lat) > 20) & (np.abs(lat) < 80)
    for label, Dshape in (("mixing sqrt(2E)", np.sqrt(2*E)), ("Green |thy|col", np.abs(thy.mean(0)))):
        pred = -(Dshape[None]*s[:,None])*thy
        c = np.sum(vth[:,m]*pred[:,m])/np.sum(pred[:,m]**2)
        err = np.sqrt(np.mean((vth-c*pred)[:,m]**2))/np.sqrt(np.mean(vth[:,m]**2))
        colc = np.corrcoef(vth.mean(0)[m], (c*pred).mean(0)[m])[0,1]
        print(f"{path.split('/')[-1]:14s} {label:16s} coef {c:.3e}  rel rms err {err:.2f}  column corr {colc:.2f}")
