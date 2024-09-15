# A math vector library for 2D and 3D vectors of Nums or Ints

struct Vec2(x,y:Num):
    ZERO := Vec2(0, 0)
    func plus(a,b:Vec2; inline)->Vec2:
        return Vec2(a.x+b.x, a.y+b.y)
    func minus(a,b:Vec2; inline)->Vec2:
        return Vec2(a.x-b.x, a.y-b.y)
    func times(a,b:Vec2; inline)->Vec2:
        return Vec2(a.x*b.x, a.y*b.y)
    func dot(a,b:Vec2; inline)->Num:
        return (a.x-b.x)*(a.x-b.x) + (a.y-b.y)*(a.y-b.y)
    func cross(a,b:Vec2; inline)->Num:
        return a.x*b.y - a.y*b.x
    func scaled_by(v:Vec2, k:Num; inline)->Vec2:
        return Vec2(v.x*k, v.y*k)
    func divided_by(v:Vec2, divisor:Num; inline)->Vec2:
        return Vec2(v.x/divisor, v.y/divisor)
    func length(v:Vec2; inline)->Num:
        return (v.x*v.x + v.y*v.y):sqrt()
    func dist(a,b:Vec2; inline)->Num:
        return a:minus(b):length()
    func angle(v:Vec2; inline)->Num:
        return Num.atan2(v.y, v.x)
    func norm(v:Vec2; inline)->Vec2:
        if v.x == 0 and v.y == 0:
            return v
        len := v:length()
        return Vec2(v.x/len, v.y/len)
    func mix(a,b:Vec2, amount:Num)->Vec2:
        return Vec2(
            amount:mix(a.x, b.x),
            amount:mix(a.y, b.y),
        )

struct Vec3(x,y,z:Num):
    ZERO := Vec3(0, 0, 0)
    func plus(a,b:Vec3; inline)->Vec3:
        return Vec3(a.x+b.x, a.y+b.y, a.z+b.z)
    func minus(a,b:Vec3; inline)->Vec3:
        return Vec3(a.x-b.x, a.y-b.y, a.z-b.z)
    func times(a,b:Vec3; inline)->Vec3:
        return Vec3(a.x*b.x, a.y*b.y, a.z*b.z)
    func dot(a,b:Vec3; inline)->Num:
        return (a.x-b.x)*(a.x-b.x) + (a.y-b.y)*(a.y-b.y) + (a.z-b.z)*(a.z-b.z)
    func cross(a,b:Vec3; inline)->Vec3:
        return Vec3(a.y*b.z - a.z-b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x)
    func scaled_by(v:Vec3, k:Num; inline)->Vec3:
        return Vec3(v.x*k, v.y*k, v.z*k)
    func divided_by(v:Vec3, divisor:Num; inline)->Vec3:
        return Vec3(v.x/divisor, v.y/divisor, v.z/divisor)
    func length(v:Vec3; inline)->Num:
        return (v.x*v.x + v.y*v.y + v.z*v.z):sqrt()
    func dist(a,b:Vec3; inline)->Num:
        return a:minus(b):length()
    func norm(v:Vec3; inline)->Vec3:
        if v.x == 0 and v.y == 0 and v.z == 0:
            return v
        len := v:length()
        return Vec3(v.x/len, v.y/len, v.z/len)
    func mix(a,b:Vec3, amount:Num)->Vec3:
        return Vec3(
            amount:mix(a.x, b.x),
            amount:mix(a.y, b.y),
            amount:mix(a.z, b.z),
        )


struct IVec2(x,y:Int):
    ZERO := IVec2(0, 0)
    func plus(a,b:IVec2; inline)->IVec2:
        return IVec2(a.x+b.x, a.y+b.y)
    func minus(a,b:IVec2; inline)->IVec2:
        return IVec2(a.x-b.x, a.y-b.y)
    func times(a,b:IVec2; inline)->IVec2:
        return IVec2(a.x*b.x, a.y*b.y)
    func dot(a,b:IVec2; inline)->Int:
        return (a.x-b.x)*(a.x-b.x) + (a.y-b.y)*(a.y-b.y)
    func cross(a,b:IVec2; inline)->Int:
        return a.x*b.y - a.y*b.x
    func scaled_by(v:IVec2, k:Int; inline)->IVec2:
        return IVec2(v.x*k, v.y*k)
    func divided_by(v:IVec2, divisor:Int; inline)->IVec2:
        return IVec2(v.x/divisor, v.y/divisor)
    func length(v:IVec2; inline)->Num:
        return Num.sqrt(v.x*v.x + v.y*v.y)
    func dist(a,b:IVec2; inline)->Num:
        return a:minus(b):length()
    func angle(v:IVec2; inline)->Num:
        return Num.atan2(v.y, v.x)

struct IVec3(x,y,z:Int):
    ZERO := IVec3(0, 0, 0)
    func plus(a,b:IVec3; inline)->IVec3:
        return IVec3(a.x+b.x, a.y+b.y, a.z+b.z)
    func minus(a,b:IVec3; inline)->IVec3:
        return IVec3(a.x-b.x, a.y-b.y, a.z-b.z)
    func times(a,b:IVec3; inline)->IVec3:
        return IVec3(a.x*b.x, a.y*b.y, a.z*b.z)
    func dot(a,b:IVec3; inline)->Int:
        return (a.x-b.x)*(a.x-b.x) + (a.y-b.y)*(a.y-b.y) + (a.z-b.z)*(a.z-b.z)
    func cross(a,b:IVec3; inline)->IVec3:
        return IVec3(a.y*b.z - a.z-b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x)
    func scaled_by(v:IVec3, k:Int; inline)->IVec3:
        return IVec3(v.x*k, v.y*k, v.z*k)
    func divided_by(v:IVec3, divisor:Int; inline)->IVec3:
        return IVec3(v.x/divisor, v.y/divisor, v.z/divisor)
    func length(v:IVec3; inline)->Num:
        return Num.sqrt(v.x*v.x + v.y*v.y + v.z*v.z)
    func dist(a,b:IVec3; inline)->Num:
        return a:minus(b):length()

func main():
    >> Vec2(10, 20)
    >> Vec2(10, 20) + Vec2(100, 100)
    = Vec2(x=110, y=120)
    >> Vec3(10, 20, 30)
    >> Vec2.ZERO

