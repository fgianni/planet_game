import sys, numpy as np, time
import zonal_model as z
from closure import load
ref = sys.argv[1]; kw = eval(sys.argv[2]) if len(sys.argv) > 2 else {}
lat, N, f = load(ref)
t = time.time(); r = z.run(N=N, **kw); el = time.time() - t
psi_ref = f["streamfunction_top_kg_s"]  # top of layer k, at band centres
print(f"wall {el:.1f}s  N={N}  {kw}")
print("  lat | u_bottom ref/model | u_top ref/model | T_bottom ref/model | T_top ref/model | psi_mid ref/model (1e9)")
m = (N - 1) // 2
psim = np.interp(r["lat"], r["lati"], r["psi"][m])
for j in range(0, len(lat), 1):
    if lat[j] < 0: continue
    print(f"{lat[j]:5.1f} | {f['eastward_m_s'][0,j]:6.2f} {r['u'][0,j]:6.2f} | {f['eastward_m_s'][-1,j]:6.2f} {r['u'][-1,j]:6.2f} | "
          f"{f['temperature_K'][0,j]:6.1f} {r['T'][0,j]:6.1f} | {f['temperature_K'][-1,j]:6.1f} {r['T'][-1,j]:6.1f} | "
          f"{psi_ref[m,j]/1e9:7.2f} {psim[j]/1e9:7.2f}")
