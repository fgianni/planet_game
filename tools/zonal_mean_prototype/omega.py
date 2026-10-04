import numpy as np, zonal_model as z, sys
O = 7.292e-5
for nu in (2e5, 5e4):
  for s in (0.0, 0.5, 1.0, 2.0):
    r = z.run(N=3, Omega=s*O, days=500, rossby=True, nu=nu)
    lat, psi, u = r['lati'], r['psi'][0], r['u']
    nh = lat > 0
    pl, pp = lat[nh], psi[nh]
    # Hadley edge: first latitude poleward of 0 where the lower-interface ψ changes sign from its equatorward value
    sgn = np.sign(pp[1]); edge = next((pl[j] for j in range(1, len(pl)) if np.sign(pp[j]) != sgn), 90.0)
    print(f"nu {nu:.0e} Omega x{s}: max|u| {np.abs(u).max():6.2f}  u_bottom [{u[0].min():5.2f},{u[0].max():5.2f}]  utop_eq {u[-1,18]:6.2f}  psi(10N) {np.interp(10,pl,pp)/1e9:7.1f}e9  edge {edge:5.1f}", flush=True)
