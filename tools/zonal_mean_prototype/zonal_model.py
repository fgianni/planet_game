"""Prototype: steady axisymmetric zonal-mean model on bands × N equal-Δp layers
(ADR-0011 §4.5 candidate), Held–Suarez forcing, marched to steady state with
a rigid lid (zero column mass flux: the steady limit of continuity).

Eddy closure (candidate from reference data):
  D_k   = c_e |θ_y|col σ_k²           heat diffusivity, decaying upward
  E     = c_E |θ_y|col²               column eddy kinetic energy
  [u'v'] = ℓ ∂E/∂y in the free troposphere (σ < σ_b), zero below
"""
import sys
import numpy as np

a = 6371000.0
R = 287.04
cp = 1004.64
kappa = R / cp
p0 = 1e5
day = 86400.0


def run(N=3, J=36, Omega=7.292e-5, c_e=3.0e11, c_E=1.7e12, ell=2.6e5, days=600,
        dt=1200.0, nu=2e5, eddies=True, verbose=False, L=1.0e6,
        mode='march', theta0=None, u0=None, newton_steps=1, baroclinicity='theta', rossby=False):
    ps = 1e5
    dp = ps / N
    sig = 1 - (np.arange(N) + 0.5) / N
    p = sig * ps
    pi_k = (p / p0) ** kappa
    edges = np.linspace(-np.pi / 2, np.pi / 2, J + 1)
    phi = 0.5 * (edges[1:] + edges[:-1])
    phii = edges[1:-1]                      # interior interfaces
    dphi = edges[1] - edges[0]
    s_e = np.sin(edges)
    area = s_e[1:] - s_e[:-1]               # / (2π a²)
    c_c = np.cos(phi)
    c_i = np.cos(phii)
    f_c = 2 * Omega * np.sin(phi)
    f_i = 2 * Omega * np.sin(phii)
    tan_c = np.tan(phi)
    tan_i = np.tan(phii)

    # Held–Suarez
    Teq = np.maximum(200.0, (315 - 60 * np.sin(phi)[None] ** 2
                             - 10 * np.log(p / p0)[:, None] * np.cos(phi)[None] ** 2) * pi_k[:, None])
    w = np.maximum(0, (sig - 0.7) / 0.3)[:, None]
    kT = 1 / (40 * day) + (1 / (4 * day) - 1 / (40 * day)) * w * np.cos(phi)[None] ** 4
    kv = (1 / day) * w[:, 0]
    free = (sig < 0.7).astype(float)
    free /= free.sum()
    dweight = sig ** 2

    th = Teq / pi_k[:, None] + 0.0
    u = np.zeros((N, J))
    v = np.zeros((N, J - 1))

    def div(F):  # F at interior interfaces, per layer -> band centres
        Fc = np.zeros((F.shape[0], J + 1))
        Fc[:, 1:-1] = F * np.cos(phii)[None]
        return (Fc[:, 1:] - Fc[:, :-1]) / (a * area[None])

    def grad(X):  # band centres -> interfaces
        return (X[:, 1:] - X[:, :-1]) / (a * dphi)

    def lap(X):
        return div(grad(X))

    lapm = np.zeros((J, J))
    for j in range(J):
        e = np.zeros(J); e[j] = 1
        lapm[:, j] = div(grad(e[None]))[0]
    screen = np.eye(J) - L ** 2 * lapm

    def tend(u, v, th):
        T = th * pi_k[:, None]
        # hydrostatic geopotential gradient (baroclinic part; the barotropic
        # part is the rigid lid's pressure, projected out below)
        # Φ_k = Σ_{m≤k} R T_m Δln p (layer-centre levels, trapezoid)
        Phi = np.zeros((N, J))
        Phi[0] = R * T[0] * np.log(ps / p[0])
        for k in range(1, N):
            Phi[k] = Phi[k - 1] + R * 0.5 * (T[k] + T[k - 1]) * np.log(p[k - 1] / p[k])
        ui = 0.5 * (u[:, 1:] + u[:, :-1])
        vc = np.zeros((N, J))
        vc[:, :-1] += 0.5 * v
        vc[:, 1:] += 0.5 * v
        dv = div(v)
        omega = np.zeros((N + 1, J))
        for k in range(N):
            omega[k + 1] = omega[k] + dp * dv[k]

        def vert_adv(X):
            out = np.zeros_like(X)
            for m in range(1, N):
                flux_grad = omega[m] * (X[m - 1] - X[m]) / dp   # ω ∂X/∂p at interface m
                out[m - 1] -= 0.5 * flux_grad
                out[m] -= 0.5 * flux_grad
            return out

        # eddies
        thy = grad(th)
        if baroclinicity == 'theta':
            thy_col = np.abs(thy.mean(axis=0))            # at interfaces
        else:
            # Eady: f ∂ū/∂z, as the θ gradient it balances (thermal wind)
            pm = 0.5 * (p[1:] + p[:-1])
            Sw = np.sum(R / pm * (pm / p0) ** kappa * (p[:-1] - p[1:]))
            ui_ = 0.5 * (u[:, 1:] + u[:, :-1])
            thy_col = np.abs(f_i * (ui_[-1] - ui_[0])) / Sw
        if eddies:
            D = c_e * thy_col[None] * dweight[:, None]
            heat = div(D * thy)                       # −div(−D θ_y)
            G = c_E * thy_col ** 2                     # generation, at interfaces
            if rossby:
                # baroclinic eddies need L_R = N H / |f| within the eddy scale L
                thi = 0.5 * (th[:, 1:] + th[:, :-1])
                Ti = thi * pi_k[:, None]
                Tm = Ti.mean(axis=0)
                dthdz = (thi[-1] - thi[0]) / (R * Tm / 9.80616 * np.log(p[0] / p[-1]))
                Nb = np.sqrt(np.maximum(9.80616 / thi.mean(axis=0) * dthdz, 1e-6))
                LR = Nb * R * Tm / 9.80616 / np.maximum(np.abs(f_i), 1e-12)
                chi = 1.0 / (1.0 + (LR / L) ** 2)
                G = G * chi
            Gc = np.r_[G[0], 0.5 * (G[1:] + G[:-1]), G[-1]]  # to centres
            Ecc = np.linalg.solve(screen, Gc)          # (1 − L²∇²) E = G
            uv = ell * grad(Ecc[None])[0]              # at interfaces
            Fm = np.zeros(J + 1)
            Fm[1:-1] = uv * c_i ** 2
            mom = -(Fm[1:] - Fm[:-1]) / (a * c_c * area)
            mom = free[:, None] * mom[None] * N
        else:
            heat = 0 * th
            mom = 0 * u
        # vertical diffusion of momentum (weak), horizontal viscosity
        # angular momentum M = (Ω a cos φ + u) a cos φ in flux form, with
        # stress-form viscosity (solid-body rotation is unstressed)
        M = (Omega * a * c_c[None] + u) * a * c_c[None]
        Mi = np.where(v > 0, M[:, :-1], M[:, 1:])   # upwind: no new extrema of M
        uc = u / c_c[None]
        Fh = v * Mi - nu * c_i[None] ** 2 * (uc[:, 1:] - uc[:, :-1]) / dphi / a * a
        dM = -div(Fh)
        for m in range(1, N):
            Fv = omega[m] * np.where(omega[m] > 0, M[m], M[m - 1])   # upwind, downward ω > 0
            dM[m - 1] += Fv / dp
            dM[m] -= Fv / dp
        du = dM / (a * c_c[None]) + mom - kv[:, None] * u
        dvv = (-(f_i + ui * tan_i / a) * ui - grad(Phi) - kv[:, None] * v
               + nu * (grad(div(v)) ))
        # upwind flux form (ω > 0 downward)
        adv = -div(v * np.where(v > 0, th[:, :-1], th[:, 1:]))
        for m in range(1, N):
            Fv = omega[m] * np.where(omega[m] > 0, th[m], th[m - 1])
            adv[m - 1] += Fv / dp
            adv[m] -= Fv / dp
        dth = (adv + heat - kT * (T - Teq) / pi_k[:, None]
               + nu * lap(th))
        return du, dvv, dth

    def project(v):
        return v - v.mean(axis=0, keepdims=True)  # zero column mass flux

    if theta0 is not None:
        th = theta0.copy()
    if u0 is not None:
        u = u0.copy()
    psi_of = lambda v: np.cumsum(v, axis=0) * 2 * np.pi * a * c_i[None] * dp / 9.80616
    if mode == 'newton':
        nu_, nv, nt = N * J, N * (J - 1), N * J

        def unpack(x):
            uu = x[:nu_].reshape(N, J)
            vv = x[nu_:nu_ + nv].reshape(N, J - 1)
            P = x[nu_ + nv:nu_ + nv + J - 1]
            tt = x[nu_ + nv + J - 1:].reshape(N, J)
            return uu, vv, P, tt

        def resid(x):
            uu, vv, P, tt = unpack(x)
            du, dv, dth = tend(uu, vv, tt)
            return np.r_[du.ravel(), (dv - P[None]).ravel(), vv.sum(axis=0), dth.ravel()]

        x = np.r_[u.ravel(), v.ravel(), np.zeros(J - 1), th.ravel()]
        # scale: residuals of u, v in m/s², θ in K/s; unknowns m/s, K
        history = []
        for it in range(newton_steps):
            r0 = resid(x)
            history.append(np.abs(r0).max())
            n = x.size
            Jm = np.zeros((n, n))
            for i in range(n):
                h = 1e-6 * max(1.0, abs(x[i]))
                xp = x.copy(); xp[i] += h
                Jm[:, i] = (resid(xp) - r0) / h
            x = x - np.linalg.solve(Jm, r0)
        history.append(np.abs(resid(x)).max())
        u, v, P, th = unpack(x)
        return dict(lat=np.degrees(phi), lati=np.degrees(phii), u=u, v=v, T=th * pi_k[:, None],
                    psi=psi_of(v), history=history)

    steps = int(days * day / dt)
    for n in range(steps):
        u0, v0, t0 = u, v, th
        k1 = tend(u, v, th)
        u1 = u0 + dt / 3 * k1[0]; v1 = project(v0 + dt / 3 * k1[1]); t1 = t0 + dt / 3 * k1[2]
        k2 = tend(u1, v1, t1)
        u2 = u0 + dt / 2 * k2[0]; v2 = project(v0 + dt / 2 * k2[1]); t2 = t0 + dt / 2 * k2[2]
        k3 = tend(u2, v2, t2)
        u = u0 + dt * k3[0]; v = project(v0 + dt * k3[1]); th = t0 + dt * k3[2]
        if verbose and n % int(50 * day / dt) == 0:
            print(n * dt / day, np.abs(u).max(), np.abs(v).max())
    # streamfunction at interfaces (kg/s): ψ_m = 2π a cos φ Σ_{k<m} v_k Δp/g
    psi = np.cumsum(v, axis=0) * 2 * np.pi * a * c_i[None] * dp / 9.80616
    return dict(lat=np.degrees(phi), lati=np.degrees(phii), u=u, v=v, T=th * pi_k[:, None], psi=psi)


if __name__ == "__main__":
    N = int(sys.argv[1]) if len(sys.argv) > 1 else 3
    r = run(N=N, verbose=True)
    print("lat " + " ".join(f"u{k}" for k in range(N)) + " " + " ".join(f"T{k}" for k in range(N)))
    for j in range(len(r["lat"])):
        print(f"{r['lat'][j]:6.1f} " + " ".join(f"{r['u'][k, j]:6.2f}" for k in range(N)) + " "
              + " ".join(f"{r['T'][k, j]:6.1f}" for k in range(N)))
    print("psi (1e9 kg/s) at interfaces")
    for j in range(len(r["lati"])):
        print(f"{r['lati'][j]:6.1f} " + " ".join(f"{r['psi'][m, j]/1e9:7.2f}" for m in range(N - 1)))
