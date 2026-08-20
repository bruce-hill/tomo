# n-body — The Computer Language Benchmarks Game
#
# A Tomo port. The simulation uses NO inline C; the only C_code use is the
# final printf() calls, which format the energy to 9 decimal places (the
# benchmark's required output format). All the actual computation is pure Tomo.
#
# Design notes (for performance without giving up readability):
#   - Vec3 has inline arithmetic metamethods, so the physics reads as vector
#     math (`a - b`, `v*k`, `v.len2()`) rather than component bookkeeping.
#   - Each body is one `Body` struct in a single `&[Body]` array (stack header,
#     heap payload; it never escapes `main`), so one
#     bounds-checked access yields a body's position, velocity, and mass
#     (all adjacent in memory) instead of indexing three parallel arrays.
#   - Full sweeps use element iteration (`for b in bodies` to read,
#     `for &b in bodies` to update in place), which skips per-index bounds
#     checks and per-write copy-on-write guards; loop counters/indices are
#     native `Int64`, not the default arbitrary-precision `Int`.
#   - Pairwise work reads as `for bi, bj in bodies.pairs()` (each distinct
#     pair once); in for-position that compiles to an inline double-index
#     loop, no iterator closure.
#
# Usage: nbody <steps>   (e.g. ./nbody 50000000)

struct Vec3{x, y, z: Float64}
    func plus(a, b: Vec3 -> Vec3; inline)
        return Vec3{a.x + b.x, a.y + b.y, a.z + b.z}

    func minus(a, b: Vec3 -> Vec3; inline)
        return Vec3{a.x - b.x, a.y - b.y, a.z - b.z}

    func scaled_by(v:Vec3, k:Float64 -> Vec3; inline)
        return Vec3{v.x*k, v.y*k, v.z*k}

    func len2(a: Vec3 -> Float64; inline)
        return a.x*a.x + a.y*a.y + a.z*a.z

struct Body{pos, vel: Vec3, mass: Float64}

func print9(x:Float64)
    # Format-only inline C: print `x` with %.9f, matching the reference output.
    C_code `printf("%.9f", @x); putchar(10);`

func advance(bodies:&[Body], steps:Int64, dt:Float64)
    n := Int64(bodies.length)
    for _ in Int64(1).to(steps)
        # The outer body is held by reference (`&bi`): direct field reads and a
        # single field write, no per-element checks. The inner `bodies[j]`
        # cross-accesses stay indexed (checked) — they're random-access by
        # nature. The list stays live inside a `for &` loop, so indexed reads
        # and writes through `bodies` are fine; only resizing/copying it
        # mid-loop is (loudly) disallowed.
        for &bi at i in bodies
            if i == n
                break
            # Hoist body i's fields into locals; accumulate velocity locally.
            pos_i := bi.pos
            mass_i := bi.mass
            vi := bi.vel
            for j in (i+1).to(n)
                bj := bodies[j]!
                d := pos_i - bj.pos
                d2 := d.len2()
                mag := dt / (d2 * Float64.sqrt(d2)!)
                vi -= d * (bj.mass * mag)
                bodies[j] = Body{bj.pos, bj.vel + d * (mass_i * mag), bj.mass}
            bi.vel = vi
        for &b in bodies
            b.pos += b.vel * dt

func energy(bodies:[Body] -> Float64)
    e := (+: 0.5 * b.mass * b.vel.len2() for b in bodies) or 0.0
    # Potential energy over each distinct pair of bodies:
    for bi, bj in bodies.pairs()
        d := bi.pos - bj.pos
        e -= (bi.mass * bj.mass) / Float64.sqrt(d.len2())!
    return e

func main(steps:Int64)
    dpy : Float64 = 365.24
    sm := 4.0 * Float64.PI * Float64.PI

    # Bodies: sun, jupiter, saturn, uranus, neptune.
    bodies := &[
        Body{Vec3{0.0, 0.0, 0.0}, Vec3{0.0, 0.0, 0.0}, sm},
        Body{
            Vec3{4.84143144246472090e00, -1.16032004402742839e00, -1.03622044471123109e-01},
            Vec3{1.66007664274403694e-03*dpy, 7.69901118419740425e-03*dpy, -6.90460016972063023e-05*dpy},
            9.54791938424326609e-04 * sm},
        Body{
            Vec3{8.34336671824457987e00, 4.12479856412430479e00, -4.03523417114321381e-01},
            Vec3{-2.76742510726862411e-03*dpy, 4.99852801234917238e-03*dpy, 2.30417297573763929e-05*dpy},
            2.85885980666130812e-04 * sm},
        Body{
            Vec3{1.28943695621391310e01, -1.51111514016986312e01, -2.23307578892655734e-01},
            Vec3{2.96460137564761618e-03*dpy, 2.37847173959480950e-03*dpy, -2.96589568540237556e-05*dpy},
            4.36624404335156298e-05 * sm},
        Body{
            Vec3{1.53796971148509165e01, -2.59193146099879641e01, 1.79258772950371181e-01},
            Vec3{2.68067772490389322e-03*dpy, 1.62824170038242295e-03*dpy, -9.51592254519715870e-05*dpy},
            5.15138902046611451e-05 * sm},
    ]

    # offset_momentum: pin the total momentum to zero via the sun's velocity.
    p := Vec3{0.0, 0.0, 0.0}
    for b in bodies
        p += b.vel * b.mass
    sun := bodies[1]!
    bodies[1] = Body{sun.pos, p * (-1.0 / sm), sun.mass}

    print9(energy(bodies[]))
    advance(bodies, steps, 0.01)
    print9(energy(bodies[]))
