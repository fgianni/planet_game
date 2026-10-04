import sys, time, numpy as np
import zonal_model as z
from closure import load, structure
lat, N, f = load(sys.argv[1])
_, theta_ref, pibar = structure(f["ps"], f["temperature_K"], N)
# prototype uses p-levels: θ = T / (p/p0)^κ at layer centres
sig = 1 - (np.arange(N) + 0.5) / N
th0 = f["temperature_K"] / ((sig * 1e5 / 1e5) ** z.kappa)[:, None]
for steps in (1, 2, 4, 8):
    t = time.time()
    r = z.run(N=N, mode='newton', theta0=th0, newton_steps=steps)
    print(f"newton_steps={steps} wall={time.time()-t:.1f}s residual history {['%.1e' % h for h in r['history']]}")
    dT = r['T'] - f['temperature_K']
    print("  max |T - T_ref| =", np.abs(dT).max().round(2), " u_top max", r['u'][-1].max().round(1))
    if steps in (1, 8):
        m = (N - 1) // 2
        psim = np.interp(r["lat"], r["lati"], r["psi"][m])
        for j in range(len(lat)):
            if lat[j] < 0: continue
            print(f"   {lat[j]:5.1f} u0 {f['eastward_m_s'][0,j]:6.2f} {r['u'][0,j]:6.2f} | utop {f['eastward_m_s'][-1,j]:6.2f} {r['u'][-1,j]:6.2f} | "
                  f"T0 {f['temperature_K'][0,j]:6.1f} {r['T'][0,j]:6.1f} | psi {f['streamfunction_top_kg_s'][m,j]/1e9:7.2f} {psim[j]/1e9:7.2f}")
