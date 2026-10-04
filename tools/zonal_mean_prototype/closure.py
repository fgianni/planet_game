"""Diagnose eddy closures against a reference zonal profile (ADR-0011 §4.5).

Reads the --zonal-csv of planet_cli dynamics/reference and compares the
measured eddy heat and momentum fluxes with Green-type diffusive closures.
"""
import sys
import numpy as np

a = 6371000.0
g = 9.80616
R = 287.04
cp = 1004.64
kappa = R / cp
p0 = 1e5
Omega = 7.292e-5


def load(path):
    d = np.genfromtxt(path, delimiter=",", names=True)
    lat = np.unique(d["latitude_deg"])
    N = int(d["layer"].max()) + 1
    J = len(lat)
    f = {}
    for name in d.dtype.names[2:]:
        f[name] = d[name].reshape(J, N).T  # layer x band
    f["ps"] = f["surface_pressure_Pa"][0]
    return lat, N, f


def structure(ps, T, N):
    """Layer-mean Exner and θ as in the M6-03 core."""
    m = np.arange(N + 1)[:, None]
    p = (1 - m / N) * ps[None, :]
    pi = (p / p0) ** kappa
    pb, pt = p[:-1], p[1:]
    pib, pit = pi[:-1], pi[1:]
    pibar = (pb * pib - pt * pit) / ((1 + kappa) * (pb - pt))
    theta = T / pibar
    pmid = 0.5 * (pb + pt)
    return pmid, theta, pibar


def main(path):
    lat, N, f = load(path)
    phi = np.radians(lat)
    J = len(lat)
    dy = a * (phi[1] - phi[0])
    ps = f["ps"]
    T = f["temperature_K"]
    pmid, theta, pibar = structure(ps, T, N)
    vth = f["eddy_heat_flux_K_m_s"] / pibar  # [v'θ'] at band centres
    uv = f["eddy_momentum_flux_m2_s2"]
    u = f["eastward_m_s"]

    thy = np.gradient(theta, dy, axis=1)  # ∂θ/∂y, layer x band
    # measured diffusivity per layer
    with np.errstate(divide="ignore", invalid="ignore"):
        Dm = -vth / thy
    # column-mean |θ_y|
    thy_col = np.abs(thy.mean(axis=0))
    print("lat   " + " ".join(f"D{k}_1e6" for k in range(N)) + "  |thy|col_K/1000km  vth_col")
    for j in range(J):
        print(f"{lat[j]:6.1f} " + " ".join(f"{Dm[k, j]/1e6:7.2f}" for k in range(N))
              + f"  {thy_col[j]*1e6:7.2f}  {vth[:, j].mean():7.2f}")

    # Green fit: D = c |θ_y|col, least squares on the column heat flux
    mask = np.abs(lat) > 15
    pred_unit = -(thy_col[None, :] * thy)  # flux for c = 1
    c = np.sum(vth[:, mask] * pred_unit[:, mask]) / np.sum(pred_unit[:, mask] ** 2)
    resid = vth - c * pred_unit
    print(f"\nGreen c_e = {c:.4e} m^3/(s K); rel. rms heat flux error (|lat|>15) = "
          f"{np.sqrt(np.mean(resid[:, mask]**2))/np.sqrt(np.mean(vth[:, mask]**2)):.3f}")

    # Momentum: measured −(1/(a cos²φ)) ∂φ(cos²φ [u'v']) per layer, and column sum
    cos = np.cos(phi)
    conv = -np.gradient(cos**2 * uv, phi, axis=1) / (a * cos**2)
    col = conv.mean(axis=0)
    # QG Taylor identity with D_k = c |θ_y|col (vertically uniform) and S = θ_y/θ_p
    fcor = 2 * Omega * np.sin(phi)
    beta = 2 * Omega * cos / a
    dp = ps / N
    thp = np.diff(theta, axis=0) / np.diff(pmid, axis=0)  # interfaces 1..N-1
    thy_i = 0.5 * (thy[1:] + thy[:-1])
    S = np.zeros((N + 1, J))
    S[1:N] = thy_i / thp
    qy = beta[None, :] - np.gradient(np.gradient(u, dy, axis=1), dy, axis=1) \
        + fcor[None, :] * (S[:-1] - S[1:]) / dp[None, :]
    for label, D in (("uniform", c * thy_col[None, :] * np.ones((N, 1))),
                     ("measured", np.where(np.isfinite(Dm), np.clip(Dm, 0, None), 0))):
        Hflux = -D * thy
        H = np.zeros((N + 1, J))
        H[1:N] = 0.5 * (Hflux[1:] + Hflux[:-1]) / thp
        vq = -D * qy
        conv_pred = vq - fcor[None, :] * (H[:-1] - H[1:]) / dp[None, :]
        print(f"\nmomentum flux convergence (1e-6 m/s^2), closure D {label}: measured | predicted, per layer, column")
        for j in range(0, J, 2):
            print(f"{lat[j]:6.1f} " + " ".join(f"{conv[k, j]*1e6:6.2f}|{conv_pred[k, j]*1e6:6.2f}" for k in range(N))
                  + f"   col {col[j]*1e6:6.2f}|{conv_pred[:, j].mean()*1e6:6.2f}  u0 {u[0, j]:5.1f}")


if __name__ == "__main__":
    main(sys.argv[1])
