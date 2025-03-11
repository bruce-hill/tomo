# Raylib wrapper for some functions and structs
use libraylib.so
use <raylib.h>
use <raymath.h>

struct Color(r,g,b:Byte,a=Byte(255); extern)
struct Rectangle(x,y,width,height:Num32; extern):
    func draw(r:Rectangle, color:Color):
        DrawRectangleRec(r, color)

struct Vector2(x,y:Num32; extern):
    ZERO := Vector2(0, 0)
    func plus(a,b:Vector2->Vector2; inline):
        return Vector2(a.x+b.x, a.y+b.y)
    func minus(a,b:Vector2->Vector2; inline):
        return Vector2(a.x-b.x, a.y-b.y)
    func times(a,b:Vector2->Vector2; inline):
        return Vector2(a.x*b.x, a.y*b.y)
    func negative(v:Vector2->Vector2; inline):
        return Vector2(-v.x, -v.y)
    func dot(a,b:Vector2->Num32; inline):
        return ((a.x-b.x)*(a.x-b.x) + (a.y-b.y)*(a.y-b.y))!
    func cross(a,b:Vector2->Num32; inline):
        return a.x*b.y - a.y*b.x
    func scaled_by(v:Vector2, k:Num32->Vector2; inline):
        return Vector2(v.x*k, v.y*k)
    func divided_by(v:Vector2, divisor:Num32->Vector2; inline):
        return Vector2(v.x/divisor, v.y/divisor)
    func length(v:Vector2->Num32; inline):
        return (v.x*v.x + v.y*v.y)!:sqrt()
    func dist(a,b:Vector2->Num32; inline):
        return a:minus(b):length()
    func angle(v:Vector2->Num32; inline):
        return Num32.atan2(v.y, v.x)
    func norm(v:Vector2->Vector2; inline):
        if v.x == 0 and v.y == 0:
            return v
        len := v:length()
        return Vector2(v.x/len, v.y/len)
    func rotated(v:Vector2, radians:Num32 -> Vector2):
        cos := radians:cos() or return v
        sin := radians:sin() or return v
        return Vector2(cos*v.x - sin*v.y, sin*v.x + cos*v.y)
    func mix(a,b:Vector2, amount:Num32 -> Vector2):
        return Vector2(
            amount:mix(a.x, b.x),
            amount:mix(a.y, b.y),
        )

extern InitWindow:func(width:Int32, height:Int32, title:CString)
extern SetTargetFPS:func(fps:Int32)
extern WindowShouldClose:func(->Bool)
extern GetFrameTime:func(->Num32)
extern BeginDrawing:func()
extern EndDrawing:func()
extern CloseWindow:func()
extern ClearBackground:func(color:Color)
extern DrawRectangle:func(x,y,width,height:Int32, color:Color)
extern DrawRectangleRec:func(rec:Rectangle, color:Color)
extern DrawRectangleV:func(pos:Vector2, size:Vector2, color:Color)
extern DrawText:func(text:CString, x,y:Int32, text_height:Int32, color:Color)
extern GetScreenWidth:func(->Int32)
extern GetScreenHeight:func(->Int32)
