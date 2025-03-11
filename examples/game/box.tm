# Defines a struct representing boxes on the terrain
use ./world.tm
use ./raylib.tm

struct Box(pos:Vector2, size=Vector2(50, 50), color=Color(0x80,0x80,0x80)):
    func draw(b:Box):
        DrawRectangleV(b.pos, b.size, b.color)
