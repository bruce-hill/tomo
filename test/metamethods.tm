struct Vec2(x,y:Int)
    func plus(a,b:Vec2 -> Vec2; inline)
        return Vec2(a.x+b.x, a.y+b.y)

    func minus(a,b:Vec2 -> Vec2; inline)
        return Vec2(a.x-b.x, a.y-b.y)

    func dot(a,b:Vec2 -> Int; inline)
        return a.x*b.x + a.y*b.y

    func scaled_by(a:Vec2, k:Int -> Vec2; inline)
        return Vec2(a.x*k, a.y*k)

    func times(a,b:Vec2 -> Vec2; inline)
        return Vec2(a.x*b.x, a.y*b.y)

    func divided_by(a:Vec2, k:Int -> Vec2; inline)
        return Vec2(a.x/k, a.y/k)

    func negative(v:Vec2 -> Vec2; inline)
        return Vec2(-v.x, -v.y)

    func negated(v:Vec2 -> Vec2; inline)
        return Vec2(not v.x, not v.y)

    func bit_and(a,b:Vec2 -> Vec2; inline)
        return Vec2(a.x and b.x, a.y and b.y)

    func bit_or(a,b:Vec2 -> Vec2; inline)
        return Vec2(a.x or b.x, a.y or b.y)

    func bit_xor(a,b:Vec2 -> Vec2; inline)
        return Vec2(a.x xor b.x, a.y xor b.y)

    func left_shifted(v:Vec2, bits:Int -> Vec2; inline)
        return Vec2(v.x >> bits, v.y >> bits)

    func right_shifted(v:Vec2, bits:Int -> Vec2; inline)
        return Vec2(v.x << bits, v.y << bits)

    func modulo(v:Vec2, modulus:Int -> Vec2; inline)
        return Vec2(v.x mod modulus, v.y mod modulus)

    func modulo1(v:Vec2, modulus:Int -> Vec2; inline)
        return Vec2(v.x mod1 modulus, v.y mod1 modulus)

func main()
    >> x := Vec2(10, 20)
    >> y := Vec2(100, 200)
    assert x + y == Vec2(x=110, y=220)
    assert x - y == Vec2(x=-90, y=-180)
    assert x * y == Vec2(x=1000, y=4000)
    assert x.dot(y) == 5000
    assert x * -1 == Vec2(x=-10, y=-20)
    assert -10 * x == Vec2(x=-100, y=-200)

    >> x = Vec2(1, 2)
    >> x += Vec2(10, 20)
    assert x == Vec2(x=11, y=22)
    >> x *= Vec2(10, -1)
    assert x == Vec2(x=110, y=-22)

    >> x *= -1
    assert x == Vec2(x=-110, y=22)

    >> x = Vec2(1, 2)
    assert -x == Vec2(x=-1, y=-2)

    x = Vec2(1, 2)
    y = Vec2(4, 3)
    assert (x and y) == Vec2(x=0, y=2)
    assert (x or y) == Vec2(x=5, y=3)
    assert (x xor y) == Vec2(x=5, y=1)
    assert x / 2 == Vec2(x=0, y=1)
    assert x mod 3 == Vec2(x=1, y=2)
    assert x mod1 3 == Vec2(x=1, y=2)

