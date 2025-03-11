# Defines a struct representing boxes on the terrain
use ./world.tm
use ./raylib.tm

struct Box(pos:Vector2, size=Vector2(50, 50), color=Color(0x80,0x80,0x80), blocking=yes):
    func draw(b:Box):
        DrawRectangleRec(Rectangle(b.pos.x, b.pos.y, b.size.x, b.size.y), b.color)
