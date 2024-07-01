struct Vec2(x,y:Int):
    func __add(a,b:Vec2; inline)->Vec2:
        return Vec2(a.x+b.x, a.y+b.y)

    func __sub(a,b:Vec2; inline)->Vec2:
        return Vec2(a.x-b.x, a.y-b.y)

    func __mul(a,b:Vec2; inline)->Int:
        return a.x*b.x + a.y*b.y

    func __mul2(a:Vec2,b:Int; inline)->Vec2:
        return Vec2(a.x*b, a.y*b)

    func __mul3(a:Int,b:Vec2; inline)->Vec2:
        return Vec2(a*b.x, a*b.y)

    func __mul4(a,b:Vec2; inline)->Vec2:
        return Vec2(a.x*b.x, a.y*b.y)

func main():
    >> x := Vec2(10, 20)
    >> y := Vec2(100, 200)
    >> x + y
    = Vec2(x=110, y=220)
    >> x - y
    = Vec2(x=-90, y=-180)
    >> x * y
    = 5000
    >> x * -1
    = Vec2(x=-10, y=-20)
    >> -10 * x
    = Vec2(x=-100, y=-200)

    >> x = Vec2(1, 2)
    >> x += Vec2(10, 20)
    = Vec2(x=11, y=22)
    >> x *= Vec2(10, -1)
    = Vec2(x=110, y=-22)
